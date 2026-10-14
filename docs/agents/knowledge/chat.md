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
- **Server messages & tag `@C` (`FUN_1401de400`):**
  - Server messages from `sv_msg`, `sv_msg2`, `msg`, `msg2`, or opcode 226/240.
  - If message ends with `@C` or `@c` (or mode == 2): tag `@C` is stripped, routed to `Thud_msg.add_center` (screen center).
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
