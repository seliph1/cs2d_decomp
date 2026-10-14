Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Entities

Source: Lua fixture, 331 samples.

- **State:** 0 = active / visible / closed, 1 = inactive / hidden / open. Triggering toggles the state.
- **Trigger_Delay:** state 1 while counting down, back to 0 when it fires.
- **Trigger_Once:** goes to state 1 after firing.
- **Env_Item:** toggles and spawns its item.
- **Env_Sound:** toggles only when looping (`ints[1]` = 0).
- **Func_GameAction:** 0 draw, 1 T win, 2 CT win, 3 restart in X s, 4 kill Ts, 5 kill CTs, 6 kill all, 7 load map.
- **Round start:** the round-end intermission is 3 s. Round start is deterministic: every state goes back
  to 0, then the Trigger_Start entities fire. Entity states are not resent at round start.
- **Dedicated quirk:** after some round restarts the dedicated stops broadcasting entity changes. The
  original client shows the same divergence.
- **Env_Decal (24):** in a game, Te.setup (0x1401ad930, case 0x18) removes it from the entity list (so triggers and
  Lua cannot reach it) and creates a `Tp` type 10 at the tile centre: frame = ints[0] clamped to the decals.bmp
  frames, colour ints[1..3], rot ints[4], alpha = `bbStringToFloat(strs[0])` ("" → 0, invisible), scale 1. The
  layer is 2 (floor), 4 (prop layer 2) or 5 (prop layer 3) of the tile. `TDecal::Add` (0x140005c70) keeps one per
  tile (first wins); `FUN_140110c60` draws it with alpha blend right after its tile in the floor / wall / obstacle
  tile loops. In the editor (state 4) it stays an ordinary entity.

- **Gen_* (50..53), REA:** spawned in `Te.draw` pass 2 (0x140271ee0, once per rendered frame), moved and drawn
  as `Tp` (Create 0x14004ac20, draw 0x140263cc0; g_dt 0x140a1b090 = 1 per 60 Hz frame). Code: `src/cs2d_gen.c`.
  - Particles: Tp 17 at tile centre, speed Rnd(ints[2..3]) / Rnd(ints[4..5]) px per frame, alpha
    `Rand(int(life_min*0.0005), int(life_max*0.0005))` - 0.01 / frame (so < 2000 ms dies at once), colour strs[0]
    = 0xRRGGBB int; 1 px plot. Only when |x*32 - camx - 425| < 670, |y*32 - camy - 240| < 590.
  - Sprites: Tp 12 = gfx/fragments.bmp frame ints[0] clamped 0..15, centre ± Rand(8), alpha strs[0], scale
    Rnd(0.4, 0.7), friction 1/15 per frame, alpha - 0.01; stops in a wall (g_prop_class 0), alpha *= 0.5 on deadly
    (class -1); no camera check. ints[8] (sprite type) is not read by the client.
  - Weather: rain = max(ints[1] >> 1, 1) Tp 20 (raindrop.bmp) per frame, snow = max(ints[1], 1) Tp 19
    (snowflake.bmp), both in screen space (offset * depth / 30 from the centre, depth Rnd(100, 150) decaying 5 % / 1 %
    per frame) and skipped while the screen centre is in an Info_NoWeather rect (x..x+ints[0], y..y+ints[1] tiles).
    Rain landing on mode-14 water splashes (wave, 1 in 3). Fog / dust / smog: max(ints[1], 1) Tp 21 (smokepuff)
    per frame at centre ± Rand(50) within 960 x 720 px of camx/camy; alpha fades in +0.01 to 0.7 then out -0.002.
    Thunder: ints[9] counts Rand(10, 30) frames down; at 2 the screen flash (0x140a17ff0) = Rnd(0.3, 0.4), at 1
    sfx/env/thunder.wav; the flash decays 0.005 / frame (FUN_140131710 draws a white 854x480 rect).
  - FX: every max(ints[5], 25) ms (ints[9] = last time), at x*32 + Rand(ints[1..2]), y*32 + Rand(ints[3..4]):
    0 waves (Tp 1 wave.bmp LIGHTBLEND), 1 smoke (Tp 3 grey 30..120), 2 fire (Tp 14 flare3 + 50 % smoke),
    3 steam (Tp 3 grey 190..255). Only within 850 x 480 of the screen centre.
  - Draw lists: wave l[1] (under players), Tp 3/12/14/17 l[5], fog l[7], rain/snow l[6]; after Te.draw pass 2:
    l5, l7, l6. Image masks: wave / raindrop / snowflake descriptors +0x74 = 3.
  - Info_Storm (9): Te.setup case 9: wind_x = ints[1]*0.1*sin(ints[0] deg), wind_y = -ints[1]*0.1*cos (globals
    0x140ddf028/024, last storm wins, reset on map load); Tp.draw adds it to the speed of Tp 3, 0x20, 14, 22.
  - Info_Quake (14): Te.draw pass 2: state 0 and g_screenshake < ints[0] → g_screenshake = ints[0].
    cs2d_draw_world (0x140290310): s = min(shake, 30); camx/camy += Rand(-s, s) for the world draw only
    (restored before HUD), shake -= g_dt. Other writers (not ported): explosions within 150 px
    ((1 - d/150) * 30), the Lua shake message (cap 1000), a 50 on an item pickup at 0x1401f0xxx.
  - Info_TileFX (10): see `include/cs2d_tilefx.h` (CreateTileFX 0x1400aa640, update 0x1400f4b60). Built at map load
    with Info_Animation by FUN_1401d7c40 when DAT_140ddf17f == 1; one effect per tile frame, first entity wins.
  - g_prop_class (TmapPreview colours): 0 wall, 1 floor (14 water), 2 obstacle, -1 deadly.

