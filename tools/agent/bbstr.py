"""Print BlitzMax string objects at virtual addresses of a PE.

usage: python -I tools/agent/bbstr.py <exe> <hexaddr...>
       python -I tools/agent/bbstr.py <exe> --array <pointer global hex> <i|$> <count>

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
    if len(sys.argv) > 2 and sys.argv[2] == "--array":
        for i, v in enumerate(read_array(data, secs, int(sys.argv[3], 16), sys.argv[4], int(sys.argv[5]))):
            print(i, repr(v))
        return
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



def read_array(data, secs, ptr_va, kind, count):
    """BBArray behind the pointer global ptr_va: element i at array + u16 [+0x1a] + i * size."""
    off = file_offset(secs, ptr_va)
    arr = struct.unpack_from("<Q", data, off)[0]
    aoff = file_offset(secs, arr)
    start = struct.unpack_from("<H", data, aoff + 0x1A)[0]
    out = []
    for i in range(count):
        if kind == "i":
            out.append(struct.unpack_from("<i", data, aoff + start + 4 * i)[0])
        else:
            s = struct.unpack_from("<Q", data, aoff + start + 8 * i)[0]
            so = file_offset(secs, s)
            n = struct.unpack_from("<I", data, so + 0xC)[0]
            out.append(data[so + 0x10: so + 0x10 + 2 * n].decode("utf-16-le", "replace"))
    return out


if __name__ == "__main__":
    main()
