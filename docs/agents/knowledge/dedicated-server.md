Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Dedicated server (cs2d_dedicated.exe → cs2d_server.exe)

Addresses are `cs2d_dedicated.exe`. Pseudo-C of the big functions lives in `docs/dump/dedicated/` (git-ignored;
regenerate with REA `procedure_pseudo_code` and save `raw_result`). Our port: `src/cs2d_server.c`.

- **Lua:** the dedicated links **PUC Lua 5.1.4 statically** (string `$Lua: Lua 5.1.4 ... PUC-Rio $`, no Lua DLL in
  its import table: ADVAPI32, bcrypt, KERNEL32, msvcrt, ole32, SHELL32, USER32, WINMM, WS2_32).
- **Tudp writer** (connection `+0x38`, vtable): `+0x98` Createpacket(reliable, new), `+0xa0` byte, `+0xa8` u16,
  `+0xb0` i32, `+0xb8` i64, `+0xd0` u8-length string, `+0xd8` u16-length string, `+0xe8` bank. **Reader**
  `PTR_DAT_1405f7998`: `+0x80` bytes left, `+0xf0` byte, `+0x100` u16, `+0x108` i32, `+0x110` i64, `+0x128` u8 str,
  `+0x130` u16 str. Createpacket(1,0) appends to the open reliable datagram (the [240] info lines share one).
- **Datagram ids** (proxy): reliable ids 2, 4, 6…; unreliable/ACK header = last reliable id + 1. The client's first
  datagram starts with a control triple `03 32 00` before `fc 00` (type 3 not identified).
