# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-16 — Antigravity — dedicated server phase 7 (part 2): join file transfer [252,50], dynamic objects snapshot [252,7,5] & spray logos [252,10]/[28]
- Implemented file transfer protocol in dedicated server (`[252,50]`):
  - Added `sv_maptransfer` setting (`sv_maptransfer 1` default). On `[252,3]` map checksum mismatch, server responds with `[252,4,13]` ("Downloading" window) followed by `[252,50,1,1,maps/<map>.map]` file list.
  - Implemented handling of client file transfer requests (`[252,50,2]`): compresses requested file with zlib (`[raw size i32][zlib]`) and streams chunked payloads in 480-byte packets using `[252,50,5]` (first part with total length), `[252,50,6]` (middle parts), and `[252,50,7]` (final part).
  - On transfer completion (`count == 0` or `0xffff`), server restarts handshake via reliable `[252,2]`.
- Implemented Section 5 dynamic objects serialization in join snapshot (`sv_send_join_snapshot`): streams all active non-image buildings (`Tdo`) in 60-object chunks (`[252,7,5,count,{id, typ, health, mode, x, y, player, team, rot}]`).
- Implemented spray logo storage and relay (`[252,10]` and `[28]`):
  - Added spray storage buffer `spray_logo` to `cs2d_sv_player_t`.
  - Parsed client spray payload on join (`[252, 10, 28, 2, size, ...]`): stores spray data and relays `[28, pid, 2, size, bytes]` to other joined players when `sv_spraytransfer == 1`.
  - Relayed all existing players' sprays to newly connecting players in `[252,7]` snapshot tail after Section 200.
- Verified: `build.bat` compiles all targets cleanly; `test_cs2d.exe` passed **69 / 69** regression tests (`test_server_phase7_transfer_and_spray`).
- Left: Dedicated server end-to-end multi-client live gameplay validation against official binaries.

## 2026-10-16 — Antigravity — dedicated server phase 7: live client interaction, 100% lua commands & hooks coverage, console commands
- Performed deep automated scrape of `cs2d.com` official documentation into `docs/cs2d_help_db_complete.json` covering:
  - 375 Console Commands (categories, parameters, full description texts).
  - 79 Lua Hooks (parameters, return behaviors, triggers).
  - 88 Lua Commands (signatures, return values, samples).
- Achieved **100% coverage** in `cs2d_sv_lua.c` and `cs2d_sv_lua.h`:
  - Registered **88 / 88** official Lua commands (0 missing).
  - Dispatched **79 / 79** official Lua hooks (0 missing).
- Added `-lua <path>` command-line argument to `main_dedicated.c` and integrated dofile execution at startup.
- Implemented full live test harness `tools/agent/lua/full_live_suite.lua` and test orchestrator `tools/agent/run_live_test.ps1`:
  - Spawned `cs2d_server.exe` with `full_live_suite.lua` and joined with the real official client `CS2D.exe`.
  - Audited global availability and callable signatures of all 88 Lua commands (`check` passed).
  - Registered all 79 Lua hooks dynamically using `addhook`.
  - Executed batch of console commands and cvars (`mp_c4timer`, `mp_roundtime`, `mp_buytime`, `mp_startmoney`, `sv_gamemode`, `mp_friendlyfire`, `setmoney`, `sethealth`, `setarmor`, `equip`, `setweapon`, `hudtxt`).
  - Successfully verified live network handshake, join, team placement, player spawn, and periodic hooks (`always`, `second`, `ms100`).
- Verified: Live interactive test suite against `CS2D.exe` passed **37 / 37** checks; regression test suite `test_cs2d.exe` passed **68 / 68** tests. Clean build with 0 warnings.

