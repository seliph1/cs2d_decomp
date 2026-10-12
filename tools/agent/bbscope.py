"""Prints the fields of a BlitzMax class from its debug scope (kind 3 = field: name, type tag, offset).

usage: python -I tools/agent/bbscope.py CS2D.exe <ClassName>
The debug scope is {kind u32, name char*, decls[]}; each decl is 40 bytes {kind u32, name char*, tag char*,
offset/address, ...}, ended by kind 0.
"""
import struct, sys

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
    out = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        va, vsz, rsz, rp = struct.unpack_from('<IIII', d, o + 8) [1], struct.unpack_from('<I', d, o + 8)[0], *struct.unpack_from('<II', d, o + 16)
        out.append((base + va, vsz, rp, rsz))
    return base, out

def main():
    d = open(sys.argv[1], 'rb').read()
    base, secs = sections(d)
    def v2o(v):
        for va, vsz, rp, rsz in secs:
            if va <= v < va + max(vsz, rsz): return rp + (v - va)
        return None
    def o2v(o):
        for va, vsz, rp, rsz in secs:
            if rp <= o < rp + rsz: return va + (o - rp)
        return None
    def cstr(v):
        o = v2o(v)
        if o is None: return None
        e = d.find(b'\0', o)
        return d[o:e].decode('latin1')
    name = sys.argv[2].encode() + b'\0'
    i = -1
    while True:
        i = d.find(name, i + 1)
        if i < 0: break
        if i and d[i - 1] != 0: continue
        v = o2v(i)
        if v is None: continue
        p = struct.pack('<Q', v)
        j = -1
        while True:
            j = d.find(p, j + 1)
            if j < 0: break
            if j % 8: continue
            kind = struct.unpack_from('<I', d, j - 8)[0]
            if kind != 2: continue   # class scope
            print('scope at', hex(o2v(j - 8)))
            k = j + 8
            while True:
                dk = struct.unpack_from('<I', d, k)[0]
                if dk == 0 or dk > 8: break
                nm = cstr(struct.unpack_from('<Q', d, k + 8)[0])
                tg = cstr(struct.unpack_from('<Q', d, k + 16)[0])
                val = struct.unpack_from('<Q', d, k + 24)[0]
                print(f'  kind {dk} {nm:24} {tg:10} {val:#x}')
                k += 40
main()
