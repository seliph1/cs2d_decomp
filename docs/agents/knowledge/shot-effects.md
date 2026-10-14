Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Shot effects

Source: REA `FUN_140148f40`, plus frame captures.

- **Muzzle flash** (`incbindata/flare_x.png`):
  - Scaled by `DAT_140dde9d8` (0.1, set at `0x140306d2b`) × 5 and stretched along the aim.
  - Life starts at 1 and drops 0.4 per frame.
  - A big yellow light surrounds the shooter.
  - Flash object: +0x08 angle offset, +0x0c x-scale, +0x10 life, +0x18 player, +0x20 distance,
    +0x24 angle, +0x28 mode (1 = attached to a player), +0x30 object. Constructor `FUN_14001e300`.
- **Tracer:** a thin yellow line from the muzzle to the impact that leaves `glowdot` sparks along the path.
  Sparks also appear at wall hits.
- **Shells** (`gfx/shells.bmp`, 4 frames of 8×8): ejected to the right of the aim; they stay on the floor.
- **Muzzle offset table** `0x1409f5d20` (`init_weapons`, read with `tools/agent/weapontables.py`; not used yet, we
  still draw at 30 px for primary weapons and 22 px for pistols): pistols 1..6 19; M3 / XM1014 28; MP5 22, TMP 20, P90 22,
  Mac 10 20, UMP45 22; AK-47 26, SG552 30, M4A1 26, Aug 30, Scout 30, AWP 33, G3SG1 / SG550 30, Galil / Famas 24;
  M249 28; Laser 30, Flamethrower 25, RPG / Rocket / Grenade Launcher 30; Knife, Machete, Wrench, Claw 25; Chainsaw 30;
  Portal Gun 30; M134 32, FN F2000 23, 92 27.
- **Firearm visual recoil kickback** (REA `init_weapons` `0x1402305db..0x1402340a9`, `Tpl.attack` `0x1402166a7`,
  `Tpl.updateAll` `0x14021d1b1`, `Tpl.draw` `0x14012131c..0x1401213cc`):
  - Per-weapon integer kickback table at `0x1409eda18`:
    - `1` px: MP5 (20), TMP (21), P90 (22), Mac 10 (23), UMP45 (24), SG552 (31), M4A1 (32), Aug (33), M134 (90), FN F2000 (91)
    - `2` px: USP (1), Glock (2), P228 (4), Elite (5), Five-Seven (6), AK-47 (30), Scout (34), Galil (38), Famas (39), M249 (40), RPG (47), Rocket Launcher (48), Grenade Launcher (49)
    - `4` px: Deagle (3), G3SG1 (36), SG550 (37)
    - `5` px: M3 (10), XM1014 (11), Portal Gun (88)
    - `6` px: AWP (35), Laser (45)
  - On shot, `player->recoil` (`Tpl +0x308`) is set to `(float)visual_recoil[id] + Rnd(0.0, 1.0)`.
  - Each 60 Hz frame, `player->recoil` decays by `0.5` (`30.0 px/s`, `0x140bd69e8 = 0.5f`) down to `0.0`.
  - In `Tpl.draw`, the player sprite is displaced backward along `-angle`:
    `sx -= sin(angle) * recoil`, `sy += cos(angle) * recoil`.
- **Melee weapon swing animation** (REA `Tpl.slash` `0x14018cebc`, `Tpl.attack2` `0x1401fdd37`, Wrench `0x1401fad67`,
  `Tpl.updateAll` `0x14021d68a..0x14021d6d2` & `0x14021da21`, `Tpl.draw` `0x14012146b` & `0x14012299b`,
  `Tp.create` `0x14004b4e0`, `Tp.drawAll` `0x140265220`):
  - On any melee swing except Chainsaw (`85`, `0x14018ce92`), `player->slash_timer` (`Tpl +0x31c`) is set to `8.0f`.
  - `player->draw_angle` (`Tpl +0x24c`) is `player->angle + 6.0f * player->slash_timer` (`0x140bd69e4 = 6.0f`),
    starting at `angle + 48°` and sweeping linearly back to `angle` as `slash_timer` decays by `1.0` per 60 Hz frame (`60.0/s`).
  - Both the torso sprite (`0x14012146b`) and held weapon sprite (`0x14012299b`) use `draw_angle` for rotation and forward offset
    (`16.5 px` for firearms `0x14012168e`, `13.5 px` for melee/bomb `0x1401229b5`, `12.5 px` for grenades `0x1401227ab`).
  - Spawns `Tp` type `13` (`gfx/knifeslash.bmp`, `0x140a0dcd8`, `maskMode = 3`, `midhandle = 1`) at `(pos.x, pos.y)`
    rotated by `angle`, drawn with additive blend (`LIGHTBLEND = 4`), alpha `1.0` fading by `0.07` per 60 Hz frame (`4.2/s`).
