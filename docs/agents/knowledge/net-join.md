Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Network: join

- **Handshake:**
  - `[252,0]` → `[252,1, name…]`
  - `[252,2,status,pid,map,secret]` → `[252,3,map sha256 hex]`
  - `[252,4,status]` → `[252,5,map,sha]`
- **`[252,2]` status (REA, join handler near 0x1402cf1c1):** 0 = ok; 1 = password missing/wrong → `Tgui_joinmen.pwprompt` (0x140055e40, vtable +0xa0): window "Password required" (lang 0782), text lang 0376 at (16,32), password input (16,64) w318, Connect (lang 0106) at (124,164), Cancel (234,164); no `[253]` is sent. Connect stores the text as the join password (0x140a18368) and restarts the join from `[252,0]` (FUN_14016a3c0(0)); Cancel sends `[253,9]`. Status 4 = banned (adds to recentlyBanned); 6 and 8 have own handlers; others → `errorprompt(status)` (vtable +0xa8).
- **Error window (`errorprompt` 0x14008b090, menu 2):** title lang 0034, red "Failed to join $1:$2" (0377) + reason lines (status 2..5,7,9,10,11 → 0378..0385, 12 → 0386, 20..23 → 0387..0390, 100..103 → 0391..0394, 200 → 0395, 201 → 0396), Okay/Cancel. Status 11 (`[252,4,11]`, local map differs) adds a Tgui_opt at (16,85) with lang 0860: choice 0/1 → `DAT_140ddf3dc` = 1 (temp.map) / 2 (overwrite) and the join restarts; choice 2 or Cancel → `[253,9]`.
  - `[252,6, map_name(RStr), server_name(RStr), …]`, then `[252,7,section,…]` snapshot chunks, ending with `[252,7,200,…]`.
- **Snapshot sections** (FUN_1402ce7c0 `switch(section - 3)`, proxy 2026-10-14): 1 players, 2 hostages, 3 items, 4 entity states, 5 dynamic objects, 6 projectiles, 7 image data, 8 tweens, 9 custom tiles, 10 resource paths, 11 weapon settings, 12 map cycle, 13 mods, 200 final ACK (see `dynobj.md`).
- **Malformed / cut join data (FUN_1402ce7c0):** every `[252,x]` message carries the join step it belongs to and the
  client only accepts it while its `joinstep` (Tgui_joinmen +0x80) matches. A truncation inside a section (a custom
  tile entry with fewer than 11 bytes, a tween, a resource path, the server settings) prints its `ERROR: …` line and
  jumps to the shared abort `LAB_1402cf3ee`: joinstep 0, the world teardown `FUN_1401d0160`, `Tgui_win::closeall` and
  `Tgui_joinmen.errorprompt(201)` (vtable +0xa8). **No `[253]` is sent there** — `[253,9]` is sent by the error
  window's Okay/Cancel, so the socket stays open until the user answers.
- **Snapshot section 2 (hostages):** `[252,7,2,n] + n×{id,health,look,follow,x u16,y u16}` (see `hostages-bomb.md`).
- **Snapshot section 4:** the non-zero entity states, `{x u16, y u16, state u8}`.
- **Snapshot section 6 (ground projectiles, REA `0x1402c0000` 0x1402d1850):** `[252,7,6,count]`, then per ground projectile `{id u8, type u8, player u8, x u16, y u16}` (7 bytes each).
- **Client data (proxy, Lua):** after the snapshot the original sends
  `[252,10, 0x1c,0x02, size u16 (=4+zlib len), raw size u32 (=1024), zlib(32x32 spray)]`.
  **The server runs its `join` (Lua hook, player list, announcement to the others) only after this.**
  Our client sends a blank spray. The `[67,1]` seen after it is the spectator camera picking its first target
  (see "Spectator"): with nobody alive the original sends no `[67]` and the server still runs `join`
  (140.238.183.194:36969).
- **`[252,6]` settings (REA `0x1402d0560`, `sv_specmode` / `sv_fow` varied on the dedicated):**
  `[252,6,status, map str, server name str, u16, u16, roundtime, freezetime, c4timer, time i32, u8, u8]`, then,
  when more than 5 bytes remain: `maxplayers, sv_fow, sv_specmode, sv_gamemode, respawn delay, infammo, ...`.
- **Snapshot section 1 (players already in game, REA `0x1402cd9a0`, proxy):** `[252,7,1,count]`, then per player
  `id, name (u8 len, cipher key "name"), spray (key "spr"), usgn id i32, steam os u8, steam name, steam id i64,
  usgn name, team, spec state, look, score u16 (+1000), deaths u16, assists u16, mvp u16, rank, x u16, y u16,
  rot u16 (x 360/65536), health, armor, special, weapon, weapon mode, votekick, votemap u16`. The steam and usgn
  names were always empty (2 bytes each); u16 count + UTF-16 like `[240]` is assumed. The name cipher is the
  inverse of `cs2d_net_encrypt` (`cs2d_net_decrypt`).
