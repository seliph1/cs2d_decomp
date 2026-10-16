Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Standard game mode (`sv_gamemode 0`)

Source: `cs2d_dedicated.exe` (`objdump -d -M intel`, addresses below; the Ghidra dump of the same
functions is `docs/dump/dedicated/FUN_14015c*.c`), `help/howtoplay.txt`, `sys/language/EN-English.txt`.
Code: `src/cs2d_sv_game.c` (`cs2d_sv_round_end_ex`, `cs2d_sv_step_round`, `cs2d_sv_count_missions`,
`cs2d_sv_assign_vip`, `cs2d_sv_vip_check`, `cs2d_sv_hostage_escaped`), test `test_standard_gamemode`.

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

### Round phases (`DAT_1407dfea0`)

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

### Still open (our port)

- `Tpl` has no separate `kills` counter, so the MVP ranking uses `score * 2 + assists` as a stand-in.
- The `[42, id, 10, n]` mvp award also happens for types 1 / 2 / 60 / 61 (the original).
- The `[226]` MVP text of type 22 is "for most eliminations" (the original's literal), even though the
  round ended by time out.
- Not verified on a proxy capture: the exact `[20, id, 3, look]` VIP announcement (our port sends it) and
  the round start sound.

- Phase 2 also resets the bomb flag (`0x1401c55ec` → `PTR_DAT_1405ac770 +8 = 0`).
