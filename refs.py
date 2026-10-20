import struct
d=open('CS2D.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]
secs=[]
for i in range(n):
    o=pe+24+opt+40*i
    name=d[o:o+8].rstrip(b'\0')
    vs=struct.unpack_from('<I',d,o+8)[0]; va=struct.unpack_from('<I',d,o+12)[0]; rs=struct.unpack_from('<I',d,o+16)[0]; raw=struct.unpack_from('<I',d,o+20)[0]
    secs.append((name,va,vs,raw,rs))
def off2va(off):
    for name,va,vs,raw,rs in secs:
        if raw<=off<raw+rs: return base+va+off-raw
targets={0x140a24082:'unknown',0x140a240a2:'expected',0x140a2406a:'old?'}
for name,va,vs,raw,rs in secs:
    if name!=b'.text': continue
    for off in range(raw,raw+rs-4):
        disp=struct.unpack_from('<i',d,off)[0]
        nxt=off2va(off+4)
        tgt=nxt+disp
        if tgt in targets:
            print(targets[tgt],hex(off2va(off)), 'disp',hex(disp&0xffffffff))