## Implementation checklist (client)

Status of each entity type in the reimplementation (2026-10-13). [x] done, [~] partial, [ ] missing.

- [x] Info_T / Info_CT (0, 1): spawn points
- [~] Info_VIP (2): game rules only
- [x] Info_Hostage (3): render + game
- [x] Info_RescuePoint (4), Info_BombSpot (5), Info_EscapePoint (6): zones
- [ ] Info_Target (7): bots only (server-side)
- [x] Info_Animation (8): tile animation
- [x] Info_Storm (9): wind on Tp 3 / 14 (`cs2d_gen.c`)
- [x] Info_TileFX (10): scroll / noise / wave frames (`cs2d_tilefx.c`)
- [ ] Info_NoBuying (11)
- [ ] Info_NoWeapons (12)
- [ ] Info_NoFOW (13)
- [x] Info_Quake (14): screen shake (`cs2d_gen_shake_offset`)
- [x] Info_CTF_Flag (15)
- [ ] Info_OldRender (16)
- [x] Info_Dom_Point (17)
- [x] Info_NoBuildings (18): build rules
- [ ] Info_BotNode (19): bots only (server-side)
- [ ] Info_TeamGate (20)
- [x] Env_Item (21)
- [x] Env_Sprite (22)
- [x] Env_Sound (23)
- [x] Env_Decal (24)
- [x] Env_Breakable (25)
- [x] Env_Explode (26)
- [ ] Env_Hurt (27)
- [x] Env_Image (28)
- [x] Env_Object (29)
- [x] Env_Building (30)
- [~] Env_NPC (31): render only, no AI / movement
- [x] Env_Room (32), Env_Light (33), Env_LightStripe (34): light engine
- [x] Gen_Particles (50)
- [x] Gen_Sprites (51)
- [x] Gen_Weather (52)
- [x] Gen_FX (53)
- [x] Func_Teleport (70)
- [x] Func_DynWall (71)
- [x] Func_Message (72)
- [x] Func_GameAction (73)
- [x] Info_NoWeather (80): stops rain / snow
- [x] Info_RadarIcon (81)
- [x] Trigger_Start (90), Trigger_Move (91), Trigger_Use (93), Trigger_Delay (94), Trigger_Once (95), Trigger_If (96)
- [ ] Trigger_Hit (92)
- Gen_Weather fog/dust/smog (2..4) spawn only on frames where DAT_140ddf540 = 1: the main loop's 1000 ms timer (0x140236801), i.e. max(ints[1],1) Tp 21 once per second, not per frame. Rain/snow are per frame.
