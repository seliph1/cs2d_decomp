Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## In-game chat and server messages

Source: dump `Thud_msg` (`add_center 0x14009d2c0`, `draw_center 0x14024d230`, `add_chat 0x140117a90`,
`draw_chat 0x14024d8a0`, `tintTeamColor 0x140083a40`), dispatcher `FUN_1401de400` (0x1401d0000.c),
chat prompt `FUN_14029f7b0`, opcode 240 handler `FUN_1402c8100` (`recv 240 msg`). Code: `src/cs2d_client.c`, `src/cs2d_scene_game.c`.

- **Chat log layout (`Thud_msg.draw_chat` 0x14024d8a0):**
  - Anchor: `base_y = screen_h - 85 - (line_height * 2)`, stacking upwards (`y -= line_height`).
  - Coordinates: text at `x = 3.0f`, black drop shadow at `x = 4.0f, y + 1.0f` with alpha `fade`.
  - Duration: 10 seconds (`hud_chattime 10`, `DAT_140a14c7c * 1000`).
  - Fade out: after 10 s, `fade -= dt * 0.05f` until `fade <= 0.0f`, then removed.
  - Radar ceiling: lines above `y = 180.0f` fade out to prevent collision with top radar.
- **Chat input prompt (`FUN_14029f7b0`):**
  - Opened via `say` (bind "Y", mode 1, prompt `"Say : "`) and `sayteam` (bind "U", mode 2, prompt `"Say (Team) : "`).
  - Drawn at `base_y` with alpha 0.4 black background box, drop shadow (+1, +1), and blinking vertical cursor.
  - Traps text input, Esc cancels, Enter sends via `cs2d_client_send_chat`. Stops movement and weapon firing while open.
  - SDL key translation: SDL3 defines `SDLK_A..SDLK_Z` as ASCII `'a'..'z'` (0x61..0x7a); `sdl_key_to_cs2d_key` maps
    all letter keys to uppercase `'A'..'Z'` so `sys/controls.cfg` binds and switch handlers match.
  - `chat_just_opened` flag suppresses the initial text input event corresponding to the opening key ('y' / 'u') so the
    prompt starts with a clean empty input buffer.
- **Team colors (`tintTeamColor` 0x140083a40):**
  - Spectator / Server: `(255, 220, 0)` (CS2D gold)
  - Terrorist: `(255, 25, 0)` (red/orange)
  - Counter-Terrorist: `(50, 150, 255)` (blue)
  - Dead player formatting: prefix `*DEAD* <name>: ` (mode 1) and `*DEAD*(TEAM) <name>: ` (mode 2).
- **Server messages & tag `@C` (`FUN_140153900` dedicated, `FUN_1401de400` client):**
  - Server messages from `sv_msg`, `sv_msg2`, `msg`, `msg2`, or opcode 226/240.
  - **The dedicated performs the center tag:** `FUN_140153900` at `0x1401546a0` compares the end of the text
    with the `@C` literal (`0x1405e80e0`), removes those 2 characters and sends `[240, 0, 2, ...]` (mode 2).
    Only the uppercase `@C` exists in `cs2d_dedicated.exe`.
  - Client `FUN_1401de400` case 1 -> `Thud_msg.add_chat`; case 2 (sender 0) -> `Thud_msg.add_center`.
    Our client also strips a trailing `@C` in `cs2d_client_handle_server_msg` for opcode 226/local paths.
  - Otherwise routed to `Thud_msg.add_chat` (HUD chat log).
  - All messages are echoed to the console (`cs2d_con_print` / `cs2d_client_console_print`).
  - Color tag: leading `\xA9RRRGGGBBB` (`U+00A9` / `\xC2\xA9` + 9 ASCII digits) sets line RGB and strips the 10-char prefix.
- **Center messages (`Thud_msg.draw_center` 0x14024d230):**
  - Centered horizontally: `x = (screen_w - text_w) / 2`.
  - Origin $Y$: `y = screen_h / 2 - 10 * count - 50`.
  - Text drop shadow at `x + 1, y + 1` (black, alpha `fade`).
  - Duration: 5 seconds (`DAT_140a14c74`, default 5). Same text restarts its timer.
- **Network opcode 240 (`CS2D_NET_MSG2`):**
  - Client send: `[240, mode u8 (1=say, 2=sayteam), char_len u16 LE, char_len * 2 bytes UTF-16LE]`.
  - Server recv & broadcast: `[240, sender_id u8, mode u8, char_len u16 LE, char_len * 2 bytes UTF-16LE]`.
  - Our client now converts its UTF-8 input buffer to UTF-16LE with `cs2d_net_utf8_to_utf16`; the
    server does the same for Lua `msg`/`menu`/`hudtxt` strings (2026-10-20).
  - Server text may also arrive as `"UTF-8:\xNN..."` (Changelog; WQ2D `utf8.convert`), including the
    `\xC2xA9` continuation-byte spelling; `cs2d_net_utf8_unescape` decodes it before the wire conversion.
- **Kill feed (`Thud_msg.add_kill` 0x1401aef60, `draw_kill` 0x14010f8d0; code `cs2d_client_add_kill`,
  `render_kill_msgs`):**
  - Client path of `[19]` (FUN_1401eaf60, DAT_140ddf400 == 0): `add_kill(killer, weapon, victim, assist, -1, "")`
    unless weapon is 0xfa / 0xfc; assist passed only when victim and killer are T vs CT.
  - Fields: txt 0 killer, 1 weapon name, 2 victim, 3 assist; txt 1 gold (255, 220, 0); names by `tintTeamColor`
    (team 0 gold, team games T (255,25,0) / CT (50,150,255), otherwise green (0,255,0)). killer == victim: suicide
    (no txt 2); empty weapon name: "<victim> died" (0738). Names: 0x1409fff98[weapon], 0xf8 NPC (0737), 0xf9 Creature
    (0736), 0xfb Explosion (0735), 0xfd / 0xfe / 0xff = 0x1409f79f8 [8] / [6] / [2] (Turret / Gate Field / Barbed Wire).
  - At most 5 rows (count > 4 drops the oldest). show = `hud_deathnotice_time` (DAT_140a14c80, .data 8) * 1000 ms;
    fade 1, offset 17; while shown offset = TextHeight("I") + 4; after: fade -= 0.05 g_dt, then offset -= g_dt,
    removed at 0. Next row y = (int)(y + offset + 2).
  - Rows start at y 3; local player not alive: 25 + 15 = 40 (+ (ymax + 1) * minimap scale while DAT_140a15478).
  - `deathnoticeimg` (DAT_140ddf191, config default 1): right aligned at screen_w - 3; "killer[ + assist]" (" + " in
    gold), the weapon `_k.bmp` (0x1409f3d18) additive (230, 180, 0) scaled min(1, (offset - 2) / h), victim. Bar:
    `gfx/hud_shade_c.bmp` (250x20, 0x140a0dd28) black, alpha fade * 0.35, scale (rowW / 350, offset / 10) at
    (x + rowW / 2, y - 2 - offset / 2), drawn twice (x scale mirrored). Text shadow at (x + 1, y + 2), text at y + 1.
  - Text mode: "killer [+ assist ]weapon victim" (the "+ " string), text at y, shadow (x + 1, y + 1).
  - Console: "<k> killed <v> with <w>", "<v> killed by <w>" (no killer name), "<k> killed himself with <w>", "<v> died".
