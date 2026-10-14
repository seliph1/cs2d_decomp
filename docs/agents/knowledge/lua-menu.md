# Lua Server Menu (`menu()`, Opcode 235 `CS2D_NET_LUAMENU`)

Decompiled and reverse engineered from `CS2D.exe` (`FUN_14015c320`, `Tgui_hudmen.update` at `0x14002ef23`, `Tgui_hudmen.draw` at `0x14022680e`, and network dispatcher `FUN_1402e4820`).

## Protocol

### Server -> Client (`[235]`)

Sent by dedicated server Lua `menu(id, page)`:
- `uint8_t opcode = 235` (`CS2D_NET_LUAMENU`)
- `uint8_t pid`: target player ID (or 0 for broadcast)
- `uint16_t slen`: length of raw string (UTF-16LE characters count, clamped to 400)
- `uint16_t text[slen]`: UTF-16LE string
- `uint8_t trailing = 0xFF` (optional)

### Client -> Server (`[235]`)

When the user selects an option (key `0..9` or clicks a button or close `X`):
- `uint8_t opcode = 235`
- `uint8_t pid`: local player ID
- `uint16_t slen`: length of menu title (clamped to 400)
- `uint16_t title[slen]`: UTF-16LE title without trailing `@b`/`@i`
- `uint8_t button`: `0` for cancel/close, `1..9` for option selected

## String Format

`"Title[@b|@i],Btn1|Right1,(Btn2|Right2),Btn3,..."`
- Comma `,` separates title and button items.
- Pipe `|` separates left label from right-aligned text (used e.g. for item prices or sub-labels).
- Parentheses `(Option)` denote a disabled option (cannot be clicked or selected with number keys).
- Backslash `\` escapes commas, pipes and parentheses (`\,`, `\|`, `\(`, `\)`).
- Up to 9 item buttons (`1..9`). Key 0 is always "Cancel" at the bottom.

## Layout & Modes

Identified by `DAT_140ddf260` (`Tgui_hudmen` menu 120 / `0x78`):
1. **Default mode (`mode = 0`)**:
   - Window size: `272 x 440` (`0x110 x 0x1b8`), positioned at `(20, (screen_h - 440) / 2)`. `Tgui_hudmen.update`
     sets 600 x 440 centred for every menu; menu 120 only overrides x / width (the old 330 height clipped item 9).
   - Button width: `240` px (`0xf0`).
2. **Big centered mode (`@b` in title, `mode = 1`)**:
   - Window size: `600 x 440`, centered `((screen_w - 600) / 2, (screen_h - 440) / 2)`.
   - Button width: `568` px (`0x238`).
3. **Hidden / background mode (`@i` in title, `mode = 2`)**:
   - Window positioned offscreen at `x = 10000`, allows Lua scripts to capture number keys without drawing a GUI.

Buttons 1..9 are laid out vertically at `y = 30 + (i - 1) * 30`.
Button 0 ("Cancel", `sys/language 0027`) is positioned at `y = wh - 46 = 394`.

## serveraction1..3 (binds F2..F4)

Command dispatcher `0x1401b1446..`: `FUN_1401de400(local id, 4, "1"|"2"|"3", 1, 0)` → client sends
`[240, mode 4, len u16 = 1, "N" UTF-16LE]` (same layout as say; mode 4 skips the 1 s chat flood check).
The dedicated fires the Lua `serveraction` hook. F1 is bound to `serverinfo`, F6 to `console`.
Close `X` button is rendered in the top-right of the title bar (`x = wx + ww - 19, y = wy + 4, 14x14`).

