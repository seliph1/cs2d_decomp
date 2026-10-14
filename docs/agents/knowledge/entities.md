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

## Implementation checklist (client)

Status of each entity type in the reimplementation (2026-10-13). [x] done, [~] partial, [ ] missing.

- [x] Info_T / Info_CT (0, 1): spawn points
- [~] Info_VIP (2): game rules only
- [x] Info_Hostage (3): render + game
- [x] Info_RescuePoint (4), Info_BombSpot (5), Info_EscapePoint (6): zones
- [ ] Info_Target (7): bots only (server-side)
- [x] Info_Animation (8): tile animation
- [ ] Info_Storm (9)
- [ ] Info_TileFX (10)
- [ ] Info_NoBuying (11)
- [ ] Info_NoWeapons (12)
- [ ] Info_NoFOW (13)
- [ ] Info_Quake (14)
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
- [ ] Gen_Particles (50)
- [ ] Gen_Sprites (51)
- [ ] Gen_Weather (52)
- [ ] Gen_FX (53)
- [x] Func_Teleport (70)
- [x] Func_DynWall (71)
- [x] Func_Message (72)
- [x] Func_GameAction (73)
- [ ] Info_NoWeather (80)
- [x] Info_RadarIcon (81)
- [x] Trigger_Start (90), Trigger_Move (91), Trigger_Use (93), Trigger_Delay (94), Trigger_Once (95), Trigger_If (96)
- [ ] Trigger_Hit (92)
