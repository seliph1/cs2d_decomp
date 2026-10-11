"""Print BlitzMax string objects at virtual addresses of a PE.

usage: python -I tools/agent/bbstr.py <exe> <hexaddr...>

A BlitzMax string object is [class ptr][hash +8][length u32 +0xc][UTF-16 text +0x10]. Pass the object
address (what `lea` loads, e.g. `bbs__` in the Ghidra dump), not the text address.
"""
import io
import struct
import sys


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    off = pe + 24 + optsz
    out = []
    for i in range(nsec):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + i * 40 + 8)
        out.append((base + va, max(vsize, rsize), raw))
    return out


def file_offset(secs, addr):
    for va, size, raw in secs:
        if va <= addr < va + size:
            return raw + (addr - va)
    return None


def main():
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)
    for a in sys.argv[2:]:
        addr = int(a, 16)
        off = file_offset(secs, addr)
        if off is None:
            print(f"{addr:#x}: not mapped")
            continue
        n = struct.unpack_from("<I", data, off + 0xC)[0]
        if n > 4096:
            print(f"{addr:#x}: length {n}, not a string object?")
            continue
        text = data[off + 0x10: off + 0x10 + 2 * n].decode("utf-16-le", "replace")
        print(f"{addr:#x}: {text!r}")


if __name__ == "__main__":
    main()
