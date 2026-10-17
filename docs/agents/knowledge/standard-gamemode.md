Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Standard game mode (`sv_gamemode 0`)

Source: `cs2d_dedicated.exe` (`objdump -d -M intel`, addresses below; the Ghidra dump of the same
functions is `docs/dump/dedicated/FUN_14015c*.c`), `help/howtoplay.txt`, `sys/language/EN-English.txt`.
Code: `src/cs2d_sv_game.c` (`cs2d_sv_round_end_ex`, `cs2d_sv_check_win`, `cs2d_sv_step_round`,
`cs2d_sv_count_missions`, `cs2d_sv_assign_vip`, `cs2d_sv_vip_check`, `cs2d_sv_hostage_escaped`),
test `test_standard_gamemode`.

### Round end (`FUN_14015c840`)

- Signature `FUN_14015c840(type, mode, delay_ms, player)`. **`type` picks winner and MVP**
  (`FUN_14015c080`), **`mode` is the reason byte** of `[226]` and the reason passed to the next round.
  Every standard mode call in the binary passes `type == mode`. `type == 0` is the silent reset
  (`0x14019f9c6`: `round_end(0, -1, 3000, null)` - clears the pending state, sends nothing).
- `[226, 1, winner u8, mvp u8, mvp_text (lstr), reason u8]` reliable to every joined player;
  `winner` = `_DAT_1407dfe78`, `mvp` = `_DAT_1407dfe7c`, reason = `param_2`. `DAT_1407dfe6c` = the
  pending reason, `DAT_1407dfe70` = the delay (the intermission before the next round).
- MVP text (`FUN_14015c840` switch, literals in `.data`): default ` - `; 1 / 2 / 22 `for most
  eliminations`; 11 `for escaping from the terrorists`; 12 `for killing the VIP`; 20 `for detonating the
  bomb`; 21 `for defusing the bomb`; 31 `for rescuing most hostages`; 60 `for most infections`;
  61 `for killing most zombies`.
- `FUN_14015c080`: **winner from the type** - 1, 10, 12, 20, 30, 40, 50, 60 → T; 2, 11, 21, 22, 31, 41,
  51, 61 → CT; 3 (draw), 4 (game commencing) and 5 (round restart) → 0. The **MVP** is
  `kills * 2 + assists` of the winning team for 1 / 2 / 60 / 61, the planter for 20, the defuser for 21
  and the best rescuer for 31 (`r9` of the call site); types 10, 11, 12, 22, 30, 40, 41, 50, 51 have no
  MVP. The MVP gets `+1` mvp count via `FUN_140159840(id, 10, mvp+1)` = `[42, id, 10, n]` (mode 10 = the
  mvp field at `Tpl +0x294`). Teams are compared exactly (`+0x230 == 2`), so a VIP (3) is never MVP.
  `FUN_14015c080` is gated on `DAT_1405a64a8 == 1` (not identified; 1 in the shipped build).
- After the banner `FUN_14015c840` calls `FUN_140123530(reason)`, the round end **sound** switch.
- **Reason ↔ message** (`sys/language/EN-English.txt` 0576..0594, in this exact order - 19 reasons for
  19 messages): 1 Terrorists Win, 2 Counter-Terrorists Win, 3 Round Draw, 4 Game Commencing,
  5 Round Restart, 10 V.I.P. has been assassinated, 11 V.I.P. escaped, 12 V.I.P. did not escape,
  20 Bomb detonated, 21 Bomb has been defused, 22 Target has been protected, 30 Hostages have not been
  rescued, 31 Hostages have been rescued, 40/41 CTF, 50/51 Domination, 60/61 Zombies.
- **Team score per reason** (`cs2d_sv_round_start` / the client): T mask `0x1004010040101000`
  = {12, 20, 30, 40, 50, 60}, CT mask `0x2008020080000804` = {2, 11, 31, 41, 51, 61} plus the explicit
  21 and 22; `0x30` (reasons 4, 5) resets both scores and every player's score/assists/deaths/mvp.
  A round won by elimination (1) or by the VIP death (10) therefore adds **no** team score - original
  quirk, kept.


### Round end conditions (`FUN_14015ce30`, called with `param_1 == 0` from `0x14015085b` / `0x140164da3`)

- `FUN_140026150` = players with `team == 1` (T), `FUN_140026210` = players with `team > 1` (CT incl.
  the VIP 3). Both alive-or-dead.
- Standard/TDM (`mode->balance == 1 && mode->respawn == 0`):
  - `T_total == 0 || CT_total == 0` → `round_end(3, 3, 3000)` (draw). A one-team server never times out.
  - alive T + alive CT == 0 → bomb planted ? `(1,1,5000)` : `(3,3,3000)`.
  - alive T == 0 → bomb planted ? *continue* : `(2, 2, 5000)`.
  - alive CT == 0 → `(1, 1, 5000)`.
  - The `param_1 == 1` branch (warmup, `round_end(4, 4, 3000)`) has no caller in this build.
