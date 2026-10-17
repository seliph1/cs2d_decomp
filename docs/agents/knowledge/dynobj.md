Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Construction mode and dynamic objects

Source: REA / dump (`Tdo` class, debug scope via `tools/agent/bbscope.py CS2D.exe Tdo`), proxy captures of the
original on 140.238.183.194:36969 ("UNLIMITED Construction", ctf_cs2dfortress) and of our client against the
offline dedicated with `sv_gamemode 3` + `tools/agent/lua/construct_probe.lua`. Code: `src/cs2d_dynobj.c`.

- **Tdo fields** (offset from the object start): id +0x8, typ +0xc, health +0x10, mode +0x14, team +0x18, player +0x1c,
  x +0x20, y +0x24 (top-left pixel of the tile), rot +0x28, t1 / t2 timers +0x48 / +0x50, countdown +0x58, rrot +0x5c,
  rotvar +0x64, target +0x68. The dump's `p[n].clas` offsets are n * 8 (BBObject is 8 bytes there).
- **Type table** (`FUN_140235390`; arrays: name 0x1409f2610 / localized 0x1409f79f8, category 0x1409f7c40, price
  0x1409f3930, max health 0x1409f6298, kill reward 0x1409f71b8, per-player limit 0x1409f2608, upgrade points 0x1409f57c8,
  upgrade to 0x1409f57c0): 1 Barricade $300 hp150 (2 hits -> 3), 2 Barbed Wire $500, 3 Wall I $1000 (10 -> 4), 4 Wall II
  $2000 (10 -> 5), 5 Wall III $3000, 6 Gate Field $1500 (limit 5), 7 Dispenser $5000, 8 Turret $5000 (75 -> 11), 9 Supply
  $5000 (200 -> 15), 10 Construction Site, 11 Dual Turret (100 -> 12), 12 Triple Turret, 13/14 Teleporter Entrance / Exit
  $3000, 15 Super Supply, 20 Mine, 21 Laser Mine, 22/23 portals, 30 NPC, 40 Image. Category 1 = building (tile aligned).
