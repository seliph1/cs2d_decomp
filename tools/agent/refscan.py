"""Byte-level xref/string scanner for the shipped CS2D binaries (no Ghidra needed).

Used when the REA/Ghidra session is not available (it times out on CS2D.exe while the analysis
is still running): the raw PE bytes are scanned directly instead of a `objdump -d` listing.

usage:
    python -I tools/agent/refscan.py <exe> call 0x14015c840 [...]   direct `call rel32` sites
    python -I tools/agent/refscan.py <exe> jmp  0x14015c840 [...]   direct `jmp rel32` sites
    python -I tools/agent/refscan.py <exe> rip  0x1405a6760 [...]   RIP-relative operands (modrm mod=00 rm=101)
    python -I tools/agent/refscan.py <exe> str  "V.I.P." [...]       UTF-16LE occurrences (BlitzMax strings)
    python -I tools/agent/refscan.py <exe> ascii "socket" [...]      ASCII occurrences

Addresses are virtual (image base + RVA). Output: `<kind> <target> at 0x<site>` per hit.
`rip` hits inside the code section are real references; a hit in .rdata/.data is a coincidence.
"""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
n = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<Q', data, pe + 24 + 24)[0]
secs = [struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i) for i in range(n)]
# PE section header: Name, VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData
secs = [(s[0].rstrip(b'\0').decode(), base + s[2], s[1], s[4], s[3]) for s in secs]  # name, va, vsize, raw, rsize


def off_of(va):
    for nm, vaddr, vs, raw, rs in secs:
        if vaddr <= va < vaddr + vs:
            return raw + va - vaddr
    return None


def va_of(off):
    for nm, vaddr, vs, raw, rs in secs:
        if raw <= off < raw + rs:
            return vaddr + off - raw
    return None


def code_ranges():
    return [(vaddr, raw, rs) for nm, vaddr, vs, raw, rs in secs if 'CODE' in nm or nm == '.text']


kind = sys.argv[2]
for arg in sys.argv[3:]:
    if kind in ('str', 'ascii'):
        pat = arg.encode('utf-16-le') if kind == 'str' else arg.encode('latin-1')
        i = data.find(pat)
        while i >= 0:
            va = va_of(i)
            print(f'{kind} {arg!r} at {hex(va) if va else hex(i)}')
            i = data.find(pat, i + 1)
        continue
    target = int(arg, 16)
    for vaddr, raw, rs in code_ranges():
        blob = data[raw:raw + rs]
        if kind in ('call', 'jmp'):
            op = b'\xe8' if kind == 'call' else b'\xe9'
            i = blob.find(op)
            while i >= 0:
                if i + 5 <= len(blob) and vaddr + i + 5 + struct.unpack_from('<i', blob, i + 1)[0] == target:
                    print(f'{kind} {arg} at {hex(vaddr + i)}')
                i = blob.find(op, i + 1)
        elif kind == 'rip':
            for i in range(len(blob) - 5):
                if (blob[i] & 0xC7) == 0x05 and vaddr + i + 5 + struct.unpack_from('<i', blob, i + 1)[0] == target:
                    print(f'rip {arg} at {hex(vaddr + i)}')
        else:
            sys.exit(f'unknown kind {kind}')
