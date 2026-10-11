# Tools available to agents

## Compiler and toolchain

| Tool | Location / notes |
|---|---|
| gcc (MinGW-w64, MSYS2) | `C:\msys64\mingw64\bin\gcc.exe`. `build.bat` prepends it to `PATH`. In Git Bash: `export PATH=/c/msys64/mingw64/bin:$PATH` |
| objdump | Same folder. Use it for functions Ghidra cannot decompile: `objdump -d -M intel --start-address=0x… --stop-address=0x… CS2D.exe` |
| SDL3 | Headers and libs in the MSYS2 mingw64 prefix; `SDL3.dll` sits next to the executables |
| Python 3.14 | `python` (Pillow installed). Run scripts that read untrusted data with `python -I` |
| Git Bash + PowerShell 5.1 | Both are available. `drive_orig.ps1` needs PowerShell |

Quick single-file builds (for example a tool linking only the map loader):

```bash
export PATH=/c/msys64/mingw64/bin:$PATH
gcc -Iinclude tools/agent/map_grid.c src/cs2d_map.c -o tools/agent/out/map_grid.exe
tools/agent/out/map_grid.exe maps/prison_funst3r.map 69 53 6      # collision grid around a tile (W wall, O obstacle)
tools/agent/out/map_grid.exe maps/prison_funst3r.map 69 53 6 m    # tile modes instead
```

Entity dump: `gcc -Iinclude tools/entity_truth/dump_entities.c src/cs2d_map.c -o tools/agent/out/dump.exe`
lists every entity with its ints and strs.

## REA MCP server (Ghidra 12.1.2 backend)

REA is the reverse-engineering MCP server (`rea-agents`, Ghidra headless). It analyzes one binary at a
time: `open_binary` switches between `CS2D.exe` and `cs2d_dedicated.exe`. The first analysis of each
binary takes about 2 minutes.

Configuration, the same for every agent (stdio):

```json
{
  "mcpServers": {
    "rea": {
      "type": "stdio",
      "command": "C:\\Program Files\\nodejs\\node.exe",
      "args": ["C:\\Users\\Gabriel\\AppData\\Local\\npm-cache\\_npx\\af752b90495482d7\\node_modules\\rea-agents\\scripts\\rea.mjs", "mcp"],
      "env": {
        "GHIDRA_INSTALL_DIR": "C:\\Users\\Gabriel\\Documents\\Aplicativos\\ghidra",
        "JAVA_HOME": "C:\\Users\\Gabriel\\Documents\\Aplicativos\\ghidra\\jdk-21.0.12.1"
      }
    }
  }
}
```

