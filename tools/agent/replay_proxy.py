"""Rebuilds the entity state a client would hold from the server->client traffic of a proxy log
(join snapshot section 4, opcode 31 entityevents, opcode 22 round starts) and compares it with the
server's final Lua dump."""
import sys

proxy_log, server_log, fixture = sys.argv[1], sys.argv[2], sys.argv[3]

init = set()
for line in open(fixture):
    if line.startswith('init '):
        for tok in line.split()[1:]:
            x, y = tok.split(',')
            init.add((int(x), int(y)))

pos = {}
last = None
for line in open(server_log, errors='replace'):
    if line.startswith('ENTPOS '):
        _, i, x, y, t = line.split()
        pos[int(i)] = (int(x), int(y))
    elif line.startswith('ENTDUMP '):
        p = line.split()
        last = p[2] if len(p) > 2 else ''
server = {pos[i + 1] for i, b in enumerate(last) if b == '1'}

state = set()
seen_reliable = set()
events = 0
rounds = []
unknown = {}
for line in open(proxy_log):
    p = line.split()
    if p[1] != 'S>C':
        continue
    t = float(p[0])
    b = bytes(int(x, 16) for x in p[3:])
    seq = b[0] | b[1] << 8
    if seq & 1 == 0 and seq != 0:
        if seq in seen_reliable:
            continue           # retransmission
        seen_reliable.add(seq)
    pay = b[2:]
    i = 0
    if seq & 1:
        while i + 3 <= len(pay) and 1 <= pay[i] <= 4:
            i += 3
    while i < len(pay):
        op = pay[i]
        if op == 0xfc and i + 2 < len(pay) and pay[i + 1] == 7 and pay[i + 2] == 4:
            j = i + 3
            n = pay[j]; j += 1
            for _ in range(n):
                x = pay[j] | pay[j + 1] << 8; y = pay[j + 2] | pay[j + 3] << 8; st = pay[j + 4]; j += 5
                (state.add if st else state.discard)((x, y))
            break
        elif op == 31:
            x = pay[i + 1] | pay[i + 2] << 8; y = pay[i + 3] | pay[i + 4] << 8; st = pay[i + 5]
            (state.add if st else state.discard)((x, y))
            events += 1
            i += 6
        elif op == 22:
            n = pay[i + 2]
            state = set(init)
            rounds.append((t, events))
            i += 3 + 16 * n
        elif op == 33:
            i += 12
        elif op == 25:
            i += 10
        elif op == 226:
            ln = pay[i + 4] | pay[i + 5] << 8
            i += 6 + ln + 1
        else:
            unknown[op] = unknown.get(op, 0) + 1
            break

print('entity events received:', events, '| round starts (time, events so far):', rounds)
print('unparsed opcodes (datagram tails dropped):', unknown)
print('reconstructed state-1 count:', len(state), '| server final:', len(server),
      '| only-client:', len(state - server), '| only-server:', len(server - state))
print('RESULT:', 'MATCH' if state == server else 'MISMATCH')
