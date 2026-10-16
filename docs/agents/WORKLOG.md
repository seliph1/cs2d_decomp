# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 5): team suicide [FUN_1401624c0], respawn modes & timer [39] [FUN_14017cbb0/FUN_1401cf8d0], position & anti-speeder validation [FUN_1401ca500/FUN_1401c99a0]
- Implemented team switch suicide (`cs2d_sv_team`, `FUN_1401624c0`): when a living player changes team, `cs2d_sv_kill(sv, id, 0, 0, 0)` is invoked immediately, incrementing deaths and broadcasting reliable `[19]`.
- Implemented respawn modes & timers (`cs2d_sv_step_respawns`, `FUN_14017cbb0`): in game modes with `respawn == 1` (Deathmatch, Team Deathmatch, Zombies), players auto-respawn after `mp_respawndelay` seconds; client respawn requests (`[39]`, `cs2d_sv_request_respawn`, `FUN_1401cf8d0`) enforce the same rule and delay.
- Implemented movement validation & anti-speeder (`cs2d_sv_validate_move`, `FUN_1401c99a0`, `FUN_1401ca500`):
  - Map boundaries and solid wall collision (`cs2d_map_is_solid`): rejects illegal moves and sends reset position `[11, id, last_valid_x, last_valid_y]`.
  - Anti-speeder: detects `dist > 160.0f` and velocity `> 650.0f` px/s, logs `"MP_ANTISPEEDER: %s is too fast - reset position!"` to console and sends reliable `[10, id, last_valid_x, last_valid_y]`.
- Integrated `cs2d_sv_validate_move` in opcode 10, 11, 71, 72 handlers, and `cs2d_sv_step_respawns` in frame loop.
- Verified: `build.bat` clean, `test_cs2d.exe` **64 / 64** passing tests.
- Left: next Phase 2 items: Lua `spawn` / `team` / `die` / `kill` hooks, team code 12 and the per-team limits.

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 4): drop [24], collect [25], reload [16], chat [240], spectator [67]/[32]
- Implemented weapon drop (`cs2d_sv_drop`, `FUN_14016bbd0`, `[24]`): validates possession, restricts dropping melee/knife, updates player inventory & bestweapon, spawns ground item slot, and broadcasts reliable `[24, id, type, clip, res, sub, item_id, tx, ty, held_weapon]` (15 bytes).
- Implemented ground item tick (`cs2d_sv_step_items`):
  - Authoritative collection `[25]`: dedicated rejects client `[25]`; server tests collision on ground item tile, validates weapon slots & gear rules, grants weapon preserving ammo, and broadcasts reliable `[25, pid, item_id, type, clip, reserve, 0]` (10 bytes).
  - Fadeout `[40]`: dropped ground items age 1s per second and fade after 15s (`0x140a14d94`), broadcasting reliable `[40, item_id u16]`.
- Implemented reload handler (`cs2d_sv_reload`, `FUN_14018b1c0`, `[16]`): mode 2 refills clip from reserve clamped to available ammunition, broadcasts `[16, pid, mode]` to all clients.
- Implemented in-game chat (`cs2d_sv_chat`, `FUN_140153900`, `[240]`): UTF-16LE conversion, console logging, `mp_deadchat` filtering between alive and dead players, team chat routing, and reliable `[240, pid, mode, len, UTF-16LE]` transmission.
- Implemented spectator target (`cs2d_sv_spec_target`, `[67]`), free-look position updates (`cs2d_sv_spec_pos`, `FUN_1401ced00`, `[32]` with -32768 bias), and spectator aim relay in `cs2d_sv_relay_movement` (`[70]-[72]` with `aim * 0.512f`).
- Verified: `build.bat` clean, `test_cs2d.exe` **63 / 63** passing comprehensive drop, pickup, fadeout, reload, chat, and spectator test.
- Left: next Phase 2 items: death and respawn timers per mode (`sv_gamemode`, `mp_respawndelay`), team change suicide (`FUN_1401647d0`), position validation & anti-speeder (`FUN_1401ca500`).

