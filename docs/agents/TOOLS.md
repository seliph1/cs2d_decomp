# Tools available to agents

## Compiler and toolchain

| Tool | Location / notes |
|---|---|
| gcc (MinGW-w64, MSYS2) | `C:\msys64\mingw64\bin\gcc.exe`. `build.bat` prepends it to `PATH`. In Git Bash: `export PATH=/c/msys64/mingw64/bin:$PATH` |
| objdump | Same folder. Use it for functions Ghidra cannot decompile: `objdump -d -M intel --start-address=0x… --stop-address=0x… CS2D.exe` |
| SDL3 | Headers and libs in the MSYS2 mingw64 prefix; `SDL3.dll` sits next to the executables |
| zlib | MSYS2 `libz.a`, linked statically (`-l:libz.a`) so the executables do not need `zlib1.dll` |
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

## Ghidra dump of CS2D.exe (`docs/dump/`)

A full Ghidra decompilation of the **client** `CS2D.exe` (830 `.c` files, ~34k functions, 59 MB), shared by
a friend of the user. It is git-ignored: never commit it. There is no dump of `cs2d_dedicated.exe`.

- **Same binary:** addresses match REA one to one (`1401f7600 Tpl::move`, `14016b5d0 Tgui_sl::draw`,
  `1403012d0 Tgui_sl::pingServers`), so cite them as REA evidence.
- **Layout:** `docs/dump/cs2d/<Class>.c` holds every method of a BlitzMax class (`Tpl.c`, `Tsv.c`, `MuzzleFlash.c`;
  subfolders `gui/`, `lighting/`, `physics/`). Free functions without a name live in `cs2d/_global/<64 KB block>.c`.
  `docs/dump/blitzmax/` is the BlitzMax runtime and Win32 structs.
- **Format:** each function starts with `// <address>  <Class::method>` followed by the BlitzMax signature comment
  (`/* BlitzMax method: Tpl.move (f,f,i,i) */`). Fields and globals are named (`self->feetrot`, `g_map_cellprop`).
- **Limits:** about 27k functions are still `FUN_…`; there is no struct header, so field offsets come from REA or
  the assembly. Ghidra pseudo-C can be wrong on floats, calling conventions ("Unknown calling convention") and
  BlitzMax arrays filled at runtime (they show up as `bbEmptyArray`). Numbers still need a capture or a test.

Use it first to find code, then confirm with REA (`procedure_assembly`) or objdump when the pseudo-C looks odd.
`tools/agent/dumpgrep.sh` wraps the lookups:

| Command | Output |
|---|---|
| `dumpgrep.sh 1401f7600` (or `0x…`, `FUN_…`, `Tpl::move`) | The whole function, with its file |
| `dumpgrep.sh -f Tpl` | Every method of a class with its address |
| `dumpgrep.sh -l '<regex>'` | `file: address name` of every function whose body matches (e.g. `'FUN_1402563a0\('` for callers; the function itself is listed too) |

An address inside a function is not found: the dump only indexes function starts. Use REA
`resolve_containing_procedure` for that.

## Capture scripts (`tools/agent/`)

All output goes to `tools/agent/out/`, which is git-ignored.

