"""Runs TLightSource.CalculateShadow (CS2D.exe 0x1400e60e0) in Unicorn and records its DrawLineLight calls.

usage: python -I tools/agent/emu_lightshadow.py CS2D.exe [cases.txt] > out.txt

Each case line: "W H  x y  lx ly  range  walls" where walls is a list of "tx,ty" wall tiles (TileMapMode 2).
The light is at the center of tile (lx, ly); x, y is the wall tile tested. Output, one line per case:
"case <line>" then "  dll A=(..) L=(..) B=(..) len flagA flagB" for every DrawLineLight call, in call order.
Without a case file a built-in sweep runs (every light offset in -6..6 around a lone wall and a few neighbour
patterns). The FBO coordinates use SCALE_LIGHTS = 1 and MaxLightSize = 512 (light center 256, 256).
"""
import struct
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UcError
from unicorn.x86_const import (UC_X86_REG_RSP, UC_X86_REG_RCX, UC_X86_REG_RIP, UC_X86_REG_XMM0,
                               UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3)

CALC_SHADOW = 0x1400E60E0
G_TILEMAPMODE = 0x140DDEEA8
G_MAPW = 0x140DDEEBC
G_MAPH = 0x140DDEEB8
G_SCALE_LIGHTS = 0x140DDEE9C

HEAP = 0x200000000
STUB = 0x300000000
RET = STUB + 0x100
STACK = 0x310000000


def f2u(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]


def u2f(u):
    return struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]


class Emu:
    def __init__(self, exe):
        pe = pefile.PE(exe, fast_load=True)
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_64)
        base = pe.OPTIONAL_HEADER.ImageBase
        for s in pe.sections:
            va = base + s.VirtualAddress
            size = (max(s.Misc_VirtualSize, s.SizeOfRawData) + 0xfff) & ~0xfff
            uc.mem_map(va & ~0xfff, size + (va & 0xfff))
            data = s.get_data()
            uc.mem_write(va, data[:max(s.Misc_VirtualSize, len(data))])
        uc.mem_map(HEAP, 0x1000000)
        uc.mem_map(STUB, 0x1000)
        uc.mem_write(STUB, b'\xc3' * 0x1000)
        uc.mem_map(STACK, 0x100000)
        self.calls = []
        uc.hook_add(UC_HOOK_CODE, self._hook, begin=STUB, end=STUB + 0x1000)
        # self object with a vtable whose DrawLineLight slot (+0xe0) points at the stub
        self.self_obj = HEAP + 0x100
        vt = HEAP + 0x1000
        uc.mem_write(self.self_obj, struct.pack('<Q', vt))
        uc.mem_write(vt, struct.pack('<Q', 0) * 64)
        uc.mem_write(vt + 0xe0, struct.pack('<Q', STUB))
        uc.mem_write(G_SCALE_LIGHTS, struct.pack('<f', 1.0))

    def _hook(self, uc, addr, size, user):
        if addr == STUB:
            rsp = uc.reg_read(UC_X86_REG_RSP)
            xs = [u2f(uc.reg_read(r) & 0xffffffff) for r in (UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3)]
            st = uc.mem_read(rsp + 0x28, 0x30)
            p4, p5, p6, p7 = (struct.unpack_from('<f', st, 8 * i)[0] for i in range(4))
            p8, p9 = (struct.unpack_from('<i', st, 8 * i)[0] for i in (4, 5))
            self.calls.append((xs[0], xs[1], xs[2], p4, p5, p6, p7, p8, p9))
        elif addr == RET:
            uc.emu_stop()

    def set_map(self, w, h, walls):
        arr = HEAP + 0x10000
        data_off = 0x30
        self.uc.mem_write(arr, b'\0' * data_off)
        self.uc.mem_write(arr + 0x1a, struct.pack('<H', data_off))
        self.uc.mem_write(arr + 0x20, struct.pack('<I', h))
        cells = bytearray(w * h * 4)
        for (x, y) in walls:
            if 0 <= x < w and 0 <= y < h:
                struct.pack_into('<i', cells, (x * h + y) * 4, 2)
        self.uc.mem_write(arr + data_off, bytes(cells))
        self.uc.mem_write(G_TILEMAPMODE, struct.pack('<Q', arr))
        self.uc.mem_write(G_MAPW, struct.pack('<i', w))
        self.uc.mem_write(G_MAPH, struct.pack('<i', h))

    def calc(self, args):
        """args = p1..p10 (floats) of CalculateShadow."""
        uc = self.uc
        self.calls = []
        rsp = STACK + 0x80000
        # Win64: stack arguments from the 5th on take 8-byte slots after the 0x20 shadow space
        frame = (struct.pack('<Q', RET) + b'\0' * 0x20 + b''.join(struct.pack('<f4x', a) for a in args[3:10])
                 + b'\0' * 0x40)
        uc.mem_write(rsp, frame)
        uc.reg_write(UC_X86_REG_RSP, rsp)
        uc.reg_write(UC_X86_REG_RCX, self.self_obj)
        for reg, v in zip((UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3), args[0:3]):
            uc.reg_write(reg, f2u(v))
        uc.reg_write(UC_X86_REG_XMM0, 0)
        try:
            uc.emu_start(CALC_SHADOW, RET + 1, count=200000)
        except UcError as e:
            return 'error %s at %x' % (e, uc.reg_read(UC_X86_REG_RIP))
        return list(self.calls)