- `PTR_DAT_1405ac770 +8` is the "C4 planted" flag used by all of these checks.

### Time out (`0x1401a3128`, phase 1; `0x1401a2fd9` is phase 2)

- `mp_roundtime` (`0x1405a6640 +4`, minutes, `100` = endless → `0x1401a3137`). `ecx` = the elapsed
  seconds; remaining minutes/seconds go to `0x1407dfd74` / `0x1407dfd78` (the HUD clock).
- Fires only when: gamemode 0, remaining <= 0, no round end pending, and **the C4 is not planted**.
  The winner comes from the map missions (`DAT_1407dfac*`, `0x1401a31e0`): `vips > 0` → `(12,12,5000)`;
  `hostages > 0` → `(30,30,5000)`; `bombspots > 0` → `(22,22,5000)`; none → `(3,3,3000)`.
  Non-standard: Zombies (`sv_gamemode 4`) → `(61,61,5000)`.
- `DAT_1407dfac8 / 1407dfac4 / 1407dfac0` are the counts behind the Lua `map("mission_vips" /
  "mission_hostages" / "mission_bombspots")` queries (string compares at `0x1400c25d8 / 2641 / 26b1`
  against `0x1405c8790` `vips`, `0x1405c87d0` `hostages`, `0x1405c8890` `bombspots`).

### When `FUN_14015ce30` runs: player events, not the tick (measured 2026-10-16)

- The check has **no per-frame caller**. Its call sites are `0x14015085b` (inside `FUN_1401502a0`, the
  player removal / kick function, gated `DAT_1407dfe48 != 0` then `DAT_1407dfe6c == 0`) and `0x140164da3`
  (the team change inside `FUN_1401624c0`), plus tail calls that end with
  `if (DAT_1407dfe6c == 0) return FUN_14015ce30(0)`: `0x140160526` (the death helper `FUN_140160500`,
  `cmp [pid+0x230], 3` = the VIP), `0x1401647c4`, `0x1401840d1` (both right after a `FUN_140159840(id, 5, ..)`
  playerdata update) and `0x14015d200 / 0x14015d220 / 0x14015d266` (inside the round-end function itself).
  The per-frame tick (`0x1401a2xxx`) only runs the freezetime phase, the C4 step, the VIP objective and
  the time out (`0x1401a3128`) - never the elimination rules.
- **Measured cadence with `cs2d_dedicated.exe` 1.0.1.8 p1** (`tools/agent/round_timing.py` timestamps the
  console, `tools/agent/lua/round_probe.lua` prints `SEC n=<players> [id team hp]` once per second):
  - 0 players: no round end / round start for 18 s (the round just runs on).
  - 1 player joining (spectator) and then picking CT: no round restart, and the team change does **not**
    trigger the "Game Commencing" restart while that player is alive.
  - that lone player killed by `parse("killplayer 1")`: `ROUND end mode=3` (draw) at once, `ROUND start
    mode=3` 3020 ms later, then nothing more for the rest of the run (the player respawned and kept the
    round). The 3000 ms is the draw delay, not a separate config.
  - Our dedicated before the fix drew + restarted every ~3000 ms in all of those states (each round lasted
    ~20 ms), which is what the user noticed; the fix moves the check to the same events.
- Consequences for the port: `cs2d_sv_check_win` (the port of `FUN_14015ce30(0)`) is called from
  `cs2d_sv_kill` (after `cs2d_sv_vip_check`, so a V.I.P. death keeps reason 10) and from the removal paths
  (`sv_remove_player`, `cs2d_sv_kick`); `cs2d_sv_step_round` keeps only the freezetime, the C4/VIP
  per-frame work and the time out. `cs2d_sv_bomb_explode` ends the round (20) *before* the blast damage,
  because the blast kills would otherwise run the check first (`DAT_1407dfe6c == 0` gate).
- "Game commencing" (`FUN_1401624c0` `LAB_14015cff0` = `0x14015cff0`) = `round_end(4, 4, 3000)` on a team
  change while the standard mode has no real match: `t + ct == 0`, `t + ct == 1` and the single player is
  **not** alive (the vtable `+0x90` "alive" test), 1 vs 1, or 2 vs 0 / 0 vs 2. The Changelog entry
  `"Game Commencing"-restart occured with 1 player who is alive already` is the fix for the missing alive
  test, so a lone alive player keeps playing.



- 0 = freezetime, 1 = playing (the round clock and the time out), 2 = round decided (the clock runs on,
  the time out no longer ends it). Read from a round object (`vtable +0xf0`, `0x1401c55d8`).