| Script | What it does |
|---|---|
| `capture_original.sh <lua> <tag> "<steps>" [map] [secs]` | Dedicated (offline, optional Lua probe) + UDP proxy + **original** `CS2D.exe`, driven by `drive_orig.ps1` |
| `capture_ours.sh <lua> <tag> "<capture steps>" [map] [secs]` | Same, but **our** `cs2d_client.exe --capture:…` with `CS2D_NET_LOG=1` |
| `capture_transfer.sh <ded dir> <tag> "<steps>" [map] [secs] [password]` | A dedicated in **another folder** (e.g. the modded server in `C:\C4`) + proxy + our client run from a fresh copy of the game data in `out/game_<tag>/`, so downloads never land in the repo. Lists the new files. `CLIENT=orig` runs the original `CS2D.exe` there (steps are `drive_orig.ps1` steps); `KEEP=1` reuses the copy (second join). Restores the dedicated's `sys/server.cfg` |
| `udp_proxy.py <listen> <server> <log> <secs>` | Logs every datagram as `t C>S/S>C len hex`. `<server>` is a local port or `host:port` (for a remote server, run the client from a copy such as `out/game_remote_orig/` so downloads stay out of the repo) |
| `browser_proxy.py <log> <secs> [server port]` | Server browser: listens on 127.0.0.1:36980 (put it in a test `sys/core/svl.cache`) and 0.0.0.0:36963 (LAN broadcast), forwards to the dedicated. Back up and restore `svl.cache` |
| `dumpops.py <proxy log> [skip ops csv]` | Strips the Tudp header and ACK triples, then prints one payload per line (e.g. skip pings `229,247,249,1`) |
| `replay_proxy.py <proxy log> <ded log> <fixture>` | Rebuilds a client's entity state from server traffic and compares it with the Lua dump |
| `drive_orig.ps1 -Steps "a;b;c"` | Input for the original window: `focus`, `tap:K`, `down:K`, `up:K`, `wait:ms`, `mouse:x:y` (client coords), `click:n:ms`, `ldown`, `lup`, `wheel:n` (notches, negative = down), `vk:hex[:up]`, `shot:C:/path.png` |
| `dumpgrep.sh <addr \| name> / -f <Class> / -l <regex>` | Looks up functions in the Ghidra dump `docs/dump` (see above) |
| `peptr.py <exe> <hexaddr…>` | Finds 8-byte pointers to an address inside the PE (vtables) |
| `initarrays.py <objdump listing> <start> <end> <global…>` | Recovers arrays the init code fills with straight-line stores (`mov [base+off], imm` or a `lea` constant): prints `global +offset value`. Used for the editor's entity tables (`knowledge/map-editor.md`) |
| `bbscope.py <exe> <Class>` | Prints a BlitzMax class's fields (name, type tag, offset), globals and methods from its debug scope (e.g. `Tdo`, `Tpl`, `Te`) |
| `weapontables.py <objdump listing> <exe> <global…>` | Recovers the per-weapon tables of `init_weapons` (`0x140230000..0x140236000`), which `initarrays.py` cannot follow because each store is indexed by the current id `DAT_140ddf57c`: prints `global id:value …`. Used for the Buy menu stats and the muzzle offsets (`knowledge/buy-menu.md`) |
| `bbstr.py <exe> <hexaddr…>` | Prints the BlitzMax string objects at those addresses: decodes the `bbs__` literals of the dump. Feed it the `lea` targets of a range: `objdump -d -M intel --start-address=… --stop-address=… CS2D.exe \| grep -o "# 0x140a[0-9a-f]*"`. `--array <pointer global> <i or $> <n>` reads a static BBArray (arrays filled at runtime point to `bbEmptyArray`: use `initarrays.py`) |
| `map_grid.c` | Prints the collision or mode grid around a tile |
| `emu_cs2d.py` | Library: runs single `CS2D.exe` functions in Unicorn (`pip install unicorn pefile`) with the Max2D primitives (SetColor, SetBlend, DrawRect, DrawImage, DrawPoly, buffers, ...) replaced by recorders. Win64 calls with 8-byte stack slots; unhooked code runs for real |
| `emu_light.py CS2D.exe <scene> <floor\|day\|main\|color>` | Runs the **original** light engine (`TSmartLighting`) on a scene file written by our client (`lightdump`, `cs2d_light_dump_scene`) and prints its draw calls in our trace format |
| `light_trace.c` | Our light engine on a map and camera: prints the trace (`cs2d_light_set_trace`) and writes the scene file. `DAYLIGHT=<angle>` overrides the sun |
| `light_check.sh <map> <cam x> <cam y> [main floor color]` | Builds `light_trace.exe`, runs both engines and diffs them (`LIGHT_CFG="<light> <mapshadows> <shadows>"`, default `5 4 50`; `DAYLIGHT=`). Exit 1 on a mismatch. Fixtures for `test_light_engine` are `out/<map>_<part>_orig.txt` copied to `tests/data/light_*.txt` |
| `light_diff.py <ours> <orig> [n]` | Compares two traces line by line (float tolerance 0.01 + 1e-4 x value), shows the first n differences |
| `emu_lightshadow.py CS2D.exe [cases]` | Runs `TLightSource.CalculateShadow` alone on wall / light layouts and prints its `DrawLineLight` calls |
| `gen_occlusion_table.py CS2D.exe` | Runs `DrawTilesFloor` on the 256 neighbour patterns and prints the occlusion table pasted into `src/cs2d_light.c` |
| `lua/*.lua` | Dedicated Lua probes; output lines start with `BP` / `WP` / `PD` / `JL` |

Performance (GPU cost), see `knowledge/performance.md` (2026-10-13):

