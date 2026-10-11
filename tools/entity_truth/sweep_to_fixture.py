"""Converts the dedicated server Lua sweep log into a compact, versioned test fixture.

Fixture format (one record per line, '#' comments):
  init <x,y> <x,y> ...                     entities in state 1 right after round start
  step <name> <sample> <+x,y|-x,y> ...     state changes since the previous sample
Samples are taken 0.5 s after triggering <name>, then every 1.0 s; the next name is triggered
0.1 s after the last sample of the previous one.
"""
import sys

log_path, plan_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

pos = {}
dumps = []
for line in open(log_path, errors='replace'):
    if line.startswith('ENTPOS '):
        _, i, x, y, t = line.split()
        pos[int(i)] = (int(x), int(y))
    elif line.startswith('ENTDUMP '):
        parts = line.split()
        tag, bits = parts[1], (parts[2] if len(parts) > 2 else '')
        dumps.append((tag, bits))

plan = [l.rstrip('\n').split('\t') for l in open(plan_path) if l.strip()]
assert dumps and dumps[0][0] == '0:0', 'missing initial dump'
expected_samples = sum(int(s) for _, s in plan)
assert len(dumps) - 1 == expected_samples, (len(dumps) - 1, expected_samples)

def coord(i):
    return '%d,%d' % pos[i + 1]

out = ['# Ground truth from cs2d_dedicated 1.0.1.8 p1 (offline, prison_funst3r), recorded with a Lua',
       '# sweep that fires every trigger name of the map ("trigger <name>") and reads entity(x,y,"state").',
       '# Samples: 0.5 s after the trigger, then every 1.0 s; next trigger 0.1 s after the last sample.',
       'init ' + ' '.join(coord(i) for i, b in enumerate(dumps[0][1]) if b == '1')]
prev = dumps[0][1]
k = 1
for name, samples in plan:
    for s in range(1, int(samples) + 1):
        tag, bits = dumps[k]
        assert tag.split(':')[1] == str(s), (tag, name, s)
        changes = []
        for i, (a, b) in enumerate(zip(prev, bits)):
            if a != b:
                changes.append(('+' if b == '1' else '-') + coord(i))
        out.append('step %s %d %s' % (name, s, ' '.join(changes)))
        prev = bits
        k += 1
open(out_path, 'w', newline='\n').write('\n'.join(out) + '\n')
print('steps', len(plan), 'samples', k - 1, 'lines', len(out))
