import urllib.request
import re
import json

base_url = 'https://www.cs2d.com/'
hdr = {'User-Agent': 'Mozilla/5.0'}

def get_soup(url):
    req = urllib.request.Request(url, headers=hdr)
    with urllib.request.urlopen(req, timeout=10) as resp:
        return resp.read().decode('latin1', errors='ignore')

# 1. Console commands
print('Fetching console commands list...')
html_cmds = get_soup(base_url + 'help.php?cat=all')
cmd_names = re.findall(r'help\.php\?cat=all&(?:amp;)?cmd=([^"#]+)#cmd', html_cmds)

# 2. Hooks
print('Fetching hooks list...')
html_hooks = get_soup(base_url + 'help.php?hookcat=all')
hook_names = re.findall(r'help\.php\?hookcat=all&(?:amp;)?hook=([^"#]+)#hook', html_hooks)

# 3. Lua commands
print('Fetching lua commands list...')
html_lua = get_soup(base_url + 'help.php?luacat=all')
lua_names = re.findall(r'help\.php\?luacat=all&(?:amp;)?luacmd=([^"#]+)#cmd', html_lua)

print(f'Total unique cmds: {len(set(cmd_names))}, hooks: {len(set(hook_names))}, lua: {len(set(lua_names))}')
print('Sample cmds:', list(set(cmd_names))[:10])
print('Sample hooks:', list(set(hook_names))[:10])
print('Sample lua:', list(set(lua_names))[:10])

