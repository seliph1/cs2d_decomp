# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-15 — Claude — bug: a de_dust2 spawn override teleported the local player
- Removed a leftover hardcoded placement in `game_load_map` (`cs2d_scene_game.c`): after `cs2d_game_start_round`
  had already spawned everyone on a spawn of their team, it moved the local player to tile (31, 44.5) - the CT
  side. A Terrorist holding the C4 was therefore yanked from the T spawn (tiles x 79..82) to the CT spawn area on
  every map load: the reported "teleporting with the bomb in hand". It came from commit f6d6bad (2026-10-10),
  not from the 2.7 work.
- The player now stays where `cs2d_game_start_round` put them. The client's only other position writes are the two
  tile-entry teleports (a `Func_Teleport`, and portals / teleporters) and de_dust2 has neither: 1403 entities, no
  type 70 and no Env_Building (checked with a throwaway dump tool, not committed).
- Verified: new assertion in the game-flow test (every player stands on an Info_T / Info_CT tile of their own team,
  so nothing may move them afterwards), suite **59/59**; `--screenshot` de_dust2 exit 0, `git status sys/` clean.

## 2026-10-15 — Claude — the NPC attack spin decays per draw (OPEN_ISSUES 2.7, 5th pass)
- `update_npc` no longer decays `rotvar`; `render_npc` does it (`rotvar -= 5`) after drawing, exactly where
  `Tdo.draw_ground` case 0x1e does it. Consequences, both faithful: a **culled** NPC keeps its spin (the early
  cull return happens before the decay) and the spin follows the drawn frames, not our fixed 60 Hz accumulator.
- `cs2d_render_dynobjs` lost its `const` (`cs2d_render.h`, the 3 calls in `cs2d_scene_game.c` pass a non-const
  world), since the draw now writes one field, like the original.
- Verified: the existing NPC assertions plus a new one (`rotvar` stays 20 across 30 simulated frames), suite **59/59**;
  `--screenshot` de_dust2 exit 0.

## 2026-10-15 — Claude — Tdo.shoot: the shot spread (+ the turret reach) (OPEN_ISSUES 2.7, 4th pass)
- `cs2d_fx_tdo_shot_angle` (cs2d_fx.c): `Tdo.shoot` 0x140209b50 hands `Tshoot.Shoot` the angle **rot + Rnd(-3, 3)**
  (the double constants 0x140bd6c18 / 0x140bd6bb8 read from CS2D.exe), the same function that sets c_dist 400
  (0x140ddea0c) and c_offset 15 (0x140ddea08). Applied in the NPC shot (cs2d_client.c kind 5, soldier tracer and
  Vortigaunt beam) and in the turret burst.
- The turret tracer reach becomes the `Tdo.shoot` c_dist **400** (and c_offset 15) instead of the calibrated 300:
  `FUN_14028c630` fires through the method at +0x120 with one call per barrel (0 / +-6 / 0,+-8 px) and passes no
  distance, so Tdo.shoot's 400 applies.
- Verified: `test_tdo_shot_spread` (200 draws stay in [rot - 3, rot + 3] and vary), suite **59/59**.

## 2026-10-15 — Claude — Tp 12 debris stops at walls (OPEN_ISSUES 2.7, 3rd pass)
- `cs2d_fx_update` takes the map (`&client->game.map`; NULL in the tests) and, after moving a `CS2D_FX_FRAGMENT`
  (Tp 12), looks the tile up in `cs2d_map_prop_class(cs2d_map_get_tile_mode())`: a wall (class 0) zeroes the speed
  (the fragment stops where it is) and a deadly tile (class -1) halves the alpha - Tp.draw 0x140263cc0 case 0xc.
- Verified: `test_fragment_walls` (a fragment that lands in the wall column stops with its x unchanged, the same
  fragment in the deadly column keeps its speed at half the alpha, no map = no check), suite **58/58**.

## 2026-10-15 — Claude — NPC death sounds + the `particles` config (OPEN_ISSUES 2.7, 2nd pass)
- `cs2d_dynobj_kill` (kind 6) + `npc_die_sound`: the die sound of `Tdo.kill` case 0x1e, all five NPC types
  (zombie `zm_die`, headcrab / snark / vortigaunt `*_die`, soldier = a random `sfx/player/die[1..3]`), played by
  `cs2d_client.c` at the NPC. Resolved from the sound loader 0x14030cc00: in each block the name `lea` belongs to
  the sound stored in the *next* global, and the literals are static BBStrings in `.data` (UTF-16 at +0x10), read
  straight from the file (KNOWLEDGE sound.md "How to resolve a global to a file").
- `particles` (`DAT_140ddf182`) and `gore` (`DAT_140ddf192`) are parsed from sys/config.cfg; `cs2d_config_particles()`
  feeds the building-hit fragment count and the wall-ricochet gate (was a fixed 4).
- Verified: `test_images_and_npcs` (die sound of every NPC type, the silent invalid type, the soldier's die1..3),
  the particles / gore config defaults in `test_item_and_player_shadows`, suite **57/57**; the zombie sheet
  (64 x 96 = 2 x 3 cells of 32 x 32) checked against the BMP header, no code change was needed.
- Left (OPEN_ISSUES "Construction"): the gore-gated ground blood decals and the snark's particle fountain, the
  remaining `3 < particles` gates, fragments stopping at walls, the vortigaunt beam / soldier tracer.

