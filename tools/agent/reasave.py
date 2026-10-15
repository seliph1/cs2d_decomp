"""Save an REA procedure_pseudo_code result as C and annotate the BlitzMax strings it references.

usage: python -I tools/agent/reasave.py <rea result json file> <out .c> [exe]

Every `PTR_PTR_14xxxxxxx` that points at a string object gets a `/*"text"*/` comment, and the Tudp
writer / reader vtable calls are shortened (vtable slots: knowledge/dedicated-server.md):
  W[conn].byte(v)  for (**(code **)(**(longlong **)(conn + 0x38) + 0xa0))(*(longlong **)(conn + 0x38),v)
  CREATEPACKET[conn](rel,new)  for the connection's vtable +0x98
  R.byte()         for (**(code **)(*(longlong *)PTR_DAT_1405f7998 + 0xf0))()
"""
import json
import re
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from bbstr import sections, file_offset  # noqa: E402

W = {"0xa0": "byte", "0xa8": "short", "0xb0": "int", "0xb8": "long", "0xc0": "float", "0xd0": "str",
     "0xd8": "lstr", "0xe8": "bank", "0x78": "size"}
R = {"0x80": "left", "0xf0": "byte", "0x100": "short", "0x108": "int", "0x110": "long", "0x118": "float",
     "0x128": "str", "0x130": "lstr"}

WRITER = re.compile(r"\(\*\*\(code \*\*\)\(\*\*\(longlong \*\*\)\(([^()]+?) \+ 0x38\) \+ (0x[0-9a-f]+)\)\)\s*"
                    r"\(\*\(longlong \*\*\)\(\1 \+ 0x38\),?")
CREATE = re.compile(r"\(\*\*\(code \*\*\)\(\*\(longlong \*\)([^()]+?) \+ 0x98\)\)\((?:\(longlong \*\))?\1,")
READER = re.compile(r"\(\*\*\(code \*\*\)\(\*\(longlong \*\)PTR_DAT_1405f7998 \+ (0x[0-9a-f]+)\)\)\(\)")
STRREF = re.compile(r"PTR_PTR_(14[0-9a-f]{7})")


def bbstring(data, secs, addr):
    off = file_offset(secs, addr)
    if off is None or off + 0x10 > len(data):
        return None
    n = struct.unpack_from("<I", data, off + 0xC)[0]
    if n > 400 or off + 0x10 + 2 * n > len(data):
        return None
    try:
        s = data[off + 0x10:off + 0x10 + 2 * n].decode("utf-16-le")
    except UnicodeDecodeError:
        return None
    if n and not all(c.isprintable() or c in "\r\n\t" for c in s):
        return None
    if struct.unpack_from("<Q", data, off)[0] == 0:
        return None
    return s


def main():
    res = json.load(open(sys.argv[1], encoding="utf-8"))
    code = res["raw_result"].replace("\r\n", "\n")
    exe = sys.argv[3] if len(sys.argv) > 3 else "cs2d_dedicated.exe"
    data = open(exe, "rb").read()
    secs = sections(data)
    code = WRITER.sub(lambda m: "W[" + m.group(1) + "]." + W.get(m.group(2), m.group(2)) + "(", code)
    code = CREATE.sub(lambda m: "CREATEPACKET[" + m.group(1) + "](", code)
    code = READER.sub(lambda m: "R." + R.get(m.group(1), m.group(1)) + "()", code)
    cache = {}

    def note(m):
        a = int(m.group(1), 16)
        if a not in cache:
            cache[a] = bbstring(data, secs, a)
        s = cache[a]
        return m.group(0) if s is None else m.group(0) + "/*" + json.dumps(s, ensure_ascii=False) + "*/"
    code = STRREF.sub(note, code)
    open(sys.argv[2], "w", encoding="utf-8").write(code)
    print(sys.argv[2], len(code.split("\n")), "lines")


if __name__ == "__main__":
    main()