def run_case(emu, w, h, x, y, lx, ly, rng, walls):
    emu.set_map(w, h, walls)
    half = 256.0
    # UpdateLightBuffer: p3, p4 = MaxLightSize / 2 / scale; p7, p8 = tile * 32 / scale + (MaxLightSize / 2 - Pos) / scale
    posx, posy = lx * 32 + 16, ly * 32 + 16
    args = (float(x), float(y), half, half, float(lx), float(ly),
            x * 32.0 + (half - posx), y * 32.0 + (half - posy), float(rng), float(rng * 32))
    return emu.calc(args)


def fmt_calls(calls):
    if isinstance(calls, str):
        return ['  ' + calls]
    out = []
    for c in calls:
        out.append('  dll A=(%g,%g) L=(%g,%g) B=(%g,%g) len=%g fa=%d fb=%d' % c)
    return out


def main():
    emu = Emu(sys.argv[1])
    if len(sys.argv) > 2:
        for line in open(sys.argv[2]):
            line = line.split('#')[0].strip()
            if not line:
                continue
            p = line.split()
            w, h, x, y, lx, ly, rng = map(int, p[:7])
            walls = [tuple(map(int, t.split(','))) for t in p[7:]]
            print('case ' + line)
            print('\n'.join(fmt_calls(run_case(emu, w, h, x, y, lx, ly, rng, walls))))
        return
    # Built-in sweep: wall at (10, 10) with several neighbour patterns
    patterns = {
        'lone': [],
        'left': [(9, 10)], 'right': [(11, 10)], 'up': [(10, 9)], 'down': [(10, 11)],
        'horiz': [(9, 10), (11, 10)], 'vert': [(10, 9), (10, 11)],
        'ul': [(9, 9)], 'ur': [(11, 9)], 'dl': [(9, 11)], 'dr': [(11, 11)],
    }
    for name, nb in patterns.items():
        for dy in range(-6, 7):
            for dx in range(-6, 7):
                lx, ly = 10 + dx, 10 + dy
                if (lx, ly) == (10, 10):
                    continue
                walls = [(10, 10)] + nb
                calls = run_case(emu, 30, 30, 10, 10, lx, ly, 5, walls)
                print('case %s d=(%d,%d)' % (name, dx, dy))
                print('\n'.join(fmt_calls(calls)))


if __name__ == '__main__':
    main()
