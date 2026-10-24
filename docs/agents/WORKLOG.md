# Work log (append-only, newest first)

Keep only the 5 newest entries here; move older ones to the top of `worklog/archive.md`.

## 2026-10-24 — Claude — dedicated: anti-speeder false positives + mp_anticlock [229]

- Cause of the kicks on `func_teleport` / round restart: our `cs2d_sv_validate_move` punished the first move >160 px & >650 px/s from `last_valid`. After `cs2d_sv_set_pos` / spawn
  `last_valid` is the new place, so the client's in-flight [10]/[11] still carrying the old position (one RTT) looked like a teleport hack. The original never punishes one packet:
  FUN_1401ca500 keeps `Tpl+0x178` (score += 1 + excess) and `+0x17c` (events += 1) on over-limit moves, decays both by 1 on every valid move, and only
  `+0x178 > 30 && +0x17c > 50` prints "Speedhack detected (speed)" and punishes. Ported (`speed_score` / `speed_events`); the 160 px / 650 px/s limit itself stays calibrated.
- `mp_anticlock` [229] clock check ported from the dedicated handler FUN_1401d7640 (`0x1401d7690`): `clock_diff` = client clock delta, `clock_ratio` = diff x 0.0005,
  strike if ratio > 1.1, else -1; `mp_anticlock` N kicks above `{0,8,4,2}[N]` strikes with "Speedhack Detected". The address 0x1402e2aa0 from OPEN_ISSUES is inside a CRT
  formatter (FUN_1402dfae0), not the handler. Tests: `test_server_phase2_respawn_and_speeder` (teleport / respawn / sustained), P1 anticlock strikes + kick. 116/116.
## 2026-10-24 — Claude — dedicated: Trigger_Delay forwarding (WQ2D trigger cascades)

- Probe `tools/agent/lua/trigger_graph_probe.lua` (in `sys/lua/autorun`, both servers headless on fun_roleplay) showed the original
  runs the `trigger` hook for names a Trigger_Delay forwards (source always false) - the 2026-10-20 "silent" note was wrong.
- Fixed: delay forwarding calls the hook with source 0; per-entity timers `delay_fire_ms[]` replace the 64-slot queue (509 delays in
  the map) and fire in entity order; round start now cancels running delays (state reset). Hook calls 851 = 851. Test
  `test_dedicated_trigger_graph` extended (116/116). Residuals in OPEN_ISSUES.
- Same day, with Lua tracebacks: `sv_gamemode` change from a script/console only queues the restart (`endround` at once, `startround` from
  `cs2d_sv_step_round`, after `cs2d_sv_lua_frame`); Lua timers start from the last frame time (frozen during load), so WQ2D's mid-load
  1 s timer fires in the first loop before the round start. Order confirmed: timer `light` -> Trigger_Start cascade -> `startround`
  (`light`). First ~565 hook calls identical (866 vs 862 in 30 s). `test_sv_gamemode_and_env_items` adapted (116/116).

## 2026-10-24 — Claude — dedicated: leave/kick text captured on the original

- Live capture (`tools/agent/lua/leave_text_probe.lua` via `capture_two.sh`, `mp_maxclientsip 5` + `sv_password` cleared in the probe; logs
  `tools/agent/out/{ded,proxy}_two_ltkick3|ltleave*`): the original dedicated prints **no console line** on leave or kick (only the Lua hook lines);
  a normal leave sends `[253,2,0]` then the `[226,1,0,0," - ",3]` draw, a kick with a quoted reason `[253,id,16,u8 str]`; `kick 2 testreason`
  (unquoted) gave reason 2 without text. The `leave` hook gets the **numeric** reason (0 / 2 / 16).
- Fixed: removed our "has left the game / has been kicked" console prints; `leave` hook now receives the numeric reason (was the text);
  join failure text "too many clients with this ip" (was "from this IP"). Test `test_dedicated_leave_hook_reason` (116/116).
- `kick` argument parsing of unquoted reasons in our `cs2d_sv_exec` not compared yet.

## 2026-10-24 — Claude — dedicated: [247] counters, ids, join snapshot tail

- `[247]` header (inline `0x1401e4ce0`): byte 3 `DAT_1407dfe28` = **min of a 180-entry ring** (`0x1405acdc8`) of "loop deltas needed to pass 999 ms"
  (`0x14019d3a0..0x14019d94a`, deltas in ring `0x1405acdd0`), pushed every loop; u16 #1 `DAT_1407dfe34/8` = bytes **sent** last second, u16 #2
  `DAT_1407dfe30/8` = bytes **received** (Tudp `0x1407e06dc` / `0x1407e06d8`, bytes, not bits; recvfrom counts every datagram). Ours was fps, bits and
  in/out swapped. Sent/received assignment from call-argument shapes (recvfrom vs sendto), not from import names: moderate confidence.
