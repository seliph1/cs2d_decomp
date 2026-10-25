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
  - **Lua `connect_attempt` inside `[252,1]` (2026-10-16, ded `0x1401dcd12`..`0x1401dcecd`):** after the whole
    packet is parsed and after the flood guard `FUN_1400d4c90(ip, 0xfc, 200, 60000)`, and **before every sanity
    check**, a "hooks on" test (`array +0x64 == 1`, `0x1401dcd41`) sends the handler to `0x1401de995`, which
    iterates the hook list at slot **`+0xc8`** of the array in `PTR_14059ea70` (the client dump uses the same
    index, 200). Arguments in order: name, DottedIP(ip) (`0x14041f690`), the connection's u16 port (`+0x10`),
    usgn id, usgn name, **steam id as a string** (`bbStringFromLong`) and steam name - the *unverified* values,
    exactly as help.php warns. A **non-empty returned string** cancels the join: the server logs
    `JOIN FAILURE: <name> (<ip>:<port>) can't join (Lua connect_attempt returned: <value>)` (literal
    `0x1405ee540`; the prefix literal `0x1405af700` holds `A9 00` + `255255000JOIN FAILURE: ` in `.data` -
    BlitzMax keeps the escape form there, our port logs the plain `JOIN FAILURE: ...` like its other messages)
    and answers **`[252,2,4,<value>]`**: status **4**
    (ban, `0x1401dce77`) plus the returned string as ban reason (`+0xd0` u8 string, `0x1401dcec7`). An empty
    string falls through to the checks above. Ported in `sv_recv_join1` (`src/cs2d_server.c`).
  - The `) can't join (...)` reason literals live in `.data` (object VA -> text); every one is logged as
    `JOIN FAILURE: <name> (<ip>:<port>) ` + literal: `0x1405EDC40` `) can't join (wrong password)`,
    `0x1405EDCA0` `) can't join (wrong game version)`, `0x1405EDD00` `) can't join (too many clients with this ip)`,
    `0x1405EDD80` `) can't join (tiles missing)`, `0x1405EDDE0` `) can't join (server is full)`,
    `0x1405EDE40` `) can't join (server is busy)`, `0x1405EDEA0` `) can't join (precon. auth failed)`,
    `0x1405EDF00` `) can't join (post load checksum missing)`, `0x1405EDF80` `) can't join (post load checksum mismatch)`,
    `0x1405EE000` `) can't join (no registered U.S.G.N. user)`, `0x1405EE080` `) can't join (map missing)`,
    `0x1405EE0E0` `) can't join (map differs)`, `0x1405EE140` `) can't join (map differs - unknown join mode: `,
    `0x1405EE1C0` `) can't join (loaded wrong map: '`, `0x1405EE420` `) can't join (auth failed)`,
    `0x1405EE480` `) can't join (auth failed [v])`, `0x1405EE4E0` `) can't join (already connected)`,
    `0x1405EE540` `) can't join (Lua connect_attempt returned: `.
  - **Password prompt / Reconnection handshake reset (CS2D.exe 0x1402cf1c1 / 0x1402d03b8):** when a client attempts
    to join a password-protected server without password, the server replies `[252,2,1]` (password required).
    `CS2D.exe` displays the password prompt `Tgui_joinmen.pwprompt`. When the user enters the password and clicks
    "Connect", `CS2D.exe` calls `FUN_14016a3c0(0)` to recreate its Tudp socket and **restarts the join handshake by
    sending `[252,0]` with reliable sequence number 2** from the same local port. On the dedicated (`sv_find_conn`),
    receiving `[252,0]` (`hello == true`) from an existing active connection must call `sv_conn_reset` (clearing
    the `seen` bitmap, setting `next_seq = 2`, `join_step = 0`) and remove any stale player (`sv_remove_player`).
    Otherwise, the incoming `seq = 2` is discarded by the replay filter as already processed, leaving the client
    unanswered until it times out into NAT hole punch retry loops.
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
  `[229, MilliSecs - DAT_1407e0184]` (`mp_anticlock` / `DAT_1405a64e4` > 0) or `[249, …]`; every 3rd second the joined players
  (`+0xf4`) get `[247, n, DAT_1407dfe28, DAT_1407dfe34/8 u16, DAT_1407dfe30/8 u16, n × {id, ping}]` over the players
  in a team (`+0x230` > 0); ping 0xffff for the host, 0xfffe for a bot (`+0x198`), else min(`+0xd0`, 30000).
  This list is the **only** source of the original client's scoreboard latency (`Thud_scoreboard`
  0x14012f2c0 writes `Tpl.ping`, 0xfffe/0xffff become "Bot"/"Server"); the RTT in it comes from the
  `[229]`/`[249]` replies. `mp_anticlock` is the dedicated's real cvar name (0x140141e51): `0` switches
  the 2nd-second ping to `[249]` (verified in `proxy_anticlock0.log`), and the original rejects `sv_antiaccel`.
  `DAT_1407dfe28` (server ticks / FPS) is calculated from real elapsed time `(frames * 1000) / elapsed` to avoid drift.
  On Windows, `timeBeginPeriod(1)` and `QueryPerformanceCounter` high-resolution sleep/spin-wait pace the loop to `g_sv.set.ticks` (default 50 ticks/s).

## Phase 2: dispatch, team, spawn, movement relay

- **Receive dispatch** (`FUN_1401e3cf0`, the network loop; the old note "dispatcher `FUN_14009bb30`" is wrong, that
  is init code): jump table `0x1406cdb14` indexed by `opcode - 7` (7..253, 247 entries). A handler returns 0 = next message,
  > 0 (specifically 2, 3, or 4) = drop the rest of the datagram cleanly. Exact table mapping verified:
  - Game action / client inputs:
    `7` 0x1401c8930 (fire); `8` 0x1401c8c50 (attack2); `9` 0x1401c8f00 (wepsel); `10` 0x1401cb570 (run); `11` 0x1401cb930 (walk); `12` 0x1401cbce0 (rot u16); `13` 0x1401cc0b0 (run+rot); `14` 0x1401cc310 (walk+rot); `16` 0x1401cc750 (reload); `17` 0x1401cc9f0 (hit report); `18` 0x1401e4350 (no-op: `xor esi, esi; jmp next`); `19` 0x1401ccc90 (kill report); `20` 0x1401cd140 (team); `23` 0x1401cd710 (buy); `24` 0x1401cda30 (drop); `26` 0x1401cde00 (use); `27` 0x1401ce010 (plant bomb); `28` 0x1401bcd60 (spray); `30` 0x1401ce7c0 (radio); `31` 0x1401cead0 (entityevent); `32` 0x1401ced00 (spectarget); `34` 0x1401cf0c0 (build); `39` 0x1401cf8d0 (respawn); `41` 0x1401cfd00 (kill projectile); `43` 0x1401d0050 (vote); `50` 0x1401d11a0 (portal); `53` 0x1401d16f0 (clientdata); `54` 0x1400bf930 (tweens); `56` 0x1401d26a0 (impulse); `57` 0x1401d28b0 (flashlight); `60` 0x1401d2d40 (specflash); `67` 0x1401d3730 (spectate mode); `68` 0x1401d3840 (firedir); `70` 0x1401d3c40 (rot+aim); `71` 0x1401d3fb0 (run+rot+aim); `72` 0x1401d4430 (walk+rot+aim); `80` 0x1401d4910 (rot phys); `81..87` 0x1401d4dc0..0x1401d64b0 (phys updates -> drop); `100` 0x1401c3500 (command relay / sub 2); `225` 0x1401d68b0 (voicechat); `227` 0x1401bb510 (keydown); `228` 0x1401bb620 (keyup); `229` 0x1401d7640 (clock ping reply); `230` 0x1401d8360 (leaderboard); `232` 0x1401bb730 (clientsetting); `235` 0x1401d8b10 (rcon challenge req); `236` 0x1401d8d90 (rcon cmd); `238` 0x1401d9450 (name change); `240` 0x1401bd120 (chat); `241` 0x1401d9990 (rcon legacy); `242` 0x1401be1c0 (sv pw); `243` 0x1401be4a0 (UTF-16LE log string); `249` 0x1401d9f10 (ping reply); `250` 0x1400d4ed0 (ping); `251` 0x1401da400 (info query); `252` 0x1401dc530 (join); `253` 0x1401e3790 (leave).
  - Server-to-Client dropped opcodes in dedicated server mode (`DAT_1407dfe48 != 0`):
    Return code 3 or 4 breaks datagram parsing cleanly dropping the remainder:
    `15`, `21`, `22`, `25`, `29`, `33`, `35`, `36`, `37`, `38`, `40`, `42`, `44`, `45`, `46`, `47`, `48`, `49`, `51`, `52`, `55`, `58`, `59`, `61`, `62`, `63`, `64`, `65`, `66`, `69`, `224`, `226`, `234`, `237`, `239`, `247`, `248`.
  - Default unused entries: all other opcodes (73..79, 88..99, 101..223, 231, 233, 244..246) point to `0x1401e3e80` (`mov esi, 2; jmp break`), cleanly terminating datagram parsing.
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
- **Rounds & End (`FUN_14015c840`, `FUN_1402520b0`):** `[226, 1, winner, mvp, reason_lstr, mode_byte]` broadcast on
  round end; score awarded, intermission countdown (default 3 s); `[22, reason, count, ...]` broadcast on round start
  resetting surviving players and spawning new/dead players.
- **Combat relay (`FUN_140158060`, `FUN_140191a70`, `FUN_1401fd590`):** `[9, pid, type]` weapon selection broadcast;
  `[7, pid, x, y, rot, weapon]` primary attack relay; `[8, pid, mode]` secondary attack relay.
- **Damage & Kills (`FUN_1401658c0`, `FUN_1401647d0`):** damage reduces armor by ~35% (`ceil(dmg * 0.35)` absorbed),
  remainder to HP; broadcasts reliable `[17, victim, attacker, hp, armor]`. On fatal damage: sets `alive = 0`,
  increments victim deaths, awards killer score (+1, +$300 for enemy kill; -1 / penalty for team kill / suicide),
  broadcasts reliable `[19, victim, killer, assist, weapon, x, y]`.
- **Buy (`FUN_1401cd710`):** client sends `[23, type]`; server verifies price and player balance, deducts money,
  adds item to inventory (`sv_give`), handles equipment/armor limits, broadcasts `[9]` on weapon change, and responds
  reliable `[23, id, type, money u16, 0]`.
- **Drop `[24]` & Collect `[25]` (`FUN_14016bbd0`, `0x1401cda30`, `0x1401e49da`):**
  - Drop: client sends `[24, type]`. Server validates player owns item; cannot drop knife / melee (types 50, 74, 78).
    Removes item from inventory, switches held weapon to `bestweapon` if holding the dropped item, creates ground item
    slot, clears bomb/kit carried flags, and broadcasts reliable `[24, pid, type, clip, reserve, sub, item_id, tx, ty, held_weapon]` (15 bytes).
  - Collect: **client never sends [25]** (dedicated dispatch case 18 rejects it with code 3). Server is authoritative
    and checks living players in contact with ground items on the same tile every frame (`cs2d_sv_step_items`).
    A player who dropped an item cannot re-collect it on the same tile until moving away to another tile (`dropped_by == pid && drop_tx == ptx && drop_ty == pty`).
    Validates slot rules (cannot carry multiple primary or secondary weapons, bomb only for T, defuse kit only for CT,
    kevlar only if armor < 100). On pickup: adds item to inventory preserving dropped clip and reserve, removes ground item,
    and broadcasts reliable `[25, pid, item_id, type, clip, reserve, 0]` (10 bytes).
  - Fadeout `[40]` (`0x140a14d94`): dropped items age 1 s per second; after 15 s server removes ground item and broadcasts
    reliable `[40, item_id u16]` (3 bytes).
- **Reload `[16]` (`FUN_14018b1c0`, `0x1401cc750`, `FUN_140194650`):**
  - Mode 1 / 100 (start reload): client requests reload. Server validates player is not already reloading, planting or defusing, clip is not full, and player has reserve ammo (or `mp_infammo != 0`). Sets `is_reloading = true`, calculates reload timer expiry `now + reload_ticks * 5` ms, and broadcasts `[16, pid, mode]`.
  - Server tick (`cs2d_sv_step_reloads`, `FUN_140194650`): server ticks active reloads against `reload_end_ms`. When timer expires, clip weapons automatically invoke mode 2, and shell weapons (M3 / XM1014) invoke mode 4.
  - Mode 2 (clip refilled): server validates `is_reloading`, refills clip from reserve clamped to `needed = clip_size - clip` (or full clip on `mp_infammo`), clears `is_reloading`, and broadcasts `[16, pid, 2]`. If player was not reloading, converts to mode 3 (abort).
  - Mode 4 (shell reload): server transfers 1 shell from reserve to clip, resets timer for next shell (`now + reload_ticks * 5` ms), and if clip is full or reserve is empty, clears `is_reloading`. Broadcasts `[16, pid, 4]`.
  - Mode 101 (`0x65`): client shell compensation; adds 1 shell, completes reload if full or out of reserve, without broadcast (`LAB_140214c10`).
  - Mode 3 (abort): cancels reload (`is_reloading = false`). Aborts automatically on weapon switch (`cs2d_sv_select_weapon`) or death (`cs2d_sv_kill`).
