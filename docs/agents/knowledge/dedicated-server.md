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
  `[229, MilliSecs - DAT_1407e0184]` (`DAT_1405a64e4` > 0) or `[249, …]`; every 3rd second the joined players
  (`+0xf4`) get `[247, n, DAT_1407dfe28, DAT_1407dfe34/8 u16, DAT_1407dfe30/8 u16, n × {id, ping}]` over the players
  in a team (`+0x230` > 0); ping 0xffff for the host, 0xfffe for a bot (`+0x198`), else min(`+0xd0`, 30000).
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
  Server verifies alive status vs `mp_deadchat` (dead players cannot chat to living players unless `mp_deadchat 1`),
  logs to console (`cs2d_server_print`), and broadcasts reliable `[240, pid, mode, len, UTF-16LE]` to eligible recipients.
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
    1. `sys/lua/wrapper.lua` (if present)
    2. `sys/lua/server.lua` (if present)
    3. `sys/lua/autorun/*.lua` (alphabetical order)
    4. `maps/<mapname>.lua` (if present)
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
  - `image(path, x, y, mode, [target])`: assigns incremental ID, stores in `lua->images`, and broadcasts reliable `[51, 0, id u16, mode u8, target u8, x u16, y u16, path (wstr)]`.
  - Manipulation functions: `imagealpha` (mode 2), `imagecolor` (mode 1), `imagescale` (mode 4), `imageblend` (mode 3),
    `imagepos` (mode 5), `freeimage` (mode 6), `imagehitzone` (opcode 59).
  - Tweens: `tween_alpha`, `tween_color`, `tween_move`, `tween_rotate`, `tween_rotateconstantly`, `tween_scale`, `tween_frame`, `tween_animate`
    broadcasting opcode `[54]` with target `200 + id`.
- **Server Timers (`timer`, `freetimer`):**
  - `timer(ms, func, param, [count])`: executes `func(param)` every `ms` milliseconds. `count == 0` loops indefinitely;
    default is `count = 1`.
  - Decrements remaining count on each invocation and frees automatically when completed.
- **Server Menus (`menu`, Opcode `235`):**
  - `menu(id, text)`: sends opcode `[235, title (wlstr), page u8, items (wlstr)]` to target player.
  - Client response `[235, pid, slen, UTF-16LE title, button u8]`: invokes Lua hook `"menu"` with `(id, title, button)`.

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
  - `player(id, prop)`: added `"tablebot"`, `"tablehuman"`, `"players"`, `"mousemapx"`, `"mousemapy"`, `"mapmousex"`, `"mapmousey"`, `"mousex"`, `"mousey"`, `"mousedist"`, `"language"`, `"favweapon"`.
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

## Dedicated server command line flags (`src/main_dedicated.c`)

- Original parameters: `-offline` / `-off`, `-map <map>`, `-port <port>`, `-lua <script>`, `-pw` / `-password <pass>`, `-name <name>`, `-rcon <pass>`, `-maxplayers <n>`.
- Our implementation supports both single and double dashes (`--map`, `-map`, `-m`), and equals/colon syntax (`--map=de_dust`, `--port:36963`).
- `cs2d_sv_sanitize_map_name`: automatically trims quotes, whitespace, directory paths (`maps/`, `maps\`, absolute paths), and case-insensitive `.map` extensions so that `--map fun_roleplay.map` or `--map maps/fun_roleplay` resolves cleanly to `fun_roleplay`.

## Network, Tudp Transport & Protocol Parity (2026-10-17)

- **Tudp Reliable Resend Queue**: BlitzMax `Fudp_update` (`0x1402bca30` / `0x1403c90c0`) queues outbound reliable datagrams (`even` seq numbers) in `con.out` and retransmits them every 500 ms (`CS2D_CLIENT_RESEND_MS` / `SV_RESEND_MS`) until acknowledged by a control triple kind 1 (`[01, seq u16]`, `Tudp_con.clearoutbuffer`). Both dedicated and client implement this retry mechanism and process ACKs.
- **Initial Handshake Control Triple (`03 32 00`)**: The original `CS2D.exe` client prefixes its first datagram with the Tudp control triple `03 32 00` (kind 3 session reset, u16 value 0x0032 = 50 netrate ticks), producing the exact 7 wire bytes: `02 00 03 32 00 fc 00`. Handled in `cs2d_client_connect` and parsed in `cs2d_server.c`.
- **Leave / Disconnect / Kick (`[253, id, reason]` / `FUN_1401502a0`, `0x140150ae0`)**:
  - `cmp edi, 0xf` (15): if `reason < 16`, sends 3 bytes `[253, id, reason]`.
  - if `reason >= 16`, appends u8-length string `[253, id, reason, str_len, str]`.
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
  - When a player presses USE within 45px Euclidean distance of a `Trigger_Use` tile center (`(x * 32 + 16, y * 32 + 16)`), fires Lua hook `usebutton(id, x, y)`.
  - If team filter passes (`int_settings[2] == 0 || int_settings[2] == pl->team`), fires linked triggers and Lua `trigger` hook.

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
  - Client sends `[227/228, key u16]`.
  - Server maps key code to canonical key name via `sv_resolve_key_name`: checks custom `addbind` keys in Lua state, falls back to `cs2d_bind_key_name(key)` or decimal string.
  - Fires Lua hook `key(id, key_name, state)` (1 for press, 0 for release).
- **Client Setting (Opcode 232, `FUN_1401bb730`)**:
  - `sub 1`: screen resolution `[232, 1, width u16, height u16]` -> updates `pl->screen_w`, `pl->screen_h`.
  - `sub 2`: aspect ratio 4:3 `[232, 2, s43 u8]` -> updates `pl->screen_aspect`.
  - `sub 3`: windowed mode `[232, 3, win u8]` -> updates `pl->screen_windowed`.
  - Fires Lua hook `clientsetting(id, setting, val1, val2)`.
  - Lua `player(id, "screenw")`, `"screenh"`, `"screen4by3"`, `"windowed"` read player fields.
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