- Fields renamed `bytes_sent_sec/bytes_recv_sec`; `sv_frame_stats` added; rings assumed to start at 0 (so the byte is 0 for the first 180 loops).
- `[252,6]` tail `FUN_1401dba60` = PhysicsPlatform / PhysicsObject / PhysicsController lists (empty here); `FUN_140098050(1,-1)` = lowest free id 1..32 (verified).
- `[253]` leave text: not found as a literal; see OPEN_ISSUES. Tests 115/115.

## 2026-10-24 — Claude — dedicated: join section 11 (wbank) built from the weapon tables

- `FUN_140112bc0` (loadweaponcfg) ends by building the wbank `PTR_DAT_14059d3a8`: 5 bytes per weapon with slot 1/2 in id order
  (`id, recoil_accuracy d408, recoil_recover d400, recoil_attack d3f8, firedelay d488`). It reads `sys/weapons_recoil.cfg` when the
  setting at `[0x1405a6590]+4` (taken as `mp_recoil`) is non-zero, else `sys/weapons.cfg` (strings 0x1405bc0a0 / 0x1405bc0e0).
- `cs2d_weapons_wbank_raw` (`src/cs2d_weapons.c`) replaces the literal in `cs2d_server.c`; recoil bytes for `mp_recoil 0` are the
  capture's defaults (weapons.cfg only sets firedelay); id 92 appended as in the capture. Test `test_join_wbank_raw` (115/115).
- Open: where the built-in recoil defaults come from (FUN_1400e0fb0), `mp_recoil 1` unverified; `[252,6]` 3 bytes, section 13 untouched.

## 2026-10-21 — Cline — dedicated: drop/collect hook arguments (rp.lua Laser drop)

- **Error:** `Lua Hook Error (drop:c4hook.drop): sys/lua/WQ2D/rp.lua:12624: bad argument #1 to
  'tile' (number expected, got nil)`. `cs2d_sv_lua_hook_drop` pushed only 5 values
  (`id, 0, type, reserve, clip`), so `rp.drop(uid, internal_id, item_id, ammoin, ammo, mode, xpos,
  ypos)` got `xpos/ypos = nil` and called `tile(nil,nil)`. It happened for every droppable item,
  not only the Laser; knife/wrench return before `tile()`.
- **Measured on `cs2d_dedicated.exe`** (`tools/agent/lua/drop_probe.lua` + `ai_selectweapon`/
  `ai_drop`, log `tools/agent/out/drop_orig5.log`): a Laser (45) drop gave
  `drop(id=1, iid=1, type=45, ammoin=15, ammo=45, mode=0, x=74, y=40)`. So `iid` is the new ground
  item id (reserved before the hook), `ammoin` = clip, `ammo` = reserve, `mode` = sub, `x/y` = the
  player's tile.
- **Fixed:** `drop(id, iid, type, ammoin, ammo, mode, xpos, ypos)` (8 args) and
  `collect(id, iid, type, ain, a, mode)` (6 args); `sv_alloc_item_id` reserves the drop id before
  the hook and `sv_place_dropped_item` now receives it (the bomb plant used `++next_item_id` and
  could collide). `collect` uses `walkover`'s argument order (`clip, reserve, sub`) and its return
  value is ignored (help.php: collect cannot cancel). Also fixed the medikit/bandage `[42]` field
  (`1` = score, must be `0` = health) and made the client treat `[42,id,6,0]` as strip-all.
- **Verified:** `test_cs2d.exe` -> **114 / 114** (new `test_dedicated_drop_collect_hooks`: exact
  8/6 argument lists, drop cancellation, collect cannot cancel, Laser pickup with ammo). Live with a
  client: `tools/agent/out/drop_live3_srv.log` -> `DL drop id=1 iid=1 type=45 ammoin=15 ammo=45
  mode=0 x=78 y=45` and no `rp.lua` hook error. Probes: `drop_probe.lua`, `drop_live_probe.lua`.


## 2026-10-21 — Cline — dedicated: sayutf8/sayteamutf8 hooks and Trigger_Use re-use

- **Chat hooks:** `cs2d_sv_chat` only ran `say` / `sayteam`; `sayutf8` / `sayteamutf8` were
  implemented but never called (WQ2D registers `menu.sayutf8` / `menu.sayteamutf8` for its chat,
  commands and RP menu). A probe on `cs2d_dedicated.exe` showed `say` runs first, then `sayutf8`
  with the UTF-8 bytes hex-encoded as `xHH` (`"hello"` = `x68x65x6Cx6Cx6F`); a return 1 from the
  first skips the second, and either return 1 suppresses the message. `src/cs2d_sv_game.c` now
  builds the hex string (`sv_chat_hex_utf8`) and calls both hooks in that order.