- **Chat `[240]` (`FUN_140153900`, `0x1401bd120`):** client sends `[240, mode, len, UTF-16LE]`. Mode 1 = all, Mode 2 = team.
  **Server `msg`/`msg2` center tag:** `FUN_140153900` at `0x1401546a0` checks whether the text ends with `@C`
  (literal `0x1405e80e0`), removes the two characters and broadcasts `[240, 0, 2, ...]`; otherwise mode 1.
  Only the uppercase spelling exists in the dedicated binary. The client's mode-2 path calls `Thud_msg.add_center`.
  Server verifies alive status vs `mp_deadchat` (dead players cannot chat to living players unless `mp_deadchat 1`),
  logs to console (`cs2d_server_print`), and broadcasts reliable `[240, pid, mode, len, UTF-16LE]` to eligible recipients.
- **UTF-8 text -> UTF-16LE (2026-10-20):** Lua/map/config strings are UTF-8; the `[234]`, `[235]`, `[240]`
  text fields (and `sys/serverinfo.txt` lines) are UTF-16LE. The dedicated must decode UTF-8
  (`cs2d_net_utf8_to_utf16`, surrogate pairs for > U+FFFF); expanding bytes showed `"não"` as `"nÃ£o"`.
  Invalid UTF-8 falls back to the byte as a Latin-1 code point. The Windows console is switched to
  `CP_UTF8` at startup so `print`/`msg` echoed to stdout stay readable.