- **Messages (server -> client)**, all verified on captures:
  - `[49, id u16, typ, x u16, y u16 (pixels), rot u16 (x 360/65536), mode, team, player]` spawn (13 bytes; `FUN_140143890`).
  - `[35, id u16]` remove (`Tdo.kill`: explosion r 32 for types 7, 8, 9, 11..15).
  - `[36, id u16, sub, ...]` (`Tdo.getdata` 0x1401edb50): 0 turret target u8 (aim + 15-frame burst), 1 Construction Site
    finished (becomes the type stored in `mode`), 2 x,y u16, 3 rot f32, 4 new type u8 (upgrade, health 100), 5 hit
    (Barbed Wire wobble 60 frames / Laser Mine armed / Teleporter Entrance charged, sent 5 s after it is built),
    6 teleporter used, 7 NPC target 6 bytes, 8 team u8, 9 image 2 bytes. Each case reads its bytes only when they are
    there, and an unknown id reads nothing after `sub`.
  - `[37, id u16, value u8]` health percent; values 101..200 are the upgrade progress (Barricade 150, 200 -> `[36,id,4,3]`;
    Wall I 110 .. 200 in 10 Wrench hits), values > 200 are ignored.
  - `[34, pid, team, typ, tx u16, ty u16, mode, rot + 1000 u16, money u16, id u16]` (15 bytes) build result. A build
    creates a Construction Site (typ 10) whose mode is the requested type, then `[36,id,1]`; codes 0xf8..0xff refuse it
    (0716 not finished, 0715 not allowed on server, 0714 same type too close, 0713 limit, 0712 players too close, 0711 not
    allowed, 0562 insufficient funds, 0710 can't build here; `g_lang` element size 0x24 at 0x1401fbece).
  - Each Wrench hit on an own building costs $100 (`[42,pid,2,money]`).
  - Join snapshot `[252,7,5, count, {id u16, typ, health %, mode, x u16 + 32768, y u16 + 32768, player, team, rot f32}]`
    (15 bytes each); `[252,7,3]` lists ground items `{id u16, typ, tx u16, ty u16, clip u16, reserve u16}`.
  - `[22]` round start frees every object on the client (`Tdo.freeall`); the server respawns them with `[49]`.
- **Client -> server:** build `[34, typ, tx u16, ty u16, 0, rot + 1000 u16]` reliable (only Barbed Wire passes the
  player's rotation; the menu "X" buttons send tile 0xffff / 0xffff to remove that type); respawn `[39]` reliable.
- **Wrench right click** (`Tpl.attack2` 0x1401fd6c0): the first click shows `gfx/hud_buildhelper.png` (82x82, additive,
  alpha sin((ms >> 3) deg), top-left at tile * 32 - 25) on the player's tile (yellow) and the cursor tile (green / red);
  the second checks `FUN_14014ef60` and opens the "Construct" menu (0372) or prints 0694 / 0695 in red in the center.
- **FUN_14014ef60 (can build):** within one tile (Chebyshev) and not the player's own tile, floor cell class, no entity
  of type 0, 1, 2, 3, 15, 17, 21, 25, 29, 70, 71 on the tile, outside active `Info_NoBuildings` areas, no object on the
  tile; -1 when another living player is within 32 px of the tile center. ctf_cs2dfortress has `Info_NoBuildings` over
  both spawn bases (11,1) and (11,63), 19 x 11.
- **"Construct" menu** (`Tgui_hudmen` menu 60): hud menu window (125, 20) 600 x 440 (`0x1401aba29`), 240 px buttons at
  window + (16, y): 1 Barricade 30, 2 Barbed Wire 60, 3/4/5 Wall I/II/III 110/140/170, 6 Gate Field 220, 7 Turret 250,
  8 Dispenser 280, 9 Supply 310, Teleporter Entrance / Exit (no key) at x 314, y 280 / 310; "X" (25 px) at x 261 / 559
  for 6..9 and the teleporters; 0 Cancel at h - 46. Right text = price + "$".
  `Tgui_hudmen.button`: black at 0.3, frame 200 hovered / 150 / 50 disabled, additive gui_shade 0.3 / 0.1, key at +5
  (alpha 0.4), label at +20, right text at w - 5 (alpha 0.4).
- **Collision** (`cs2d_update_mapdata` 0x1401413b0, written after tiles and entities): Barricade cell 6 (obstacle),
  Construction Site 5, walls / Dispenser / turrets / supplies 1; Barbed Wire, Gate Field and teleporters stay walkable.
- **g_prop_class** (`CS2D.exe` 0x140a0fd90, a BlitzMax array indexed by the tile mode / `g_map_cellprop`, also
  read as `g_prop_class[g_tile_prop[map_id]]` for the per-frame mode): 0 wall, 1 floor, 2 obstacle, -1 deadly. It is
  the class, not the collision value: a deadly tile is walkable (collision 3) but is not a floor. Read by the build
  rules (`Tdo.checkareaentity` 0x140170000, `FUN_14014ef60` can-build), the Portal Gun / teleporters (0x140180000),
  pathfinding (0x1402b/0x1402c0000 `while (class == 1)`), the light engine (0x140260000, 0x140090000) and the wall
  tile height (0x1402829ed `class == 0`). Code: `cs2d_map_prop_class` in `cs2d_map.c` (one helper for all callers).
- **Drawing order** (`FUN_14028f890`): floor, `Tdo.draw_ground` (Barbed Wire rot + 90, Gate Field frame 5 additive team
  color |sin * 0.7| + 0.3 over a 2x shadow, Site frame 7, Teleporters 8 / 9 over a 2.3x shadow, mines), items,
  `Tdo.draw_obstacle` (Barricade frame 0), players, `Tdo.draw_wall` (walls frames 2..4; Dispenser / turrets / supplies on
  frame 6 with a team-colored flare1: Dispenser scale 2 rot ms/10, turret scale 1 + `gfx/weapons/turret*.bmp` at `rot`,
  supply scale 0.6 rot ms/15 alpha |sin|). Team colors (`Tdo.setteamcolor`): team game T red, CT blue, neutral yellow;
  otherwise own yellow, others green. `buildings.png` frames: 0 barricade .. 4 Wall III, 5 gate, 6 base plate, 7 site,
  8 / 9 teleporters.
- **Shadows (our choice, not from REA):** solid buildings except the Construction Site cast a 32 px wall shadow
  (`map->object_shadow`); the build helper alpha is floored at 0.35; explosions draw additive.
- **Turret** (`FUN_14028c630`): idle `rot = rrot + 60 sin(rotvar)`, rotvar += 1 per frame; while `countdown` runs it aims
  at the target (`atan2(tx - 16 - x, y + 16 - ty)`, within 445 x 260 px) and fires while countdown > 10, calling the
  method at +0x120 (`Tdo.shoot`) once per barrel: type 8 one shot at the centre, 11 two at +-6 px, 12 three at 0 and
  +-8 px, every one with weapon 0xfd and **no distance argument** (the distance is the 400 that `Tdo.shoot` sets).
- **Respawn modes** (DM, TDM, Construction, Zombies): joining a team leaves the player dead; a left click between the HUD
  bands sends `[39]` unless a living player is within 20 px of the cursor; the server answers `[21]`. While
  `mp_respawndelay` (`[252,6]` byte after sv_gamemode) runs the HUD shows 0767 in (200, 0, 0).
- **Kill:** `[19, victim, killer, assist, weapon, x u16, y u16]` (9 bytes; REA `FUN_1401eaf60` reads the 3rd byte as a
  player id and compares the 4th with 0xfa / 0xfc).
- **`particles`** (`DAT_140ddf182`, sys/config.cfg `particles` 4): `Tp.breakable` material 1 bursts 2 x `particles`
  `Tp.matp` fragments, and the gates `3 < particles` enable the wall ricochet (`Tshoot.WallImpactFx` 0x14011aeb0),
  the water splash, the flashbang Tp and the explosion lights. Now read through `cs2d_config_particles()`
  (2026-10-15, used by `cs2d_fx_building_hit` and the ricochet); the `> 3` gates in `cs2d_projectile.c` / `cs2d_gen.c`
  still assume the shipped 4. `gore` (`DAT_140ddf192`, shipped 2) gates the blood decals (see "NPC death").
- **Hover text** (`FUN_1402a12d0`, called only with the cursor on the 850x480 screen): `mp_hovertext` (`0x140a14e00[1]`)
  is a bit mask, 1 | 2 players, 4 hostages, 8 buildings. It comes in `[252,6]` at maxplayers + 12 + both string
  lengths (31 with the dedicated's default config, proxy) and in `[237,16,i32]`. Bit 8: the first category 1 object
  in `Tdo.list` with `|mouse + cam - (x + 16)| < 17` on both axes. Lines through `FUN_14024fb10` (text centered on a
  point, shadow +1, black 0.4 box with `gfx/gui_c.bmp` corners 8 px, text width rounded to even + 8, max(text h + 2,
  16) high): at cursor y - 25 the type name, or "$1 by $2" (0777) when the owner is in game; colors: owner set and no
  team game -> own (255,255,0) / others (0,255,0), else team 1 (255,25,0), 2 (50,150,255), other yellow (unowned with
  another team value: no title). At y + 25 "n%": a client (`DAT_140ddf400 == 0`, set at join) reads health as a percent;
  above 100 it shows 100 % in (0,510,0) and "Upgrade n%" (0778 + " " + n + "%") at y + 45 in yellow, else clamps to
  >= 1 with ((100 - p) * 5.1, p * 5.1, 0). The listen server computes health / max health and upgrade points itself.
- **Player hover text** (same function, bits 1 | 2, tried before hostages and buildings, which it then skips): the first
  `Tpl.list` player that is not the local one, team >= 1, health >= 1, `Tpl.fog` (+0x3f8) 0, within 20 px of the cursor
  on both axes. Bit 1 title at y - 25, bit 2 "hp%" at y + 25 in ((100 - hp) * 5.1, hp * 5.1, 0). `0x140a015d0[sv_gamemode]
  == 0` (no teams): "Enemy: name" (0774 + ": ") in (255,0,0). Team game: no local player or a Spectator -> the name in
  (255,255,0); team mate (T with T, CT with VIP) -> "Friend: name" (0775) in (0,255,0); else enemy. An enemy covered by
  smoke (`Tpro.smokegren_cover`) gets nothing, and its health only shows under sv_specmode 1 while the flag 0x140a1afd8
  is 0 (the local player is not alive in a team; `FUN_1402559c0` sets it). Then `FUN_14029fc70`: a left click on the
  hovered player while that flag is 0 follows it as a spectator (specmode 1, or 2 for a team mate) and sends `[67]`.
- **Hostage hover text** (same function, bit 4, tried after the players and before the buildings; a hovered hostage
  suppresses the building text): the first `Thostage.list` hostage with health >= 1, `fog` (+0x5c) 0 and
  |cursor - hostage| < 20 px on both axes. "Hostage" (language 0776, the g_lang[0xac] group 0776..0778) is at
  cursor y - 25 in (255,255,0), the health "n%" at y + 25 in ((100 - hp) * 5.1, hp * 5.1, 0). Thostage fields:
  id +0x8, health +0xc, follow +0x10, used +0x14, look +0x18, x +0x1c, y +0x20, fog +0x5c. Code:
  `cs2d_hostages_hovered` + `render_hostage_hover` (scene_game).
- **Shot debris** (`Tshoot.Shoot` 0x1402073ed, client side for every shot): each tile the bullet enters is checked with
  `Tdo.getatpos`; an object of category 1 or 2 within 457 x 272 px of the shooter, of type 1, 3..5 or below 16 in the
  mask 0x9b80 (7, 8, 9, 11, 12, 15), hit with damage > 0, calls `Tp.breakable(x, y, 1, angle + 180)` and
  `Tp.breakable(x, y, 1, angle)` (a Wrench on an own building, `Tdo.sameteam` 0x140005460, repairs instead).
  `Tp.breakable` (0x1401197c0) material 1: a random sound of the array at 0x140a0d890, then 2 x `particles`
  (`DAT_140ddf182`, 4 in the shipped config) `Tp.matp(1, ...)` at impact + Rnd(-5, 5). `Tp.matp` (0x14004c710)
  material 1: another Rnd(-5, 5), frame Rand(4, 7), gray Rand(50, 150), speed Rnd(0.2, 3) px per frame along the
  direction (constants 0x140bd6b30 / p5 = 3). Tp type 12 (`Tp.Create` 0x14004ac20, `Tp.draw` 0x140263cc0): rotation
  Rand(1, 360), alpha Rnd(0.7, 1.2) - 0.01 per frame, scale Rnd(0.4, 0.7), alpha blend, speed - speed / 15 per
  frame; **inside a wall tile (g_prop_class 0) the speed is zeroed, so the fragment stops where it is, and inside a
  deadly tile (class -1) the alpha halves** (2026-10-15: `cs2d_fx_update` takes the map and does this check);
  on layer 5 a fragment slower than 0.3 moves to another Tp list.
- **Supply menu** (`Tgui_hudmen` menu 61 Supply / 62 Super Supply, title 0373 "Supply Selection"): the use key
  (`FUN_14014f820`, tiles 0/16/32 px along the aim) on an own Tdo 9 / 15 opens it and sends nothing. Buttons x 16
  from y 30 every 30 px (+20 gap after the 6th) for each non-empty `mp_supply_items` slot with a name, slots 7..10
  only on 62; key = slot; 0 Cancel (0027) at h - 46. Pick: `[26, 150, slot i32, tile x u16, tile y u16]` -> server
  sets `Tdo.mode` (slots 1..6 / 1..9, owner within 60 px). `mp_supply_items` (0x140a14e28[1]) is a string, one char
  per item id: first string of `[252,6]` (proxy `08 33 3a 0b 16 20 23 50 49` = 51,58,11,22,32,35,80,73) and
  `[237,11]` ("supply items have been changed"). Offline spawn every 7 frames when < 3 items around: `FUN_14028dba0`.

## Lua images (Tdo 40) and NPCs (Tdo 30)

Source: dump / REA (addresses below), proxy of our client on the offline dedicated with
`tools/agent/lua/image_probe.lua` (2026-10-14: `[51,0]`, `[51,4]`, `[54]` mode 0 / 3 and `[36,id,9]` match). Code: `src/cs2d_dynobj.c`.

- **[51, sub, id u16, ...]** (client FUN_140182240, server FUN_14017c310 ...): 0 create `{path str, id, x + 32768, y + 32768,
  mode, player}` (path first, id after it), 1 color r g b, 2 alpha u8 / 255, 3 blend (clamped 3), 4 scale f32 f32,
  5 pos x, y (+32768), rot u16 x 360/65536, 6 free (Tdo.free(id, 40): images only), 7 frame (1-based, clamped to frames),
  8 physics parent typ u8, typid u16, mode u8, 9 shadow u8. Sent only to `player` when mode 2 and player != 0.
- **[59, id u16, mode, xo + 32768, yo + 32768, w u16, h u16]** hit zone (FUN_14017d7e0); mode 0 removes it.
- **[54] / [252,7,8] Ttween.receive** (0x1400b4420): `mode, target u16` (> 200 = Tdo id + 200, else hud text), body 0 / 1
  `{ms i32, x, y +32768}`, 2 `{ms, rot u16}`, 3 `{speed f32}` (rot += speed each update, never ends), 4 `{ms, a u8}`,
  5 `{ms, r, g, b}`, 6 `{ms, sx f32, sy f32}`, 7 / 8 `{ms, frame}` (8: every ms, 0 loop, 1 back, 2 / 3 ping-pong, 4 random).
  Ttween.addtween replaces a tween of the same image when the modes conflict (moves / rotations, 7 with 8).
- **Join snapshot sections** (FUN_1402ce7c0 `switch(section - 3)`, proxy): 7 image data per object `{id, path str, r, g, b,
  alpha f32, sx f32, sy f32, blend, frame u16, parent typ (+ typid u16, mode), shadow, hitzone flag (+ mode, xo, yo, w, h)}`,
  8 tweens, 9 custom tiles (11 bytes each), 10 resource paths, 11 weapon settings (zlib), 12 map cycle, 13 mods.
- **Trestable**: section 10 lists path prefixes (count 0 ends); a path `"?" + u16 (two chars, low first) + rest` is
  `table[u16] + rest` (longversion 0x1400919c0). The index bytes can be 0: read the string raw.
- **iniimage** (0x14022b9f0): `path<m|a|b|n>` mask suffix (Tres mode 4 / 3 / 1 / 0, default 1), `<spritesheet:path:w:h[:mask]>`
  (frames = (H / h) * (W / w)), `<tile:N>`, `<avatar_N>`, `<flag:cc>`, `<light>` (light source r 128).
- **Draw** (FUN_1402809b0 → FUN_14025eb80): passes by mode: background 4 / 233..263 (before the tiles), ground 0 / 101..132,
  wall 1 / 201..232, overwall 3 / 133..164 (after Te.draw 2), HUD 2. 101+ follow player mode - 100 / 132 / 200 / 232 (alive,
  team > 0); image x picks the rotation (<= 0 own, 1 player rot, 2+ weapon rot). player != 0 and != local → not drawn;
  alpha <= 0.0001 skipped; culled at ceil(max(w,h) * max scale * 0.7) around the screen. Blend 0 alpha, 1 light, 2 shade,
  3 solid. Mode 10..12 draw relative to Tdo.parent (pos = int(parent) + int(own), rot / scale from the parent), but
  parentId is only reset (FUN_1402caa20) and never received, so the original never draws them.
- **Draw positions** (FUN_1402809b0 assembly, xmm1 / xmm2 into FUN_14025eb80): world `x - (float)camx` (integer camera
  0x140ddf06c / 0x140ddf068); HUD `x, y` under FUN_140291080 (the mp_hudscale transform; Tdo.draw_hud runs first in the
  game HUD 0x1402a0000 when Tdo.hudobjects); follow `Tpl.x - camx + recoil` with recoil (`-recoil sin(rot)`,
  `recoil cos(rot)`, ints) only when image x > 2.
- **Special paths**: `<tile:N>` = g_tileset_frames[clamp(N, 0, tilemax)] 32 x 32; `<flag:cc>` = incbindata/flags.png (16 x 11,
  midhandled) frame = index of cc among the 2+ character lines of flags_iso.txt (missing → 0); `<avatar_N>` = the player's
  avatar, else incbindata/avatar_load.png (black masked) turning at ms / 5, 64 x 64; `<light>` = no image, a
  Tp_lightpool.GetLight(x, y, 128, white, 512, 1.0) when the light engine is on.
- **NPC**: `player` = type 1 zombie, 2 headcrab, 3 snark, 4 vortigaunt, 5 soldier (else "Invalid NPC Player Value"); Tdo.add
  gives a 32 x 32 Tdoc at -16; the client health is 0x1409ed7f0[type]. `[36,id,7]` sets m_x, m_y, m_rot (u16 each);
  `[36,id,2]` sets x, y and m_x, m_y. **[36,id,9, pid, kind]** = attack (FUN_1401ed310): rotvar 40 (zombie, vortigaunt melee) /
  20 (headcrab, snark), Tp 13 slash at x, y along rot, sounds zm_attack[] / npc/headcrab_attack / snark_attack /
  vortigaunt_attack, vortigaunt kind 1 charge (upgrade = 1, vortigaunt_charge) / kind 2 ranged (vortigaunt_ranged + beam),
  soldier weapons/mp5 + shot; then m_rot = angle to the player. The attack spin is purely visual: `rot + rotvar` is
  drawn and **the draw** (`Tdo.draw_ground` case 0x1e) does `rotvar -= 5` afterwards, so an NPC that is culled
  (off screen) keeps its spin; our update does not touch it (`update_npc`), `render_npc` decays it.
- **`Tdo.shoot`** (0x140209b50, the method at vtable +0x120 used by NPCs and turrets): it sets `c_dist` 400
  (0x140ddea0c = 0x190), `c_offset` 15 (0x140ddea08 = 0xf), then calls `Tshoot.Shoot` with the angle
  **`rot + Rnd(-3, 3)`** (the double constants 0x140bd6c18 = -3 and 0x140bd6bb8 = 3) and the weapon 0xf8 (soldier,
  style 0 bullet) or 0xf9 (Vortigaunt ranged, style 2 beam). Our code: `cs2d_fx_tdo_shot_angle` (cs2d_fx.c) in the
  NPC path (cs2d_client.c kind 5) and in the turret burst (cs2d_scene_game.c).
- **NPC death** (`Tdo.kill` case 0x1e; `cs2d_dynobj_kill` kind 6): the die sound by the NPC type - 1 zombie
  0x140a0d8d8 = `sfx/player/zm_die.wav`, 2 headcrab 0x140a0d608 = `headcrab_die`, 3 snark 0x140a0d618 = `snark_die`,
  4 vortigaunt 0x140a0d5f8 = `vortigaunt_die`, 5 soldier = a random element of the array 0x140a0d958
  (`sfx/player/die[1..3]`, `Rand(0, length - 1)`); any other value is silent (the file names come from the sound
  loader, see KNOWLEDGE sound.md "How to resolve a global to a file"). With `gore` (`DAT_140ddf192`, config `gore`)
  the zombie / soldier get `Tp::groundblood(x, y, -1, -1, -1)`, the headcrab and vortigaunt `Rand(140, 200)` size in
  colour 230, and the snark `Rand(120, 180)` in colour 255 plus 9 Tp 3 particles 45 deg apart (a = Rnd(0.4, 0.9),
  sx = Rnd(0.3, 0.8), speed 0.3, rots = Rnd(-0.5, 0.5)). **Our ground decals and the snark fountain are not drawn.**
- **NPC sprites:** `gfx/npc/zombie.bmp` is 64 x 96 = 2 x 3 cells of 32 x 32 (BMP header, 2026-10-15), so the zombie
  uses a 2 column grid and frame 2 (row 1, col 0); headcrab / snark / vortigaunt are 32 x 32 and the soldier 40 x 40
  as a single cell (scale 0.5 only when the cell is >= 64 px wide).
- **NPC motion** (FUN_140283f20 → FUN_140283120(speed, turn), per frame): turn by <= turn deg while > 3 deg off m_rot, else
  walk speed px toward m_x / m_y while > 3 px away. speed / turn: zombie 1 / 5, headcrab 2.5 / 5, snark 6 / 4, vortigaunt
  7 / 20, soldier 3 / 7. Draw (Tdo.draw_ground 0x1e): gfx/npc/*.bmp (magenta), zombie frame 2 of 32 x 32, scale 0.5 when
  >= 64 px wide; rot + rotvar (rotvar -= 5 per draw) + walking wobble sin(ms / 2 + id) * 10 (zombie) / * 15 (ms * 2 for
  headcrab and snark); idle snark sin(ms / 5 + id) * 15; shadow black at +3 with alpha shadows / 100 when
  playershadows and mapshadows (DAT_140ddf190) > 1.

## Construction mode, server side (`src/cs2d_sv_game.c`, 2026-10-16)

The client half (build helper, Construct menu, drawing) was already in place; this is the dedicated's half.
`[34]` receive = `cs2d_dedicated.exe 0x1401cf0c0`, which calls **`FUN_140175550`** ("place object"); the same
function in CS2D.exe is `FUN_1401fab30` (`docs/dump/cs2d/_global/1401f0000.c:1202`), which is what the rules
below were read from.

- **`[34]` receive (dedicated 0x1401cf0c0):** `find player by ip:port`, then
  `FUN_1400d4f80(ip, 0x22, 0x14, 0xbb8)` = **20 requests per 3000 ms**, then place with
  `(player, team=0, typ, tx u16, ty u16, mode u8, rot+1000 u16, 0, 0, 1)` — so the client payload is 8 bytes:
  `typ, tx, ty, mode, rot + 1000`. A failed flood guard silently drops the message (returns 0 = next).
- **Build times** (`FUN_1401fab30` switch at `1401f0000.c:1572`; the dedicated loads them from a table):
  1 Barricade 1000, 3 Wall I 2000, 4 Wall II / 6 Gate Field / 13 Teleporter Entrance / 14 Exit 3000,
  5 Wall III 4000, 7 Dispenser / 8 Turret / 9 Supply 5000 ms. Type 2 (Barbed Wire) is **instant** and is
  created as itself with **mode 60** (the hit wobble). Everything else first becomes a **Construction Site**
  (typ 10) whose `mode` is the wanted type.
- **Refusal codes** (byte 3 of the 15 byte `[34]` answer when >= 0xf8; only the requesting client shows the
  line, `FUN_1401fab30` 0x1401fbece reads `g_lang`, element size 0x24):
  `f8` 0716 last building not finished, `f9` 0715 not allowed on this server, `fa` 0714 same type too close,
  `fb` 0713 limit, `fc` 0712 players too close, `fd` 0711 not allowed to build this, `fe` 0562 insufficient
  funds, `ff` 0710 can't build here. Check order: funds -> Wrench -> own unfinished Site -> `FUN_14014ef60`
  -> price (> 16000, < 0 or typ 10) -> per-player limit -> same-type distance.
- **Only the Dispenser (7) uses the 0714 line**: `Tdo.sametypetoclose(7, x, y, 0xc0 /*192 px*/, team)`.
  A Turret (8) counts its Dual + Triple upgrades in the limit of id 8. Mines / portals (category 2) skip the
  money check. Without the Wrench (item 74) the original only logs
  `ERROR: <name> tries to build without having a wrench!`.
- **`tx == ty == 0xffff`** (the menu "X" buttons): the dedicated jumps to the epilogue (0x1401758f0 ->
  0x140175787) and **returns** - it does not remove anything in that path. Our server mirrors that.
- **Wrench primary attack** (`Tshoot.Shoot` wrench branch, `docs/dump/cs2d/Tshoot.c:594..650`): the target is
  the object at the tile where the melee ray ends (`Tdo.getatpos`, the Wrench's range is 15 px in the weapon
  table) and it must be a **same team** building (`Tdo.sameteam` 0x140005460):
  - `health < max_health` -> **repair**: `Tdo.damage(obj, -15, player, 0)` (a negative amount heals) for **$50**
    (`money > 0x31`); if the damage returns 1 the money drops by 50 (`FUN_1401cc9d0(id, 2, money-50)`).
  - else, `upgrade_points[typ] > 0` -> `Tdo.doupgrade(obj, player, 1)` for **$100** (`money > 99`).
- **`Tdo.damage` (0x140035110):** `p3 == 0` -> the portals (22/23) and Lua images (40) ignore damage; a
  category 1 building of the **attacker's own team** (and not his own object) is skipped while friendly fire
  is off (`DAT_140a14d98 == 0`); losing health clears the Wrench upgrade progress; the max health clamps and
  `[37, id, Tdo.hpupnetvalue]` is sent unreliable.
  `Tdo.healthpercent` clamps to 0..100, `Tdo.hpupnetvalue` = the health percent, or **100 + the upgrade
  progress** when the health is full and the type has upgrade points. The dedicated keeps the health in
  points, the client in percent: the join snapshot section 5 sends the percent.
- **`Tdo.doupgrade` (0x140143d60):** below the target it adds the points ([37]); at `upgrade_points` the type
  becomes `upgrade_to`, the upgrade resets, the health becomes the new max and `[36, id, 4, typ]` goes reliable
  to everyone (`cs2d_update_mapdata` re-runs over the tile).
- **Turret (CS2D.exe `FUN_14028c630`, `docs/dump/cs2d/_global/140280000.c:1971..2150`):** at most one scan per
  **1000 ms** (the Ttimer at +0x48); a Lua `turretscan` hook (slot 0x318) may return a target id, otherwise
  every player with `team > 0`, health > 0, **not the builder**, on the other side (`no teams` -> anyone),
  `|dx| < 440` and `|dy| < 255` of the turret centre, the nearest one with a clear line (`FUN_14011c3a0`).
  Then `target = id`, **countdown = 15** and a reliable `[36, id, 0, target]`. While the countdown runs the
  turret aims within **445 x 260** (`atan2((tx - 16) - x, (y + 16) - ty)`) and fires at most every **100 ms**
  (the Ttimer at +0x50) while **countdown > 10**, one `Tdo.shoot` per barrel: type 8 one shot from the centre,
  11 two at +-6 px, 12 three at 0 and +-8 px; `Tdo.shoot` uses `rot + Rnd(-3, 3)`, `c_dist` 400, `c_offset` 15
  and the weapon byte 0xfd. Its damage argument is -1, which falls back to `DAT_140a14da8`
  (**CS2D.exe .data = 5**, the default of the `mp_turretdamage` cvar). The burst ends with `rrot = rot` and
  `rotvar = 0`, and the next scan is 1000 ms later.
- **Laser Mine beam & detonation (`0x14000aa30` in cs2d_dedicated.exe):** `Tdo.inlasermine(o, tx, ty)` checks
  whether tile (tx, ty) intersects the active beam (target is beam length 1..6 tiles). Directions (`dir = (int)o->rot`):
  0: down (+Y) `tx == mx && ty >= my && ty < my + target`
  1: left (-X) `ty == my && tx <= mx && tx > mx - target`
  2: up (-Y)   `tx == mx && ty <= my && ty > my - target`
  3: right (+X) `ty == my && tx >= mx && tx < mx + target`
  Beam length calculation (`FUN_14028e310`): steps 1..5 tiles ahead along the facing direction, stopping early on
  walls, map boundaries or solid buildings. Crossing the beam (`sv_tile_object_effect`) detonates the mine with
  weapon 87 (`CS2D_WEP_LASERMINE`), 150 radial damage at radius 200, credited to the owner. Client renders the flare
  and beam line up to target.
- **Dynamic building cvars (`0x140118060` in cs2d_dedicated.exe):** `mp_building_price`, `mp_building_limit` and
  `mp_building_health` allow runtime overrides by building name or numerical ID (1..CS2D_DYNOBJ_TYPES). Lowering
  `mp_building_health` clamps existing active buildings of that type to the new max (`0x1401182a6`).
- **Wrench raycast probe (`Tdo.getatpos`):** tests the player's own tile first, then steps every 2 px along `rot`
  up to the weapon's range (15 px), stopping early if a solid tile is hit.
- **Ping/join:** the join snapshot section 5 lists the objects with the **health percent**; `[36, id, 1]` on a
  finished Site and `[36, id, 5]` when a Teleporter Entrance is charged (**5 s** after the build).


## Construction mode, server side, part 2 (2026-10-16)

- **A bullet that reaches a building damages it** (`Tshoot.Shoot` 0x1402073ed and the server branch
  `docs/dump/cs2d/Tshoot.c:581..592`): the ray runs from the shooter along its aim up to
  `cs2d_weapon_shot_reach` (range + 15, melee range + 6), stops on the tile of a solid object and calls
  `Tdo.damage(object, weapon damage, shooter, 0)`. Only the types whose bullets throw debris count
  (`cs2d_dynobj_hit_debris`: Barricade, Wall I..III, Dispenser, Turret, Supply, Dual / Triple / Super
  Supply); a Barricade is an obstacle and takes "close combat / explosions only" (Changelog), so it is
  skipped. The player damage stays the client's `[17]` report - the server must not apply it twice.
  Our port: `cs2d_sv_shot_at_building`, called from `cs2d_sv_fire` for every non-melee shot.
- **`turretscan` Lua hook** (`FUN_14028c630` slot +0x318): 4 arguments (the turret's object id, its team,
  x + 16, y + 16); a returned player id **> 0 becomes the target directly**, without the range / team test
  (0 falls through to the normal scan). Our port: `cs2d_sv_lua_hook_turretscan`.
- **Teleport** (`Tpl.tilepos` 0x14020f7d0, server side; the client only applies `[15]`): stepping onto one
  of the owner's portal tiles (22 / 23) moves him to the other colour's centre; a charged (mode 1)
  Teleporter Entrance of his side to the owner's exit of that team, a random exit of the entrance's team
  otherwise. `-1` = not charged (0807 "Teleporter is not charged! Please wait!"), `-2` = no exit (0806
  "Teleport failed! No exit found!"). After use `[36,id,6]` (mode 0) **only when `mp_teleportreload`
  (`DAT_140a14d48`, CS2D.exe .data = 5) > 0**, and `Tdo.draw_ground 0xd` charges it again with `[36,id,5]`
  (mode 1) after `reload x 1000` ms - the same timer as the first charge after the build. Our port:
  `cs2d_sv_validate_move` (the tile change) + `cs2d_sv_step_buildings` (the charge), settings field
  `mp_teleportreload` (not wired to the console parser yet).
- **Not done yet**: Dispenser (health + ammo + `mp_dispenser_money`), Supply (item spawn) and Gate Field
  (damage) have no server effect, and no explosion / melee damages buildings. Leads for the Dispenser: the
  dedicated registers `mp_dispenser_money` / `mp_dispenser_health` / `mp_turretdamage` at 0x14021ef21 with
  `FUN_140021be0(name, default, ...)` into a **generic cvar table** (the counter at 0x1407dffc4), and the
  console parser compares them at 0x140141ba2..0x140141bca.


## Construction mode, server side, part 3 (2026-10-16)

- **Dispenser** (`CS2D.exe FUN_14028b980`, dedicated `FUN_1401b5890`, the type 7 draw pass; `Tdo +0x58` is our `countdown`):
  `DAT_140ddf540 != 0 && server`. Operates on a **1 Hz cadence (1 second interval)** tracked via `scan_ms`.
  Every **living player of the served side within 48 px** of the dispenser centre (`x + 16`, `y + 16`) is healed
  by `mp_dispenser_health` (`DAT_140a14dac` = **10**) up to maxhealth (`[42,id,0,health]`) and gets
  `mp_dispenser_money` (`DAT_140a14db0` = **500**, the Changelog default) money up to 16000 (`[42,id,2,money]`)
  **once per second**. The served side: everyone in deathmatch (the `0x140a015d0[sv_gamemode]` table is 0),
  a team 0 building, `team == 1` for T and `team > 1` for the CT.
  The seconds counter increments every second and at **second 5** spawns a **secondary ammo (62)** and at
  **second 10** (then it wraps to 0) a **primary ammo (61)** `[33]` item on a random free neighbour tile, but
  only when `mp_infammo` is off and no item of that type is within one tile.
- **Supply** (`CS2D.exe FUN_14028dba0`, dedicated `FUN_1401b7380`, type 9 and its Super Supply 15): also runs
  on the 1 Hz cadence; the same counter spawns the item `mp_supply_items[mode - 1]` every **7 seconds** (mode =
  the slot the owner picked with `[26,150,slot]`) when fewer than **three** of them lie within one tile.
- **The spawn tile** (both functions): the 8 neighbours, `(-1,-1)` first and then the 7 entries of the two
  int tables `0x140bc8a00` (y = -1 -1 0 0 1 1 1) and `0x140bc8a20` (x = 0 1 -1 1 -1 0 1) =
  `(0,-1) (1,-1) (-1,0) (1,0) (-1,1) (0,1) (1,1)`, each tried once (random order) and only when
  `g_prop_class[cellprop] == 1` (a free floor tile). The item itself is `FUN_14011ba70(type, tx, ty, -1, 0,
  -1, -1)`, i.e. `[33, type, id u16, tx u16, ty u16, clip + 1 u16, reserve + 1 u16]` with clip/reserve 0
  (the client reads them -1 and fills the type's defaults).
- **A melee swing hits buildings too** (`Tshoot.Shoot` is weapon agnostic): the knife / machete / claw /
  chainsaw damage the building they reach with their own damage, so `cs2d_sv_shot_at_building` is called
  for every weapon except the Wrench (which repairs / upgrades instead).
- **Spawn money** (`cs2d_dedicated.exe FUN_140160740` 0x1401618xx..0x140161c89): the first spawn (no
  spawns, no deaths) sets `mp_startmoney` and clears the per-player seconds counter at `+0x3fc`; every
  later spawn **of a respawn mode** (DM / TeamDM / Construction / Zombies, the `0x14059c098` table) adds
  `mp_dmspawnmoney` (`DAT_1405a6554` = **2000**, the Changelog default) when that counter is **> 14** (the
  1 Hz `add [rax+0x3fc],1` at 0x140193ea3 caps it at 499). Changelog: "Player gets money each spawn
  (DM/TeamDM/Construction, 2000$ by default)" + "spawnmoney time limit (max every 15 sec)". Money is
  clamped to 0..16000 right after.
- **Gate Field (6)** is still missing: its damage lives in `Tpl.tilepos` (CS2D.exe 0x14020f7d0,
  `docs/dump/cs2d/Tpl.c:2827..3615`), the same ~790 line tile-interaction pass that handles the
  teleporters, the mines and the item pick-ups; `Tpl.takeDamage` is not called by name there (it goes
  through a method pointer), so it needs its own pass with `dedscan.py refs` on the dedicated.


## Construction mode, server side, part 4 (2026-10-16)

- **Explosions damage buildings** (`FUN_1401f1130`, `docs/dump/cs2d/_global/1401f0000.c:1..169`): the
  function at (x, y, radius, damage, player, weapon, mode, no_los) walks three lists -
  1. every **player** with `team > 0` and health > 0 without the spawn-protection flag at +0x7b inside the
     radius gets `FUN_1401ec180(victim, player, damage, ...)`; with an attacker only the other side (the
     `0x140a015d0[sv_gamemode]` team table), otherwise everyone;
  2. every **Te** of type `0x19` (the breakables) whose tile centre is inside the radius loses the damage
     from `ints[1]` and runs its trigger at 0;
  3. every **Tdo** whose **centre** `(x + 16, y + 16)` is inside the radius takes
     `(radius - distance) * damage / radius` (**the full damage at the centre, 0 at the rim**) through the
     vtable slot `+0xb8` = `Tdo.damage`.
  `mode == 1` skips the falloff (full damage anywhere in the radius) and `no_los != 0` skips the
  `FUN_14011c3a0` line-of-sight test. The callers pass `Tp::explosion(x, y, radius, 5.0)` for the visual at
  the same radius (grenade 0x31 -> 40, rocket 0x28 -> 45, mine 100, ...) and the damage from
  `damage_table[typ] * DAT_140a14dcc` (`Tpro.c` 1380..1460, 1600, 3138).
  Our port: `cs2d_sv_explosion_objects` (the object loop only), wired to the **C4** blast (350 px, 500,
  the same falloff the player loop already used); the Grenade / rocket path needs a server-side `Tpro`
  simulation first - `cs2d_sv_projectile` (`[27]`) only relays today.
- **The round model of a respawn mode is inert**: `cs2d_sv_check_win` returns early for
  `sv_mode(sv)->respawn != 0` and `cs2d_sv_step_round` only runs the mission objectives and the time out
  for `respawn == 0 && balance == 1`, so `sv_gamemode 3` never ends a round (the freezetime phase still
  runs, once, at server start).
- **`DAT_140ddf540`** (the gate the Dispenser and Supply passes share): it lives in the client's
  zero-initialized section (`0x140dde000`, raw size 0 - no `.data` image to read, which is why
  `pevalue.py` reports it unmapped), so it is a **runtime** flag. It gates item pick-ups (`Titem.c`),
  projectile updates (`Tpro.c`), entity triggers (`Te.c`), the hostage logic (`Thostage.c`) and the
  regeneration-armor decay (`Tpl.c:3893`), always as `DAT_140ddf540 != 0 && DAT_140ddf418 in {1,2}` =
  "the game is actually running". Our server has no pause state, so treating it as enabled is right.
- **Gate Field (6)** is still missing: its draw has no logic of its own (the ground pass only draws frame 5
  with the pulsing alpha of `cs2d_render.c`), so the "hurt" lives in the tile-enter pass `Tpl.tilepos`
  (0x14020f7d0, `Tpl.c:2827..3615`) next to the teleporters, the mines and the pick-ups. `Tpl.takeDamage`
  is only reached through a method pointer there, so it needs a `dedscan.py refs` pass on the dedicated.


## Construction mode, server side, part 5 (2026-10-16)

- **The building on the tile a player enters** (`Tpl.tilepos` 0x14020f7d0, `Tpl.c:3443..3604`; the switch
  on the first Tdo of the tile, one object per tile):
  - **Barbed Wire (2)**: 20 damage (weapon `0xff`) credited to its owner, the wire takes
    `Tdo.damage(wire, 60, 0)` and a `[36,id,5]` wobble. **No team test** - a wire hurts its own side too.
  - **Gate Field (6)**: `(0x140a015d0[sv_gamemode] == 0 && field.player != self->id)` (deathmatch, not the
    builder) or `(self->team == 1 && field.team == 2) || (self->team > 1 && field.team == 1)`; a `team == 0`
    field hurts everyone with attacker 0. Damage **20** (weapon `0xfe`, `mov r8d,0x14` at CS2D.exe
    `0x140210459`), credited to the field's owner, and the field takes **100** (`edx=0x64` after the call).
  - **Mine (20)**: triggers for the other side (a team 0 mine: everyone) and `Tdo.kill` explodes it -
    `Tp::explosion(x+16, y+16, range[77], 5.0)` plus `FUN_1401f1130(x+16, y+16, range[77], damage[77],
    player, 0x4d (77), 0, 0)`. The weapon table (`0x1409f62a0` damage / `0x1409f62a8` range, offsets
    0x134 = 77*4 and 0x15c = 87*4) gives **range 96 / damage 150** for both the Mine (77) and the Laser Mine
    (87). The Laser Mine (21) adds `mode == 1 && Tdo.inlasermine()` (its beam), not ported.
  - 13 / 14 / 22 / 23 are the teleporters (`cs2d_dynobj_teleport`).
  Our port: `sv_tile_object_effect`, called from `cs2d_sv_validate_move` on the tile change before the
  teleport test (the kinds are exclusive, so it returning "handled" skips the teleporter of the same tile).
- **The four Construction settings are console cvars now**: `mp_teleportreload` (5), `mp_dispenser_health`
  (10), `mp_dispenser_money` (500) and `mp_dmspawnmoney` (2000) are in `cs2d_sv_settings_set` /
  `cs2d_sv_settings_get` (`SV_INT` / `SV_GET_INT`), so `server.cfg`, the console and the Lua `game("...")`
  reach them. None of them is part of the original's `[237]` live-setting broadcast (ids 1..24).