- **Trigger_Use one-shot:** `cs2d_sv_use` mode 100 called `cs2d_sv_trigger_at_position`, which
  toggled the button's own state on every press; with no reset the client then rejected it forever
  (state must be 0). fun_roleplay's `rp_ticket` button at (275,154) (ints[3] = 0) could only be
  used once. The dedicated (`FUN_140138a40`) does **not** toggle the button: it validates
  type/state/team/reach/re-use-delay and calls `Te.triggerentities(1)` (the button's `trigger`
  list). Implemented that, plus the `ints[3]` re-use delay (`sv->entity_reuse_ms`, reset at round
  start / map load) and the same local delay on the client (`cs2d_ents_tick_use_cooldowns`).
- **Test runner:** `main` had a hardcoded `total = 112` that had to be edited for every new suite.
  It now uses `RUN_TEST(test_x)`, which counts the executed suites, prints `[FAILED] test_x` for a
  failing one, and accepts a suite-name filter (`test_cs2d.exe trigger_use`, ASCII
  case-insensitive; exit 2 when nothing matches).
- **Verified:** `test_cs2d.exe` -> **113 / 113** (new `test_dedicated_trigger_use`; chat hooks
  added to `test_server_phase4_lua`). Filtered run `test_cs2d.exe Trigger_Use` -> 1 / 1.
  - Live, our server + our client (`connect <ip> <port> <password>`; the join needs
    `sv_password`): `tools/agent/out/br_live5_srv.log` shows three `usebutton 275 154 state=false`
    in a row and the client keeps the button at state 0 (fun_roleplay `rp_ticket`).
  - `tools/agent/out/say_live_srv.log`: a real client message hits `say` then `sayutf8`
    (`x68x65x6C...`), and `sayteam` then `sayteamutf8`; the bot probe (`say_srv4.log`) matches the
    original's order/hex output.
  - Probes: `tools/agent/lua/{say,say_suppress,trigger_use,button_reuse}_probe.lua`.


## 2026-10-20 — Cline — dedicated: NPC Lua hooks wired (objectdamage/objectkill/hit/die/hitzone/projectile_impact)

- **Mapping (help.php `lua_hooks`, Object category):** the hooks that touch NPCs are
  `objectdamage(id,damage,player)`, `objectkill(id,player)`, `hit(...,object)`,
  `die(...,killerobject)`, `kill(...,killerobject,assistant)`, `hitzone(image,player,object,
  weapon,x,y,damage)` and `projectile_impact(...,mode,projectileid)` (mode 3 = NPC). `hit` and
  `hitzone` mention NPCs explicitly; `die`/`kill` carry the object that caused the death.
- **Dano/morte:** `cs2d_sv_damage_ex(sv, victim, attacker, dmg, weapon, object_id)` and
  `cs2d_sv_kill_ex(..., killer_object)` were added; `cs2d_sv_damage` / `cs2d_sv_kill` keep object 0.
  NPC melee/ranged, turrets, mines, laser mines, gate fields and barbed wire now pass their Tdo id.
  The `hit` hook has all 7 arguments and the `kill` hook all 7 (assistant); `kill` now only runs when
  a player caused the death (an NPC kill fires `die` only, with its id as killerobject).
- **objectupgrade:** was declared but never called; `cs2d_sv_object_upgrade` now calls it with
  (object, player, progress, total) and a return of 1 cancels the step (and the money charge).
- **Hitzone:** `cs2d_sv_hitzone_at` implements the `imagehitzone` rectangles (mode > 100 blocks,
  mode 2 HUD images skipped) and calls the 7-argument `hitzone` hook. Used by player/turret shots
  (`sv_trace_shot`) and NPC shots (`ai_npc_shoot`: player 0, NPC object id, weapon 0xf8/0xf9).
- **projectile_impact:** now fired with mode 0 wall / 1 player / 2 hostage / 3 NPC / 100 ground and
  `projectile` moved from spawn to detonation. Only RPG-7, rocket launcher, snowball and gut bomb
  collide with players/hostages/NPCs (help.php).
- **Verified:** `test_cs2d.exe` -> **112 / 112** (new `test_dedicated_npc_hooks`: hit object id, die
  killerobject without kill, cancellable objectdamage, objectkill, NPC shot → blocking hitzone with
  player 0 / NPC object id, projectile_impact mode 3).


## 2026-10-20 — Cline — dedicated: bot team broadcast and NPC AI (client sync / walls / damage)

- **Bot team:** `cs2d_sv_bot_add` set `pl->team` but never sent `[20,id,team,look]`; `[248]` has no
  team, so every client kept the bot at team 0 (Spectator) while the server already ran the AI. The
  bots therefore "walked and shot as spectators". `bot_add` now broadcasts `[20]` (and marks
  `team_chosen`). Regression: `test_dedicated_bot_and_npc_ai` scans the connection buffer for both
  `[20]` records.
