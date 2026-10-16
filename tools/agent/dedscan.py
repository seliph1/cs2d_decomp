#!/usr/bin/env python3
"""Disassemble a range of `cs2d_dedicated.exe` and inspect it (the dedicated has no Ghidra dump).

objdump (MSYS2 MinGW) prints no symbols for the stripped PE, so this tool keeps one text disassembly
in `tools/agent/out/ded.asm` (built on first use, ~80 MB, git-ignored) and answers questions over it:
the code around an address, the callers of a function, the call targets inside a range, and the
readers/writers of a global. That is how the round-end call sites of `FUN_14015ce30` and the
`DAT_1407dfe48` gate were found (see `docs/agents/knowledge/standard-gamemode.md`).

usage:
  python tools/agent/dedscan.py show <addr> [lines before] [lines after]
  python tools/agent/dedscan.py fn <addr>                 # nearest preceding call target (function head)
  python tools/agent/dedscan.py callers <addr> [before]    # every `call <addr>` with context
  python tools/agent/dedscan.py calltargets <lo> <hi>      # distinct call targets inside [lo, hi)
  python tools/agent/dedscan.py refs <sym>                 # every line mentioning a global address
  python tools/agent/dedscan.py writes <sym>               # the stores (mov/inc/... dest) of a global
  python tools/agent/dedscan.py build                      # (re)build the disassembly cache
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "tools", "agent", "out", "ded.asm")
EXE = os.path.join(ROOT, "cs2d_dedicated.exe")
OBJDUMP = r"C:\msys64\mingw64\bin\objdump.exe"

L = None


def build():
    os.makedirs(os.path.dirname(ASM), exist_ok=True)
    out = subprocess.run([OBJDUMP, "-d", "-M", "intel", EXE], capture_output=True, text=True)
    with open(ASM, "w", encoding="utf-8") as f:
        f.write(out.stdout)
    print("wrote %s (%d bytes)" % (ASM, len(out.stdout)))


def lines():
    global L
    if L is None:
        if not os.path.exists(ASM):
            build()
        L = open(ASM, encoding="utf-8", errors="replace").read().splitlines()
    return L


def mnemonic(line):
    return line.split("\t", 2)[-1].strip().split(None, 1)[0] if "\t" in line else ""


def operand1(line):
    parts = line.split("\t", 2)[-1].strip().split(None, 1)
    return parts[1].split(",", 1)[0] if len(parts) > 1 else ""


def index_of(addr):
    needle = "%x" % addr
    for i, l in enumerate(lines()):
        head = l.split("\t", 1)[0].strip().rstrip(":")
        if head == needle:
            return i
    return -1


def show(addr, before, after):
    i = index_of(addr)
    if i < 0:
        print("address %x not found" % addr)
        return
    for j in range(max(0, i - before), min(len(lines()), i + after)):
        print((">>" if j == i else "  "), lines()[j].rstrip())


def fn(addr):
    """The nearest preceding line that some call targets: a decent function-head heuristic."""
    i = index_of(addr)
    targets = set()
    for l in lines()[:i]:
        if "\tcall" in l:
            t = l.split("\t", 2)[-1].strip().split()[-1]
            try:
                targets.add(int(t, 16))
            except ValueError:
                pass
    lo = max((t for t in targets if t <= addr), default=None)
    print("%x" % lo if lo else "none")


def callers(addr, before=5):
    target = "%x" % addr
    for i, l in enumerate(lines()):
        if "\tcall" in l and l.rstrip().endswith(target):
            print("---- line %d" % i)
            for j in range(max(0, i - before), i + 2):
                print((">>" if j == i else "  "), lines()[j].rstrip())


def calltargets(lo, hi):
    seen = {}
    for l in lines():
        if "\tcall" not in l:
            continue
        t = l.split("\t", 2)[-1].strip().split()[-1]
        try:
            v = int(t, 16)
        except ValueError:
            continue
        if lo <= v < hi:
            seen[v] = seen.get(v, 0) + 1
    for v in sorted(seen):
        print("%x  x%d" % (v, seen[v]))


def refs(sym):
    sym = "%x" % int(sym, 16) if not sym.startswith("0x") else sym[2:]
    for l in lines():
        if sym in l and "\t" in l:
            print(l.strip())


def writes(sym):
    sym = "%x" % int(sym, 16) if not sym.startswith("0x") else sym[2:]
    for l in lines():
        if sym not in l or "\t" not in l:
            continue
        if mnemonic(l) in ("cmp", "test"):
            continue
        if "rip+" in operand1(l):
            print(l.strip())


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "build":
        build()
    elif cmd == "show":
        show(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 0,
             int(sys.argv[4]) if len(sys.argv) > 4 else 16)
    elif cmd == "fn":
        fn(int(sys.argv[2], 16))
    elif cmd == "callers":
        callers(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 5)
    elif cmd == "calltargets":
        calltargets(int(sys.argv[2], 16), int(sys.argv[3], 16))
    elif cmd == "refs":
        refs(sys.argv[2])
    elif cmd == "writes":
        writes(sys.argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
