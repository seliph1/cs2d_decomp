d=open('CS2D.exe','rb').read()
s='unexpected joindata'.encode('utf-16le')
i=d.find(s)
while i>=0:
    txt=d[i:i+160].decode('utf-16le','ignore').split('\x00')[0]
    print(hex(i),repr(txt))
    i=d.find(s,i+1)
