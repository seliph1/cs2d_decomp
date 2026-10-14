# AGENTS.md — CS2D decompilation / reimplementation

Shared instructions for every AI agent working in this repository (Claude Code, Antigravity, others).
`CLAUDE.md`, `GEMINI.md` and `.agent/rules/` only point here: **edit this file, not the pointers.**

## Goal

A faithful C99 reimplementation of **CS2D 1.0.1.8 p1** (Unreal Software, written in BlitzMax), built
by reverse engineering the shipped binaries in this folder:

- `CS2D.exe`: the original client. It is the reference for rendering, input, physics and the client protocol.
- `cs2d_dedicated.exe`: the original server. It is the reference for game rules and the server protocol.

"Faithful" means **behaviour you can verify against the original**:

- the same packets, byte for byte, as a UDP proxy records them from the original client;
- the same numbers (speeds, ranges, damage, timings);
- the same visuals, compared screenshot to screenshot.

Our client must be able to play on the original dedicated server without the server noticing a difference.

## Ground rules

1. **Evidence before code.** Every constant, opcode or formula comes from one of these sources, and you
   cite it in a comment:
   - REA/Ghidra (function address, e.g. `FUN_1401f7600`);
   - a proxy capture of the original;
   - the dedicated's Lua API;
   - an original asset file.

   If something is only estimated or calibrated, say so in the comment (for example "calibrated on
   captures"). Never invent values silently.
2. **Measure, then lock it in a test.** When you verify a behaviour against the original, add a
   regression case to `tests/test_cs2d.c`. For example, `test_entities_prison` uses the fixture
   `tests/data/prison_funst3r_triggers.txt`, and `test_player_physics` and `test_breakables` use values
   measured on the original.
3. **Record what you learned** in the matching topic file of `docs/agents/knowledge/` (index:
   `docs/agents/KNOWLEDGE.md`): protocol layouts, addresses, measured numbers. Add a dated line to `docs/agents/WORKLOG.md` for each work session, and keep
   `docs/agents/OPEN_ISSUES.md` current. These files are the shared memory between agents. **Context budget:** read the index and only the
   topic files you need, never `knowledge/*` or `worklog/archive.md` in full; keep entries terse. Read them
   before starting.
4. **Never run the dedicated server online.** Always pass `-offline`; without it, the server registers
   on the public U.S.G.N. serverlist.
5. **Restore what the original binaries rewrite.** `cs2d_dedicated.exe` rewrites `sys/server.cfg`
   (`sv_map`), and `CS2D.exe` rewrites `sys/recentlyjoined.lst`. The `tools/agent/*.sh` scripts back
   these files up and restore them. After manual runs, check `git status sys/`.
6. **Kill stale processes** (`cs2d_dedicated.exe`, `CS2D.exe`, `cs2d_client.exe`) before a run.
   A process left running holds the UDP port, and a running `cs2d_client.exe` makes the build fail to link.
7. **Commits are made by the user** unless they ask you to commit. Do not commit generated binaries.
8. **Write code that reads like the surrounding code.** Match its comment density, its naming
   (`cs2d_<module>_<verb>`) and its use of the existing helpers.
9. **The user writes in Portuguese (pt-BR).** Reply in Portuguese. Code, comments and these docs stay in English.

## Layout

| Path | What |
|---|---|
| `src/`, `include/` | The reimplementation (one module per `cs2d_<name>.c/.h`) |
| `src/main_client.c` | SDL3 entry point and the `--capture` test harness |
| `src/cs2d_client.c` | Client core: console, commands, **network dispatcher** (`cs2d_client_poll_network`) |
| `src/cs2d_scene_{lobby,game,editor}.c` | The three scenes |
| `src/cs2d_entities.c` | Entity logic (triggers, DynWall, Breakable, Use, Delay, …) |
| `src/cs2d_player.c` | Player state and movement with collision (port of `FUN_1401f7600`) |
| `src/cs2d_fx.c` | Cosmetic effects: breakable debris, explosions, tracer, muzzle flash, shells |
| `src/cs2d_gen.c` | Gen_Particles / Gen_Sprites / Gen_Weather / Gen_FX particles (Te.draw pass 2 + Tp types), Info_NoWeather, thunder flash |
| `src/cs2d_dynobj.c` | Dynamic objects (`Tdo`): buildings, their messages and snapshot, collision overlay, build rules |
| `src/cs2d_tilefx.c` | Info_TileFX frame generation (scroll / noise / wave, `Ttilefx.CreateTileFX`) |
| `src/cs2d_serverlist.c` | Server browser (Find Servers): server list, `svl.cache`, info/ping queries, LAN scan |
| `src/cs2d_editor.c` | Map editor model: brush / modifier byte, painting, tile properties and `.inf`, entity tables |
| `src/cs2d_transfer.c` | Join-time file transfer `[252,50]`: file list checks, requests, blocks, zlib, saving, `dls.cache` |
| `src/cs2d_radar.c` | Radar icon queue (`FUN_140291bd0`): which blips the HUD radar draws, and their colours |
| `src/cs2d_render.c` | OpenGL 1.x immediate mode, GDI+ TrueType text, shadows, HUD |
| `src/cs2d_image.c` | Portable asset decoding (BMP / PNG / ICO) on the vendored `stb_image`; replaced the GDI+ image path |
| `src/cs2d_hudmen.c` | Hud menus (`Tgui_hudmen`): window, `button` / `fbutton` / `box`, and the Buy menu (pages, weapon info, previews) |
| `src/cs2d_ui.c` | Immediate-mode UI library (textbox, dropdown, numberbox, shared scrolling `cs2d_ui_scroll_*` / `cs2d_ui_scrollbar`) |
| `tests/test_cs2d.c` | Regression suite (`test_cs2d.exe`) |
| `tools/agent/` | Capture and reverse-engineering scripts (see `docs/agents/TOOLS.md`) |
| `tools/entity_truth/` | Generator of the entity fixture from the dedicated's Lua |
| `third_party/` | Vendored single-header libraries (`stb_image.h`, `stb_truetype.h`) — `-Ithird_party` in `build.bat` |
| `docs/` | Scraped references (`cs2d_entities_db`, `cs2d_help_db`, `cs2d_gfx_descriptors.json`) |
| `docs/dump/` | Ghidra decompilation of `CS2D.exe`, one file per BlitzMax class (git-ignored, client only) |
| `docs/agents/` | **Shared agent memory**: `TOOLS.md`, `KNOWLEDGE.md` (index) + `knowledge/<topic>.md`, `WORKLOG.md` (5 newest) + `worklog/archive.md`, `OPEN_ISSUES.md` |
| `cs2d_netcode_codes.txt` | Opcode names from the dedicated (`recv N name`) |
| `cs2d_spec_map_format.txt` | `.map` format notes |
| `gfx/ sfx/ maps/ sys/ incbindata/` | Original game data (assets the reimplementation loads) |

## Build and test

```bat
build.bat            :: builds test_cs2d.exe and cs2d_client.exe (MinGW gcc, C:\msys64\mingw64\bin)
test_cs2d.exe        :: must print "Tests Passed: N / N"
```

- **New `.c` file:** add it to **both** gcc lines in `build.bat`.
- **Compiler:** MSYS2 MinGW-w64 gcc with SDL3, opengl32, gdi32, ws2_32 and zlib (static). Details are in `docs/agents/TOOLS.md`.
- **From the Bash tool:** run the build with `cmd /c ".\build.bat"` from PowerShell, or call gcc
  directly after `export PATH=/c/msys64/mingw64/bin:$PATH`.

## Workflow that works

1. **Read the shared memory:** the `docs/agents/KNOWLEDGE.md` index, the topic files for your area, and the
   relevant part of `OPEN_ISSUES.md`.
2. **Find the original code in the Ghidra dump, then confirm with REA.** For the client, start with
   `docs/dump/` (class-named, git-ignored): `tools/agent/dumpgrep.sh Tpl::move`, `-f <Class>`, or
   `-l '<regex>'` (see `docs/agents/TOOLS.md`). The dump has no `cs2d_dedicated.exe`, and its pseudo-C is a
   lead, not evidence of numbers. With REA, search the strings first (BlitzMax strings are objects with
   UTF-16 text at +0x10, and the string object starts 0x10 bytes before what `search_strings` returns).
   Then get the xrefs and decompile.
   - Functions too large to decompile, such as the 138 KB dispatcher `FUN_14009bb30` in the dedicated,
     need `objdump -d -M intel --start-address=… --stop-address=…`.
3. **Record the original's behaviour:**
   - `tools/agent/capture_original.sh` records the original client with a Lua probe on the dedicated;
   - `tools/agent/dumpops.py` decodes the capture.
4. **Implement it, then run our client** against the same server with `tools/agent/capture_ours.sh`,
   and compare the proxy logs and screenshots.
5. **Add a test, update the `knowledge/` topic file, `WORKLOG.md` and `OPEN_ISSUES.md`.**