## 2026-10-16 — Antigravity — dedicated server phase 6: 1:1 Lua C API, world & dynobj queries, freeline raycasting, hook log, generic cvars & binds
- Implemented all 18 missing CS2D Dedicated Server Lua API functions from `cs2d_dedicated.exe` (`FUN_14013a980`):
  - World & Map: `map(prop)` (dimensions, tileset, counts, buildings), `tile(x, y, prop)`, `tileproperty(id)` (wall/obstacle/deadly collision modes).
  - Combat & Items: `itemtype(type, prop)` (weapon damage, prices, slots, ammo), `item(id, prop)` (ground item states and slot queries).
  - Dynamic Objects: `object(id, prop)`, `objectat(tx, ty)`, `closeobjects(x, y, r [, type])` integrated with `cs2d_dynobj`.
  - Players & Geometry: `closeplayers(x, y, r [, team])` (`FUN_14004cc30`) and `hascloseplayers(x, y, r [, team])` (`FUN_14004ce50`), `freeline(x1, y1, x2, y2 [, mode])` collision raycasting.
  - Entities & Stats: `entity(x, y, prop)` (int0..9, str0..9, state), `entitylist([type])`, `stats(id, prop)`.
  - Client Binds & Stubs: `addbind(key)`, `removebind(key)`, `removeallbinds()`, `reqcld`, `reqhttp`.
- Extended `player(id, prop)` with `tablebot`, `tablehuman`, `players`, `mousemapx/y`, `mapmousex/y`, `mousex/y/dist`, `language`, `favweapon`.
- Extended `game(prop)` with version, dedicated, phase, round, ticks, time, maptimeleft, port, sysfolder, bombplanted, and generic cvar fallback.
- Added generic cvar storage `sv->cvars[128]` with lookup and setting fallback, `mp_luaserver`, `mp_luamap`.
- Implemented Console Log Hook (`hook "log"`): wired `cs2d_server_print` to `cs2d_sv_lua_log` with recursion protection.
- Verified: `build.bat` compiles cleanly (`test_cs2d.exe`, `cs2d_client.exe`, `cs2d_server.exe`), `test_cs2d.exe` **68 / 68** tests pass (`test_server_phase6_lua`). `cs2d_server.exe -offline` starts and loads maps seamlessly.
- Left: Server protocol end-to-end regression validation against live game scenarios.

## 2026-10-15 — Antigravity — dedicated server phase 5: bomb system [30]/[26], hostages [29], bots, bans, projectiles [27]/[41], flashlight [57] & map change [239]
- Implemented C4 Bomb plant, abort, defuse and detonation (`FUN_14016a2d0`, `FUN_14016a700`, `FUN_14016ab00`):
  - Plant: `[30,1]` start countdown (`mp_c4timer`), `[30,2]` abort, `[30,3]` plant complete with ground bomb placement and broadcast.
  - Defuse: `[26,3]` (Use) start, `[30,4]` abort, `[30,5]` defuse complete with reward ($3250 CT, +3 score) and round end mode 21.
  - Explosion: `[30,6]` at timer 0; deals 500 splash damage over 350px radius attenuated linearly, round end mode 20, $3250 T reward.
- Implemented Hostage system (`FUN_140167200`, `FUN_1401676f0`, `[29]`):
  - Loaded from `Info_Hostage` map entities, following/releasing CT player on Use (`[26]`), stepping towards rescuer.
  - Rescue detection at `Info_RescuePoint` entities (type 8): +2 rescuer score, $1000 CT team reward, broadcast `[29,5,h_id]`.
  - Damage & death: broadcasts `[29,3]` and `[29,4]`.
- Implemented Bot management (`bot_add`, `bot_kick`, `bot_kill`): slot allocation with ping `0xfffe`, conn `-1`, `[248]` join broadcast.
- Implemented Ban system (`banip`, `banusgn`, `bansteam`, `unban`, `listbans`): join-time check rejecting with status 4 and custom reason string.
- Implemented Map change (`cs2d_sv_change_map`, `[239,1]`): dynamic heap map loading (fixing stack overflow from 14MB `cs2d_map_t`), SHA256 checksum calculation, hostage re-initialization, round restart.
- Connected snapshot serialization for Section 2 (hostages) and Section 3 (ground items).
- Implemented Opcode 27 (Grenade consumption & projectile relay), Opcode 41 (Kill projectile), Opcode 57 (Flashlight).
- Verified: `build.bat` compiles all 3 targets cleanly, `test_cs2d.exe` **67 / 67** passing `test_server_phase5_bomb_hostages_bots`.
- Left: Phase 6 (`C:\C4` scripts end-to-end testing).