- **0 rejects movement** (`0x1401c9a02`: the position validation bails out before the collision work) and
  attacks (`0x140191ace`); the round clock starts when the phase becomes 1 (`0x1401a326e`, followed by a
  random `Rand(1,4)` round start sound `0x1403977c0`).
- Freezetime length = `mp_freezetime` (`0x1405a6638 +4`, seconds; `0x1401a2eb1`).

### V.I.P. (AS maps)

- `mission_vips > 0` (Info_VIP) → one CT becomes team **3** each round: `0x1401717a1` / `0x140174254`
  set `+0x230 = 3` and the armor object `+0x270` to **0xc8 = 200**; the flag bitmask around the write
  (`test bl, 2 / 4 / 8 / 0x10` = CT / VIP / ...) belongs to the spawn/team setup.
- `help/howtoplay.txt`: "One of the counter-terrorists will start as V.I.P.! This player can't buy any
  equipment. He just has a knife, a pistol and extra armor and he has to reach the escape point before
  the round time elapsed." → the CT spawn list (pistol + knife) is his inventory. The Lua `player`
  docs list `team: 0 spec, 1 t, 2 ct, 3 VIP (ct)`.
- **Death** (`0x14015d230`, `0x140160500`: `cmp [pid+0x230], 3`) → `round_end(10, 10, 5000)`
  (T win, "V.I.P. has been assassinated").
- **Escape** (`0x140188167`) → `round_end(11, 11, 3000)` (CT win, "V.I.P. escaped") when the VIP stands
  on an Info_EscapePoint (`0x158`, entity flag).
- **Time out** (`0x1401a3446`) → `round_end(12, 12, 5000)` (T win, "V.I.P. did not escape").

### Hostages (CS maps)

- Rescuing one hostage awards the follower `+2` score and every CT `$1000`; `[29, h_id, 2, tx, ty]`
  (region `0x140189xxx`).
- `FUN_14015ffb0`'s tail (`0x14015ff9f`): `rescued / mission_hostages > 0.5` (the float **0x1406da614 =
  0.5f**) → `round_end(31, 31, 3000)` (CT win). **More than half of the map's hostages**, not all of
  them. `esi` (nonzero → return) is a "already counted" guard.
- Time out (`0x1401a345a`) → `(30, 30, 5000)` (T win, "Hostages have not been rescued").

### Bomb (DE maps)

- Plant `[30,1]`, abort `[30,2]`, complete `[30,3, tx, ty, id]`; defuse `[26,3]`, abort `[30,4]`,
  complete `[30,5, id]`; explosion `[30,6, tx, ty]` (`FUN_14016a2d0` / `FUN_14016a700` /
  `FUN_14016ab00`, region 0x1401588b0..0x14016ad00).
- The C4 timer is `mp_c4timer` (`0x1405a65f0 +4`); its step is `FUN_1401a2910` (called every frame from
  the tick, `0x1401a2f5a`), which owns `[0x1407dfe80]` = the planter and clears the HUD clock
  `0x1407dfd74/78` while it ticks.
- Explosion → `round_end(20, 20, 5000)` with `r9` = the planter (`0x14016aca2`); defuse →
  `round_end(21, 21, 5000)` with `r9` = the defuser (`0x14016aa78`).
- The C4 goes to a random terrorist at round start when the map has bomb spots (our `[22]` path).

### Verified in our port (2026-10-17)

- `cs2d_sv_player_t` now has separate `kills` and `hostages_rescued` counters.
- MVP ranking uses `kills * 2 + assists` for elimination wins (1, 2) and Zombies (60, 61), the planter/defuser for bomb (20, 21), and the CT who rescued the most hostages for CS rescue (31). Locked in `test_standard_gamemode`.
- A 2-player 1v1 elimination round (T vs CT) terminates immediately with reason 2 (or 1) and awards the killer the MVP (+1 MVP count, `[42, id, 10, mvp]`), verified without per-frame polling leaks. Locked in `test_standard_gamemode`.
- The `[42, id, 10, n]` mvp award is dispatched on all MVP wins (types 1, 2, 20, 21, 31, 60, 61).
- V.I.P. announcement `[20, id, 3, look]` and 200 armor `[42, id, 3, 200]` verified; VIP players (team 3) are strictly excluded from MVP eligibility matching `FUN_14015c080`.

### Still open (our port)

- The offline client (`src/cs2d_game.c`) still checks its simplified win conditions every frame, so with one
  player it ends the round on its own - its model is not the dedicated's (see OPEN_ISSUES).
- Proxy capture comparison of the VIP announcement sound and exact byte streams side-by-side with 2 live clients.

- Phase 2 also resets the bomb flag (`0x1401c55ec` → `PTR_DAT_1405ac770 +8 = 0`).
