import re
import json

sv_server = open('src/cs2d_server.c', encoding='utf-8').read()
# Find all strcmp / _stricmp(cmd, "...") in cs2d_sv_exec
handled_cmds = set(re.findall(r'_stricmp\s*\(\s*cmd\s*,\s*"([^"]+)"\s*\)', sv_server))
handled_cmds.update(re.findall(r'strcmp\s*\(\s*cmd\s*,\s*"([^"]+)"\s*\)', sv_server))

db = json.load(open('docs/cs2d_help_db_complete.json', encoding='utf-8'))
all_cmds = set(db['console_commands'].keys())

server_cmds = []
client_only_cmds = []

for name, data in db['console_commands'].items():
    cats = data.get('categories', [])
    if 'server' in cats or 'settings' in cats or 'bots' in cats or 'security' in cats:
        server_cmds.append(name)
    else:
        client_only_cmds.append(name)

print(f"Total commands in help: {len(all_cmds)}")
print(f"Handled in dedicated server parser: {len(handled_cmds)}")
print(f"Server-relevant commands: {len(server_cmds)}, Client-only: {len(client_only_cmds)}")

missing_server_cmds = sorted(list(set(server_cmds) - handled_cmds))
print(f"Missing server-specific commands ({len(missing_server_cmds)}): {missing_server_cmds[:25]}")

