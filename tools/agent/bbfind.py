"""Find BlitzMax string objects by their text (the inverse of bbstr.py).

usage: python -I tools/agent/bbfind.py <exe> <text...>

Prints the object address of every string whose whole text equals <text> ([length u32 +0xc][UTF-16 +0x10]).
Then grep a disassembly for the `lea` of that address: objdump ... | grep "# 0x<addr>".
"""
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from bbstr import sections  # noqa: E402


def main():
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)
    for text in sys.argv[2:]:
        needle = struct.pack("<I", len(text)) + text.encode("utf-16-le")
        i = data.find(needle)
        while i >= 0:
            obj = i - 0xC
            for va, size, raw in secs:
                if raw <= obj < raw + size:
                    print(f"{text!r} 0x{va + obj - raw:x}")
            i = data.find(needle, i + 1)


if __name__ == "__main__":
    main()