- **recv 252 = `FUN_1401dc530`** (29 KB). Flood guard `FUN_1400d4c90(ip, 0xfc, n, 60000)` per sub-op (n = 60 for 0 /
  3 / 100, 200 for 1, 30 for 5). Sub-ops 0, 1, 3, 5, 10, 50, 51, 100.
  - `[252,0]` → `[252,0, secret str]`; the secret is the global `PTR_PTR_1405a6800` (10 letters, same for every
    client; its generator is not decompiled).
  - `[252,1]` checks, in order, each answered `[252,2,status]` and logged `JOIN FAILURE: name (ip:port) can't join
    (...)`: name length 1..25 else **22**; transformed secret = `FUN_1401be680(secret)` (= `cs2d_net_secret`) else
    22 "precon. auth failed" + 30 min IP ignore; scramble field == `FUN_1401be810(...)` else 22 — **that function
    always returns ""** (it builds a string and returns the empty one), so the field must be empty; client id
    starts with `\xfa` else 22 "auth failed [v]"; password (`encrypt(sv_password,"j01nk3y")`) else **1** (logged
    only when one was sent); players < sv_maxplayers else **3**; ban `FUN_1400df2c0` → **4** + reason str;
    version `encrypt("1.0.1.8","funkymonky")` and u16 1018 else **5**; busy (`DAT_1407dfe0c/08`) **8**; same
    connection already a player **9**; `DAT_1405a6514` clients per IP **23**; usgn 0 with sv_usgnonly **2**;
    online U.S.G.N. verification **6**. Success: `[252,2,0,id,map,secret]`, then `FUN_1401bb970` prints
    `<name> is using IP <ip>:<port>, no U.S.G.N. ID and no Steam`.
  - `[252,3, sha, enc secret, download]` → `[252,4,status]`: enc secret = reverse(secret) shifted by the key
    table `DAT_1405f02d0` (= client's `"!aCz0jK5f\xfc"`) else **10** "client verification failed"; then the map
    checks (0 ok, 11 differs, 12 missing, 13 transfer follows).
  - `[252,5, map, sha]` → `[252,6,0x18]` on a mismatch; else sets Tpl `+0xf4` = 1 and sends `[252,6,0,...]` (writer
    at 0x1401ded48, field list in `cs2d_sv_build_join_settings`), the snapshot sections 1, 2 (50/msg), 3 (80),
    4 (180), 5 (60), 6 (100), 10 (15), 7 (20), 8 (30), 9 (70), 11 (bank), 12 (`0xff` + u16 count, then 10/msg
    `{u16 index, str}`), 13 (u16 str mod list), 200 `"ACK"`; each list closes with a `count 0` message. With
    `PTR_DAT_1405a6690` = 1: `[28,0,1]` appended + `FUN_14002e9e0` per other player. Then the server info
    `[240,0,50,"1"]`, one `[240,0,51,line]` per `sys/serverinfo.txt` line (u16 count + UTF-16), `[240,0,50,"2"]`.
- **[252,6] values with the shipped server.cfg** (proxy_ph1.log): `DAT_1407dfa80` = map header byte 0
  (`scroll_map`, 1 on de_dust2); `sv_daylighttime` internal default **-1** (sent +1 = 0); OS byte 1; lag comp 2;
  ticks 16; `PTR_DAT_1405a65d0` 1, `65b8` 127, `65b0` 1 (not identified); `FUN_1401dba60` writes 3 zero bytes.
- **[252,7,1] record:** 50 bytes for a fresh player; after the score `+0x298 +0x28c +0x294` u16, `+0x90` u8, x / y /
  rot u16, health / armor objects (`+0x90`), vtable `+0x200`, weapon (`+0xc`, `+0x1c`), `+0x3dc` u8, `+0x3e0` u16
  (0xffff). Tpl `+0x3c8` (the "spr" field) is still empty at join.
- **Section 10** = the images of the map's Env_Sprite entities, first occurrence, entity order (21 on de_dust2);
  **section 12** = `sys/mapcycle.cfg` sorted by name (14 maps). Both match the capture; their builders are not
  located in the binary yet.
- **Name sanitizer `FUN_14010d970`:** trim, cut 25, `PIayer`→`Player`, drop `*DEAD*`, `(Radio)`, `©`; keep chars
  ≥ 0x20 except `,` `;` 0x7f..0xa0 0xa6 0xae; an all-number or empty name becomes `Player` (language string
  `PTR_DAT_1405adb10 +0x120`); a taken name gets ` 2` … ` 65`.
- **Per-second block (0x1401e505d, gate `DAT_1407dff88`):** every 2nd second each connection gets unreliable
  `[229, MilliSecs - DAT_1407e0184]` (`DAT_1405a64e4` > 0) or `[249, …]`; every 3rd second the joined players
  (`+0xf4`) get `[247, n, DAT_1407dfe28, DAT_1407dfe34/8 u16, DAT_1407dfe30/8 u16, n × {id, ping}]` over the players
  in a team (`+0x230` > 0); ping 0xffff for the host, 0xfffe for a bot (`+0x198`), else min(`+0xd0`, 30000).

## Phase 2: dispatch, team, spawn, movement relay

- **Receive dispatch** (`FUN_1401e3cf0`, the network loop; the old note "dispatcher `FUN_14009bb30`" is wrong, that
  is init code): jump table `0x1406cdb14` indexed by `opcode - 7` (7..253). A handler returns 0 = next message,
  > 0 = drop the rest of the datagram. Handler per opcode (the `?` names are not in `cs2d_netcode_codes.txt`):
  7 0x1401c8930; 8 0x1401c8c50; 9 0x1401c8f00; 10 0x1401cb570; 11 0x1401cb930; 12 0x1401cbce0; 13 0x1401cc0b0; 14 0x1401cc310; 15 0x1401cc570; 16 0x1401cc750; 17 0x1401cc9f0; 18 0x1401d3840; 19 0x1401ccc90; 20 0x1401cd140; 21 0x1401cd470; 22 (inline 0x1401e4929); 23 0x1401cd710; 24 0x1401cda30; 25 (inline 0x1401e49da); 26 0x1401cde00; 27 0x1401ce010; 28 0x1401bcd60; 29 0x1401ce2f0; 30 0x1401ce7c0; 31 0x1401cead0; 32 0x1401ced00; 33 (inline 0x1401e48b6); 34 0x1401cf0c0; 35 0x1401cf350; 36 0x1401cf4b0; 37 0x1401cf630; 38 (inline 0x1401e4820); 39 0x1401cf8d0; 40 0x1401cfba0; 41 0x1401cfd00; 42 0x1401cfe80; 43 0x1401d0050; 44 0x1401d03f0; 45 0x1401d06b0; 46 0x1401d0970; 47 0x1401d0ca0; 48 0x140103e80; 49 (inline 0x1401e4708); 50 0x1401d11a0; 51 0x14012ede0; 52 (inline 0x1401e4652); 53 0x1401d16f0; 54 0x1400bf930; 55 0x1401d2520; 56 0x1401d26a0; 57 0x1401d28b0; 58 (inline 0x1401e456d); 59 (inline 0x1401e450e); 60 0x1401d2d40; 61 (inline 0x1401e44a3); 62 (inline 0x1401e4444); 63 (inline 0x1401e43e5); 64 0x1401d3280; 65 0x1401d3430; 66 (inline 0x1401e436f); 67 0x1401d3730; 68 0x1401d3840; 69 (inline 0x1401e42fb); 70 0x1401d3c40; 71 0x1401d3fb0; 72 0x1401d4430; 80 0x1401d4910; 81 0x1401d4dc0; 82 0x1401d4f30; 83 0x1401d50b0; 84 0x1401d5360; 85 0x1401d57f0; 86 0x1401d62b0; 87 0x1401d64b0; 100 0x1401c3500; 224 0x1401d6730; 225 0x1401d68b0; 226 (inline 0x1401e4083); 227 0x1401bb510; 228 0x1401bb620; 229 0x1401d7640; 230 0x1401d8360; 232 0x1401bb730; 234 0x14008d3f0; 235 0x1401d8b10; 236 0x1401d8d90; 237 (inline 0x1401e41af); 238 0x1401d9450; 239 0x1401d9720; 240 0x1401bd120; 241 0x1401d9990; 242 0x1401be1c0; 243 0x1401be4a0; 247 (inline 0x1401e3fdf); 248 0x140118970; 249 0x1401d9f10; 250 0x1400d4ed0; 251 0x1401da400; 252 0x1401dc530; 253 0x1401e3790.
- **Console command parser:** one long compare chain (`cmp r14,rdx` + `FUN_14004ef00`) around `0x140140a6b`
  .. `0x14014acc1`; each command block is reached from the previous block's `jne`, whose target loads the name
  (`lea rdx,[name]`). E.g. `mp_postspawn` → `DAT_1405a64c4` (clamped by `FUN_1400554a0(v, 0x1734)`).
- **`mp_postspawn`** (`DAT_1405a64c4`, default **30**): the **first** team choice spawns at once while the round
  is ≤ 30 s old, in sv_gamemode 0, with no round end pending (`FUN_1401624c0`). Defaults read from `.data`:
  `DAT_1405a64c8` 16, `64cc` 2, `64e4` 1, `6514` **5** (sv_maxclientsperip), `64c0` 1.
- **Team (`FUN_1401624c0`):** spectator needs look 5; T/CT look 0..5; team 4 = auto (smaller team, random when
  even); look 4 = random skin 0..3. Look 5 on a team = "team picked, skin menu next": `[20,id,team,5]` to the
  player only (stores `+0x400`). Balance (mp_autoteambalance, mode flag 0x14059c0a8): T only while T ≤ CT+1, CT
  while CT ≤ T+1, else `[20,id,10|11,look]` to the player. Success: `[20,id,team,look]` to all joined.
- **Mode tables** (init 0x14021f4f4..): T/CT start items `"2"/"1"` (modes 0..2), `"2,50"/"1,50"` (3),
  `""/"1"` (4); melee T/CT 50/50, 74/74 (Construction), 78/50 (Zombies); balance flag 1,0,1,1,1; respawn
  flag (0x14059c098) 0,1,1,1,1. Mode names Standard / Deathmatch / Team Deathmatch / Construction / Zombies!.
- **Spawn (`FUN_140160740`):** random active spawn entity of the team (list `DAT_1407dfa18`), redrawn when a
  living player stands on that tile; position = tile × 32 + 16; health 100, armor 0; items = mode list (or the
  Lua `spawn` hook's list; `"x"` = none), then the melee weapon if missing; held weapon = `Tpl.bestweapon`
  (last slot-1 item, else last slot-2, else melee); money = mp_startmoney on the first spawn (score and deaths
  0), clamped 0..16000. `[21,id,x,y,weapon,special,money u16,n,n×item]` reliable to all joined. Our `[21]`
  matched the original byte for byte (`test_server_join`, live).
- **Movement relay (`FUN_1401a68e0`, every frame):** players with `+0xfc` (1 ran / 2 walked) or `+0x118` (turned)
  set; counter `+0x164` cycles 0..2 and recipients ≥ 525 × 340 px away only get the update when it is 0.
  Unreliable, appended: `[12,id,rot]` (turn only), `[10|11,id,x,y]` (move only), `[13|14,id,x,y,rot]` (both);
  a dead recipient spectating the mover gets `[70]/[71]/[72]` with `aim × 0.512` when `DAT_1405a64ac` != 0.
  rot = degrees × 182.04445. The `[10]` inside `FUN_1401ca500` is the anti-speeder sending a position **back to
  the mover** (`MP_ANTISPEEDER` / "is too fast - reset position!"), not the relay.
- **`[248]` (`FUN_1401184f0`, at the end of a successful `[252,1]`):** `[248,id,name,spray,usgn i32,usgn name
  u16str,steam id i64,steam name u16str,os]` reliable to every joined player but the new one.
