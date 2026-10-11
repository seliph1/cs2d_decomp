import sys
for line in open(sys.argv[1]):
    p=line.split()
    b=bytes(int(x,16) for x in p[3:])
    seq=b[0]|b[1]<<8; pay=b[2:]; i=0
    if seq&1:
        while i+3<=len(pay) and 1<=pay[i]<=4: i+=3
    pay=pay[i:]
    if not pay: continue
    if len(sys.argv)>2 and pay[0] in [int(x) for x in sys.argv[2].split(',')]: continue
    print(p[0],p[1],'seq%d'%seq,' '.join('%02x'%v for v in pay[:60]))
