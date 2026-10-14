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
  at the target (`atan2(tx - 16 - x, y + 16 - ty)`, within 445 x 260 px) and fires while countdown > 10.
- **Respawn modes** (DM, TDM, Construction, Zombies): joining a team leaves the player dead; a left click between the HUD
  bands sends `[39]` unless a living player is within 20 px of the cursor; the server answers `[21]`. While
  `mp_respawndelay` (`[252,6]` byte after sv_gamemode) runs the HUD shows 0767 in (200, 0, 0).
- **Kill:** `[19, victim, killer, assist, weapon, x u16, y u16]` (9 bytes; REA `FUN_1401eaf60` reads the 3rd byte as a
  player id and compares the 4th with 0xfa / 0xfc).
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
- **Shot debris** (`Tshoot.Shoot` 0x1402073ed, client side for every shot): each tile the bullet enters is checked with
  `Tdo.getatpos`; an object of category 1 or 2 within 457 x 272 px of the shooter, of type 1, 3..5 or below 16 in the
  mask 0x9b80 (7, 8, 9, 11, 12, 15), hit with damage > 0, calls `Tp.breakable(x, y, 1, angle + 180)` and
  `Tp.breakable(x, y, 1, angle)` (a Wrench on an own building, `Tdo.sameteam` 0x140005460, repairs instead).
  `Tp.breakable` (0x1401197c0) material 1: a random sound of the array at 0x140a0d890, then 2 x `particles`
  (`DAT_140ddf182`, 4 in the shipped config) `Tp.matp(1, ...)` at impact + Rnd(-5, 5). `Tp.matp` (0x14004c710)
  material 1: another Rnd(-5, 5), frame Rand(4, 7), gray Rand(50, 150), speed Rnd(0.2, 3) px per frame along the
  direction (constants 0x140bd6b30 / p5 = 3). Tp type 12 (`Tp.Create` 0x14004ac20, `Tp.draw` 0x140263cc0): rotation
  Rand(1, 360), alpha Rnd(0.7, 1.2) - 0.01 per frame, scale Rnd(0.4, 0.7), alpha blend, speed - speed / 15 per
  frame, stops inside a wall tile; on layer 5 a fragment slower than 0.3 moves to another Tp list.
