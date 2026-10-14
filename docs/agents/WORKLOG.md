# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — Lua HUD texts (hudtxt, hudtxt2, hudtxtclear, move / alpha / colour fades)

- New `src/cs2d_hudtxt.c`: [234] (FUN_1400a3530), [54] Ttween for hud texts, update and draw (FUN_14024f5a0 /
  FUN_14024cc00, inline `©` colours and `®` images); render: sized fonts `CS2D_FONT_SIZED(px)` 10..100.
- Verified: proxy capture + screenshots vs CS2D.exe with `tools/agent/lua/hudtxt_probe.lua`; `test_hudtxt`.
  Build, 43 / 44 tests (`test_light_engine` fails on fun_roleplay, map file modified in the working tree).
- Left: mp_hudscale, Tdo (Lua image) tweens, `®` images not screenshot-compared (OPEN_ISSUES).

## 2026-10-14 — Claude — join password prompt

- `[252,2,1]` now opens the "Password required" window (Tgui_joinmen menu 1) instead of aborting; Connect restarts the join with the typed password, Cancel/Esc sends `[253,9]`. Layout and strings from the dump (net-join.md).
- Verified: build + 43/43 tests. Not yet tested live against a dedicated with `sv_password` (Esc = Cancel is an assumption).
- Refused joins (`[252,2]` / `[252,4]` status != 0) now open the original error window instead of aborting; status 11 offers temp / overwrite / cancel and rejoins with that map mode. Not tested live yet.

## 2026-10-14 — Claude Code — Ground item hover tooltip
- **Changed:** `render_item_hover` (FUN_14029ffb0, mp_hovertext bit 16); `net_infammo` read from `[252,6]`. Not yet compared with a screenshot of the original.

## 2026-10-14 — Claude Code — CTF flags: carried flag, drop / take / return messages, draw over walls
- **Changed:** `Tpl.flag` is `cs2d_player_t.has_flag` (Titem.give 70 / 71 sets it, it is not an inventory item): `[25]`
  of a flag sets it with the 0707 / 0708 / 0709 messages, `[24]` clears it with 0704 / 0705, `[31]` of a CTF base back
  to state 0 shows "... Flag returned". Radar, scoreboard and the carried-flag sprite read it. The waving base flag
  (`cs2d_render_map_flags`) and the carried flag are drawn after the wall tiles.
- **Verified:** proxy capture with `tools/agent/lua/ctf_probe.lua` on ctf_cs2dfortress (screenshots: flag on the back,
  dropped flag on the ground, message); build, 43 / 43 tests.

## 2026-10-14 — Claude Code — World draw order (light buffer last) and Env_Sprite layer
- **Changed:** `game_render` follows `FUN_140281fd0` again: CPU shadow chunks before the players, `DrawLightsColorList`
  after them, the light buffer (day shadows of walls / buildings / DynWalls) last, over players and Tp 5/7/6. The
  player day-shadow silhouette moved out of the light buffer into `cs2d_render_player` (depth-mask pass, 4 px along
  ShadowRotation - 180, alpha 0.5; our deviation) so the player is not darkened by its own shadow.
  Env_Sprite layer: `Len(strs[4]) > 0` read per draw (the `sprite_layer` cache went stale after inspector edits).
- **Verified:** build, 43 / 43 tests. No screenshot comparison yet.

