# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — Hostages, bomb plant / defuse, use key targets
- New `cs2d_hostage.c` (Thostage list: round start / snapshot [252,7,2], Thostage.use, [29], client movement, drawn from the
  list); E probes hostage / planted bomb / Trigger_Use per distance like FUN_14014f820; handlers for the [26] echo, [29],
  [30]; plant ([30,1]/[30,2]) and defuse (mode 3, [30,4] on release) online; [22] special byte (bomb / kit / flag);
  item 63 renamed Planted Bomb.
- Verified on the original ded: `hostage_probe.lua` (cs_office: [26,1] echo, [29] targets, hostage stops at 172,176 like
  the server) and `bomb_probe.lua` (de_dust2: our [30,1] → bombplant 28 22, CT E → use 3 / bombdefuse after 10 s).
  Test `test_hostages`. Left: OPEN_ISSUES "Hostages / bomb".

## 2026-10-14 — Claude — Use key [26] always sent (Lua "use" hook)
- E now sends `[26][0][0 i32][0 u16][0 u16]` when no Trigger_Use is in reach, like FUN_14014f820 (the original sends
  the packet whatever it finds); the server runs the Lua `use` hook with event 0. Trigger_Use still sends mode 100.
- Verified: `tools/agent/capture_ours.sh use_probe.lua` (new probe) logs `US 1 0 0 0 0` per press on the original ded.
- Left: hostage (modes 1/2), bomb defuse (3) and own dispenser/teleporter menu (Tdo 9/15) targets (OPEN_ISSUES).

## 2026-10-14 — Claude — Ingame menu (Esc): briefing, team, leaderboard, vote, options, server info

- Esc in game opens `Tgui_hudmen` menu 2 (was: disconnect). Buttons and the server info box from `Tgui_hudmen.draw`;
  `[252,6]` tail parsed (FF, voice, recoil, off-screen, physics, Lua, OS, lag comp, leaderboard flag).
- Map Briefing (`maps/<map>.txt`, Server Info window generalized), leaderboard `[230]`, vote screen + `[43]`, map cycle
  `[252,7,9]`, votekick/votemap from snapshot. Facts in `knowledge/game-menu.md`.
- Verified: build, `test_game_menu`; capture against the offline dedicated (screens: menu, vote, briefing, leaderboard;
  `[230,0]` answered, `[43,1,1]` self-vote refused with 0xfff4). Harness step `!esc` added.
- Light engine test still fails (unrelated, pre-existing).

## 2026-10-14 — Claude — Lua menu height, serveraction, F-key binds

- Lua menu (menu 120) window is 440 tall, centred vertically (`Tgui_hudmen.update` 0x1b8); 330 clipped item 9 over Cancel.
- `serveraction1..3` (F2..F4) send `[240, 4, 1, "N"]` (0x1401b1446 → FUN_1401de400); F1 `serverinfo`, F6 `console` via binds.
  Removed the hardcoded F1 console / F2 New Game / F3 Options keys. F-keys use `CS2D_KEY_F1` (VK_F* collide with 'p'..'{').
- Verified: build, tests (light engine test `light_fun_roleplay_15_20_floor` fails, unrelated). Not proxied against original yet.

## 2026-10-14 — Antigravity — Lua server menu (menu(), opcode 235 CS2D_NET_LUAMENU)

- **Changed:**
  - Implemented `cs2d_luamenu_t` parsing, rendering, and input in `src/cs2d_hudmen.c` matching CS2D.exe (`FUN_14015c320`, `Tgui_hudmen.update` at `0x14002ef23`, `Tgui_hudmen.draw` at `0x14022680e`).
  - Supports comma-separated `"Title[@b|@i],Btn1|Right1,(Btn2|Right2),..."`:
    - Mode 0 (default): `272x330` window at `(20, 20)` with `240` px buttons.
    - Mode 1 (`@b` big centered): `600x330` window centered at `y = 20` with `568` px buttons.
    - Mode 2 (`@i` invisible): positioned off-screen at `x = 10000` for script input capture.
    - Parentheses `(option)` mark items as disabled.
    - Escaping for `\,`, `\|`, `\(`, `\)`.
    - Key 0 ("Cancel", `sys/language 0027`) at `y = wh - 46` and top-right close `X` button.
  - Added opcode 235 (`CS2D_NET_LUAMENU`) dispatcher in `src/cs2d_client.c` for receiving server menu strings, and client -> server reply `cs2d_client_send_luamenu` (`[235, pid, slen, title_utf16, button]`).
  - Integrated into `src/cs2d_scene_game.c` for rendering, number key selection (`0..9`), and mouse clicks.
- **Verified:** test `test_lua_menu` in `tests/test_cs2d.c` testing string parsing, modes `@b`/`@i`, disabled options, keys and mouse clicks (45/46 tests passed). Full build clean.

