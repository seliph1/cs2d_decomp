"""Runs single CS2D.exe functions in Unicorn with the Max2D draw primitives replaced by recorders.

Used to read the exact draw calls of the light engine (TSmartLighting & co.) and to check our ports against them
(tools/agent/emu_lightshadow.py, tools/agent/emu_light.py). Library only:

    emu = Cs2dEmu('CS2D.exe')
    emu.set_i32(addr, v); emu.set_f32(addr, v); arr = emu.array_2d_i32(w, h, cells); emu.set_ptr(addr, arr)
    log = emu.call(0x14025e420, ints=[...], floats=[...], stack=[...])   # Win64: see Cs2dEmu.call
    for c in log: print(c)       # ('SetColor', r, g, b), ('DrawPoly', [x0, y0, ...]), ...

Every hooked primitive returns immediately. Unhooked code runs for real, so a function that reaches the BlitzMax
GC, lists or the renderer has to get more hooks (Cs2dEmu.hook).
"""
import math
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UcError
from unicorn.x86_const import (UC_X86_REG_RSP, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9,
                               UC_X86_REG_RIP, UC_X86_REG_RAX, UC_X86_REG_XMM0, UC_X86_REG_XMM1,
                               UC_X86_REG_XMM2, UC_X86_REG_XMM3)

XMM = (UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3)
GPR = (UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9)

# Max2D / draw primitives (CS2D.exe)
A = {
    'Max2D_SetColor': 0x140326560, 'Max2D_SetBlend': 0x1403268E0, 'Max2D_SetAlpha': 0x1403269A0,
    'Max2D_SetAlpha_3': 0x140326A00, 'Max2D_SetRotation': 0x140327450, 'Max2D_SetScale': 0x140327580,
    'Max2D_SetTransform': 0x1403276D0, 'Max2D_SetOrigin': 0x1403271B0, 'cs2d_DrawImage': 0x14010A7C0,
    'cs2d_DrawImageRect': 0x140113C70, 'cs2d_DrawRect': 0x140120A50, 'DrawPoly': 0x14013B1B0,
    'cs2d_DrawTexturedPoly': 0x140131CD0, 'bbArrayFromData': 0x1408DB7B0, 'sincos': 0x140916780,
    'atan2': 0x140916530, 'cos': 0x140916630, 'sin': 0x140917520, 'acos': 0x1409204E0,  # msvcrt import thunk
}

HEAP = 0x200000000
HOOKS = 0x300000000
RET = HOOKS + 0xffff0
STACK = 0x310000000
NULL_OBJ = 0x140DE8F80   # bbNullObject


