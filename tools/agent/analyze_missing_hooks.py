import re
import json

sv_lua = open('src/cs2d_sv_lua.c', encoding='utf-8').read()
sv_game = open('src/cs2d_sv_game.c', encoding='utf-8').read()
sv_server = open('src/cs2d_server.c', encoding='utf-8').read()
all_src = sv_lua + '\n' + sv_game + '\n' + sv_server

handled_hooks = set(re.findall(r'dispatch_hook\s*\(\s*[^,]+,\s*"([^"]+)"', all_src))
handled_hooks.update(re.findall(r'cs2d_sv_lua_hook_([a-zA-Z0-9_]+)', all_src))

db = json.load(open('docs/cs2d_help_db_complete.json', encoding='utf-8'))
all_hooks = set(db['lua_hooks'].keys())

missing_hooks = sorted(list(all_hooks - handled_hooks))
present_hooks = sorted(list(all_hooks & handled_hooks))

print(f"Handled hooks: {len(handled_hooks)}, Documented hooks: {len(all_hooks)}")
print(f"Present ({len(present_hooks)}): {present_hooks}")
print(f"Missing ({len(missing_hooks)}): {missing_hooks}")

for name in missing_hooks:
    info = db['lua_hooks'][name]
    cats = info.get('categories', [])
    params = info.get('parameters', [])
    print(f"- {name} [{', '.join(cats)}]: {params}")

