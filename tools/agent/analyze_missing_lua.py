import re
import json

sv_lua = open('src/cs2d_sv_lua.c', encoding='utf-8').read()
registered_lua = set(re.findall(r'lua_register\s*\([^,]+,\s*"([^"]+)"', sv_lua))

db = json.load(open('docs/cs2d_help_db_complete.json', encoding='utf-8'))
all_lua = set(db['lua_commands'].keys())

missing_lua = sorted(list(all_lua - registered_lua))
present_lua = sorted(list(all_lua & registered_lua))

print(f"Registered Lua: {len(registered_lua)}, Documented Lua: {len(all_lua)}")
print(f"Present ({len(present_lua)}): {present_lua}")
print(f"Missing ({len(missing_lua)}): {missing_lua}")

# Categorize missing
for name in missing_lua:
    info = db['lua_commands'][name]
    cats = info.get('categories', [])
    params = info.get('parameters', [])
    print(f"- {name} [{', '.join(cats)}]: {params}")

