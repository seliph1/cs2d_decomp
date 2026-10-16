import re
import json

with open("src/cs2d_sv_lua.c", "r", encoding="utf-8") as f:
    lua_c = f.read()

# Registered lua functions: lua_register(lua->L, "name", func)
funcs = re.findall(r'lua_register\s*\(\s*lua->L\s*,\s*"([^"]+)"\s*,', lua_c)
registered_names = set(funcs)

with open("docs/cs2d_help_db_complete.json", "r", encoding="utf-8") as f:
    help_db = json.load(f)

official_lua = help_db.get("lua_commands", {})
print(f"Total registered Lua functions in C: {len(registered_names)}")
print(f"Total official Lua functions: {len(official_lua)}")

missing = [k for k in official_lua if k not in registered_names]
print(f"Missing official Lua functions ({len(missing)}): {missing}")

# Hooks dispatched in C: dispatch_hook(sv, "name", ...)
hook_dispatches = set(re.findall(r'dispatch_hook\s*\(\s*sv\s*,\s*"([^"]+)"', lua_c))
with open("tools/agent/extracted_api.json", "r", encoding="utf-8") as f:
    api = json.load(f)

official_hooks = set(api.get("hooks", []))
print(f"Total dispatched hooks in C: {len(hook_dispatches)}")
print(f"Total official hooks: {len(official_hooks)}")

missing_hooks = [h for h in sorted(list(official_hooks)) if h not in hook_dispatches]
print(f"Missing hooks ({len(missing_hooks)}): {missing_hooks}")

