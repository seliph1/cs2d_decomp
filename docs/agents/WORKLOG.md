# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

## 2026-10-11 — Claude Code — server browser (Find Servers)
- **Changed:**
  - New `cs2d_serverlist.c/.h`:
    - `Tsv` model;
    - `svl.cache`, `favorites.lst` and `recentlyjoined.lst` readers;
    - info / ping packets;
    - `pingServers` scheduler;
    - LAN broadcast;
    - its own UDP socket.
  - "Servers" window in `cs2d_client.c`: tabs, flag icons, mode letters, players and bots, latency bars,
    scrollbar, stats, tooltip, the 15 s cache reload, joining, and "Connect to IP". It opens from the lobby's
    "Find Servers".
  - Fixed `cs2d_net_parse_info_packet`: strings are u8-length, and bots + flags2 are always present.
- **Verified:**
  - Proxy captures of the original (`tools/agent/browser_proxy.py` + a test `svl.cache`), and our client
    against the same dedicated: identical request and ping bytes, the same 15 s reload, and joining from the
    list reaches "Connected".
  - Screenshots compared with the original.
  - `test_serverlist` (17/17).
- **Left:** see `OPEN_ISSUES.md` (U.S.G.N. list, filters, favorites, writing the lists).

## 2026-10-11 — Antigravity — firearm visual recoil kickback, melee swing animation, and GitHub Actions release workflow
- **Firearm visual recoil (`Tpl +0x308`):** extracted the per-weapon visual recoil table (`0x1409eda18` in
  `CS2D.exe`, `1..6 px`) from `init_weapons` (`0x1402305db..0x1402340a9`). Firing sets
  `recoil = visual_recoil + Rnd(0, 1)` (`0x1402166a7`), which displaces the player backward along `-angle`
  in `cs2d_render_player` (`0x14012131c..0x1401213cc`) and decays by `0.5 px` per 60 Hz frame (`30 px/s`, `0x14021d1b1`).
- **Melee swing (`Tpl +0x31c`, `+0x24c`, `Tp` type 13):** melee attacks (except Chainsaw `85`) set
  `slash_timer = 8.0f` (`0x14018cebc`) and `draw_angle = angle + 6.0f * slash_timer` (`+48° -> 0°` over 8
  frames at 60 Hz, `0x14021d68a` & `0x14021da21`), rotating both the torso and held weapon (`13.5 px` offset)
  and spawning the additive `gfx/knifeslash.bmp` arc (`0.07/frame` fade, `0x14004b4e0` & `0x140265220`).
- **GitHub Actions CI & Releases:** added `.github/workflows/build.yml` (MSYS2 MinGW-w64 + SDL3) to compile
  `cs2d_client.exe` and `test_cs2d.exe`, run the regression suite, upload workflow artifacts, and publish
  `cs2d-binaries-win64.zip` + `SDL3.dll` to GitHub Releases.
- **Verification:** added `test_recoil_and_swing` in `tests/test_cs2d.c` (`16 / 16` passing).

## 2026-10-11 — Antigravity — Server Info ("<Server> - Info") and "Select Team" / skin subcategory UI
- **Protocol & parsing:** parsed `server_name` from `[252,6]`, `opcode 240` (`CS2D_NET_MSG2`) `mode 50`
  (`"1"` clear, `"2"` open) and `mode 51` (UTF-16LE `serverinfo.txt` lines with `U+00A9` `RRRGGGBBB`
  color tags and `U+00BB` gold link highlighting), and `opcode 20` (`CS2D_NET_TEAM`) send/receive.
- **Clay UI windows (`cs2d_ui`):** implemented the `600×440` `"<Server Name> - Info"` window (`553×374`
  scrollable box + `15 px` scrollbar + `568×20` `Close` button) and the `600×440` `"Select Team"` menu
  (`M` key) with `240×25` numbered buttons, T/CT skin subcategories (`1..4` skins, `5 Auto-Select`,
  `Back`, `0 Close`), and immediate selection (no subcategory) for `5 Auto-Select` and `6 Spectator`.
- **Verification:** added `test_server_info_and_team_menu` in `tests/test_cs2d.c` (`15 / 15` passing) and
  captured live screenshots against `cs2d_dedicated.exe -offline` verifying the full join → Server Info →
  Select Team → CT skin selection → live spawn flow.

## 2026-10-11 — Claude Code — agent memory structure
- Added `AGENTS.md` and its pointers (`CLAUDE.md`, `GEMINI.md`, `.agent/rules/`), plus `docs/agents/*`,
  `tools/agent/*` (capture scripts and Lua probes) and `.mcp.json` (REA).

## 2026-10-11 — Claude Code — weapon sync, shot effects, join/leave
- **Weapon sync:** the client follows the server's inventory: opcodes 9, 16, 21, 22 (fixed record size),
  23, 24, 25, 33, 42 and 61, plus ground items. Validated with the `inv` command against the Lua dump:
  identical.
- **Shot effects:** tracer, glowdots, muzzle flash with light, wall sparks and shells (`cs2d_fx_shot`).
  Remote shots come from `[7,pid,seed]`.
- **Join/leave:** the missing client data `[252,10]` + `[67,1]` meant the server never ran `join`; the
  client now sends both. Two-client test: join 1, join 2, leave 2, leave 1; player count 0→1→2→1→0.
  The client also handles `[248]`, `[253]` and `[28]`.

## 2026-10-11 — Claude Code — Env_Breakable
- **Behaviour:** health, damage, hitscan trace, explosion and break effects (`cs2d_fx.c`). The fire
  packet is now `[7,seed]`.
- **Verification:** `test_breakables` uses numbers measured on the dedicated.

## Earlier (Claude Code / Antigravity)
- **Scenes and UI:** scene split (lobby, game, editor); UI library (textbox, dropdown, numberbox, console
  ring buffer).
- **Shadows:** fixed shadow halos and removed the invented roof parapet.
- **Entities:** entity logic validated state for state against the dedicated (fixture of 331 samples).
- **Movement and protocol:** movement is an exact port of `FUN_1401f7600`; the protocol matches the
  original client (positions, rotation, use, setpos).
- **Restart:** the offline `restart` command restarts the round (states, Trigger_Start, freeze time, random spawns).