- **Join fields (REA `FUN_1402ce7c0`, proxy 2026-10-11 `proxy_orig1.log`):**
  - **`[252,0]` is exactly 2 bytes** (`fc 00`, no string): the original sends it before it has received
    anything, and the server alone appends the secret in its `[252,0]` reply
    (`S>C 15 02 00 fc 00 0a <10-byte secret>`). We used to add an empty string (3-byte payload) - fixed
    2026-10-15, `test_network_protocol` asserts both lengths.
  - **`[252,1]`, field for field as `FUN_1402ce7c0` writes it** (u8 str = 1 length byte, u16 str = 2):
    `name` (sliced to 25) | `password` enc key `j01nk3y` | `"1.0.1.8"` enc key `funkymonky` |
    `u16 1018` = version_short | `i32` usgn id (`DAT_140ddf1f0`: the logged-in U.S.G.N. account) |
    `u16 str` usgn name (`0x140a15000`) | `u8` **os id: 1 Windows, 2 MacOS, 3 Linux** (`DAT_140ddf5d8`,
    the `Windows`/`MacOS`/`Linux` line of Tgui_hudmen 0x140a1cd88) | `u16 str` steam name | `i64` steam id |
    `u8 str` **spray logo file name** (`0x140a14f90` = the `spraylogo` value of `sys/config.cfg`,
    `unrealsoftware.bmp`, not the menu label) | `u8 str` transformed secret | `u8 str` nickname scramble
    (`FUN_1402c97f0`, empty in the capture) | `u8 str` client id | `u8` map transfer mode (`DAT_140ddf3dc`) |
    `i32` screen_w | `i32` screen_h | `u16 str` language name | `u16 str` language code | `u8` 4:3
    (`DAT_140ddf1c8`) | `u8` voice available (`TVoiceRecorder.Error == 0`) | `u8` `DAT_140ddf6a8` (1).
    Every field name above came from the writer; the **byte order was verified against the capture's
    138-byte payload**, which `test_network_protocol` now holds as a literal
    (`join1_want`), so the layout cannot drift. A join against the dedicated (`-offline`, de_dust2)
    accepts it and logs `real is using IP …, no U.S.G.N. ID and no Steam`.
  - **The client id** (field 13) is `"\xfa" + "0" + g_core_edc_id + <number>`, and both halves are
    known now:
    - the id is 32 characters: `FUN_140060a10` reads `sys/core/core.edc`, skips 0x523 bytes and takes
      the next 32. The bytes of the shipped file match the capture exactly
      (`28cefe3ac30ab30ab5268978955263db`).
    - the number is the **size in bytes of the client executable**:
      `bbStringFromLong(FUN_1404b7860(AppArgs[0]))` (`PTR_bbEmptyArray_140b13060` is AppArgs).
      `CS2D.exe` is 14789120 bytes = the capture's number, and the writer of `core.edc`
      (`0x1400ec850`'s caller) stores the same value at +0x543 with
      `WriteInt(FUN_1404b7860(AppArgs[0]))`. Our `cs2d_client_join_core_id` reports CS2D.exe's size
      (the binary the id belongs to) and falls back to core.edc's stored value.
    A live join (`-offline`, de_dust2) compares **field for field with 0 differences** against the
    capture (only the transformed secret is random per join), see WORKLOG 2026-10-15.
  - The language name/code are the two halves of the `language` setting of `sys/config.cfg`, which holds
    the language **file base name** (`EN-English` -> name `English`, code `EN`), split at the first `-`
    by the loader at 0x1400ef8da.
  - `[252,2,1]` is the answer to a missing or wrong password.
  - `[252,3, sha(str), enc secret, download u8]`: `sha` is the SHA-256 of `maps/<map>.map` (`""` when missing,
    `FUN_1402c9f40`); the last byte is the `download` setting of `sys/config.cfg` (`DAT_140a14f4c`; 6 in the shipped config).
  - The map is loaded at `[252,4,0]` (not at `[252,2]`), and `[252,5]` carries the recomputed checksum.
  - The original stays in the main menu during the handshake and the download (screenshot) and enters the game at
    `[252,4,0]`. Ours does the same: `connect` keeps the current scene, the lobby polls the socket, and
    `join_load_map` switches to the game scene on the server's map (no `de_dust2` in between).
- **Leave:** the client sends `[253,0]`. The other clients receive `[253,pid,reason]`; the server then
  drops what the player carried (`[24]`) and prints a `[226]` message.
- **Others joining:** `[248,pid,name(u8 len+text),18 bytes]`, followed by the spray relay
  `[28,pid,2,size u16,size bytes]`.
