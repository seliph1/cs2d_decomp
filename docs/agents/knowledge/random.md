Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## TRandom (BlitzMax PRNG) - the shot spread seed

The client does not spread its pellets with a per-frame random: every `Tpl.shoot` (0x140209810) reads a *global table*
that the server's join settings seeded, which is why all clients spread exactly like the server.

- **Send:** the seed is an `i32` in the settings `[252,6]` (0x1402d0560: `Tudp_p.rint`, then
  `TRandom.GenerateSeed`), right after the Lua bind list (`n` u16 binds, then the seed, then `sv_offscreendamage`,
  the leaderboard flag and the physics flag; 1402d0000.c:1219-1230).
- **`TRandom.GenerateSeed(seed)` (0x14000fe90, objdump):** `B = abs(seed)`; `SeedTable[0] = (B + 1) % Range`;
  `aX = SeedTable[0] + 1`; `for i in 1..999: SeedTable[i] = (B + aX) % Range; aX += SeedTable[i]`.
- **Table / constants:** 1000 ints (the loop ends at `data + 0xfa0`), `Range` = **123**
  (`mov dword [0x140dde720], 0x7b` at 0x14030720e), `B` / `aX` at 0x140dde728 / 0x140dde724, the array pointer at
  0x140dde730 (all `.bss`, set by the BlitzMax globals init).
- **`TRandom.GetNumber(index, range)`** (0x14000ff50) = `SeedTable[index] % range`, **-1 when index >= 1000**.
- **Use:** `Tpl.shoot` clamps `Tpl.random` to >= 1 and adds `seed % (2·acc) - acc` (plus two more moduli from the
  attack's own spread args), then advances `Tpl.random = (Tpl.random + 1) % 200` - once per pellet. `acc` is the
  weapon table `0x1409f5d18` (below).
- **The accuracy table `0x1409f5d18` (complete, extracted with `tools/agent/weapontables.py`):**
  1..6 (pistols) 0, 10 M3 **10**, 11 XM1014 **13**, 20 MP5 2, 21 TMP 4, 22 P90 3, 23 MAC10 4, 24 UMP45 3,
  30 AK-47 3, 31 SG552 0, 32 M4A1 2, 33 AUG 0, 34 Scout 0, 35 AWP 0, 36 G3SG1 1, 37 SG550 1, 38 Galil 1,
  39 Famas 1, 40 M249 8, 90 M134 5, 91 FN F2000 2, and an id 92 (2) we do not model. Locked by `test_weapon_modes`.
- **Only the shotgun spreads.** `Tpl.attack` (0x14021a6xx) reaches `Tpl.shoot` for the M3 / XM1014 alone
  (`typ - 10 < 2`: five pellets at 0/-5/+5/-9/+9); every other weapon fires through `Tpl.shoot_recoil`
  (0x140209a90) = `Tshoot.Shoot` at the angle it got, i.e. without any accuracy turn, and the knife / claw strong
  slash calls `Tpl.shoot` with `p6 = p7 = 0` and an accuracy of 0. The other table entries are only reachable
  through `Tpl.shoot` (nothing else in `Tpl.attack` calls it), so they never affect a player shot.
- **Implementation:** `src/cs2d_random.c` (`cs2d_random_seed` / `cs2d_random_table` / `cs2d_random_get`); the seed is
  parsed into `cs2d_client_t.net_seed`; `test_random_and_dropwep` locks four seeds. Live check (`-offline`,
  de_dust2, `CS2D_NET_LOG`): the dedicated sent seed **33081** -> `SeedTable[1] = 113`, matching the algorithm.