- Claude Code: `.mcp.json` at the repo root (project scope), also registered in the user config.
- Antigravity: `~/.gemini/antigravity/mcp_config.json`.
- If the npx cache path changes (`af752b90495482d7`), find `rea-agents\scripts\rea.mjs` under
  `%LOCALAPPDATA%\npm-cache\_npx\` and update both files.

Most useful tools:

| Tool | Use |
|---|---|
| `search_strings` | Find strings (regex mode available) |
| `xrefs` | References to an address (use the string object = text address − 0x10) |
| `resolve_containing_procedure` | Which function contains an address |
| `procedure_pseudo_code` | Decompile one function |
| `read_bytes` | Raw bytes at an address |

Limitations:

- `procedure_pseudo_code` fails ("Response buffer size exceeded") on huge functions. Use objdump.
- BlitzMax calls methods through type tables, so direct callers are often missing. Search the PE for
  the function pointer with `python -I tools/agent/peptr.py CS2D.exe <hexaddr>` to find the vtable slot.
- Globals loaded by the resource registry are documented in `docs/cs2d_gfx_descriptors.json`
  (`globalVa` and file).

## Capture scripts (`tools/agent/`)

All output goes to `tools/agent/out/`, which is git-ignored.

| Script | What it does |
|---|---|
| `capture_original.sh <lua> <tag> "<steps>" [map] [secs]` | Dedicated (offline, optional Lua probe) + UDP proxy + **original** `CS2D.exe`, driven by `drive_orig.ps1` |
| `capture_ours.sh <lua> <tag> "<capture steps>" [map] [secs]` | Same, but **our** `cs2d_client.exe --capture:…` with `CS2D_NET_LOG=1` |
| `udp_proxy.py <listen> <server> <log> <secs>` | Logs every datagram as `t C>S/S>C len hex` |
| `browser_proxy.py <log> <secs> [server port]` | Server browser: listens on 127.0.0.1:36980 (put it in a test `sys/core/svl.cache`) and 0.0.0.0:36963 (LAN broadcast), forwards to the dedicated. Back up and restore `svl.cache` |
| `dumpops.py <proxy log> [skip ops csv]` | Strips the Tudp header and ACK triples, then prints one payload per line (e.g. skip pings `229,247,249,1`) |
| `replay_proxy.py <proxy log> <ded log> <fixture>` | Rebuilds a client's entity state from server traffic and compares it with the Lua dump |
| `drive_orig.ps1 -Steps "a;b;c"` | Input for the original window: `focus`, `tap:K`, `down:K`, `up:K`, `wait:ms`, `mouse:x:y` (client coords), `click:n:ms`, `ldown`, `lup`, `vk:hex[:up]`, `shot:C:/path.png` |
| `peptr.py <exe> <hexaddr…>` | Finds 8-byte pointers to an address inside the PE (vtables) |
| `map_grid.c` | Prints the collision or mode grid around a tile |
| `lua/*.lua` | Dedicated Lua probes; output lines start with `BP` / `WP` / `PD` / `JL` |

Lua probes:

| Probe | What it does |
|---|---|
| `break_probe.lua` | setpos + AK, logs breaks and attacks |
| `weapon_probe.lua` | Gives AK/USP, logs reload, select, drop, collect and setammo |
| `playerdata_probe.lua` | setmoney, sethealth, setarmor, equip, strip, setmaxhealth (to read opcode 42) |
| `joinleave_probe.lua` | join, leave and team hooks plus a player count every 2 s |
| `twoplayer_probe.lua` | Places players 1 and 2 side by side, AK for player 2 |

Notes:

- The dedicated's `-lua <path>` is **relative to `sys/lua`**. The scripts pass `../../tools/agent/lua/<file>`.
- The dedicated does not read stdin. Use Lua `timer(ms, "fn")` plus `parse("<server command>")` to act
  on it, and `print` to report.
- `mp_idlekick` kicks clients that send nothing for a while, so keep test runs short or keep moving.
- Ports: server 36970, proxies 36980 and 36981.

## Our client's test harness (`cs2d_client.exe --capture:<steps>`)

The harness runs one step every 5 frames, then saves `screens/capture.bmp` and exits. Steps:

| Step | Effect |
|---|---|
| any console command | e.g. `map prison_funst3r`, `connect 127.0.0.1 36980`, `tp 105 57`, `equip 30` (`equip` takes an id; the name is "AK-47") |
| `!wait N` | Wait N frames |
| `!press K` | Press and release a game key |
| `!mouse x y [down\|up]` | Move the pointer; with `down`/`up`, press or release the button |
| `!shot file.bmp` | Save the current frame |
| `!console`, `!text`, `!key <SDL key> [shift] [ctrl]`, `!wheel n` | UI tests |

Debug console commands:

| Command | What it shows |
|---|---|
| `inv` | Health, armor, money, held weapon, weapons with ammo |
| `where` | Player position |
| `entstate x y` | State of the entity on a tile |
| `entdump` | Positions of all entities in state 1 |
| `walkto tx ty` | Walks the BFS path to a tile |
| `net_send <hex>` | Sends a raw reliable message |
| `consolebench` | Console performance benchmark |

With `CS2D_NET_LOG=1`, the client prints every datagram and every console line to stdout.