- **NPC desync/walls:** the server sent `m_rot * 182.04445` in `[36,id,7]`, but `Tdo.getdata` case 7
  reads the ushort as **degrees** (`FUN_140283a10` writes `(int)m_rot`). The clients spun instead of
  walking while the server chased, which showed the NPC far away but taking damage. The old server
  motion also ignored the original tile waypoints. `src/cs2d_sv_ai.c` now ports `FUN_140283f20`:
  target picker `FUN_140283760` (405x220 + clear 32 px `FUN_14011c3a0` line, team > 0 only), path
  step `FUN_140283a10` (45 px probe + walkable adjacent tile, tile-centre `m_x/m_y`, raw degree
  `m_rot`), motion `FUN_140283120` without the extra map test, and the server-only attack timer.
  An NPC now never targets or damages a spectator or a player behind a wall.
- **Indestructible NPCs:** player shots never reached them: `cs2d_dynobj_hit_debris(30)` is false and
  `sv_trace_shot` only reported blocking tiles. The trace now stops on an NPC tile and
  `cs2d_sv_shot_at_building` accepts typ 30; `cs2d_sv_object_damage` no longer clamps NPC health to
  the building table (10 HP), and the join snapshot sets the NPC `m_rot`.
- **Attack values from `FUN_1401ed310`:** zombie 10, headcrab 2, snark 1, Vortigaunt 15 melee / 40
  ranged (400 px), soldier 5 (400 px); the old far damage came from applying damage from the server
  position at any distance.
- **Verified:** `test_cs2d.exe` -> **111 / 111** (bot `[20]`, degree `m_rot`, tile-centre waypoint,
  wall blocks target/damage, shot damages and kills an NPC).


## 2026-10-20 — Cline — dedicated: bot AI (`bots/Standard AI.lua`) and NPC AI

- **Finding:** CS2D's AI is shipped as Lua, not native code. `bots/Standard AI.lua` +
  `bots/includes/*.lua` are in the repository; the client loads the script at startup
  (CS2D.exe `_global/140190000.c:2263`) and the engine calls its globals per bot. The dedicated has the
  same `ai_*` engine API (client dump `cs2dlua_ai_*`), which was still a set of no-ops in our port.
- **Bot engine (`src/cs2d_sv_ai.c`, new):** loads the script from `cs2d_sv_lua_init`; calls
  `ai_onspawn` on spawn, `ai_update_living` every frame (after the skill-based rotation) and
  `ai_update_dead` every second; `ai_hear_radio` / `ai_hear_chat` from `[240]` modes 3 and 1/2.
- **`ai_*` API:** `ai_move` (movement model of movement.md), `ai_goto` (BFS over floor + collision,
  0/1/2), `ai_aim`/`ai_rotate` (smooth turn: 8 + 2*skill deg/frame, calibrated), `ai_findtarget`
  (420x235 + skill, LOS with `sv_fow`), `ai_freeline` (40 px ray), `ai_attack`/`ai_iattack` (server
  resolves the hit for bots; auto reload/weapon switch), `ai_buy`, `ai_selectweapon`, `ai_reload`,
  `ai_use`, `ai_respawn`, `ai_drop`, `ai_say`/`ai_sayteam`/`ai_radio`, `ai_spray`, `ai_build`,
  `ai_debug`. `setentityaistate`/`entity("aistate")` now store/read a per-entity value reset on round
  start. `randomentity` returns **x, y** (or -100,-100) and honours the AI-state/int0 filters.
  `player(0, prop)` returns false for non-table props.
- **Bots:** `bot_count`, `bot_autofill`, `bot_keepfreeslots`, `bot_jointeam`, `bot_skill`,
  `bot_weapons`, `bot_freeze`, `bot_prefix`, `debugai` defaults and behaviour; flashbang sets
  `ai_flash = Rand(4,6)` for bots in 640x480 with a free line (Tpro.c:3236).
- **NPC AI:** `Tdo` 30 chases the closest living player with the per-type speed/turn of dynobj.md,
  sends `[36,id,7]`/`[36,id,9]` and applies server damage (calibrated values).
- **Verified:** `test_cs2d.exe` -> **111 / 111** (new `test_dedicated_bot_and_npc_ai`: script load,
  smooth aim, BFS path, target/LOS, iattack damage, flash, AI state, bot_count, NPC chase+damage).
  Live smoke on `cs2d_server.exe -offline -map de_dust2`: two bots spawned, roamed, met, fought
  (one died), round restarted; no Lua errors. `sys/server.cfg` restored, `git status sys/` clean.
