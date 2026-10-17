"""Read a 4 byte value at a virtual address of a PE (no dependencies).

usage: python -I tools/agent/pevalue.py <exe> <addr> [addr...]

The address includes the image base (e.g. 0x140a14dac in CS2D.exe). Prints the file offset, the raw bytes,
the u32 and the f32 reading. This is how the calibrated constants were read (`mp_teleportreload`
DAT_140a14d48 = 5, DAT_140a14dac = 10, DAT_140a14db0); `bbfind.py` gives the address of a string object.
"""
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from bbstr import sections  # noqa: E402


def main():
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)   # (absolute va, size, raw offset)
    for arg in sys.argv[2:]:
        t = int(arg, 16)
        for va, size, raw in secs:
            if va <= t < va + size:
                if raw == 0:
                    # A section without file bytes (the zero-initialized block of CS2D.exe at
                    # 0x140dde000): the value is a runtime flag, not an image constant.
                    print(f"{arg} no file bytes (zero-initialized section at 0x{va:x})")
                    break
                off = raw + (t - va)
                chunk = data[off:off + 4]
                print(f"{arg} file 0x{off:x} {chunk.hex()} u32={struct.unpack('<I', chunk)[0]} "
                      f"f32={struct.unpack('<f', chunk)[0]:g}")
                break
        else:
            print(f"{arg} not mapped")


if __name__ == "__main__":
    main()
