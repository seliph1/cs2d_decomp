import sys, re, collections, math

ents_txt, plan_out, lua_out = sys.argv[1], sys.argv[2], sys.argv[3]

ents = []
for line in open(ents_txt):
    m = re.match(r"#(\d+) (\S+)\s+\(\s*(\d+),\s*(\d+)\) name='(.*?)' trig='(.*?)' ints=\[(.*?)\]", line)
    if m:
        ents.append((int(m[1]), m[2], int(m[3]), int(m[4]), m[5], m[6], [int(v) for v in m[7].split(',')]))

named = collections.defaultdict(list)
for e in ents:
    if e[4]:
        named[e[4]].append(e)

def names(t):
    return [x.strip() for x in t.split(',') if x.strip()]

FWD = {'Trigger_Delay', 'Trigger_Once', 'Trigger_If'}

def maxdelay(name, seen):
    best = 0
    for e in named.get(name, []):
        if e[1] in FWD and name not in seen:
            d = (e[6][1] / 1000 if e[6][1] > 0 else e[6][0]) if e[1] == 'Trigger_Delay' else 0
            for n in names(e[5]):
                best = max(best, d + maxdelay(n, seen | {name}))
    return best

alln = sorted(set(n for e in ents for n in names(e[5])))
plan = [(n, int(math.ceil(maxdelay(n, frozenset())))) for n in alln]

# Plan shared with the C side: "name<TAB>samples" (samples = 1 + longest delay chain in seconds)
with open(plan_out, 'w') as f:
    for n, d in plan:
        f.write('%s\t%d\n' % (n, d + 1))

def lua_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

lua = ['-- Generated: fires every trigger name of the map and dumps all entity states (ground truth)',
       'et_names = {']
for n, d in plan:
    lua.append('  {%s, %d},' % (lua_str(n), d + 1))
lua += [
    '}',
    'et_list = {}',
    'et_step, et_sample = 0, 0',
    'function et_dump(tag)',
    '  local t = {}',
    '  for i, e in ipairs(et_list) do t[i] = entity(e.x, e.y, "state") and "1" or "0" end',
    '  print("ENTDUMP " .. tag .. " " .. table.concat(t))',
    'end',
    'function et_next()',
    '  et_step = et_step + 1',
    '  if et_step > #et_names then print("ENTDONE") return end',
    '  et_sample = 0',
    '  parse("trigger " .. et_names[et_step][1])',
    '  timer(500, "et_take")',
    'end',
    'function et_take()',
    '  et_sample = et_sample + 1',
    '  et_dump(et_step .. ":" .. et_sample)',
    '  if et_sample < et_names[et_step][2] then timer(1000, "et_take") else timer(100, "et_next") end',
    'end',
    'function et_begin()',
    '  et_list = {}',
    '  for _, e in pairs(entitylist()) do et_list[#et_list + 1] = e end',
    '  for i, e in ipairs(et_list) do print("ENTPOS " .. i .. " " .. e.x .. " " .. e.y .. " " .. entity(e.x, e.y, "type")) end',
    '  et_dump("0:0")',
    '  timer(100, "et_next")',
    'end',
    'print("ENTSTART " .. #et_names .. " names")',
    'timer(3000, "et_begin")',
]
open(lua_out, 'w').write('\n'.join(lua) + '\n')
total = sum(0.6 + (d + 1) * 1.0 for n, d in plan)
print('plan steps', len(plan), 'samples', sum(d + 1 for n, d in plan), 'approx seconds', round(total))
