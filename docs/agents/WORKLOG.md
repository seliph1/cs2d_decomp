# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-16 — Antigravity — dedicated server: frame pacing, timer resolution & accurate tick/FPS reporting
- Fixed low/skewed tickrate and connection stability on dedicated server (`main_dedicated.c`, `cs2d_server.c`, `build.bat`):
  - Enabled Windows 1ms timer resolution via `timeBeginPeriod(1)` on startup (and `timeEndPeriod(1)` on exit), linking `-lwinmm`.
  - Implemented high-resolution frame pacing loop in `main_dedicated.c` using `QueryPerformanceCounter`/`QueryPerformanceFrequency` targeting 60 ticks/s (BlitzMax standard 60~63 FPS). Includes coarse sleep followed by microsecond spin-wait, and de-synchronization protection against latency spirals.
  - Fixed `sv->second_ms` drift in `cs2d_server_frame`: calculates real `sv->fps = (sv->frames * 1000) / elapsed` and syncs `sv->second_ms = now` on each second barrier, preventing consecutive 0/1/3 FPS calculations from reporting erroneous red "Ticks: 3" to clients.
- Verified: `build.bat` compiles cleanly, `test_cs2d.exe` **69 / 69** passing regression tests.

## 2026-10-16 — Antigravity — dedicated server: weapon firing ammo deduction & reload state machine [16]
- Fixed weapon firing ammo maintenance on dedicated server (`FUN_140191a70`, `Tpl.decammo`):
  - Updated `cs2d_sv_fire` (`[7]` opcode handler) to decrement 1 bullet from `pl->clip[pl->weapon]` for bullet-based weapons with non-zero clip sizes.
  - Previously, `pl->clip` was never decremented on the server when firing, meaning the server believed the clip was always full (12/12 for USP, 30/30 for M4A1) unless a weapon was dropped with depleted clip and re-collected. This caused reload requests (`[16, 1]`) to be rejected with `clip >= clip_size`.
- Fixed weapon reload on dedicated server (`FUN_14018b1c0`, `FUN_140194650`, `0x1401cc750`):
  - Added `is_reloading` and `reload_end_ms` to `cs2d_sv_player_t` (`+0x320`, `+0x328`).
  - Implemented reload start (`mode 1` / `100`): validates living state, active weapon, ammo needs and absence of ongoing process (plant/defuse), sets `is_reloading = true` and `reload_end_ms = now + reload_ticks * 5`, and broadcasts `[16, pid, mode]`.
  - Implemented authoritative server reload timer tick (`cs2d_sv_step_reloads`, `FUN_140194650` in main tick): when reload timer expires, automatically transitions clip weapons to `mode 2` (refill clip from reserve, or full on `mp_infammo`), and shell weapons (M3 / XM1014) to `mode 4` (1 shell per step, looping until full or out of ammo).
  - Implemented client compensation (`mode 101` / `0x65`), reload aborts (`mode 3`) on weapon switch (`cs2d_sv_select_weapon`), player death (`cs2d_sv_kill`), and round respawn (`cs2d_sv_round_start`).
  - Exposed accurate `player(id, 'reloading')` state in Lua API.
- Verified: `build.bat` clean, `test_cs2d.exe` **69 / 69** passing regression tests with test cases covering firing ammo deduction, mode 1 initiation without drop/pickup, in-progress timer stepping, automatic clip refill on expiry, and multi-stage shell-by-shell reloading.

## 2026-10-16 — Antigravity — dedicated server: fix weapon drop immediate re-pickup & standard gamemode win conditions
- Fixed weapon drop immediate re-pickup bug:
  - Added `dropped_by`, `drop_tx`, and `drop_ty` to `cs2d_sv_item_t`.
  - When dropping a weapon via `[24]` (`cs2d_sv_drop`), records dropping player ID and tile.
  - In `cs2d_sv_step_items`, player cannot immediately collect their dropped weapon on the drop tile until moving away to another tile.
  - In `cs2d_sv_validate_move`, cleared `dropped_by` on ground items when player moves to a different tile (`new_tx != old_tx || new_ty != old_ty`).
- Implemented standard gamemode (`sv_gamemode 0`) win condition progression (`FUN_14015ce30`, `cs2d_sv_step_round`):
  - Fixed single-player death: when all players of a team die or a solo player on the server dies, round ends as round draw (`reason 3`, 3000 ms), matching official `cs2d_dedicated.exe` lines 69-74 (`if (t_total == 0 || ct_total == 0) -> round_end(3, 3000)`).
  - Fixed team elimination logic: CTs win (`reason 2`, 5000 ms) when Ts are eliminated and no bomb is planted; Ts win (`reason 1`, 5000 ms) when CTs are eliminated or bomb is planted and all players die.
  - Implemented standard mode round timer expiration (`0x1401a3128`): when `mp_roundtime` minutes elapse without a planted bomb, triggers round end with CT victory (`reason 22` Target Saved / Time Out, 5000 ms).
- Verified: `build.bat` clean, `test_cs2d.exe` **69 / 69** passing regression tests including new drop immediate re-pickup prevention, single-player round draw, and round timer timeout.

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

