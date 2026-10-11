"""UDP logging proxy: client <-> 127.0.0.1:LISTEN <-> 127.0.0.1:SERVER. Logs every datagram in hex with a timestamp."""
import socket, sys, time, select

listen_port, server_port, log_path, duration = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], float(sys.argv[4])

front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
front.bind(('127.0.0.1', listen_port))
back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
back.bind(('127.0.0.1', 0))
server = ('127.0.0.1', server_port)
client = None
t0 = time.time()
with open(log_path, 'w') as log:
    while time.time() - t0 < duration:
        r, _, _ = select.select([front, back], [], [], 0.2)
        for s in r:
            data, addr = s.recvfrom(65536)
            t = time.time() - t0
            if s is front:
                client = addr
                back.sendto(data, server)
                log.write('%9.3f C>S %4d %s\n' % (t, len(data), data.hex(' ')))
            elif client:
                front.sendto(data, client)
                log.write('%9.3f S>C %4d %s\n' % (t, len(data), data.hex(' ')))
        log.flush()