## 2026-10-15 — Antigravity — dedicated server phase 4: Lua 5.1.4 integration, addhook priorities, parse, player, image, timers, hudtxt, menus
- Vendored official PUC Lua 5.1.4 into `third_party/lua51/` (`lua_all.c` unity build with `-Ithird_party/lua51/src` in `build.bat`).
- Implemented Lua server subsystem (`include/cs2d_sv_lua.h`, `src/cs2d_sv_lua.c`):
  - Lifecycle: `cs2d_sv_lua_init`, `cs2d_sv_lua_frame` (timer stepping), `cs2d_sv_lua_close`.
  - Script loading sequence: `sys/lua/wrapper.lua`, `sys/lua/server.lua`, `sys/lua/autorun/*.lua` (alphabetical order), `maps/<map>.lua`.
  - Hook engine (`addhook`, `freehook`, `sethookstate`): priority ascending order (highest priority runs last and determines return value); veto/cancellation semantics (return `1` cancels action in `team`, `buy`, `drop`, `collect`, `name`, `hit`).
  - Wired native hooks: `join`, `leave`, `team`, `spawn` (custom items override or cancel), `select`, `attack`, `attack2`, `hit`, `kill`, `die`, `buy`, `drop`, `collect`, `reload`, `say`, `sayteam`, `name`, `endround`, `startround`, `menu`, `serveraction`.
  - `player(id, prop)` (40+ properties & table queries `"table"`, `"tableliving"`, `"team1"`, `"team2"`, etc.), `playerweapons(id)`, `playerammo(id, type)`.
  - `parse(cmd)` with HUD text broadcast `[234]` and tween `[54]` interception (`200 - id`).
  - Dynamic images (`image`, `freeimage`, manipulation & tweens `[51]`, `[59]`, `[54]` targeting `200 + id`).
  - Server timers (`timer(ms, func, param, count)`, `freetimer`) with fractional frame countdown.
  - Server menus (`menu(id, text)` sending `[235]` and receiving client menu response `[235]` invoking hook `menu`).
- Verified: `build.bat` clean, `test_cs2d.exe` **66 / 66** passing `test_server_phase4_lua`.
- Left: Phase 5 (construction, hostages, bomb, entities/triggers, items, projectiles, bots).

## 2026-10-15 — Antigravity — dedicated server phase 3: team codes 12/13 & spawn auto-balance, [238] name change, RCON [236]/[241]/[242], playerdata [42] & console commands [FUN_140140a00]
- Completed remaining non-Lua Phase 2 items: per-spawn team change limit (`pl->team_changes`, refusal `[20, id, 12, look]`), Zombies mode team switch refusal (`[20, id, 13, look]`) and >30s force-T rule (`FUN_1401624c0`), and spawn-time auto-teambalance (`FUN_140160740`).
- Implemented in-game name change `[238]` (`cs2d_sv_change_name`, `FUN_14010ddb0`): 25-char sanitize, per-spawn limit refusal `[238, 255, 0, ""]`, dead player deferred `pending_name` `[238, 254, 0, ""]` applied on `cs2d_sv_spawn`, and broadcast `[238, id, hide, name]`.
- Implemented RCON (`[236]` `cs2d_sv_rcon_check` `FUN_1401d8d90`, `[241]` `cs2d_sv_rcon` `FUN_1401d9990`, `[242]` `cs2d_sv_public_rcon` `FUN_1401be1c0`, `FUN_1401bd520`) with `"mysecretremotecontrolpw"` / `"rconcmd"` ciphers, `sv_rcon` / `sv_rconusers`, and error replies `"1"`/`"2"`/`"4"`.
- Implemented playerdata setter & broadcaster `[42]` (`cs2d_sv_set_playerdata`, `FUN_140159840` modes 0..11), teleport `[15]` (`cs2d_sv_set_pos`, `FUN_140102950`), team scores `[64]` (`cs2d_sv_set_teamscores`, `FUN_140051ac0`), server messages (`cs2d_sv_msg`), `slap` / `deathslap` (`FUN_140167e40`), `kick` (`FUN_1401502a0`), and console command parser `cs2d_sv_exec` (`FUN_140140a00`).
- Verified: `build.bat` clean, `test_cs2d.exe` **65 / 65** passing `test_server_phase3_rcon_and_commands`.
- Left: Phase 4 (Lua 5.1.4 integration, `addhook`, `parse`, `player()`, `image()`, timers, hudtxt, menus).


