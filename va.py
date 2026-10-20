import struct
d=open('CS2D.exe','rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
n=struct.unpack_from('<H',d,pe+6)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<Q',d,pe+24+24)[0]
for off in [10629250,10629282,10629226]:
    for i in range(n):
        o=pe+24+opt+40*i
        vs=struct.unpack_from('<I',d,o+8)[0]; va=struct.unpack_from('<I',d,o+12)[0]; rs=struct.unpack_from('<I',d,o+16)[0]; raw=struct.unpack_from('<I',d,o+20)[0]
        if raw<=off<raw+rs:
            print(hex(off),'->',hex(base+va+off-raw));break
