"""Recover BlitzMax arrays that CS2D.exe fills with straight-line code during init.

usage: python -I tools/agent/initarrays.py <objdump -d -M intel listing> <start hex> <end hex> <global hex>...

The init code loads an array global (`mov reg,[rip+..] # global`), adds its data offset
(`movzx r,WORD PTR [reg+0x1a]` + `add`), then stores immediates or `lea` constants at fixed offsets.
This tracks registers symbolically over [start, end) and prints every store into the named globals as
`global +offset value` (value in hex; string objects can be decoded with bbstr.py).
"""
import re
import sys

listing, start, end = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
wanted = {int(g, 16) for g in sys.argv[4:]}

REG64 = {"rax": "rax", "eax": "rax", "rbx": "rbx", "ebx": "rbx", "rcx": "rcx", "ecx": "rcx", "rdx": "rdx",
         "edx": "rdx", "rsi": "rsi", "esi": "rsi", "rdi": "rdi", "edi": "rdi", "rbp": "rbp", "ebp": "rbp"}
for i in range(8, 16):
    for suf in ("", "d", "w", "b"):
        REG64[f"r{i}{suf}"] = f"r{i}"


def reg(name):
    return REG64.get(name.strip())


regs = {}
stores = []
line_re = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$")
with open(listing) as f:
    for line in f:
        m = line_re.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        if addr < start or addr >= end:
            continue
        op, args = m.group(2), m.group(3)
        comment = None
        if "#" in args:
            args, comment = args.split("#", 1)
            comment = int(comment.split()[0], 16)
        args = args.strip()
        if op == "lea":
            d, _ = args.split(",", 1)
            if reg(d) and comment is not None:
                regs[reg(d)] = ("const", comment)
            elif reg(d):
                regs.pop(reg(d), None)
        elif op == "mov" and args.split(",", 1)[0].strip() in REG64 and "PTR [rip" in args and comment in wanted:
            regs[reg(args.split(",", 1)[0])] = ("ptr", comment)
        elif op == "movzx" and "+0x1a]" in args:
            d, src = args.split(",", 1)
            b = reg(re.search(r"\[(\w+)\+0x1a\]", src).group(1))
            if b in regs and regs[b][0] in ("ptr",):
                regs[reg(d)] = ("hdr", regs[b][1])
            else:
                regs.pop(reg(d), None)
        elif op == "add":
            d, s = [x.strip() for x in args.split(",", 1)]
            rd, rs = reg(d), reg(s)
            a, b = regs.get(rd), regs.get(rs)
            if a and b and {a[0], b[0]} == {"ptr", "hdr"} and a[1] == b[1]:
                regs[rd] = ("base", a[1])
            elif rd:
                regs.pop(rd, None)
        elif op == "mov" and "PTR [" in args.split(",", 1)[0]:
            dst, src = args.rsplit(",", 1)
            mm = re.search(r"\[(\w+)(?:\+(\w+)\*1)?(?:\+0x([0-9a-f]+))?\]", dst)
            if not mm:
                continue
            b, idx, disp = reg(mm.group(1)), mm.group(2), int(mm.group(3) or "0", 16)
            base = regs.get(b)
            if idx:
                ib = regs.get(reg(idx))
                if base and ib and {base[0], ib[0]} == {"ptr", "hdr"} and base[1] == ib[1]:
                    base = ("base", base[1])
                else:
                    base = None
            if not base or base[0] != "base":
                continue
            src = src.strip()
            if src.startswith("0x") or src.lstrip("-").isdigit():
                val = int(src, 16) if src.startswith("0x") else int(src)
            elif reg(src) and regs.get(reg(src), ("",))[0] == "const":
                val = regs[reg(src)][1]
            else:
                continue
            stores.append((base[1], disp, val))
        elif op in ("call", "ret", "jmp"):
            regs = {k: v for k, v in regs.items() if k in ("rbx", "rsi", "rdi", "rbp", "r12", "r13", "r14", "r15")}
        else:
            d = args.split(",", 1)[0].strip()
            if reg(d) and op not in ("cmp", "test", "push"):
                regs.pop(reg(d), None)

for g, off, v in stores:
    print(f"{g:#x} +{off:#x} {v:#x}")