| Script | What it does |
|---|---|
| `perf_gpu_matrix.ps1 -Out <file> [-CaseList @('name:key=val,…')]` | Steady-state GPU: launches our client on a map and samples the Task-Manager counter `\GPU Engine(*)\Utilization Percentage` (`engtype_3D`) for its pid, next to its CPU. Each case rewrites the given `sys/config.cfg` keys and the file is restored from `sys/config.cfg.perfbak` at the end. `-CaseList` defaults to the four shipped/light-off/VSync cases |
| `perf_gpu_frames.ps1 -Label <name> [-Msaa 0] [-Menu] [-Frames 600] [-DrawCalls] [-Config 'key=val,…']` | GPU **ms per frame**: runs `perf_frames.sh` (a known 636-frame `--capture` run) and divides the sampled GPU busy time by the frames. `-Menu` measures the main menu instead of a map, `-Msaa 0` sets `CS2D_MSAA=0`, `-DrawCalls` sets `CS2D_DRAWCALLS=1` (the client then prints `[gl] draws/frame=… verts/frame=… tex_switch/frame=… max_batch=…` on exit, see `src/cs2d_gl.h`), `-Config` applies a `sys/config.cfg` override for the run only (backed up as `sys/config.cfg.perfbak` and restored) |
| `perf_frames.sh [frames] [map]` | The `--capture:"map <map>;!wait <frames>"` invocation `perf_gpu_frames.ps1` wraps (`-` = no map). Runs from Git Bash so the spaces in the capture string survive |

Lua probes:

| Probe | What it does |
|---|---|
| `break_probe.lua` | setpos + AK, logs breaks and attacks |
| `weapon_probe.lua` | Gives AK/USP, logs reload, select, drop, collect and setammo |
| `playerdata_probe.lua` | setmoney, sethealth, setarmor, equip, strip, setmaxhealth (to read opcode 42) |
| `joinleave_probe.lua` | join, leave and team hooks plus a player count every 2 s |
| `twoplayer_probe.lua` | Places players 1 and 2 side by side, AK for player 2 |
| `construct_probe.lua` | Run with `sv_gamemode 3` in `sys/server.cfg` (back it up): logs `build` / `objectkill` / `spawn` / `die` (`CP` lines), spawns several objects around the first spawned player, sets $16000 and kills the player after 25 s |
| `light_probe.lua` | Spawns player 1 on fun_roleplay and moves it through 6 fixed positions, one every 4 s, for light engine screenshots (`drive_orig.ps1` `shot` steps every 4 s) |
| `night_probe.lua` | `sv_daylighttime 90` (night), then `light_probe.lua` |
| `hudtxt_probe.lua` | On join: hudtxt with every align / size / colour tag, hudtxt2, then move / alpha / colour fades at +25 s, an empty text at +32 s and hudtxtclear at +36 s (`HT` lines) |
| `spec_probe.lua` | Adds a T and a CT bot; logs `specswitch` (whom each spectator follows, `SP` lines) and team changes |

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
| `!hold K down\|up` | Press or release a game key and keep it (`tab` = scoreboard) |
| `!mouse x y [down\|up\|rdown\|rup]` | Move the pointer; with `down`/`up` (`rdown`/`rup`), press or release the left (right) button |
| `!shot file.bmp` | Save the current frame |
| `!console`, `!text`, `!key <SDL key> [shift] [ctrl]`, `!wheel n` | UI tests |

Debug console commands:

| Command | What it shows |
|---|---|
| `inv` | Health, armor, money, held weapon, weapons with ammo |
| `where` | Player position |
| `entstate x y` | State of the entity on a tile |
| `entdump` | Positions of all entities in state 1 |
| `objlist` | Dynamic objects (`Tdo.list`): id, type, tile, health, mode, team, owner |
| `walkto tx ty` | Walks the BFS path to a tile |
| `net_send <hex>` | Sends a raw reliable message |
| `consolebench` | Console performance benchmark |
| `light n`, `mapshadows n` | Light engine settings (`light 0` = off) |
| `lightdump [path]` | Writes the light engine scene for `emu_light.py` (default `tools/agent/out/light.scene`) |
| `killmsg k w v [a]` | Adds a kill message (killer, weapon id, victim, assist) like `[19]` |
| `sun n` / `daylight n` | Sets the map's sun angle (`daylight_time`, 0..360) |

With `CS2D_NET_LOG=1`, the client prints every datagram and every console line to stdout.
