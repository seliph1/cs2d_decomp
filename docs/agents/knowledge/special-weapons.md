Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Special weapons (REA/dump, CS2D.exe)

- Attack dispatch `FUN_14021b910`: Laser (45) / Flamethrower (46) = normal shot (`FUN_1402164d0`) with Tshoot style 2 / 3;
  RPG 47, Rocket 48, Grenade Launcher 49 (and grenades, HUD slot 4/6) = `FUN_1402193e0` → `FUN_140217d80` (throw, `[27]`
  only, no `[7]`); Portal Gun 88 = `FUN_1401fc9f0(id, 1, mouse world x, y)`, fire timer 500 ms; attack2 = mode 2.
- Client stats (`init_weapons`, weapontables.py): Laser 15|45 range 1000 rate 30; Flamethrower 100|200 range 55;
  RPG 1|2 range 700; Rocket 20|60 range 700; GL 20|60 range 425; Portal Gun clip 0 ammo −1 (no ammo) slot 1.
- Throw distance: grenades + GL (bitmask 0x12009000001 from 0x31: 49, 73, 76, 86, 89, plus 51..54, 72) = distance from
  screen centre to cursor − 12, max 385; rockets = min(range, 385). Start = pos + 10 px along the aim. Launcher: weapon
  sound, fire timer rate × 39 ms, one bullet.
- RPG backblast: Tp 3 smoke at 25 px ahead and Tp 3 / Tp 14 at 20..50 px towards aim + 195°, light.
- Laser (Tshoot style 2): `Tp.CreateLine` muzzle → end, width 3, alpha Rnd(0.4, 0.6), −0.03/frame; colour by mode:
  0 green (R0-50 G100-255), 1 red (R100-255 G0-50), 2 yellow, 3 blue (G25-75 B100-255); sparse Tp 14 glows, sparkles.
- Flame (style 3): Tp 23 every 5 px past 20 px, fx/fy aim ± 50°, colour by distance: < 30 blue, < 60 purple, < 120 orange.
- Portal Gun: client `[50, mode, x i32, y i32]` reliable; server relays `[50, player, mode, x, y]` (11 bytes) → recoil,
  Tp 0x1d flash 24 px ahead (orange 200,100,0 / blue 50,100,255), portalgun sound. Portal = Tdo 22 / 23 on a wall tile
  within 457 × 272 px; drawn (`Tdo.draw_ground` 0x16 / 0x17): shadow 0.5 at 0.4, flare1 spinning (ms + rot) × 0.05,
  8 flares orbiting 10 px, scale 0.4 + 0.3 |sin(ms / 30)|.

## Items

- Ids (init_weapons names): 64 Medikit, 65 Bandage, 66 Coins, 67 Money, 68 Gold, 89 **Satchel Charge** (not Medikit).
- Pickup (`FUN_14020ce60`): 61 / 62 `Titem.refillammo` (reserve of HUD slot 1 / 2 to max), 64 health = max, 65 +20
  (≤ max), 66 / 67 / 68 money +100 / +500 / +1000 (≤ 16000, `Thud_msg.add_money` for the local player). Not kept.
- Spawned item ammo (`FUN_14011ba70`): HUD slot 4..6 except C4: ammoin clamp(1, max) (1 when < 0), ammo 0; others:
  negative = table default, else clamp(0, max). Join snapshot (case 3) keeps the raw values.
