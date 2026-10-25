# Work log (append-only, newest first)

Keep only the 5 newest entries here; move older ones to the top of `worklog/archive.md`.

## 2026-10-24 — Claude — dedicated: `flashplayer`, `flashposition`, `shake` + client [48] types 2/4/5

- REA ded: `FUN_14011daf0` (flashplayer, reliable `[48,5,id+0x8000,0x8000,int,0]` to all), `FUN_14011e010` (flashposition via `FUN_140102be0` type 4), `FUN_14011d2c0` (shake, unreliable `[48,2,0x8000,0x8000,player,power]` to that player only). Client `FUN_14014cf50` cases 2/4/5 ported in `cs2d_client.c` (shake -> `gen.shake` cap 1000, flash += p1*0.01 / 0.003 cap 1.3, line test assumed 8 px step). Test in `test_dedicated_p3` block (`flashplayer`/`flashposition`/`shake`). 116/116. Details: `knowledge/dedicated-server.md` "[48] FX".

## 2026-10-24 — Claude — dedicated: `spawnnpc` / `spawnobject` position and argument order

- Original `spawnnpc` (`FUN_14011d910`) spawns NPCs at the tile CENTRE (`tile*32 + 16`); we used the corner. `spawnobject` (`FUN_14011d6c0` -> `FUN_1400fa060`) keeps `tile*32`.
- Argument order fixed from the same functions: `spawnobject <type> <x> <y> <rot> <mode> <team> <player>`, `spawnnpc <npc type> <x> <y> <rot>` (the 4th arg of spawnnpc is rotation, not team). `cs2d_sv_spawn_object` got a `center` flag; regression in `test_dedicated_commands` (spawnnpc block).

## 2026-10-24 — Claude — dedicated: `effect` command [48] types and clamps

- Our `effect` sent wrong types (fire 100, smoke 101+gray rgb, flare 102). Original `FUN_14011e170` -> `FUN_140102be0`: fire 6, smoke 7, blood 8, flare 100, colorsmoke 101, sparkles 102; p1 clamped (100 / blood 35 / sparkles 50); no rgb under 100.
- Fixed `cs2d_server.c` and the client [48] handler (6/7 added, 100 = tinted fire particle via `cs2d_fx_flare_puff`). Test extended in `test_cs2d.c`. Client particle look is still approximate (not checked against a capture).

## 2026-10-24 — Claude — dedicated: NPC behaviour (soldier / Vortigaunt / snark / obstacles)

- Soldier and Vortigaunt always approached and swayed: after the attack / charge the port fell through into the "walk to last seen position" code; the original `FUN_140283f20`
  returns there (stand still while the player is in sight, charge = ~40 idle frames so the beam is dodgeable). Added (calibrated, not original): approach when the target is > 380 px.
- NPC sight line `FUN_14011c3a0` steps **5 px** (not 32) and only collision 1 (g_prop_class 0) blocks; waypoints are rejected when the segment crosses a blocked tile (obstacle / DynWall hop).
- Snark death explosion: `FUN_1401f1130(x, y, 75, 30, 0, 0xf9)` in `sv_object_kill`. Tests in `test_dedicated_bot_and_npc_ai`. Not checked against a live original run.

## 2026-10-24 — Claude — dedicated: anti-speeder false positives + mp_anticlock [229]

- Cause of the kicks on `func_teleport` / round restart: our `cs2d_sv_validate_move` punished the first move >160 px & >650 px/s from `last_valid`. After `cs2d_sv_set_pos` / spawn
  `last_valid` is the new place, so the client's in-flight [10]/[11] still carrying the old position (one RTT) looked like a teleport hack. The original never punishes one packet:
  FUN_1401ca500 keeps `Tpl+0x178` (score += 1 + excess) and `+0x17c` (events += 1) on over-limit moves, decays both by 1 on every valid move, and only
  `+0x178 > 30 && +0x17c > 50` prints "Speedhack detected (speed)" and punishes. Ported (`speed_score` / `speed_events`); the 160 px / 650 px/s limit itself stays calibrated.
- `mp_anticlock` [229] clock check ported from the dedicated handler FUN_1401d7640 (`0x1401d7690`): `clock_diff` = client clock delta, `clock_ratio` = diff x 0.0005,
  strike if ratio > 1.1, else -1; `mp_anticlock` N kicks above `{0,8,4,2}[N]` strikes with "Speedhack Detected". The address 0x1402e2aa0 from OPEN_ISSUES is inside a CRT
  formatter (FUN_1402dfae0), not the handler. Tests: `test_server_phase2_respawn_and_speeder` (teleport / respawn / sustained), P1 anticlock strikes + kick. 116/116.