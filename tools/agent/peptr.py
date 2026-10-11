import sys,struct
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]
secs=[]
for i in range(n):
    o=pe+24+opt+40*i
    va,vs,raw,rs=struct.unpack_from('<IIII',d,o+8)[0:1]+struct.unpack_from('<I',d,o+8)+struct.unpack_from('<I',d,o+20)+struct.unpack_from('<I',d,o+16)
    va=struct.unpack_from('<I',d,o+12)[0]; vs=struct.unpack_from('<I',d,o+8)[0]
    secs.append((va,vs,raw,rs))
def off2va(off):
    for va,vs,raw,rs in secs:
        if raw<=off<raw+rs: return base+va+off-raw
for a in sys.argv[2:]:
    t=int(a,16)
    pat=struct.pack('<Q',t); i=d.find(pat)
    while i>=0:
        print(a,'ptr at',hex(off2va(i)))
        i=d.find(pat,i+1)
    # rip-relative lea refs: search for disp32 such that next_ip+disp=t
    for k in range(len(d)-7):
        pass
