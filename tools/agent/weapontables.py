"""Recover the per-weapon tables CS2D.exe init_weapons fills with `mov [base + id*4 + data], imm`, the id being
DAT_140ddf57c (set before each weapon). Prints `global id:value ...` (floats decoded from movss constants;
a float stored with an integer `mov` prints as its bit pattern, e.g. 1036831949 = 0.1f).

usage: objdump -d -M intel --start-address=0x140230000 --stop-address=0x140236000 CS2D.exe > initw.txt
       python -I tools/agent/weapontables.py initw.txt CS2D.exe <global hex>...
"""
import re, sys, struct

listing, exe = sys.argv[1], sys.argv[2]
want = {int(g, 16) for g in sys.argv[3:]}
data = open(exe, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
n = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<Q', data, pe + 24 + 24)[0]
secs = [struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i) for i in range(n)]

def rd(va, fmt):
    for nm, vs, vaddr, rs, ro in secs:
        if base + vaddr <= va < base + vaddr + rs:
            return struct.unpack_from(fmt, data, ro + va - base - vaddr)[0]

R = {}
for r in "abcd":
    for f in (f"r{r}x", f"e{r}x"): R[f] = f"r{r}x"
for r in ("si", "di", "bp"):
    for f in (f"r{r}", f"e{r}"): R[f] = f"r{r}"
for i in range(8, 16):
    for s in ("", "d", "w", "b"): R[f"r{i}{s}"] = f"r{i}"

regs, stack, xmm = {}, {}, {}
cur = None
out = {}
line_re = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$")
for line in open(listing):
    m = line_re.match(line)
    if not m: continue
    op, args = m.group(2), m.group(3)
    cm = re.search(r"# (0x[0-9a-f]+)", args)
    tgt = int(cm.group(1), 16) if cm else None
    args = args.split('#')[0].strip()
    a = [x.strip() for x in re.split(r",(?![^\[]*\])", args)] if args else []
    if op == "mov" and len(a) == 2 and tgt == 0x140ddf57c and a[0].startswith("DWORD PTR [rip"):
        cur = int(a[1], 16) if a[1].startswith("0x") else None; continue
    if op in ("mov", "movzx", "lea", "movss") and len(a) == 2:
        d, s = a
        if d in R:
            dr = R[d]
            if tgt is not None and "rip" in s:
                regs[dr] = ("ID",) if tgt == 0x140ddf57c else ("G", tgt)
            elif m2 := re.match(r"QWORD PTR \[rsp\+(0x[0-9a-f]+)\]", s):
                regs[dr] = stack.get(int(m2.group(1), 16))
            elif m2 := re.match(r"\[(\w+)\+(\w+)\*4(?:\+0x0)?\]", s):
                b = regs.get(R.get(m2.group(1)))
                regs[dr] = ("E", b[1]) if b and b[0] == "G" else None
            elif op == "lea" and (m2 := re.match(r"\[(\w+)\+(\w+)\*1(?:\+0x0)?\]$", s)):
                x, y = regs.get(R.get(m2.group(1))), regs.get(R.get(m2.group(2)))
                if x == ("ID4",) and y and y[0] == "G": regs[dr] = ("E", y[1])
                elif y == ("ID4",) and x and x[0] == "G": regs[dr] = ("E", x[1])
                else: regs[dr] = None
            elif m2 := re.match(r"\[(\w+)\*4\+0x0\]", s):
                regs[dr] = ("ID4",) if regs.get(R.get(m2.group(1))) == ("ID",) else None
            elif s in R:
                regs[dr] = regs.get(R[s])
            else:
                regs[dr] = None
            continue
        if d.startswith("xmm") and tgt is not None:
            xmm[d] = rd(tgt, '<f'); continue
        if m2 := re.match(r"QWORD PTR \[rsp\+(0x[0-9a-f]+)\]", d):
            stack[int(m2.group(1), 16)] = regs.get(R.get(s)); continue
        if m2 := re.match(r"(?:DWORD PTR )?\[(\w+)\+(\w+)\*1(?:\+(0x[0-9a-f]+))?\]", d):
            for x, y in ((m2.group(1), m2.group(2)), (m2.group(2), m2.group(1))):
                v = regs.get(R.get(x))
                if v and v[0] == "E" and v[1] in want and cur is not None and m2.group(3) in (None, "0x0"):
                    val = xmm.get(s) if s.startswith("xmm") else (int(s, 16) if s.startswith("0x") else s)
                    if isinstance(val, int) and val >= 0x80000000: val -= 1 << 32
                    out.setdefault(v[1], {})[cur] = val
                    break
            continue
    if op == "shl" and len(a) == 2 and a[0] in R and a[1] == "0x2":
        if regs.get(R[a[0]]) == ("ID",): regs[R[a[0]]] = ("ID4",)
        continue
    if op == "add" and len(a) == 2 and a[0] in R and a[1] in R:
        x, y = regs.get(R[a[0]]), regs.get(R[a[1]])
        if x == ("ID4",) and y and y[0] == "G": regs[R[a[0]]] = ("E", y[1])
        elif y == ("ID4",) and x and x[0] == "G": regs[R[a[0]]] = ("E", x[1])
        else: regs[R[a[0]]] = None
        continue
    if a and a[0] in R:
        regs[R[a[0]]] = None
    if op == "call":
        for k in ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11"): regs[k] = None

for g in sorted(out):
    print(hex(g), " ".join(f"{k}:{v}" for k, v in sorted(out[g].items())))