def f2u(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def u2f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


def u2d(u):
    return struct.unpack('<d', struct.pack('<Q', u & 0xffffffffffffffff))[0]


def d2u(d):
    return struct.unpack('<Q', struct.pack('<d', d))[0]


class Cs2dEmu:
    def __init__(self, exe):
        pe = pefile.PE(exe, fast_load=True)
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_64)
        base = pe.OPTIONAL_HEADER.ImageBase
        for s in pe.sections:
            va = base + s.VirtualAddress
            size = (max(s.Misc_VirtualSize, s.SizeOfRawData) + (va & 0xfff) + 0xfff) & ~0xfff
            uc.mem_map(va & ~0xfff, size)
            uc.mem_write(va, s.get_data())
        uc.mem_map(HEAP, 0x4000000)
        uc.mem_map(HOOKS, 0x100000)
        uc.mem_write(HOOKS, b'\xc3' * 0x100000)
        uc.mem_map(STACK, 0x100000)
        self.heap_top = HEAP + 0x1000
        self.log = []
        self.hooks = {}
        uc.hook_add(UC_HOOK_CODE, self._on_ret, begin=RET, end=RET)
        self._install_primitives()

    # ---- memory helpers
    def alloc(self, size, align=16):
        self.heap_top = (self.heap_top + align - 1) & ~(align - 1)
        a = self.heap_top
        self.heap_top += size
        self.uc.mem_write(a, b'\0' * size)
        return a

    def set_i32(self, addr, v):
        self.uc.mem_write(addr, struct.pack('<i', v))

    def set_f32(self, addr, v):
        self.uc.mem_write(addr, struct.pack('<f', v))

    def set_ptr(self, addr, v):
        self.uc.mem_write(addr, struct.pack('<Q', v))

    def get_i32(self, addr):
        return struct.unpack('<i', self.uc.mem_read(addr, 4))[0]

    def get_f32(self, addr):
        return struct.unpack('<f', self.uc.mem_read(addr, 4))[0]

    def _array(self, fmt, dims, values, elem):
        """BBArray: +0x1a u16 data offset, +0x1c i32 length, +0x20.. scales (2D: +0x20 = inner size)."""
        n = 1
        for d in dims:
            n *= d
        hdr = 0x30
        a = self.alloc(hdr + n * elem)
        self.uc.mem_write(a + 0x1a, struct.pack('<H', hdr))
        self.uc.mem_write(a + 0x1c, struct.pack('<i', n))
        if len(dims) == 2:
            self.uc.mem_write(a + 0x20, struct.pack('<i', dims[1]))
        self.uc.mem_write(a + hdr, struct.pack('<%d%s' % (n, fmt), *values))
        return a

    def array_1d(self, fmt, values):
        return self._array(fmt, [len(values)], values, struct.calcsize(fmt))

    def array_2d(self, fmt, w, h, get):
        """get(x, y) -> value; element [x][y] is at x * h + y."""
        vals = [get(x, y) for x in range(w) for y in range(h)]
        return self._array(fmt, [w, h], vals, struct.calcsize(fmt))

    def read_array(self, a, fmt):
        off = struct.unpack('<H', self.uc.mem_read(a + 0x1a, 2))[0]
        n = struct.unpack('<i', self.uc.mem_read(a + 0x1c, 4))[0]
        sz = struct.calcsize(fmt)
        return list(struct.unpack('<%d%s' % (n, fmt), self.uc.mem_read(a + off, n * sz)))

    # ---- calling
    def hook(self, addr, fn):
        """fn(emu) runs instead of the function at addr; it may call emu.ret(value)."""
        if addr not in self.hooks:
            self.uc.hook_add(UC_HOOK_CODE, self._on_code, begin=addr, end=addr)
        self.hooks[addr] = fn

    def _on_code(self, uc, addr, size, user):
        fn = self.hooks.get(addr)
        if fn is None:
            return
        self._retval = None
        fn(self)
        rsp = uc.reg_read(UC_X86_REG_RSP)
        ret = struct.unpack('<Q', uc.mem_read(rsp, 8))[0]
        uc.reg_write(UC_X86_REG_RSP, rsp + 8)
        uc.reg_write(UC_X86_REG_RIP, ret)

    def _on_ret(self, uc, addr, size, user):
        uc.emu_stop()

    def ix(self, i):
        return self.uc.reg_read(GPR[i]) if i < 4 else struct.unpack('<q', self.uc.mem_read(
            self.uc.reg_read(UC_X86_REG_RSP) + 8 + 8 * i, 8))[0]

    def i32(self, i):
        v = self.ix(i) & 0xffffffff
        return v - (1 << 32) if v & 0x80000000 else v

    def fx(self, i):
        if i < 4:
            return u2f(self.uc.reg_read(XMM[i]) & 0xffffffff)
        return struct.unpack('<f', self.uc.mem_read(self.uc.reg_read(UC_X86_REG_RSP) + 8 + 8 * i, 4))[0]

    def dx(self, i):
        return u2d(self.uc.reg_read(XMM[i]) & 0xffffffffffffffff)

    def ret_int(self, v):
        self.uc.reg_write(UC_X86_REG_RAX, v & 0xffffffffffffffff)

    def ret_double(self, v):
        self.uc.reg_write(UC_X86_REG_XMM0, d2u(v))

    def call(self, addr, args, count=20000000):
        """args: list of ('i', int) / ('f', float) / ('p', pointer) in Win64 order (slot i = rcx/xmm0, rdx/xmm1, ...)."""
        uc = self.uc
        self.log = []
        rsp = STACK + 0x80000
        frame = bytearray(struct.pack('<Q', RET) + b'\0' * 0x20 + b'\0' * 8 * max(0, len(args) - 4) + b'\0' * 0x40)
        for i, (kind, v) in enumerate(args):
            if i < 4:
                if kind == 'f':
                    uc.reg_write(XMM[i], f2u(v))
                else:
                    uc.reg_write(GPR[i], v & 0xffffffffffffffff)
            else:
                data = struct.pack('<f4x', v) if kind == 'f' else struct.pack('<q', v)
                frame[8 + 8 * i:16 + 8 * i] = data
        uc.mem_write(rsp, bytes(frame))
        uc.reg_write(UC_X86_REG_RSP, rsp)
        try:
            uc.emu_start(addr, RET + 1, count=count)
        except UcError as e:
            self.log.append(('error', str(e), hex(uc.reg_read(UC_X86_REG_RIP))))
        return self.log

    # ---- primitives
    def _install_primitives(self):
        L = self.log_append

        self.hook(A['Max2D_SetColor'], lambda e: L(('SetColor', e.i32(0), e.i32(1), e.i32(2))))
        self.hook(A['Max2D_SetBlend'], lambda e: L(('SetBlend', e.i32(0))))
        self.hook(A['Max2D_SetAlpha'], lambda e: L(('SetAlpha', round(e.fx(0), 5))))
        self.hook(A['Max2D_SetAlpha_3'], lambda e: L(('SetAlpha', round(e.fx(0), 5))))
        self.hook(A['Max2D_SetRotation'], lambda e: L(('SetRotation', round(e.fx(0), 4))))
        self.hook(A['Max2D_SetScale'], lambda e: L(('SetScale', round(e.fx(0), 5), round(e.fx(1), 5))))
        self.hook(A['Max2D_SetTransform'], lambda e: L(('SetTransform', round(e.fx(0), 4), round(e.fx(1), 5),
                                                         round(e.fx(2), 5))))
        self.hook(A['Max2D_SetOrigin'], lambda e: L(('SetOrigin', round(e.fx(0), 3), round(e.fx(1), 3))))
        self.hook(A['cs2d_DrawRect'], lambda e: L(('DrawRect', round(e.fx(0), 3), round(e.fx(1), 3),
                                                    round(e.fx(2), 3), round(e.fx(3), 3))))
        self.hook(A['cs2d_DrawImage'], lambda e: L(('DrawImage', self.img_name(e.ix(0)), round(e.fx(1), 3),
                                                     round(e.fx(2), 3), e.i32(3))))
        self.hook(A['cs2d_DrawImageRect'], lambda e: L(('DrawImageRect', self.img_name(e.ix(0)),
                                                         round(e.fx(1), 3), round(e.fx(2), 3), round(e.fx(3), 3),
                                                         round(e.fx(4), 3), e.i32(5))))
        self.hook(A['DrawPoly'], lambda e: L(('DrawPoly', [round(v, 3) for v in e.read_array(e.ix(0), 'f')])))
        self.hook(A['cs2d_DrawTexturedPoly'], lambda e: L(('DrawTexturedPoly', self.img_name(e.ix(1)),
                                                            [round(v, 3) for v in e.read_array(e.ix(0), 'f')])))
        self.hook(A['bbArrayFromData'], self._bb_array_from_data)
        self.hook(A['sincos'], self._sincos)
        self.hook(A['atan2'], lambda e: e.ret_double(math.atan2(e.dx(0), e.dx(1))))
        self.hook(A['cos'], lambda e: e.ret_double(math.cos(e.dx(0))))
        self.hook(A['sin'], lambda e: e.ret_double(math.sin(e.dx(0))))
        self.hook(A['acos'], lambda e: e.ret_double(math.acos(max(-1.0, min(1.0, e.dx(0))))))
        self.images = {}

    def log_append(self, item):
        self.log.append(item)

    def img_name(self, ptr):
        return self.images.get(ptr, hex(ptr))

    def _bb_array_from_data(self, e):
        tchar = chr(e.uc.mem_read(e.ix(0), 1)[0])
        n = e.i32(1)
        elem = {'i': 4, 'f': 4, 'd': 8, 'l': 8, 'b': 1, 's': 2}.get(tchar, 4)
        raw = bytes(e.uc.mem_read(e.ix(2), n * elem)) if n > 0 else b''
        a = self.alloc(0x30 + len(raw))
        e.uc.mem_write(a + 0x1a, struct.pack('<H', 0x30))
        e.uc.mem_write(a + 0x1c, struct.pack('<i', n))
        e.uc.mem_write(a + 0x30, raw)
        e.ret_int(a)

    def _sincos(self, e):
        x = e.dx(0)
        e.uc.mem_write(e.ix(1), struct.pack('<d', math.sin(x)))
        e.uc.mem_write(e.ix(2), struct.pack('<d', math.cos(x)))