- **`UTF-8:` escaped text (2026-10-20):** the engine also accepts a textual form documented by the
  Changelog (`"hello" = "UTF-8:\x68\x65\x6C\x6C\x6F"`) for `print`, `msg`, `msg2`, `parse`, `hudtxt`
  and `sv_msg`. WQ2D's `utf8.convert` emits the first byte as `\xNN` and continuation bytes as a bare
  `xNN`: `©` = `UTF-8:\xC2xA9`, `€` = `UTF-8:\xE2x82xAC`. `cs2d_net_utf8_unescape` decodes both spellings
  to raw UTF-8 before `cs2d_net_utf8_to_utf16`; it is applied to msg/msg2/sv_msg, Lua menu, hudtxt,
  `print` and `parse` (so WQ2D's `parse(utf8.convert(cmd))` runs the decoded command).
- **Spectator `[67]` & `[32]` (`0x140108260`, `FUN_1401ced00`):**
  - `[67, target_id]`: updates player's `spectating_player` target.
  - `[32, sx, sy]`: free-look position updates sent by dead/spectating players where `sx/sy = pos + 32768`. Server decodes
    via `pos = s - 32768` and sets player `x` and `y`.
  - Aim relay: when a player moves/rotates, spectators following that player receive `[70, id, rot, aim]`, `[71, id, x, y, aim]`,
- **Team Switch Suicide (`FUN_1401624c0`, `FUN_1401647d0`):**
  If a living player (`pl->alive`) on team 1 (T) or 2 (CT) switches team (to spectator or to the opposing team), the
  server immediately triggers suicide via `cs2d_sv_kill(sv, id, 0, 0, 0)`: victim deaths incremented, score updated,
  and reliable `[19, victim, 0, 0, 0, x, y]` broadcast to all players.
- **Respawn Timers & Modes (`FUN_14017cbb0`, `FUN_1401cf8d0`, `[39]`):**
  - In modes with `respawn == 1` (Deathmatch mode 1, Team Deathmatch mode 2, Zombies mode 3, etc.): dead players automatically
    respawn after `mp_respawndelay` seconds (`sv_gamemode` mode rule table). `cs2d_sv_step_respawns` checks `death_ms` every
    frame and calls `cs2d_sv_spawn`.
  - Opcode 39 (`[39]`): client request to respawn. Server checks that the gamemode allows respawns (`sv_mode(sv)->respawn == 1`)
    and that `mp_respawndelay` seconds have elapsed since death before spawning the player.
- **Position Validation & Anti-Speeder (`FUN_1401c99a0`, `FUN_1401ca500`):**
  - Limit & Collision checks (`FUN_1401c99a0`): coordinates must remain within `[16 .. width*32 - 16]` and `[16 .. height*32 - 16]`.
    Target tile must not be solid (`cs2d_map_is_solid(map, tx, ty)`). If a player steps outside the map or into a solid wall,
    server rejects the movement and resets player position via unreliable `[11, id, last_valid_x, last_valid_y]`.
  - Anti-speeder (`FUN_1401ca500`): calculates distance moved since `last_move_ms`. If `dist > 160.0f` and velocity
    `dist / dt > 650.0f` px/s, server logs `"MP_ANTISPEEDER: %s is too fast - reset position!"` to console and sends reliable
    `[10, id, last_valid_x, last_valid_y]` back to the mover.
- **Anti-speeder strike model (FUN_1401ca500, REA + objdump 2026-10-24):** the function is the server's move step `(pl, x, y, walk)`; the reported distance to `Tpl +0x240/+0x244` is compared with an allowed
  distance (elapsed ms x speed, ping tiers 100/200/500, `mp_antispeeder` level selects a tolerance via the jump table `0x1406cda6c`). Over the limit it adds `1 + excess` to `+0x178` and 1 to `+0x17c`;
  a valid move subtracts 1 from each. `+0x178 > 30 && +0x17c > 50` -> "MP_ANTISPEEDER: Speedhack detected (speed)!". Other detectors: moves/s `+0x180 > 64` and summed distance `+0x184` per second
  ("packet flow" / "dist", strikes `+0x188`/`+0x18c`, needing > 4 with ping <= 199 or > 9 with ping <= 249). Counters are only zeroed in the Tpl constructor (0x140042241): teleport and respawn do not reset them.
- **[229] anti-clock (handler FUN_1401d7640 @ 0x1401d7690):** reply `[229, srv_ts, cli_ts]`; ping = now - start - srv_ts (< 30000); `+0xd8 = cli_ts - +0xd4`, `+0xdc = diff x 0.0005`; ratio > 1.0 + `DAT_1405a64e0` (0.1)
  -> `+0xe0++`, else `-1`; `mp_anticlock` N > 0 kicks (`kick <id> "Speedhack Detected"`) above `{0,8,4,2}[N]` strikes (array from `0x1406dafe0`). 0x1402e2aa0 is NOT this handler (CRT formatter).
- **Per-spawn Team Change Limit & Zombies Team Rules (`FUN_1401624c0`, `FUN_140160740`):**
  - `pl->team_changes` (`Tpl +0x2a0`) is reset to `0` on `cs2d_sv_spawn` (`FUN_140160740` line 198) and incremented on every
    completed team switch (`FUN_1401624c0` line 464).
  - Client-initiated (`!force`) switch when `pl->team_changes > 0 && pl->team >= 1` is refused with reliable `[20, id, 12, look]`
    to `id` alone (`FUN_1401624c0` lines 144-147, 193, 245-254; if `look == 5 && (team == 1 || team == 2)`, stores `pl->pending_team = team`).
  - In Zombies mode (`sv_gamemode == 4`, `FUN_1401624c0` lines 266-313): switching between T and CT (`pl->team >= 1 && team >= 1`)
    is refused with reliable `[20, id, 13, look]`; joining CT (`team == 2`) when both teams have `>= 1` player and round age `> 30` s
    forces `team = 1` (T).
  - Spawn-time Auto-Teambalance (`FUN_140160740` lines 162-191): when `mp_autoteambalance == 1 && sv_gamemode != 4 && mode->balance == 1`
    and `|T - CT| > 1`, spawning a player on the larger team switches them to the smaller team with a random skin `0..3` and broadcasts
    reliable `[20, id, new_team, look]` before `[21]`.

## Phase 3: in-game name change [238], RCON [236]/[241]/[242], playerdata [42] & console commands

- **In-Game Name Change `[238]` (`FUN_1401d9450` → `FUN_14010ddb0`):**
  - Client sends `[238, new_name (rstr)]`; server sanitizes via `FUN_14010d970` (truncating to 25 chars). If equal to `pl->name`, returns.
  - Unless `force` (`setname`): if `pl->name_changes > 0` (`Tpl +0x2a4`, reset to `0` on spawn), refuses with reliable `[238, 255, 0, ""]`
    to `id` alone (`0x14010e610`); if `!pl->alive`, stores `pl->pending_name` (`Tpl +0x2a8`), replies reliable `[238, 254, 0, ""]` to `id`
    alone (`0x14010dfef`), and applies it automatically on `cs2d_sv_spawn` (`FUN_140160740` lines 200-203).
  - On success: updates `pl->name`, increments `pl->name_changes++`, clears `pending_name`, logs `"%s changed name to %s"`, and broadcasts
    reliable `[238, id u8, hide_msg u8, name (wstr)]` (`0x14010e108`).
- **RCON (`[236]`, `[241]`, `[242]` — `FUN_1401d8d90`, `FUN_1401d9990`, `FUN_1401be1c0`, `FUN_1401bd520`):**
  - Password key: `"mysecretremotecontrolpw"` (`0x1405c6b00`); command key: `"rconcmd"` (`0x1405c4a80`). `sv_rcon` (`PTR_DAT_1405a66e0`)
    is truncated to 50 chars (`0x14014908d`). Players whose U.S.G.N. ID is in `sv_rconusers` (`PTR_DAT_1405a6500`) are authorized without a password.
  - `[236, enc_pw (rstr)]` (`rconcheck`, `0x1401d8d90`): decrypts `enc_pw`; if valid, sets `pl->rcon_auth = 1` (`Tpl +0xf0`); replies reliable
    `[236, enc_pw (wstr), ok u8]`.
  - `[241, enc_pw (rstr), enc_cmd (rlstr)]` (`rcon`, `0x1401d9990` → `0x1401bd520`): if `sv_rcon` is empty, replies reliable
    `[241, enc("2", pw_key) (wstr), enc("", cmd_key) (wlstr)]` (`0x1401bd826`); if password mismatches, replies with `"1"` (`0x1401bd90d`);
    on match, sets `pl->rcon_auth = 1`, logs `"Parse RCon (%s:%d): %s"`, and calls `FUN_140140a00(cmd)`.
  - `[242, pw (rstr), cmd (rlstr)]` (`public rcon`, `0x1401be1c0` → `0x1401bd520`, connectionless): if `sv_rcon` is empty, replies unreliable
    `[242, "2" (wstr), "" (wlstr)]` (`0x1401bdc50`); if password mismatches, replies with `"4"` (`0x1401bdd16`); on match, logs
    `"Parse RCon ext. (%s:%d): %s"` and calls `FUN_140140a00(cmd)`.
- **Playerdata `[42]` (`FUN_140159840`, `[42, id u8, mode u8, val u16]`):**
  - Modes: `0` health (`0..maxhealth`, kills if `0`), `1` score, `2` money (`0..16000`), `3` armor (`0..255`), `4` deaths, `5` equip weapon
    (`0x14015bc60` redirects armor items `57->100`, `58->200`, `79..84->201..206` to mode `3`), `6` strip weapon (`0` strips all; armor items
    redirect to mode `3, val=0`), `7` speedmod (`wire = speedmod + 100`, clamped `0..200`, `Tpl +0x390`), `8` maxhealth (`1..250`, also sets
    `health = maxhealth`, `Tpl +0x268`), `9` assists, `10` mvp, `11` rank (`0..17`).
  - The setter broadcasts the applied `[42, id, mode, val]` to every joined player (the loop at `0x1401599f1` writes
    opcode `0x2a`). Lua `equip`/`strip` rely on it so the clients' inventory/HUD stays in sync.
- **Item hooks `drop` / `collect` (help.php; measured 2026-10-21 on `cs2d_dedicated.exe` with
  `tools/agent/lua/drop_probe.lua`):**
  - `drop(id, iid, type, ammoin, ammo, mode, xpos, ypos)`; returning `1` cancels the default drop. A Laser (45)
    drop produced `id=1 iid=1 type=45 ammoin=15 ammo=45 mode=0 x=74 y=40` (`ammoin` = clip, `ammo` = reserve,
    `mode` = sub, `x/y` = the player's tile).
  - `iid` is the id of the ground item that will be created; the original allocates it before the hook (the item
    data itself is not readable yet). Our `sv_alloc_item_id` reserves it before `cs2d_sv_lua_hook_drop` and passes
    it to `sv_place_dropped_item`.
  - `collect(id, iid, type, ain, a, mode)` uses the same order as `walkover` (`ain` = clip/ammo in weapon,
    `a` = spare ammo, `mode` = sub). Its return value cannot cancel the pickup (`walkover` does that).
  - Passing fewer arguments made WQ2D `rp.drop` receive nil `xpos/ypos` and fail in `tile(nil,nil)` at `rp.lua:12624`.
- **Admin & Server Commands (`FUN_140140a00`):**
  - `setpos <id> <x> <y>` (`0x14011be70` → `FUN_140102950`): teleports player and broadcasts reliable `[15, id u8, x u16, y u16]`.
  - `setteamscores <t> <ct>` (`0x14011f890` → `FUN_140051ac0`): clamps `0..65535` and broadcasts reliable `[64, t u16, ct u16]`.
  - `sv_msg <text>` / `sv_msg2 <id> <text>` (`0x140144a92` / `0x140144b99` → `FUN_140153900`): sends reliable `[240, 0, 1, len u16, UTF-16LE]`.
  - `maket` / `makect` / `makespec <p>` (`0x140164590`): resolves player by name or ID (`FUN_14006a840`), forces team switch (`force = 1`).
  - `slap` (10 HP) / `deathslap` (1000 HP) (`0x140167e40`): broadcasts `"<name> was slapped"` (`[240,0,1]`) and applies armor-bypassing damage (`weapon = 255`).
  - `kick <p> [reason]` (`0x1401532f0` → `FUN_1401502a0`): resolves by name, IP, or ID; broadcasts reliable `[253, id, 2]` (no reason) or
    `[253, id, 16, reason (wstr)]` and closes the connection.
  - `killplayer <id>` (`0x140149ae0`), `spawnplayer <id> <x> <y>` (`0x14017c910`), `setweapon <id> <wpn>` (`0x140159700`),
    `sv_restart` / `sv_restartround [delay]` (`0x14015d2a0`), `endround [mode]` (`0x14015d3c0`), `listplayers` (`0x1401417d9`).

## Phase 4: Lua 5.1.4 subsystem (addhook, parse, player, image, timer, hudtxt, menu)

- **Lua Lifecycle (`src/cs2d_sv_lua.c`, `include/cs2d_sv_lua.h`):**
  - Uses vendored PUC Lua 5.1.4 (`third_party/lua51/lua_all.c`).
  - `cs2d_sv_lua_init`: creates state via `luaL_newstate()`, opens standard libraries (`luaL_openlibs()`), sets `_CS2DVERSION = "1.0.1.8"`,
    stores `cs2d_server_t *` in `LUA_REGISTRYINDEX["cs2d_sv"]`, registers native functions, and executes in order:
    1. Server entry point via `mp_luaserver` (`0x1405a64f8`, default `"server.lua"`; resolves in `sys/lua/`, supports paths/extensions; `""` or `"0"` disables)
    2. `sys/lua/autorun/*.lua` (alphabetical order)
    3. Map script `maps/<mapname>.lua` (if `mp_luamap != 0`, `0x1407dfb94`, default 1; also reloaded on map change)
    (Note: `wrapper.lua` is not auto-loaded; user scripts load it manually via `dofile` if needed)
  - `cs2d_sv_lua_frame`: stepped every server frame, advances active timer countdowns and runs expiration callbacks.
  - `cs2d_sv_lua_close`: closes Lua state, frees all hook nodes, and deallocates memory.
- **Hook Engine (`addhook`, `freehook`, `sethookstate`):**
  - Priority ordering: hooks are stored sorted by `priority` ascending. When an event fires, functions are invoked in order;
    the highest priority runs last, and its return value is the final return value of the hook chain.
  - Cancellation semantics: for cancellable hooks (`team`, `buy`, `drop`, `collect`, `name`, `hit`), a return value of `1` vetoes/cancels
    the action before game state changes or network packets are sent.
  - Supported game hooks: `join`, `leave`, `team`, `spawn` (can return string item list or 1 to cancel), `select`, `attack`, `attack2`,
    `hit`, `kill`, `die`, `buy`, `drop`, `collect`, `reload`, `say`, `sayteam`, `name`, `endround`, `startround`, `menu`, `serveraction`.
- **Player API (`player`, `playerweapons`, `playerammo`):**
  - `player(id, prop)`: implements 40+ properties including `"health"`, `"armor"`, `"money"`, `"score"`, `"deaths"`, `"assists"`,
    `"mvp"`, `"name"`, `"ip"`, `"port"`, `"ping"`, `"team"`, `"look"`, `"weapon"`, `"weapontype"`, `"x"`, `"y"`, `"rot"`,
    `"table"`, `"tableliving"`, `"team1"`, `"team2"`, `"team1living"`, `"team2living"`, `"spectators"`.
  - `playerweapons(id)`: returns 1-indexed Lua array of weapon IDs currently held.
  - `playerammo(id, type)`: returns clip ammo of weapon type.
- **Command Parser & HUD Text (`parse`):**
  - Parses native server commands (e.g. `setmoney`, `sethealth`, `equip`, `strip`, `slap`, `kick`).
  - Intercepts and executes Lua HUD text commands:
    - `hudtxt <id> "<text>" <x> <y> [align]`: broadcasts reliable `[234, id, align, x+0x4000 u16, y+0x4000 u16, len, UTF-16LE]`.
    - `hudtxtclear [id]`: id 255 clears all.
    - `hudtxtalphafade`, `hudtxtcolorfade`, `hudtxtmove`: broadcasts opcode `[54]` targeting `200 - id`.
- **Dynamic Images & Tweens (`image`, `tween_*`):**
  - `image(path, x, y, mode, [target])` (`FUN_14011aab0`): allocates dynamic object (`Tdo`, `typ = 40`, `CS2D_DO_IMAGE`) in `sv->dynobj`, resolves image dimensions via `stbi_info`, and broadcasts reliable `[51, 0, id u16, mode u8, target u8, x u16, y u16, path (wstr)]`.
  - Manipulation functions: `imagecolor` (`0x140129a50`, mode 1), `imagealpha` (`0x14012a940`, mode 2), `imageblend` (`0x14012a090`, mode 3),
    `imagescale` (`0x14012a280`, mode 4), `imagepos` (`0x14012a560`, mode 5), `freeimage` (mode 6), `imageframe` (`0x14012b1d0`, mode 7),
    `imagephysicsparent` (mode 8), `imageshadow` (mode 9), `imagehitzone` (opcode 59). In `cs2d_dedicated.exe`, all call `Createpacket(1, 0)`: reliable UDP grouped into the open datagram (`fresh = false`).
  - Round start lifecycle: `cs2d_sv_round_start` removes images from the previous round BEFORE `startround_prespawn`, spawns players, sends and flushes Opcode 22 (`Tdo.freeall` on client), and only then invokes `startround`, ensuring images created in `startround` (like in `sys/lua/samples/hudimg.lua`) survive on both client and server.
  - Client resolution defaults: `screen_w = 850`, `screen_h = 480` default on connection for proper `mp_hudscale 0` placement.
  - Tweens: `tween_alpha`, `tween_color`, `tween_move`, `tween_rotate`, `tween_rotateconstantly`, `tween_scale`, `tween_frame`, `tween_animate`
    broadcast opcode `[54]` with target `200 + id`. **The dedicated also creates the local Ttween and runs
    `Ttween.update` every frame** (see the "Lua object()/player() screen values and the shared Tdo space" section):
    without it `object()`/`imageparam()` never see the animated values and scripts like the C4 image-projectile
    system (`cm.create_projectile` / `cm.projectile_update`) break.
- **Server Timers (`timer`, `freetimer`):**
  - `timer(ms, func, param, [count])`: executes `func(param)` every `ms` milliseconds. `count == 0` loops indefinitely;
    default is `count = 1`.
  - Decrements remaining count on each invocation and frees automatically when completed.
- **Server Menus (`menu`, Opcode `235`):**
  - `menu(id, text)`: sends opcode `[235, title (wlstr), page u8, items (wlstr)]` to target player.
  - Client response `[235, pid, slen, UTF-16LE title, button u8]`: invokes Lua hook `"menu"` with `(id, title, button)`.
- **Terminal Output & Interactive CMD Console (`print`, `msg`, `msg2`, errors):**
  - All Lua outputs (`print(...)`, `msg(...)`, `msg2(...)`, Lua runtime/syntax errors) print to `stdout` in the CMD terminal where `cs2d_server.exe` runs, unbuffered (`setvbuf(stdout, NULL, _IONBF, 0)`), without color tag corruption (stripped via `cs2d_server_print`), and dispatch to `log` hook.
  - `print(...)`: follows Lua 5.1 / BlitzMax `FUN_1400f3a10` conventions, converting all arguments to strings separated by `\t`.
  - Non-blocking CMD terminal input: `_kbhit()` and `_getch()` poll user input every frame in `main_dedicated.c`, executing lines through `cs2d_sv_exec` (allowing `lua <code>`, `msg <text>`, `echo <text>`, and all server commands directly in the CMD prompt).

## Phase 5: Bomb system, hostages, bots, bans & map change

- **Bomb Defuse & Explosion (`[30]`, `[26]` — `FUN_14016a2d0`, `FUN_14016a700`, `FUN_14016ab00`):**
  - Plant: Terrorist holding C4 initiates plant via `[30, 1]`. Server starts countdown (`mp_c4timer` / 35s default).
    Plant abort sends `[30, 2]`. Plant completion sends reliable `[30, 3, tx, ty, id]` and equips bomb on ground.
  - Defuse: Counter-Terrorist initiates defuse via `[26, 3]` (Use). Defuse abort sends `[30, 4]`.
    Completion sends reliable `[30, 5, id]`, awards CT team $3250, defuser +3 score, and calls `cs2d_sv_round_end(sv, 21)` (Bomb Defused).
  - Explosion: when timer reaches 0, bomb detonates via `[30, 6, tx, ty]`. Deals 500 splash damage over a 350px radius
    attenuated linearly (`dmg = 500 * (1 - dist / 350)`), calls `cs2d_sv_round_end(sv, 20)` (Target Bombed), and awards T team $3250.
- **Hostages (`[29]` — `FUN_140167200`, `FUN_1401676f0`):**
  - Spawning: initialized from map entities (`Info_Hostage`, type 4).
  - Use (`[26]` / `[29, 1/2]`): CT pressing Use toggles follow mode (`[29, 1, h_id, ct_id]`) or release (`[29, 2, h_id]`).
  - Movement & Rescue: hostages step towards following player. When reaching an `Info_RescuePoint` entity (type 8),
    hostage is rescued: awards rescuer +2 score, CT team $1000 each, and broadcasts reliable `[29, 5, h_id]`.
  - Damage & Death (`[29, 3/4]`): hostages take damage; dying plays sound and broadcasts reliable `[29, 4, h_id]`.
- **Projectiles & Flashlight (`[27]`, `[41]`, `[57]`):**
  - Grenades / Projectiles (`[27, id, type, x, y, rot, speed]`): server consumes one grenade from player's inventory
    and relays reliable `[27]` to all other joined players.
  - Kill projectile (`[41, id]`): relayed to clients to clean up active projectiles.
  - Flashlight (`[57, id, on/off]`): toggles player flashlight state and relays to other joined players.
- **Bot Management (`bot_add`, `bot_kick`, `bot_kill`):**
  - `bot_add [team] [look] [name]`: allocates a player slot with `bot = true`, ping `0xfffe`, and conn `-1`.
    Broadcasts join announcement `[248]` and spawns the bot.
  - `bot_kick <name>`: frees the bot slot and sends leave notification `[250]`.
  - `bot_kill <name>`: kills the bot with `cs2d_sv_kill(sv, id, 0, 0, 0)`.
- **Ban System (`banip`, `banusgn`, `bansteam`, `unban`, `listbans`):**
  - Stores up to `CS2D_SV_MAX_BANS` records with IP, U.S.G.N. ID, Steam ID, and reason.
  - On client connection (`FUN_1401dc530`), checks bans with `cs2d_sv_is_banned`. If banned, server rejects join with
    status `4` and the custom ban reason string.
- **Map Change (`cs2d_sv_change_map`, `[239, 1]` — `FUN_1401104f0`):**
  - Dynamically loads new map from `maps/<name>.map` on the heap (avoiding 14 MB stack allocation from `cs2d_map_t`).
  - Broadcasts reliable `[239, 1]` to notify all clients to reload the map.
  - Frees old map resources, recalculates SHA256 checksum, re-initializes hostages, resets team scores, and triggers `cs2d_sv_round_start(sv, 0)`.

## Phase 6: Lua C API 1:1, World Queries, DynObj & Hook Extensions

- **Lua C API Registrations (`src/cs2d_sv_lua.c`, `include/cs2d_sv_lua.h`):**
  - All 18 missing CS2D Dedicated Server Lua API functions decompiled from `cs2d_dedicated.exe` (jump tables and callback registries at `FUN_14013a980`):
    - `map(prop)`: queries map dimensions (`"xsize"`, `"ysize"`), `"tileset"`, entity counts (`"mission_vips"`, `"mission_bombspots"`, `"mission_escapepoints"`, `"mission_hostages"`, `"mission_ctfflags"`, `"mission_dompoints"`, `"teleporters"`), and dynamic objects (`"buildings"`).
    - `tile(x, y, prop)`: queries tile properties at tile coords (`"frame"`, `"property"`, `"wall"`, `"obstacle"`, `"deadly"`, `"entity"`).
    - `tileproperty(id)`: returns the collision property mode of a tile index (0 floor, 1/3/5 wall, 2/4 obstacle, 50..53 deadly).
    - `itemtype(type, prop)`: weapon and item metadata (`"name"`, `"slot"`, `"price"`, `"dmg"`, `"dmg_z1"`, `"dmg_z2"`, `"ammo"`, `"ammoin"`, `"range"`, `"dispersion"`, `"speedmod"`).
    - `item(id, prop)`: queries ground/inventory items (`"exists"`, `"type"`, `"name"`, `"x"`, `"y"`, `"ammo"`, `"ammoin"`, `"player"`, `"dropped"`, `"table"`).
    - `object(id, prop)`: queries dynamic objects / buildings (`"exists"`, `"type"`, `"name"`, `"x"`, `"y"`, `"tilex"`, `"tiley"`, `"health"`, `"maxhealth"`, `"team"`, `"player"`, `"table"`).
    - `objectat(tx, ty)`: returns object ID at tile coords or 0.
    - `closeobjects(x, y, radius [, type])`: returns array of object IDs within Euclidean distance.
    - `closeplayers(x, y, radius [, team])` (`FUN_14004cc30`): returns array of alive player IDs within radius; team filter maps 1 to T, 2 to CT/VIP.
    - `hascloseplayers(x, y, radius [, team])` (`FUN_14004ce50`): returns boolean `true`/`false` whether any alive player is within Euclidean radius.
    - `freeline(x1, y1, x2, y2 [, mode])`: raycasts line of sight through map collision grid in 5px steps. Mode 0 checks walls & obstacles; mode 1 ignores obstacles; mode 2+ checks player hitboxes (12px radius).
    - `entity(x, y, prop)`: queries entity attributes (`"typename"`, `"trigger"`, `"int0"`..`"int9"`, `"str0"`..`"str9"`, `"state"`).
    - `entitylist([type])`: returns list of entity locations with coordinates `{ x, y }` and string keys `"X Y"`.
    - `stats(id, prop)`: query player persistent stats (`"exists"`, `"name"`, `"score"`, `"kills"`, `"deaths"`, `"assists"`, `"shots"`, `"hits"`, `"count"`).
    - `addbind(key)` / `removebind(key)` / `removeallbinds()`: manages custom client key hooks intercepted by the server.
    - `reqcld` / `reqhttp`: stubs registered for script compatibility.
- **Extended `player` and `game` queries:**
  - `player(id, prop)`: added `"tablebot"`, `"tablehuman"`, `"players"`, `"language"`, `"favweapon"`.
  - **Mouse properties (2026-10-20):** the dedicated derives them from the rotation + aim distance the
    client already sends in `[70]`/`[71]`/`[72]` (no extra traffic):
    `mousex = 425 + sin(rot) * aim`, `mousey = 240 - cos(rot) * aim` (850x480 virtual screen),
    `mousemapx = x + (mousex - 425)`, `mousemapy = y + (mousey - 240)`,
    `mousedist = (int)distance(player, world cursor)`. `mousemapx/y` and `mapmousex/y` are the
    aliases. Dead players and clients that have not sent an aim packet yet return **-1**.
    Measured on `cs2d_dedicated.exe` (0x1400c936c..): `x=1360 y=816 rot=118.07007 aim=254`
    -> `mousex=649 mousey=359 mousemapx=1584 mousemapy=935 mousedist=253`; reproduced with the
    original client against our server (`x=1392` -> `1621,938,654,362,259`).
    Probe: `tools/agent/lua/mouse_probe.lua`.
  - `game(prop)`: added `"version"` (1.0.1.8), `"dedicated"` (1), `"phase"`, `"round"`, `"time"`, `"maptimeleft"`, `"ticks"`, `"port"`, `"sysfolder"`, `"bombplanted"`, and generic cvar fallback.
- **Generic CVar Storage & Settings:**
  - `cs2d_sv_cvar_t cvars[128]` in `cs2d_server_t`: stores custom script cvars dynamically set via server commands or config files, queried via `game(cvar_name)`.
  - Settings fields: `mp_luaserver` (custom server script path, e.g. `"WQ2D/_server.lua"`), `mp_luamap` (toggle for per-map Lua loading).
- **Console Log Hook (`hook "log"`):**
  - `cs2d_server_print` dispatches the `"log"` hook with color-stripped text to the active server instance, protected against recursion via `s_in_log` guard.

## Live cvar change [237] (`FUN_14015d960`, dump `docs/dump/dedicated/FUN_14015d960_setting_broadcast.c`)

- Each cvar handler in the console parser (`FUN_140140a00`) stores the value and calls `FUN_14015d960(id, int, _, str)`
  **only if the value changed** (`cmp [rax+4], edx`). The function, in server mode (`DAT_1407dfe48`), sends reliable
  `[237, id u8, value]` to every connection whose player has `+0xf4 == 1`: ids **1, 11, 12 = u8-length string**, every
  other id an **i32** (the raw stored value; `sv_daylighttime` has no +1 here, unlike `[252,6]`).
- Ids (ecx at the call sites 0x14014b867..0x14014d143): 1 sv_name, 2 sv_fow, 3 sv_specmode, 4 mp_roundtime,
  5 mp_freezetime, 6 mp_c4timer, 7 sv_gamemode (clamped 0..4), 8 mp_respawndelay, 9 mp_infammo, 10 mp_radar,
  11 mp_supply_items, 12 mp_buymenu, 13 mp_hud, 14 mp_flashlight, 15 mp_smokeblock, 16 mp_hovertext, 17 sv_maxplayers,
  18 sv_friendlyfire, 19 sv_forcelight, 20 sv_daylighttime, 22 mp_hudscale, 23 sv_voicechat, 24 sv_offscreendamage.
  21 and 25 are not console cvars (the function prints a message; callers 0x14015eab9 / 0x14015fe9b, not identified).
- Our client decodes only some of them (client `FUN_1401e3f10`: 3, 11, 14, 16, 19, 20, 22 do something visible); the
  rest are stored in the settings arrays. Port: `sv_broadcast_setting` in `src/cs2d_server.c`.

## Lua hook inventory of 1.0.1.8 (2026-10-19)

- Checked against the UTF-16 literals in `cs2d_dedicated.exe` and their code references (objdump),
  plus `docs/cs2d_help_db_complete.json`: the real hooks of this build include `connect`,
  `connect_initplayer`, `disconnect` and `select`. **Correction (2026-10-20):** `shutdown` does
  exist; the original log registers `rp.shutdown` on both `mapchange` and `shutdown`. There is no
  literal (and no reference) for `frame`, `http`, `filetransfer`, `post_endround`, `oncollision`,
  `oncollisionhit` or `onhit`. `addhook` on those names must therefore fail like the original
  ("hook does not exist").
- Documented argument lists: `connect_initplayer(id)`, `connect(id)`, `disconnect(id, reason id)`;
  execution order is `connect_attempt` → `connect_initplayer` → `connect` → `join`, and `disconnect`
  runs after the player is removed (so `player(id)` no longer works inside it).

## P1 rules, admin limits and spawn commands (2026-10-19)

- **Server-to-client spawn formats** (client handlers, `src/cs2d_client.c` / `src/cs2d_dynobj.c`):
  - `[33, type u8, id u16, tx u16, ty u16, clip+1 u16, reserve+1 u16]` (12 bytes): ground item; 0 in
    clip/reserve means "use the type defaults" (FUN_14011c0b0 reads `ushort - 1`).
  - `[49, id u16, typ u8, x u16, y u16, rot u16, mode u8, team u8, player u8]` (13 bytes): dynamic
    object; x/y are pixels, rot is degrees * 182.04445, NPCs are typ 30 with the NPC type in `player`.
  - `[58, player u8, type u8, id u8, x+0x8000 u16, y+0x8000 u16, flydist u16, dir u16]` (12 bytes):
    projectile; player 0 = server.
- **Flood guard `FUN_1400d4c90(ip, 0xfc, n, 60000)`**: per `[252]` sub-op, n = 60 for 0/3/100, 200 for
  1, 30 for 5. Our port counts per IP in a 60 s window and, on excess, ignores the IP for
  `mp_floodprotignoretime` seconds (help.php default 600, max 86400). `mp_floodprot 0` disables it.
- **RCON failures (`mp_maxrconfails`)**: failed `[236]`/`[241]`/`[242]` passwords are counted per IP;
  at the limit the IP's RCON attempts are ignored. A correct password resets the counter.
- **Ping**: the `[229, srv_ts, cli_ts]` / `[249, srv_ts]` replies carry `sv_now() - start_ms`, so the
  RTT is `(sv_now() - start_ms) - ts` (stored in `Tpl +0xd0`, sent in `[247]`).
- **Config**: `sys/server.cfg` is executed line by line through the console parser (as `exec`/`include`),
  so parser-only commands (`mp_building_price`, `alias`, `settile`, ...) work from the config.
- **help.php cvars ported**: `mp_buytime` (float minutes), `mp_roundlimit`, `mp_winlimit`,
  `mp_damagefactor`, `mp_wpndmg <name> <dmg>` (simplified names like `ak47`), `mp_weaponfadeout`,
  `mp_dropgrenades`, `mp_teamkillpenalty`, `mp_tkpunish`, `mp_hostagepenalty`,
  `mp_vulnerablehostages`, `mp_unbuyable`, `mp_unbuildable`, `mp_idlekick`/`mp_idletime`/
  `mp_idleaction`, `mp_pinglimit`, `mp_connectionlimit`, `mp_maxclientsip`, `mp_reservations`,
  `mp_maxrconfails`, `mp_antispeeder` 1..6, `sv_gm`, `sv_lan`, `sv_checkusgnlogin`.
- **Idle (`mp_idlekick` / `mp_idletime` / `mp_idleaction`)**: the timer is `last_move_ms`, a
  `cs2d_sv_now()` (`GetTickCount`) stamp. It must be set when `[252,1]` creates the player instance,
  before the team choice: left at 0 it means "idle since Windows boot", so `sv_second` kicked every
  fresh join on the next one-second tick (regression fixed 2026-10-19, `test_dedicated_idle_join`).
  `cs2d_sv_spawn_at`, `cs2d_sv_set_pos` and accepted movement reset it. The original counts from the
  join: `sys/logs/11-Oct-2026_09-09-30.txt` has the join at 09:09:33 and the idle kick at 09:10:34
  (61 s with the default `mp_idletime` 60).

## P2 rules and world commands (2026-10-19)

- **Armor (`FUN_1401658c0`, decompiled in `src/cs2d_weapons.c:610-633`)**: with `0 < armor < 201`,
  `absorbed = (int)(dmg * mp_kevlar)` and `new_armor = armor - dmg` (armor loses the full raw damage
  while it lasts; when it would go below 0, `absorbed = (int)(armor * mp_kevlar)` and armor = 0).
  `mp_kevlar` default is **0.35** (`DAT_1405a6508`; help.php text says 0.3). Armors 201..205 do not
  lose points and scale HP damage by 0.75/0.50/0.25/0.50/0.05.
- **mp_deathdrop**: 0 all, 1 primary+secondary+defuse+NV, 2 primary+secondary+defuse, 3 primary+
  secondary, 4 nothing. Drops use the normal `[24, id, type, clip, reserve, sub, item_id, tx, ty,
  held_weapon]` message; the C4 is never dropped.
- **mp_killbuildings** on leave: 0 all, 1 all except walls (3/4/5), 2 dispensers/turrets/supplies
  (7/8/9/11/12/15) only, 3 keep all; removed objects broadcast `[35]`.
- **mp_killteambuildings**: 0 teammate buildings unkillable, 1 killable, 2 killable + score/money
  penalty. `mp_killbuildingmoney` gates the `def->kill_reward` money.
- **Votes (`[43]`)**: `mp_kickpercent` / `mp_mapvoteratio` are fractions of the whole player base
  (`ceil(ratio * players)`); a reached kick vote tempbans the IP for `mp_tempbantime` and kicks.
- **Temporary bans**: `cs2d_sv_ban_t.expire_ms` (0 = permanent); `mp_tempbantime` is clamped to
  1..1440 minutes.
- **World commands**: `damageobject <id> <dmg> [player]`, `killobject <id>`,
  `triggerposition <tx> <ty>` (toggles the entity state and broadcasts `[31]`).

## P3: HTTP, server sounds and FX (2026-10-19)

- **reqhttp(url, path, mode)** (`FUN_140...` handler, `reqhttp` Lua command): HTTP GET to port 80;
  response goes to `httpdata(requestid, state, data)` (state 1 = 2xx data, 0 = failure). Our port
  connects synchronously and polls the socket from `cs2d_sv_lua_frame` (5 s timeout, 256 KiB cap).
- **Server sounds** (`cs2d_dedicated.exe` parser 0x140144c28 / 0x140144d28 / 0x140144e58 /
  0x140144fa0):
  - `sv_sound <file>`: `[240, 0, 10, len u16, UTF-16LE "path[|volume]"]` to all (FUN_140153900).
  - `sv_sound2 <player> <file>`: `[65, 0x8000, 0x8000, len u8, path]` to that player (FUN_140107560).
  - `sv_soundpos <file> <x> <y> [player]`: `[65, x+0x8000, y+0x8000, len, path]` (FUN_140107560).
  - `sv_stopsound <file> [player]`: `[240, 0, 11, len, UTF-16LE path]` (mode 11 = stopServer).
- **[48] FX** (`FUN_14014e350` -> `FUN_14014cf50`): `[48, type, x+0x8000, y+0x8000, p1 u16, p2 u16,
  (type>=100) r,g,b, (type>=200) len u8 + path]`. Types: 1 explosion, 2 screen shake, 3 object fx,
  4 flashposition, 5 flashplayer, 6 fire, 7 smoke (gray), 8 blood, 100 flare (Tp 14 + rgb), 101 colorsmoke, 102 sparkles.
  **Flash / shake commands (REA ded, 2026-10-24):** `flashplayer <id> <int>` = `FUN_14011daf0`: int clamped 0..100, reliable `[48,5,id+0x8000,0x8000,int,0]`
  to every joined client (client tests id, 0 = all). `flashposition <x> <y> <int>` = `FUN_14011e010` -> `FUN_140102be0(4,x,y,clamp(int),0)`. `shake <player> <power>` =
  `FUN_14011d2c0`: only for an existing player, UNRELIABLE `[48,2,0x8000,0x8000,player,power]` to that player alone, power unclamped (arg 2 is cut to 9 chars before Int).
  Client (`FUN_14014cf50` cases 2/4/5): shake: p1 == local id -> p2==0 clears `g_screenshake`, else raises it to min(p2,1000); flashplayer: flash += p1*0.01 (cap 1.3);
  flashposition: local alive, |dx|<640, |dy|<480, line clear (`FUN_14011c3a0`), flash += p1*0.01 inside 325x245 else p1*0.003 (cap 1.3).
  Console `effect <name> x y p1 p2 r g b` = `FUN_14011e170` -> `FUN_140102be0` (REA, ded): name -> type (fire 6, smoke 7, blood 8,
  flare 100, colorsmoke 101, sparkles 102); p1 clamped before sending (100; blood 35 and floor 0; sparkles 50), p2 sent as is,
  r,g,b only for type >= 100. Client applies the same function (6/7: Tp 14 / Tp 3 with `rand(0,p2)` radius, 7 gray rand 30..120).
- **Grenade re-buy** (`mp_grenaderebuy`): a thrown or dropped grenade sets a per-life flag; with the
  cvar at 0 the same grenade cannot be bought again until the next spawn.
- **Map goals** (`mp_mapgoalscore`): gates the +2 score for rescuing a hostage.

## Dedicated server command line flags (`src/main_dedicated.c`)

- Original parameters: `-offline` / `-off`, `-map <map>`, `-port <port>`, `-lua <script>`, `-pw` / `-password <pass>`, `-name <name>`, `-rcon <pass>`, `-maxplayers <n>`.
- Our implementation supports both single and double dashes (`--map`, `-map`, `-m`), and equals/colon syntax (`--map=de_dust`, `--port:36963`).
- `cs2d_sv_sanitize_map_name`: automatically trims quotes, whitespace, directory paths (`maps/`, `maps\`, absolute paths), and case-insensitive `.map` extensions so that `--map fun_roleplay.map` or `--map maps/fun_roleplay` resolves cleanly to `fun_roleplay`.

## Network, Tudp Transport & Protocol Parity (2026-10-17)

- **Tudp Reliable Resend Queue**: BlitzMax `Fudp_update` (`0x1402bca30` / `0x1403c90c0`) queues outbound reliable datagrams (`even` seq numbers) in `con.out` and retransmits them every 500 ms (`CS2D_CLIENT_RESEND_MS` / `SV_RESEND_MS`) until acknowledged by a control triple kind 1 (`[01, seq u16]`, `Tudp_con.clearoutbuffer`). Both dedicated and client implement this retry mechanism and process ACKs.
- **Initial Handshake Control Triple (`03 32 00`)**: The original `CS2D.exe` client prefixes its first datagram with the Tudp control triple `03 32 00` (kind 3 session reset, u16 value 0x0032 = 50 netrate ticks), producing the exact 7 wire bytes: `02 00 03 32 00 fc 00`. Handled in `cs2d_client_connect` and parsed in `cs2d_server.c`.
- **Leave / Disconnect / Kick (`[253, id, reason]` / `FUN_1401502a0`, `0x140150ae0`)**:
  - `cmp edi, 0xf` (15): if `reason < 16`, sends 3 bytes `[253, id, reason]`.
  - if `reason >= 16`, appends u8-length string `[253, id, reason, str_len, str]` (the original writes
    the string unconditionally at `0x140150b12`, empty included).
  - The per-player loop at `0x1401509a8` walks the players that still have a connection and
    `joined == 1`. The client `FUN_1402eee40` -> `FUN_1401c7500` only shows
    "Disconnect Reason: ..." when the `[253]` id is the local player.
  - Our `sv_remove_player` broadcasts before clearing `used`, so the leaver/kicked client also gets
    the packet. The caller **must flush** the connection before deactivating it:
    - a client that quits sends `[253,0]` and waits for `[253, id, 0]` to finish the disconnect
      (`FUN_1402eee40` -> `FUN_1401c7500`); without the flushed reply it stays in
      "Disconnecting / Please wait";
    - a kicked client needs `[253, id, 16, reason]` to show the kick message.
    `sv_conn_reset` (duplicate hello from the same address) resets the buffer, so that path does
    not send the reply.
  - Client parser in `src/cs2d_client.c` consumes the optional reason string on `reason >= 16`, preventing subsequent opcodes from desyncing.
- **`mp_startmoney` Default and Clamp**:
  - In code (`cs2d_sv_settings_default`): default fallback is 800 (CS default).
  - Shipped `sys/server.cfg`: sets `mp_startmoney 16000`.
  - REA `0x14014c217`: `mov edx, 0x3e80` clamps console input to max 16000 (`0x3e80`).
- **Ground Item Fadeout (`[40, id u16]`, `0x140a14d94`)**:
  - Dropped items (`dropped == true`) age 1 second per second on the server.
  - After 15 seconds (`drop_time_ms >= 15000`), the server deactivates the item and broadcasts reliable `[40, id u16]`. Map spawned items (`dropped == false`) never expire.

## Gamemode Round Restart, Shotgun Reload Sync & Env_Item (2026-10-17)

- **Gamemode Change Auto-Restart**:
  - When `sv_gamemode` or `gamemode` is modified via console, RCON or cvar, if the new gamemode differs from the current mode, the dedicated server immediately restarts the round (`cs2d_sv_round_end(sv, 5, 0)` + `cs2d_sv_round_start(sv, 5)`), resetting scores/spawns and broadcasting `[22, 5]` to all connected clients.
- **Shotgun Reload Lag Compensation (Mode 101 / `0x65`)**:
  - For weapons with shell-by-shell reload (`M3`, `XM1014`), if the client's shell animation expires before receiving `[16, id, 4]`, `CS2D.exe` fires fallback opcode `[16, 101]`.
  - When receiving mode 101, the server increments clip and decrements reserve. Crucially, the server MUST re-arm the reload timer `pl->reload_end_ms = now + (reload_ticks * 5)` so that `cs2d_sv_step_reloads` does not immediately fire `mode 4` on the very next frame, which would double-load shells and make reload 2x faster.
- **Env_Item Map Entities (Entity ID 21)**:
  - `Env_Item` properties: `ints[0]` = weapon/item ID (1..89); `ints[1]` = spawn mode (0 = one time, 1 = instant/infinite, 2 = every X sec, 3 = on trigger only); `ints[2]` = delay in seconds (for mode 2).
  - On round start (`cs2d_sv_round_start`), ground items from the previous round are cleared and active `Env_Item` entities (modes 0, 1, 2) are spawned via `sv_spawn_item`, placing them in `sv->items`.
  - During the round (`cs2d_sv_step_items`), mode 1 entities immediately respawn if the tile is clear, and mode 2 entities respawn after `delay * 1000` ms. Mode 3 entities spawn when triggered (`cs2d_sv_trigger_env_items`).
  - When a player connects, Section 3 of the join snapshot (`[252, 7, 3, count, ...]`) sends all active ground items in `sv->items` to the client, ensuring all `Env_Item` weapons appear immediately on the ground when entering the server.

## Deathmatch, Team Deathmatch, mp_randomspawn, mp_timelimit & mapcycle (2026-10-17)

- **Random Spawns (`mp_randomspawn 1`) in DM (`mode 1`) and TDM (`mode 2`)**:
  - Addresses: `0x140160da0` - `0x140160dae` checks `(gamemode - 1) <= 1` (modes 1 and 2). If `0x1407dfb98` (`mp_randomspawn`) is 1, jumps to `0x140162313`, calling `0x1400d5650`.
  - `0x1400d5650`: performs up to 100 random probes on map tiles `(rand() % width, rand() % height)`. A tile is valid if it is non-solid (`!cs2d_map_is_solid`), has no deadly/obstacle property, and has no active player or solid building within 32 px. Player spawns at tile center: `x = tx * 32 + 16, y = ty * 32 + 16`.
- **Deathmatch Spawn Fallback**:
  - In `sv_gamemode 1` (Deathmatch), if a map does not contain spawn entities matching the player's team (e.g. a CT player on a map with only `Info_T` spawns), `sv_pick_spawn` falls back to any available spawn point (`Info_T` or `Info_CT`), allowing players of either team to spawn instead of failing or getting stuck.
- **Map Timelimit (`mp_timelimit`) & Mapcycle (`sys/mapcycle.cfg`)**:
  - Address: `0x1401a3058` reads `mp_timelimit` in minutes (`DAT_1405a6658`), multiplies by 60,000 ms (`imul eax, eax, 0xea60`).
  - `0x1401a3082`: compares `now - sv->map_start_ms >= limit_ms`.
  - `0x1401a3099`: if in a respawn mode (`respawn == 1`, e.g. Deathmatch / TDM / Construction) or if connected player count <= 1, branches to `0x1401a30c5`, prints `sv_timelimit reached - changing map...`, queries next map via `0x14013b430` (`cs2d_sv_next_map`), and calls `0x1401104f0` (`cs2d_sv_change_map`).
  - `cs2d_sv_next_map` scans `sv->mapcycle` (populated in file order from `sys/mapcycle.cfg`) for the current map, returning the subsequent entry with wrap-around to index 0.
- **Lua Extensions**:
  - `game("maptimeleft")`: returns seconds remaining until `mp_timelimit` expires as a float. If `mp_timelimit <= 0`, returns `1000000.0` (matching original BlitzMax constant).
  - `game("nextmap")`: returns the name of the next map in the rotation cycle.

## Client Message Dispatch: Spray [28], Vote [43], Leaderboard [230] & Opcodes < 7 (2026-10-17)

- **In-Game Spray (Opcode 28, `FUN_1401bcd60`)**:
  - Client sends `[28, sub u8, ...]`:
    - `sub == 0`: in-game spray `[28, 0, x u16, y u16, mode u8]` (6 payload bytes). Server calls `cs2d_sv_lua_hook_spray` (`FUN_14015a890`) and relays reliable `[28, player_id, 0, x u16, y u16, mode u8]` to all other connected players.
    - `sub == 2`: logo trigger / request `[28, 2]` (1 byte).
    - Prevents packet desync where reading only 2 bytes left the remaining 4 bytes in the UDP stream to be misinterpreted as bogus opcodes (such as opcode 0).
- **Vote (Opcode 43, `FUN_1401d0050`)**:
  - Client sends `[43, type u8, value u16]` (3 payload bytes):
    - `type == 1`: votekick (`value` = target player ID).
    - `type == 2`: votemap (`value` = map cycle index).
  - Updates `pl->votekick` / `pl->votemap`, triggers Lua hook `vote` (`cs2d_sv_lua_hook_vote`), and broadcasts reliable `[43, voter, type, value]` to all connected players.
- **Leaderboard (Opcode 230, `FUN_1401d8360`)**:
  - Client sends `[230, sub u8, ...]`:
    - `sub == 0`: leaderboard timestamp request (1 byte). Server replies with reliable `[230, 0, timestamp i32]` (0 = empty / not updated).
    - `sub == 1`: rank row request `[230, 1, rank u16]` (3 payload bytes). If row is not present/empty in dedicated, returns 0 without replying (matching `0x1401d8650`).
- **Opcodes < 7 Guard (`FUN_1401e3cf0`)**:
  - The dedicated network dispatcher jump table strictly indexes opcodes 7..253 (`opcode - 7`).
  - When encountering `op < 7` (e.g. padding zeros `0x00`, incomplete control triples such as lone `0x03` at the end of a datagram), `FUN_1401e3cf0` branches to `LAB_1401e3e80` (`iVar7 = 2`), cleanly dropping the rest of the datagram without logging unhandled client opcode warnings.

## Client-World/Entity Interactions: [31], [50], [56], [57], [60], [68], [35-37], [65], [26,100] (2026-10-17)

- **Entity Event (Opcode 31, `FUN_1401cead0` -> `FUN_14016e0a0`)**:
  - Payload: `[31, x u16, y u16, mode u8]` (5 bytes).
  - Triggers entity at `(x, y)` if `e->trigger[0]` is present, invoking `cs2d_sv_trigger_env_items` and Lua `trigger` hook. Also invokes Lua `triggerentity` hook.
  - Relays 6-byte reliable `[31, x u16, y u16, mode u8]` to all other connected players (`FUN_14010ddb0`).
- **Portal Fire (Opcode 50, `FUN_1401d11a0` -> `FUN_140177230`)**:
  - Payload: `[50, color u8, tx i32, ty i32]` (9 bytes).
  - Updates player portal position `pl->portal_x[color & 1] = tx`, `pl->portal_y[color & 1] = ty`, `pl->portal_active[color & 1] = true`.
  - Broadcasts 11-byte reliable `[50, player_id u8, color u8, tx i32, ty i32]` to all joined players.
- **Weapon Impulse (Opcode 56, `FUN_1401d26a0` -> `FUN_1401694e0`)**:
  - Payload: `[56, value u8]` (1 byte).
  - Broadcasts 3-byte reliable `[56, player_id u8, value u8]` to all other connected players.
- **Flashlight (Opcode 57, `FUN_1401d28b0` -> `FUN_140106350`)**:
  - Payload: `[57, state u8]` (1 byte).
  - Checked against `sv->set.mp_flashlight != 0` (`0x1401063b7`).
  - Updates `pl->flashlight = state` (`0x14010659a`), broadcasts 3-byte reliable `[57, player_id u8, state u8]`, and fires Lua hook `flashlight`.
- **Spectator Flashlight (Opcode 60, `FUN_1401d2d40` -> `FUN_1401b2bf0`)**:
  - Payload: `[60, state u8]` (1 byte).
  - Updates `pl->specflash = state` and broadcasts 3-byte reliable `[60, player_id u8, state u8]` to all joined players.
- **Fire Direction (Opcode 68, `FUN_1401d3840` -> `FUN_140192080`)**:
  - Payload: `[68, dir u16]` (2 bytes).
  - Converts direction `pl->rot = (float)dir / 182.04445f` and sets `pl->rot_flag = 1`.
  - Relays unreliable `[70, player_id u8, dir u16]` to dead players currently spectating this player.
- **Server-to-Client Drops: [35], [36], [37], [65]**:
  - `FUN_1401cf350` (35 dynobj remove), `FUN_1401cf4b0` (36 dynobj mode), `FUN_1401cf630` (37 dynobj hp), `FUN_1401d3430` (65 positionalsound).
  - In dedicated server mode (`DAT_1407dfe48 != 0`), all four functions immediately return 3.
  - In `FUN_1401e3cf0`, any handler returning `> 0` breaks out of the datagram opcode loop, safely dropping the remainder of the packet.
- **Use Button (Opcode 26 mode 100, `FUN_140138a40`)**:
  - The button must be a `Trigger_Use` (93) at `(x,y)`, in **state 0**, within 45px Euclidean
    distance of its tile center (`(x * 32 + 16, y * 32 + 16)`) and pass its "Triggered by" filter
    (`int_settings[2] == 0 || int_settings[2] == pl->team`).
  - Re-Use Delay = `Te.ints[3]` ms (the map probe on fun_roleplay shows `ints[3] = 0` for most
    buttons; the entity editor field is "Re-Use Delay (ms)"). The original keeps a `Te.timer`
    (`Te+0x58`) and rejects the press while `now - timer.start < ints[3]`; the timer is restarted on
    every accepted press. Our equivalent is `cs2d_server_t.entity_reuse_ms[]` (reset at round
    start / map load).
  - A press **does not toggle the button's state**. The original calls `Te.triggerentities(1)`
    (vtable `Te` +0xf0), which runs the names in the button's `trigger` field (`usebutton` first,
    then the per-name Lua `trigger` / `triggerentity` hooks), and relays `[26]` to the clients.
    Toggling the button itself locks it forever afterwards — fun_roleplay's `rp_ticket` button at
    (275,154) was only usable once this way.
  - The client runs the same check in `FUN_14014f820`, including the `ints[3]` delay
    (`cs2d_ents_tick_use_cooldowns` / `use_cooldown` in our client). Regression:
    `test_dedicated_trigger_use`.

## Client -> Lua Communication: [53], [227/228], [232], [80..87], [225] (2026-10-17)

- **reqcld & clientdata (Opcode 53, `FUN_140129a50`, `FUN_1401780d0`, `FUN_1401d16f0`)**:
  - Server command `reqcld(id, mode [, param])`: sends reliable packet `[53, mode u8 (, param string if mode 4 or 5)]`.
  - Client response `[53, mode u8, ...]`:
    - `mode 0`: cursor on screen `[53, 0, x u16, y u16]`
    - `mode 1`: map scrolling offset `[53, 1, (sx + 0x8000) u16, (sy + 0x8000) u16]`
    - `mode 2`: absolute cursor on map `[53, 2, (mx + 0x8000) u16, (my + 0x8000) u16]`
    - `mode 3`: light engine enabled `[53, 3, enabled u8]`
    - `mode 4`: file loaded `[53, 4, loaded u8, checksum_sha256 rstr]`
    - `mode 5`: mod info `[53, 5, sub u8, val u16, mod_info rstr]`
    - `mode 6`: render3d enabled `[53, 6, enabled u8]`
  - Dispatches Lua hook `clientdata(id, mode, data1, data2)` where `data2` is string for modes 4 and 5, integer for others.
- **Key Press & Release (Opcodes 227 & 228, `FUN_1401bb510`, `FUN_1401bb620`)**:
  - Client sends `[227/228, key u16]` with a **Win32 VK code** (`Q = 0x51`, `E = 0x45`,
    `leftshift = 0xA0`, `space = 0x20`, `mouse1 = 1`, `mouse2 = 2`, `mwheelup = 0x122`,
    `f1 = 0x70`, `kp_5 = 0x65`; captured from `cs2d_dedicated.exe` with `tools/agent/lua/bind_probe.lua`).
  - The client only reports keys listed in the join snapshot bind list, so `addbind` must be
    advertised or the `key` hook never fires.
  - `[252,6]` tail: `b+22` is the bind count **u8**, followed by `n * u16` VK codes (captured:
    6 binds -> `06 51 00 45 00 a0 00 20 00 02 00 01 00`). Our `cs2d_sv_build_join_settings` writes
    the real list (`cs2d_bind_key_code`).
  - When `addbind` / `removebind` / `removeallbinds` changes the list, every joined player gets a
    reliable `[224, count u16, count * key u16]` (`lua_bind.SendBindListToClients`,
    `CS2D.exe 0x14005be90`; our `cs2d_sv_send_lua_binds`).
  - Server maps key code to canonical name via `sv_resolve_key_name`: checks custom `addbind` keys by
    VK code first (`cs2d_bind_key_code`), then `cs2d_bind_vk_name` (`f1`/`kp_*`; the client-facing
    `cs2d_bind_key_name` cannot represent 0x60..0x7B because they overlap ASCII), then decimal string.
  - Our client now stores the `[252,6]` / `[224]` list (`lua_binds`), reports `[227]/[228]` only for
    announced VK codes from `cs2d_client_handle_key` / `_mouse` / `_mouse_wheel`, and releases held
    binds on `SDL_EVENT_WINDOW_FOCUS_LOST`. Verified end to end with `bind_probe.lua`: `Q`, `mouse1`
    and `mwheelup` reach the server `key` hook.
- **Client Setting (Opcode 232, `FUN_1401bb730`)**:
  - `sub 1`: screen resolution `[232, 1, width u16, height u16]` -> updates `pl->screen_w`, `pl->screen_h`.
  - `sub 2`: aspect ratio 4:3 `[232, 2, s43 u8]` -> updates `pl->screen_aspect`.
  - `sub 3`: windowed mode `[232, 3, win u8]` -> updates `pl->screen_windowed`.
  - Fires Lua hook `clientsetting(id, setting, val1, val2)`.
  - Lua `player(id, "screenw")`, `"screenh"`, `"widescreen"` (0/1, inverted 4:3 setting), `"windowed"` read player fields.
- **Player Physics Rotation (Opcode 80, `FUN_1401d4910`)**:
  - `[80, flags u8, ...]`: if `flags & 0x40` reads pos bytes; if `flags & 0x80` reads `dir u16` and sets `pl->rot = (float)dir * 0.005493164f` (360.0f / 65536.0f).
- **Physics Updates [81..87] Drop (`FUN_1401e3cf0`)**:
  - Functions `0x1401d4dc0` through `0x1401d64b0` return 4 in server mode, terminating packet parsing.
- **Voice Chat (Opcode 225, `FUN_1401d68b0`)**:
  - `[225, target u8, sub u8, ...]`: sub 0 (ping), sub 1 (`vlen u16 + data`), sub 2.
  - If `sv_voicechat != 0`, relays sub 1 audio packet to target/team. Fires Lua hook `voice(id)`.
- **Server Browser Query: Ping Echo [FA] & Info Reply [FB] (2026-10-17, `FUN_1400d4ed0`, `FUN_1401da400`)**:
  - Intercepted connectionless in `sv_handle_datagram` before `sv_find_conn` and supported in-session via `sv_recv_game`:
  - **Ping [FA] (`FUN_1400d4ed0`)**: request `[01 00] [FA] [millisecs i32]`; server echoes the exact 7 bytes `[01 00] [FA] [millisecs i32]`.
  - **Info [FB] (`FUN_1401da400`)**: request `[01 00] [03] [tick u16] [FB] [mode u8] [version u16]` (mode 1 direct, mode 2 LAN broadcast to `255.255.255.255:36963`).
  - Reply wire format: `[01 00] [FB] [mode] [flags] [name u8 str] [map u8 str] [players u8] [max_players u8] ([gamemode u8] if flags bit 5) [bots u8] [flags2 u8]`.
  - Flags: bit 0 password, 1 USGN only, 2 fog of war, 3 friendly fire, 4 same version (1018), 5 gamemode present (if != 0), 6 Lua scripts active, 7 force light.
  - Flags2: bit 0 dynamic recoil, 1 off-screen damage, 2 transfer files active.
  - Modes 3 (ack), 4 (player names: `[01 00] [FB] [4] [n] [n x name str]`), 5 (player details: `[01 00] [FB] [5] [n] [n x {id u8, name str, team u8, score i32, deaths i32}]`).
  - Matches the captured 36-byte offline reply `fb 01 10 0b "CS2D Server" 0e "prison_funst3r" 00 0c 00 02` byte for byte.

## Dedicated Server: HUD Texts [234], Tweens [54] & Commands (2026-10-18)

- **HUD Text Creation & Update (`FUN_14011c0d0`, Opcode 234 `0xea`)**:
  - `hudtxt(id [, text, x, y, align, valign, size])` (all players) & `hudtxt2(player, id [, text, x, y, align, valign, size])` (single player).
  - Validation: `id < 0 || id >= 200` prints `©255000000ERROR: hudtxt(2) - text id out of bounds (0-199 expected)!`.
  - Max text length clamped to 400 UTF-16 code units (`0x190`).
  - Default font size is 13 (`0xd`) if omitted or <= 0.
  - Wire format (sent **reliable = true**):
    `[234, id u8, chars u16, chars * u16, (x + 0x4000) u16, (y + 0x4000) u16, align u8, valign u8, size u8]`.
- **HUD Text Clear (`FUN_14011c5e0`)**:
  - `hudtxtclear([player])`: clears all HUD texts for player (or broadcast if player == 0) by sending reliable `[234, 255]`.
  - Clearing a single ID sends `hudtxt(id, "")` with blank text.
- **HUD Text Tweens (Opcode 54 `0x36`, `FUN_1401a8c00`)**:
  - Interpolation target ID: HUD text tweens use target `(200 - id)` (u16) to distinguish from dynamic image tweens.
  - Sent **unreliable = false** (`plVar[0x19]->send(1, 0)`).
  - `hudtxtalphafade(player, id, duration, alpha)` (`FUN_14011cc20`):
    - Mode 4: `[54, 4, (200 - id) u16, duration i32, (uint8_t)(alpha * 255.0f) u8]`.
    - Error on out-of-bounds: `©255000000ERROR: hudtxtalphafade - text id out of bounds (0-199 expected)!`.
  - `hudtxtcolorfade(player, id, duration, r, g, b)` (`FUN_14011c800`):
    - Mode 5: `[54, 5, (200 - id) u16, duration i32, r u8, g u8, b u8]`.
    - Error on out-of-bounds: `©255000000ERROR: hudtxtcolorfade - text id out of bounds (0-199 expected)!`.
  - `hudtxtmove(player, id, duration, x, y)` (`FUN_14011cea0`):
    - Mode 0: `[54, 0, (200 - id) u16, duration i32, (x + 32768) u16, (y + 32768) u16]`.
    - Error on out-of-bounds: `©255000000ERROR: hudtxtmove - text id out of bounds (0-199 expected)!`.
- **Command Dispatch Surfaces**:
  - Registered as Lua global C functions: `hudtxt`, `hudtxt2`, `hudtxtclear`, `hudtxtalphafade`, `hudtxtcolorfade`, `hudtxtmove`.
  - Hooked into `parse(...)` and server console / CLI (`cs2d_sv_exec`): parses quoted strings, coordinate biasing, align/valign/size arguments, and shorthand clearing.

## Dedicated Server: settile [62], Snapshot Section 9 [252,7,9] & Lua/Console (2026-10-18)

- **`cs2d_sv_settile` (`FUN_14011ed00`)**:
  - Updates server map tiles, modifiers, and tile_data. Recalculates `map->base_collision` and `map->collision`.
  - Broadcasts reliable opcode `[62, x u16, y u16, tile u8, rot u8, r u8, g u8, b u8, overlay u8, blend u8]` (12 bytes) to all connected clients with `joined == true`.
- **Join Snapshot Section 9 (`FUN_1401e1130` / `0x1401e1555`)**:
  - Sent in chunks of up to 70 entries (`0x46`): `[252, 7, 9, chunk_count]` followed by 11 bytes per custom tile (`{x u16, y u16, tile, rot&3, r, g, b, overlay, blend}`).
  - Closes with a terminator packet `[252, 7, 9, 0]`.
- **Server CLI / Console / RCON (`FUN_14011f210`)**:
  - `settile <x> <y> <tile> [rot] [r] [g] [b] [overlay] [blenddir] [blendtype]`.
- **Lua Subsystem Integration**:
  - Lua global function `settile(...)` calls `cs2d_sv_settile`.
  - `tile(x, y, prop)` (`FUN_1400c0df0`) provides queries for `"frame"`, `"originalframe"`, `"hascustomframe"`, `"walkable"`, `"rot"`, `"blend"`, `"color"`, `"brightness"`, `"height"`, `"entity"`.

## Dedicated Server: Lua Items & Weapons API (2026-10-18)

Verified against `cs2d_dedicated.exe` reverse engineering:
- **`itemtype(type, prop)` (`FUN_14009b4c0`)**:
  - Validates `0 <= type < CS2D_MAX_WEAPONS` and definition existence (`def != NULL && def->name != NULL`). If out-of-bounds, undefined weapon (such as custom item IDs like 80, 202, 290) or unknown property, returns boolean `false`.
  - `"name"`: returns string weapon name (`def->name`). If weapon is not a valid CS2D weapon, returns `false` (allowing scripts to check `if itemtype(id, "name") == false then -- Not cs2d item`).
  - Numeric properties: `"dmg"`, `"dmg_z1"`, `"dmg_z2"`, `"rate"`, `"reload"`, `"ammo"`, `"ammoin"`, `"price"`, `"range"`, `"dispersion"`, `"slot"` (returns `def->cs2d_slot`), `"recoil"` (returns `def->recoil_accuracy`).
- **`item(id, prop)` (`FUN_140099a70`)**:
  - `id == 0`: if `prop == "table"` returns 1-indexed Lua array containing all active ground items (`player == 0`); any other prop returns `false`.
  - `id > 0`: searches active ground item by entity ID. If missing or invalid prop, returns `false` (`item(id, "exists")` returns `false`).
  - Supported properties:
    - `"exists"`: returns `true`.
    - `"name"`: returns weapon name string (or `""`).
    - `"type"`: returns weapon type integer.
    - `"player"`: returns `0` for ground items (or holder player ID).
    - `"ammo"`: returns reserve ammo integer.
    - `"ammoin"`: returns clip ammo integer.
    - `"mode"`: returns sub-mode integer.
    - `"x"`, `"y"`: **returns integer coordinates in TILES** (`it->tile_x`, `it->tile_y`), not pixels.
    - `"dropped"`: returns boolean dropped flag.
    - `"droptimer"`: returns elapsed drop time in **whole seconds** (`(now_ms - it->drop_time_ms) / 1000`).
    - `"physicsid"`: returns associated physics object ID or `0`.
- **`playerammo(id, type)` (`FUN_14004caf0`)**:
  - Validates player existence and `0 <= type < CS2D_MAX_WEAPONS`.
  - Checks if player holds the weapon.
  - If held, returns **two values**: `clip, reserve` (`pl->clip[type], pl->reserve[type]`).
  - If player does not exist or does not hold the weapon, returns boolean `false`.
- **`playerweapons(id)` (`FUN_14004bd20`)**:
  - Validates player ID. If player does not exist, returns empty table `{}`.
  - Iterates player weapons (`pl->items`) and returns a 1-indexed table with weapon type IDs.
- **`closeitems(id [, range])` (`FUN_14004bf30`)**:
  - Returns table of ground item IDs within `range` tiles of player `id`. Default `range` is 64 tiles.
  - Uses player tile position `(int)(pl->x / 32.0f), (int)(pl->y / 32.0f)`.
  - Distance check: Chebyshev distance in tiles `dx = abs(it->tile_x - ptx)`, `dy = abs(it->tile_y - pty)`, `dx <= range && dy <= range`.
  - Proximity bucket ordering: uses `min(range, 5) + 1` buckets indexed by `min(max(dx, dy), 5)`.
  - Populates table iterating buckets `0..max_bucket`, giving strict nearest-first ordering for tiles 0..4.
  - Non-existent player returns empty table `{}`.

## Dedicated Server: Lua `object()` / `player()` screen values and the shared Tdo space (2026-10-20)

Verified against `CS2D.exe` (the shared BlitzMax game module; `cs2dlua_object` 0x1400dfc40,
`cs2dlua_closeobjects` 0x140059330, round start `FUN_1402520b0` / `FUN_140190e10`) and the
dedicated's `[232]` handler.

- **`object(id, prop)`**:
  - `id == 0`: only `"table"` (1-indexed ids of every active Tdo); any other value -> `false`.
  - Properties: `exists`, `typename` (type table name), `type`, `health`, `mode`, `team`, `player`,
    `x`, `y`, `rot`, `tilex`, `tiley`, `countdown`, `rootrot` (`rrot`), `idle` (number, `Tdo.idle`),
    `rotvar`, `target`, `upgrade`, `entity` (boolean, `Tdo.entity[0]`), `entityx`/`entityy`
    (`Tdo.entity[1]`/`[2]`, entity tile), `alpha`.
  - `tilex` / `tiley` are `(int)((x + 1) * 0.03125)`, i.e. +1 px compared with `objectat`.
  - Missing object, unknown property -> boolean `false`.
- **`closeobjects(x, y, radius [, type])`**: Euclidean distance from `(x, y)` to the object's own
  `x`/`y` (top-left), no +16 centre offset; list order, optional type filter.
- **`player(id, ...)` screen values** (`cs2dlua_player` 0x1400e9440): `screenw` (`screenResX`),
  `screenh` (`screenResY`), `widescreen` (0/1, inverted `screen4by3`), `windowed` (0/1). The
  dedicated initialises remote players to 850x480 and updates them from `[232,1,w,h]`.
- **Shared Tdo space**: Lua images (typ 40), buildings (1..23) and NPCs (typ 30) live in
  `sv->dynobj`, share `next_free_id`, appear in `object(0,"table")`, `objectat` and
  `closeobjects`. `Tdo.entity` records the spawning map entity: `[0]=1`, `[1]/[2]` = entity tile.
- **Round start** (`FUN_1402520b0`): `Tdo.freeall()` then `FUN_140190e10` rebuilds the map's
  Env_Building (type ints[0], team ints[1], rot ints[2]) **and** Env_NPC entities **in entity
  order**. Player-built objects and Lua images do not survive. Both server and client run the same
  pass, so the Tdo ids stay in sync through `[22]`.
- **Env_NPC (map entity 31)**: typ 30 at `(x*32+16, y*32+16)`, rot `ints[0]`, NPC type `ints[1]`,
  health `ints[2]` (0 = type default), `ints[3] == 1` = do not spawn on start. Default NPC health
  (`Tdo.add` array 0x1409ed7f0, initialised at 0x14031a195): 1 zombie 150, 2 headcrab 30, 3 snark 20,
  4 vortigaunt 200, 5 soldier 100.
- **Server building health**: `spawnobject` sets `max_health[type]` (`Tdo.inibuilding` on the
  dedicated), while the client keeps the percentage in `Tdo.health`.
- **Image tweens are server state** (`cs2dlua_tween_*` 0x14009dbb0.. and `Ttween.update` 0x14027fc50):
  the dedicated creates a `Ttween` in its own list *and* sends `[54]`. `Ttween.update` writes the
  interpolated values into the Tdo (`x`, `y`, `rot`, `alpha`, `color`, `scale`, `frame`), so
  `object(id, ...)` / `imageparam(id, ...)` return the animated values. `tween_move(id, time, x, y
  [, rot])`: without `rot` the mode is 0 (move only, `[54,0,dur,x,y]`); with `rot` it is mode 1
  (move + rotate, `[54,1,dur,x,y,rot]`). `tween_animate(id, speed, mode)` is `[54,8,speed,mode]`
  (the client keeps its current frame; only the animation mode is sent).
  `cs2d_dynobj_read_tween` parses the same packets on the client; mode 1 body size is 10 (not 8) and
  mode 8 body size is 5.
- **`closeplayers` / `hascloseplayers`** (`cs2dlua_closeplayers` 0x140058f40): only existing, alive
  players (`team >= 1`, `health >= 1`); dead players and unused slots are not returned.

## WQ2D roleplay startup: trigger graph, bot reqcld and API types (2026-10-20)

Measured by running the original `cs2d_dedicated.exe` and our `cs2d_server.exe` with the same
`sys/lua/WQ2D` scripts and a Lua probe (autorun) that logged `join`, `clientdata`, `trigger` and
`entity`/`map`/`game` values. The RP map (`fun_roleplay`) is driven by 2.5k+ map entities.

- **Entity trigger graph is server-side.** The dedicated fires the map's triggers itself; the Lua
  `trigger` hook (`trigger: string, source: 0 map / 1 script`) runs **once per comma-separated
  name**, even when no entity matches. `triggerentity(x, y)` runs once per affected entity and may
  return 1 to cancel it. `parse("trigger a,b")` is source 1; entity chains are source 0.
  - `Trigger_Start` (90) fires its list at round start (after the state reset and the Tdo rebuild).
    `Trigger_If` (96) runs its condition and only then fires `trigger`; condition type 0 is the Lua
    chunk `"_cs2d_trigger_if=" + strs[0]` (the map uses `(1) action code` and pure expressions).
    `Trigger_Once` (95) fires only on state 0 -> 1. `Trigger_Delay` (94) sets state 1, waits
    `ints[1]` ms (else `ints[0]` s) and forwards its list **through the Lua trigger hook, always with
    source false** (2026-10-24 probe `tools/agent/lua/trigger_graph_probe.lua` on fun_roleplay: script `trigger light`
    -> `c4_lde false`; `bank_l` -> `bank_li,bank_tr,bank_func false`; the 2026-10-20 "silent" reading was wrong).
    Delays that elapse together fire in **entity order** (per-entity timer `delay_fire_ms[]`, no 64-slot queue). `Trigger_Move` (91)
    and `Trigger_Hit` (92) fire only on walkover/hit, never when their name is triggered.
  - A plain entity (Func_DynWall, Env_Sprite, Gen_Weather, Trigger_Use, ...) only toggles its state.
    Its own `trigger` field is **not** propagated. A Trigger_Use fires its list only when a player
    presses it (opcode 26 mode 100), not when `trigger <name>` toggles it.
  - Every entity with a matching name is affected (duplicate names group sprites). Toggling an
    Env_Building/Env_NPC also creates/removes the linked Tdo object.
- **Bot reqcld:** a bot answers `reqcld(id, mode)` immediately (the original emitted `clientdata`
  for modes 0..6): 0/1/2 -> 0,0; 3 -> 1,0; 4 -> 0, checksum of the file ("" if missing);
  5 -> false,""; 6 -> 0,0. WQ2D's `menu.join` -> `reqcld(uid,4,"gfx/c4/c4.bmp")` depends on this
  (`login.query` is loaded once per bot). `player(bot,"ip")` is **"0.0.0.0"** and
  `player(bot,"ping")` is **0** (the snapshot still sends 0xfffe); the empty IP made WQ2D's
  `geoip_lookup` arithmetic fail in two timers.
- **API values confirmed against the original** (probe): `game("dedicated")` is boolean `true`;
  `game("timeleft")` is the remaining round seconds; `map("xsize"/"ysize")` are the **highest tile
  indices** (fun_roleplay: 361/511 for a 362x512 grid); `map("tilesize")` is 0 in 1.0.1.8;
  `map("botnodes")` counts Info_BotNode, `map("nobuying")`/`map("noweapons")` are 0/1;
  `entity(x,y,"state")` is a **boolean**; `entity(x,y,"str0")` returns the full (256 byte) field.
  `steamstats` returns boolean false without a stats store (the old stub returned a table, which
  made WQ2D compare a table with 30).
- **Hook inventory:** `shutdown` **does exist** in 1.0.1.8; the earlier note that it did not was
  wrong. `rp.shutdown` registers on `mapchange` and `shutdown` in the original log.
- **Map format strings:** entity `name`/`trigger`/`strs[0..9]` are CRLF lines of arbitrary length.
  fun_roleplay's longest trigger is 175 bytes and its longest `strs[0]` is 255; only `strs[0]`
  exceeds 63. `cs2d_entity_t` keeps `trigger[256]` and a dedicated `str0[256]` (the other nine
  strings stay 64) to avoid the 2 GB static-data limit of the test binary.
- **Round start order (2026-10-24, probes with Lua tracebacks on both servers, fun_roleplay):** a script's `parse("sv_gamemode 1")` during
  load only queues the restart: `endround 5` at once, then the first loop's timers / `ms100` / `second` hooks, then `startround 5` (our
  `cs2d_sv_step_round` runs after `cs2d_sv_lua_frame`). Inside the round start the Trigger_Start cascade runs **before** the `startround`
  hook. The original's first hook calls are: WQ2D's load-time timer `timer(1000,"rp.light_switch","false",1)` -> `light true`, the
  cascade (`rain false` ...), then `rp.startround` -> `light true` (the round start reset the Env_Light state in between).
  **Lua timers use the main loop's last frame time as their start** (frozen while scripts load; before the first frame the server start), so
  timers created mid-load are already due in the first loop (`l_timer`). `parse()` during script load runs at once in both servers.
  `rp_data.lua` sets `rp.light=1`, so `rp.daylight` does not trigger `light` on the first tick.
- **Remaining difference (2026-10-24):** the first ~565 hook calls (names, order, source) match; the totals are close (866 vs 862 in 30 s).
  Left: `t2 true` etc. (`timer(3000,"rp.train_cycle")` made at the end of load) fires in the original's first tick but in ours at sec 2
  (our load is faster than the original's, so it is not yet due); a few Lua results depend on the wall clock (`crd2`/`crd3`,
  `h_path`/`h_water`); the original itself varies between runs (851 / 862 hook calls, `state1` 1621 / 1511).
  Recipe: copy `tools/agent/lua/trigger_graph_probe.lua` to `sys/lua/autorun/zz_tg.lua`, run `cs2d_dedicated.exe` / `cs2d_server.exe -offline
  -map fun_roleplay` 30 s each, `grep '^TG '`, diff the names; remove the copy and `git checkout sys/server.cfg`.

## Bot AI and NPC AI (2026-10-20)

The AI is **not native code in the dedicated**: 1.0.1.8 ships it as the Lua script
`bots/Standard AI.lua` + `bots/includes/*.lua` (both are in the repository). The client loads it at
startup (CS2D.exe `_global/140190000.c:2263` checks `FileType("bots/Standard AI.lua")`); the engine
then calls the script's globals for every bot and the script drives the bot through the `ai_*` API.
Our port: `src/cs2d_sv_ai.c` (engine side) + `src/cs2d_sv_lua.c` (bindings). `cs2d_sv_lua_init`
loads `bots/Standard AI.lua` after the server/map scripts.

- **Engine callbacks** (`bots/Standard AI.lua:58..244`): `ai_onspawn(id)` after every spawn,
  `ai_update_living(id)` every frame for living bots, `ai_update_dead(id)` every second for dead
  bots, `ai_hear_radio(source, radio)` and `ai_hear_chat(source, msg, teamonly)` per message. The
  calls order is rotate -> `ai_update_living` (CS2D.exe `_global/1402a0000.c:3490..3560`).
- **`ai_*` semantics** (help.php + `cs2dlua_ai_*` in `docs/dump/cs2d`):
  - `ai_move(id, angle [, walk])` -> 1 moved / 0 blocked; `(2 + weapon bonus) * 1.2` per 60 Hz frame,
    walk halves it (same model as KNOWLEDGE movement.md). `ai_goto(id, tx, ty [, walk])` -> 0 no
    path / 1 reached / 2 still moving; BFS over `g_prop_class == 1` + map collision (original
    `FUN_1401893c0` tries a straight line first, then `FUN_14017ac90`; ours is BFS, throttled 250 ms
    after a failure like `DAT_140ddf29c`).
  - `ai_aim` stores `Tpl.ai_aim_x/y` and the engine turns by `bot_skill` degrees per frame
    (`AI_TURN_MIN 8 + 2 * skill`, calibrated: the original per-skill turn table
    `PTR_bbEmptyArray_1409e9418` is a runtime BlitzMax array); `ai_rotate` sets the angle directly.
  - `ai_findtarget` = closest enemy inside 420x235 (+ skill), line of sight only with `sv_fow 1`
    (`FUN_14018bc30`); `ai_freeline` = 40 px ray (`FUN_14018b520`).
  - `ai_attack`/`ai_iattack`: bots have no client, so the dedicated resolves the hit itself
    (`cs2d_sv_damage`) in addition to the `[7]` effect relay; `iattack` reloads / switches to the
    next usable weapon. Hit test uses the 24x24 player box (ray reach + 14 px).
  - `ai_buy`, `ai_selectweapon`, `ai_reload`, `ai_drop`, `ai_use` (Trigger_Use in front of the bot),
    `ai_respawn` (only `sv_gamemode` 1..4 and after `mp_respawndelay`), `ai_say` / `ai_sayteam`
    (chat modes 1/2), `ai_radio` (`[240]` mode 3, text = id), `ai_spray` (`[28]`), `ai_build` (`[34]`
    rules), `ai_debug` (printed with `debugai 1`).
  - `setentityaistate(x,y,ai)` / `entity(x,y,"aistate")`: per map-entity scratch value, reset to 0 on
    round start (help.php). `entity()` now reads it.
  - `randomentity(type [, ai_state [, int0]])` returns **two values** (tile x, y) or `-100,-100`;
    the earlier table return broke `vai_destx, vai_desty = randomentity(...)` in the AI script.
  - `player(0, prop)` for a non-table prop is `false` (help.php); it used to return an empty table,
    which made `player(0,"health")` a table in `fight.lua`.
- **Bot settings** (`sys/server.cfg`, generic cvars): `bot_prefix`, `bot_count`, `bot_autofill`,
  `bot_keepfreeslots`, `bot_jointeam`, `bot_skill`, `bot_weapons`, `bot_freeze`, `debugai` have
  defaults now; `bot_count` / `bot_autofill` are enforced once per second, `bot_freeze` blocks the
  `ai_*` movement/attack API, `bot_skill`/`bot_weapons` are read by the AI script through `game()`.
- **Flashbang**: bots within 640x480 and with a free line get `ai_flash = Rand(4,6)` seconds
  (CS2D.exe `Tpro.c:3227..3237`), decremented once per second; `player(id,"ai_flash")` returns it.
- **Bot teams:** `bot_add` sets a team (given, else the smaller one) and must broadcast
  `[20, id, team, look]` to every joined client — `[248]` carries no team. Without it a client keeps
  the bot at team 0 (Spectator) while the server already runs its AI, so the bot walks and shoots
  "as a spectator" (bug fixed 2026-10-20).
- **NPCs** (`Tdo` 30, `FUN_140283f20`): movement (`FUN_140283120`) runs first: turn by the type's
  turn table (zombie 5, headcrab 5, snark 4, vortigaunt 20, soldier 7 deg/frame) until within 3 deg
  of `m_rot`, then walk the type's speed (1 / 2.5 / 6 / 7 / 3 px per 60 Hz frame) to `m_x`, `m_y`
  while more than 3 px away (no map test; the client runs the identical motion). While it moves the
  attack timer is reset; only when it has arrived does the **server** (the branch is gated by
  `DAT_140ddf400`, so clients never attack) run it:
  - target picker `FUN_140283760`: closest player with `team > 0` and health > 0 in a 405 x 220 box
    and a clear `FUN_14011c3a0(x1,y1,x2,y2,5.0,0)` line (**5 px** steps: `0x14028394b` pushes `0x40a00000`;
    earlier notes said 32, which skipped thin walls). Only `g_prop_class 0` (collision 1: wall, closed DynWall wall,
    building wall) blocks it; an obstacle does not. A spectator (team 0), a dead player or a player behind a wall is never picked;
  - path step `FUN_140283a10`: probe 45 px ahead, require that tile to be walkable, then choose an
    adjacent walkable tile (free side tile first, else the diagonal), set `m_x/m_y` to its **tile
    centre** (our addition: the waypoint is rejected when the straight segment crosses a blocked tile; from a tile edge the
    45 px probe can hop an obstacle / DynWall tile, and the walk has no map test) and `m_rot` to the **raw degree** angle (`(int)m_rot`; **not** the 182.04445 fixed point
    of the player rotation packets) and send reliable `[36,id,7]`;
  - types 1..3 (`countdown` threshold 11) melee when the player is inside a 32 x 32 box and start a
    cooldown of -50 / -20 / -15 frames; farther away they set a new waypoint at the player's
    position. Damage (`FUN_1401ed310` -> `FUN_1401ec180`): zombie 10, headcrab 2, snark 1;
  - Vortigaunt: `FUN_1401ed310` kind 0 melee 15 when within 32 px; kind 1 starts `+0x6c` (charge)
    when farther, and after 40 idle frames kind 2 fires `Tdo.shoot` (400 px, 40 damage, weapon 0xf9);
  - soldier (threshold 8, no 32 px test): `Tdo.shoot` with weapon 0xf8, 5 damage, 400 px, then **returns**: it
    stands still while the player is in sight (burst every 8 frames). Only when no target is found does it walk to the
    last seen position (`+0x110/+0x114`), like the Vortigaunt (kind 1 charge also returns: it stands ~40 frames, so the
    beam, fired along the frozen `rot`, can be dodged). Our port used to fall through into that walk after every shot
    (always approaching, aim swaying); fixed 2026-10-24. **Added (not in the original):** a target farther than 380 px
    (view box corners reach ~460) is approached with `FUN_140283a10` instead of fired at.
  - **Snark death** (`Tdo.kill` case 0x1e/3, server only): `FUN_1401f1130(x, y, 75, 30, attacker 0, weapon 0xf9, falloff 0)`
    (floats `0x140bd6aa8` / `0x140bd6a98`): players within 75 px take `(75 - d) * 30 / 75`, objects through Tdo.damage;
    `sv_object_kill` does it after removing the snark (no re-entry).
- **NPC damage:** `Tdo.damage` skips the max-health array for typ `0x1e` (NPC), so the building
  table must not clamp NPC health; the NPC's health is its type default (150 / 30 / 20 / 200 / 100).
  A player's shot stops on the NPC's tile (`Tshoot.Shoot` scans the Tdo before the players), and
  `cs2d_sv_shot_at_building` must let the NPC through (the debris bitset only covers buildings).

## NPC / dynamic object Lua hooks (2026-10-20)

Source: `docs/cs2d_help_db_complete.json` (`lua_hooks`, category Object) and the Tdo damage / kill /
attack paths. `help.php` argument lists, in order:

- `objectdamage(id, damage, player)` — before Tdo.damage; returning 1 cancels. NPCs go through
  `cs2d_sv_object_damage` (player shots, explosions, damage commands).
- `objectkill(id, player)` — every Tdo death, NPCs included (`sv_object_kill`).
- `objectupgrade(id, player, progress, total)` — each Wrench step (buildings); returning 1 cancels
  the step. `progress` is 1..total.
- `hit(id, source, weapon, hpdmg, apdmg, rawdmg, object)` — **7 arguments**. `object` is the
  attacking dynamic object: the NPC `Tdo` id for NPC melee/ranged, the turret / mine / gate field /
  barbed wire id for those. `source` stays 0 for an NPC. Use `cs2d_sv_damage_ex(..., object_id)`;
  `cs2d_sv_damage` keeps `object = 0`.
- `die(victim, killer, weapon, x, y, killerobject)` — every death. `killerobject` is the object id
  for an NPC / turret / mine / gate / wire kill (killer is 0 for an NPC).
- `kill(killer, victim, weapon, x, y, killerobject, assistant)` — **only when a player caused the
  death** (killer id >= 1, directly or through a building). An NPC kill fires `die` only, with its id
  in killerobject. `assistant` is the assist id (our damage path currently passes 0).
- `hitzone(image id, player id, object id, weapon, x, y, damage)` — every shot sample that enters a
  `Tdo` 40 image hit zone (`imagehitzone`). An NPC shot passes player 0 and the NPC id as object id;
  a turret passes its owner and its own id; a player passes its id and 0. `image x/y` is the centre
  and the zone is the axis-aligned rect `x+xo .. x+xo+w`, `y+yo .. y+yo+h`. Modes > 100 block the
  shot; modes 1..4 do not. The return value replaces the damage (> 0) or zeroes it (< 0); 0 keeps
  it. HUD images (mode 2) never have a working hit zone.
- `projectile_impact(id, weapon, x, y, mode, projectileid)` — 0 wall, 1 player, 2 hostage, 3 NPC,
  100 ground / maximum flight distance. Per help.php only the RPG-7, rocket launcher, snowball and
  gut bomb collide with players/hostages/NPCs; every other projectile stops on walls/ground.
- `projectile(id, weapon, x, y, projectileid)` — at detonation. (It used to be called at spawn.)

`turretscan` and `build`/`buildattempt` are buildings, `use` covers supply objects; they are not NPC
hooks. `startround_prespawn` is already wired and runs before the NPC rebuild.

Residuals: bot secondary attack (`ai_attack(id, 1)`) only relays `[8]` (grenades are spawned by a
client in the original); shotgun bots apply a single pellet hit; the original per-skill target range
arrays and turn table are runtime arrays, so ranges/turn are calibrated; the NPC and client
`[36,7]` positions use the same deterministic motion, but a lost/reordered reliable packet can
still leave a temporary offset until the next waypoint.



- **Join section 11 (wbank, `[252,7,11]`)**: built at the end of `FUN_140112bc0`: 5 bytes (`id, recoil_accuracy, recoil_recover, recoil_attack, firedelay`) per slot 1/2 weapon, id order, zlib `[u16 size][u32 raw size][zlib]`; file = `weapons_recoil.cfg` if mp_recoil else `weapons.cfg`. Code: `cs2d_weapons_wbank_raw`; test `test_join_wbank_raw`.
- **Join `[252,6]` tail (`FUN_1401dba60`)**: 3 lists PhysicsPlatform `DAT_1407df510`, PhysicsObject `df5e0`, PhysicsController `df400` (names from the client twin, `docs/dump/cs2d/_global/1402e0000.c`), each u8 count + entries. **Player id** (`FUN_140098050(1,-1)`): lowest id in 1..32 not held by a Tpl; none free -> null.
- **`[247]` header**: ticks byte = min over a 180-entry ring of "loops to pass 999 ms" (`0x14019d3a0`); u16 #1 = bytes sent/8 (`DAT_1407dfe34`), u16 #2 = bytes received/8 (`DAT_1407dfe30`), counters `0x1407e06dc` / `0x1407e06d8` (Tudp). Code: `sv_frame_stats`.

## spawnobject / spawnnpc (REA, 2026-10-24)

- `spawnobject <type> <x> <y> <rot> <mode> <team> <player>` = `FUN_14011d6c0` -> `FUN_1400fa060`; x/y in tiles, spawned at `tile*32` (corner). Errors printed: wrong tile properties / there already is an object / out of map bounds.
- `spawnnpc <npc type> <x> <y> <rot>` = `FUN_14011d910` -> `FUN_1400fa060(-1, 30, tx*32+16, ty*32+16, rot, 0, 0, npctype, 1)`: NPC at the tile centre, mode/team 0.
