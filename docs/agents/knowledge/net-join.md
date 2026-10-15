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
- **Join fields (REA `FUN_1402ce7c0`, proxy):**
  - `[252,1]`: the password is encrypted with key `j01nk3y` (`0x140a3bf20`); the byte after the OS string is the
    map transfer mode `DAT_140ddf3dc` (0 normal, 1 save as `maps/temp.map`, 2 overwrite). Wrong password: `[252,2,1]`.
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
