"""Server browser proxy: logs what a client's "Find Servers" sends and forwards it to a dedicated.

Listens on 127.0.0.1:36980 (put that address in a test sys/core/svl.cache) and on 0.0.0.0:36963
(the LAN broadcast port), forwards both to the dedicated on 127.0.0.1:<server port> and logs every
datagram as "t C>S[front port from ip:port] len hex" / "t S>C[front port] len hex".

usage: python -I tools/agent/browser_proxy.py <log> <seconds> [server port = 36970]

svl.cache entries are {ip i32 LE with the first octet in the MSB, port u16 LE}: 127.0.0.1:36980 is
bytes 01 00 00 7f 74 90 after the comment line. Back up and restore the real svl.cache.
"""
import socket, sys, time, select

log = open(sys.argv[1], 'w')
duration = float(sys.argv[2])
server = ('127.0.0.1', int(sys.argv[3]) if len(sys.argv) > 3 else 36970)

fronts = {}
for host, port in (('127.0.0.1', 36980), ('0.0.0.0', 36963)):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((host, port))
    fronts[s] = port
backs = {}
for f in fronts:
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b.bind(('127.0.0.1', 0))
    backs[b] = f
client = {}

t0 = time.time()
while time.time() - t0 < duration:
    r, _, _ = select.select(list(fronts) + list(backs), [], [], 0.2)
    for s in r:
        try:
            d, a = s.recvfrom(65536)
        except ConnectionResetError:   # ICMP port unreachable from an earlier send
            continue
        t = time.time() - t0
        if s in fronts:
            client[s] = a
            next(k for k, v in backs.items() if v is s).sendto(d, server)
            log.write('%9.3f C>S[%d from %s:%d] %4d %s\n' % (t, fronts[s], a[0], a[1], len(d), d.hex(' ')))
        else:
            f = backs[s]
            if f in client:
                f.sendto(d, client[f])
            log.write('%9.3f S>C[%d] %4d %s\n' % (t, fronts[f], len(d), d.hex(' ')))
        log.flush()
