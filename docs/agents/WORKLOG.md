# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 3): damage [17], kill [19], buy [23]
- Implemented damage calculation and absorption (`cs2d_sv_damage`, `FUN_1401658c0`) deducting kevlar (~35%) and broadcasting reliable `[17, victim, attacker, hp, arm]`.
- Implemented kill logic (`cs2d_sv_kill`, `FUN_1401647d0`): sets `alive = false`, updates victim deaths, killer scores (+1, +$300 on enemy kill, friendly fire / suicide penalties), assist tracking, and broadcasts `[19, victim, killer, assist, weapon, x, y]`.
- Implemented buy handler (`cs2d_sv_buy`, `FUN_1401cd710`): price and money verification, money deduction, inventory addition (`sv_give`), special items / equipment handling (defuse kit, kevlar, helmet, nightvision, gas mask), and response `[23, id, type, money u16, 0]`. Broadcasts `[9]` weapon switch on weapon purchases.
- Integrated opcode handlers for `17` (hit report), `19` (suicide / kill), and `23` (buy request) in `sv_recv_game` in `src/cs2d_server.c`.
- Verified: `build.bat` compiled all 3 binaries, `test_cs2d.exe` **62 / 62** passing all damage, kill, score and buy assertions.
- Left: next Phase 2 items: drop / collect `[24]`/`[25]`, reload `[16]`, chat `[240]`, spectate `[67]`/`[32]`.

## 2026-10-15 — Antigravity — dedicated server phase 2 (part 2): rounds, leave, weapon selection [9], fire [7]/[8]
- Implemented round end (`cs2d_sv_round_end`, `FUN_14015c840`), round start (`cs2d_sv_round_start`, `FUN_1402520b0` / `FUN_140123530`), and per-frame round progression (`cs2d_sv_step_round`, `FUN_14015ce30`).
- Added "Game commencing" trigger in `cs2d_sv_team` (`FUN_1401624c0` LAB_14015cff0: mode 4, 3000 ms delay).
- Broadcasts opcode 226 mode 1 (`[226, 1, winner, mvp, reason_lstr, mode_byte]`) on round end, and opcode 22 (`[22, reason, count, ...]` + inventory) on round start.
- Handled opcode 253 leave reason and optional reason string (`FUN_1401e3790`).
- Implemented weapon selection `[9]` (`cs2d_sv_select_weapon`, `FUN_140158060`), primary fire relay `[7]` (`cs2d_sv_fire`, `FUN_140191a70`), and secondary attack relay `[8]` (`cs2d_sv_attack2`, `FUN_1401fd590`).
- Verified: `build.bat` clean, `test_cs2d.exe` **62 / 62** (passed dedicated server round end / start tests and combat relay test).
- Left: next Phase 2 items: damage / kill `[17]`/`[19]`, buy `[23]`, drop / collect `[24]`/`[25]`, reload `[16]`, chat `[240]`.
