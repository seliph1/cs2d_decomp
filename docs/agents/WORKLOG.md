# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-14 — Antigravity — mp_hudscale (modes 0..3, zoom scaling, aspect letterboxing, font sharpness)

- **Changed:**
  - Implemented `cs2d_hudscale_compute` in `src/cs2d_hudtxt.c` from CS2D.exe (`FUN_140291080`, `FUN_14024f5a0`, `1402b0000.c:6760-6785`):
    - Mode 0: 1:1 screen pixels (`screenw x screenh`).
    - Mode 1: 850x480 virtual resolution, scaled by `g_zoom = min(screen_w / 850, screen_h / 480)` and letterbox offsets.
    - Mode 2: 640x480 virtual resolution stretched across viewport (`screen_w / 640`, `screen_h / 480`).
    - Mode 3: 640x480 virtual resolution centered unscaled (`off_x = (screen_w - 640) / 2`, `off_y = (screen_h - 480) / 2`).
    - Font sharpness (`FUN_14024f5a0:8645`): when `mp_hudscale` in 1..2 and `screen_h > 589`, selects raster font size `size * g_zoom` (clamped 10..100) and scale `1.0 / g_zoom` for pixel-crisp 1:1 rasterized text on high-res displays.
  - Added `cs2d_render_text_transform_ex` to `src/cs2d_render.c` supporting non-uniform 2D font scaling.
  - Updated `cs2d_hudtxt_render` to transform coordinates, line height, image sizes, and alignment across all 4 modes.
  - Added `net_hudscale` to `cs2d_client_t`: parsed from `[252, 6]` byte at offset `b + 16` (`0x140a14de8[1]`), `[237, 22, val]` (`changesettings` case 0x16 in `FUN_1401e3f10`), and console command `mp_hudscale [0..3]` (`0x1401bbc89`).
- **Verified:** regression test `test_hudscale` in `tests/test_cs2d.c` (44 / 45 passed, only known pre-existing `test_light_engine` formatting diff remains). Build succeeds cleanly.

## 2026-10-14 — Claude — Lua HUD texts (hudtxt, hudtxt2, hudtxtclear, move / alpha / colour fades)

- New `src/cs2d_hudtxt.c`: [234] (FUN_1400a3530), [54] Ttween for hud texts, update and draw (FUN_14024f5a0 /
  FUN_14024cc00, inline `©` colours and `®` images); render: sized fonts `CS2D_FONT_SIZED(px)` 10..100.
- Verified: proxy capture + screenshots vs CS2D.exe with `tools/agent/lua/hudtxt_probe.lua`; `test_hudtxt`.
  Build, 43 / 44 tests (`test_light_engine` fails on fun_roleplay, map file modified in the working tree).
- Left: mp_hudscale, Tdo (Lua image) tweens, `®` images not screenshot-compared (OPEN_ISSUES).

