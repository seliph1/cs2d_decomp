Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Projectiles and Persistent Fields (Tpro, Tfield)

Source: REA `CS2D.exe` class `Tpro` (`0x14001c900`, `add 0x1400a8b30`, `addground 0x140030120`, `spawnprojectile 0x1400a9190`, `spawngroundprojectile 0x140060ee0`, `remove 0x1402daf80`, `kill 0x1402dd7a0`), `Tpl.attack / throw` (`0x140217d80`). Code: `src/cs2d_projectile.c`, `include/cs2d_projectile.h`.

- **Throw Distance:** Aim distance clamped to max 385 px (`0x181` in `FUN_140217d80`).
- **Throwables:** HE Grenade (51), Flashbang (52), Smoke Grenade (53), Flare (54), Gas Grenade (72), Molotov Cocktail (73), Snowball (75), Air Strike (76), Gut Bomb (86).
  - All disappear from player inventory upon throwing except **Flashbang (52)** which starts with 2 charges (1 in clip, 1 reserve).
- **Rockets / Missiles:** Rocket Launcher (48) and RPG-7 (47) fire as weapon ammo.
- **Flight & Wall Collision:**
  - Bouncing Grenades (HE, Flashbang, Smoke, Gas): bounce off map walls and obstacle tiles. Reflection reverses axis velocity component, reduces remaining `flydist *= 0.5f`, speed `speed *= 0.65f`, and reverses spin `rot_speed = -rot_speed`. Detonates/activates at end of lifetime.
  - Impact Projectiles (Rocket, RPG, Molotov, Snowball, Gut Bomb): stop and detonate immediately upon hitting the first obstacle (map wall, breakable, building, or player).
    - Molotov: explosion + spawns persistent `CS2D_FIELD_FIRE` (radius 45 px, duration 20s).
    - Snowball: spawns snow puff particles.
    - Gut Bomb: spawns blood particles.
    - Gas Grenade: grenade bounce + spawns persistent `CS2D_FIELD_GAS` (radius 64 px, duration 15s).
    - Smoke Grenade: grenade bounce + spawns persistent `CS2D_FIELD_SMOKE` (cover radius 64 px, duration 30s) providing smoke cover.
    - Flare: settles on ground, stays for ~60s emitting bright sparkle particles, then disappears silently.
    - Air Strike: stays on ground for ~7s emitting marker smoke puffs, then creates massive cluster explosions.
- **Visuals:** Items without dedicated projectile sprite fallback to their dropped ground sprite (`tex_weapons_d[type]`).
- **Network Opcodes:**
  - Client -> Server: `[27, type u8, x u16, y u16, dir u16 (deg*65536/360), flydist u16]` (10 bytes).
  - Server -> Client: `[27, player u8, type u8, x u16, y u16, dir u16, flydist u16]` (11 bytes).
  - Spawn flying projectile: `[58, player u8, type u8, id u8, x_bias u16, y_bias u16, flydist u16, dir u16]` (12 bytes, signed coord bias 0x8000).
  - Spawn ground projectile: `[52, player u8, type u8, id u16, x u16, y u16, rot u32]` (13 bytes).
  - Move projectile: `[63, id u8, player u8, x_bias u16, y_bias u16]` (7 bytes).
  - Kill / Detonate projectile: `[41, id u8, player u8]` (3 bytes).
  - Silent kill projectile: `[55, id u8, player u8]` (3 bytes).
  - Snapshot section 6: `[252, 7, 6, count u8, count * { id u8, type u8, player u8, x u16, y u16 }]` (7 bytes each).
- Smoke grenade cloud (Tpro.draw_ground case 0x35): with particles (DAT_140ddf182) >= 3 (shipped 4) one Tp 0x20 per 100 ms tick (DAT_140a1b05c) while on the ground (30 s): Tp 3 creation (grey Rand(0,100), smokepuff frame) then sx Rnd(1.4,1.9), a Rnd(1.5,2), rots Rnd(-0.5,0.5), fx/fy Rnd(-0.2,0.2); a -= 0.005/frame. Particles < 3: one Tp 0x1b sgsmoke.bmp x4 instead. Screenshots of CS2D.exe: drawn over walls and players and over the day shadows (Gen layer SMOKE, after cs2d_light_draw_buffer). Cover radius 64 (Tpro.smokegren_cover).
- Gas grenade (Tpro.draw_ground case 0x48): per 100 ms tick (particles >= 3, else 50%) Tp 3: sx Rnd(2,2.9), a Rnd(0.6,0.8), rots Rnd(-0.5,0.5), fx/fy Rnd(-0.8,0.8), colour (Rand(0,30), Rand(100,200), Rand(0,30)); damage every 3rd tick (300 ms) to players without gasmask within 90 px (dist^2 < 8100), host only. Thrown grenade sprite = held image 0x1409f3ce8[typ] (HE: he.bmp, not grenade.bmp).
- Molotov (0x49): lands -> time 20 s, Tp.explosion(x, y, 45, 5). On the ground (Tpro.draw_ground), every frame: 50% (and particles >= 3, else another 50%) one Tp 14 at Rand(1,360) deg, Rand(1,40) px, list 5: sx Rnd(0.1,0.3), fx/fy Rnd(-0.5,0.5), a Rnd(0.8,1.3). Light radius 60 + 10 sin((t + x - y) * 0.2 deg) (not ported). Damage every 300 ms within 40 px (players; hostages 90 px), host only.
