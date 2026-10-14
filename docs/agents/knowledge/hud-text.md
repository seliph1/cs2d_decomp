Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Lua HUD texts (hudtxt, hudtxt2, hudtxtclear, hudtxtmove, hudtxtalphafade, hudtxtcolorfade)

Source: CS2D.exe receive `FUN_1400a3530` ([234]), `Ttween.receive 0x1400b4420` / `addtween 0x1400a14c0` /
`update 0x14027fc50` / `freehudtext 0x140097be0` ([54]), draw `FUN_14024f5a0` (inline tags `FUN_14024cc00`),
`lua_font.SetFont 0x140030be0`. Code: `src/cs2d_hudtxt.c`. Verified by proxy + screenshots against the original
(`tools/agent/lua/hudtxt_probe.lua`, 2026-10-14); fixture bytes in `test_hudtxt`.

- **[234, id u8, chars u16, chars x u16 UTF-16, x u16, y u16, align u8, valign u8, size u8]**; x / y biased by
  0x4000. `[234, 255]` = hudtxtclear (all 200 texts, their tweens). hudtxt2 uses the same message (sent to one
  player). `hudtxt <id> ""` sends an empty text (len 0 = hidden). Ids 0..199; each [234] drops the id's tweens.
- **[54, mode u8, target u16, duration i32 ms, ...]** (Ttween, also used by Lua images / Tdo): target 1..200 = hud
  text `200 - target`, > 200 = Tdo `target - 200`. Bodies: mode 0 move `x u16, y u16` (biased 0x8000), 1 move+rot
  `x, y, rot u16`, 2 rot `u16` (x 0.005493164 deg), 3 spin `float` (no duration), 4 alpha `u8 / 255`, 5 colour
  `r, g, b`, 6 scale `float, float`, 7 / 8 frame `u8`. The dedicated packs several [54] in one datagram.
  Hud texts only use 0 / 4 / 5 (update drops other modes). Same id + same mode replaces the running tween.
- Start values: current x / y / alpha; colour from a leading `©RRRGGGBBB` tag, else (255, 220, 0). Update: f =
  min(1, elapsed / duration); x / y truncated to int; colour fade strips the leading tag (10 chars) and prepends
  `©` + zero-padded RRR GGG BBB (FUN_14027fb80) every frame. Removed once elapsed > duration.
- **Inline tag flag** (array 0x140a15430): `FindLast(text, "©") > 0 || FindLast(text, "®") >= 0`
  (FUN_1408c3b70 is bbStringFindLast, start 0). Leading-tag-only texts take the plain path.
- **Plain draw:** blend alpha, font size clamped 10..100 (Liberation Sans, LoadImageFont flags 5 like the UI font),
  leading tag gives the colour (default 255, 220, 0); align 1 `x -= (int)w / 2`, 2 `x -= w`; valign by TextHeight
  the same way. Black shadow at (+1, +1), both at the text alpha.
- **Tagged draw (FUN_14024cc00):** prepend `©255220000` if the text does not start with `©`, split at `©`, each part
  = RRRGGGBBB + text; `®path.ext` (up to the first `.` + 4 chars) draws that image TextHeight high, aspect kept.
  Alignment uses the float width of the joined parts.
- Draw order: game HUD (0x1402a0000) after the radar / HUD bars, before chat, kill feed and GUI windows.
- Not reconstructed: `mp_hudscale` (0x140a14de8[1] in 1..2 with screen_h > 589 scales by g_zoom); Tdo tweens.
