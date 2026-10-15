Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Light engine (TSmartLighting)

Source: dump `docs/dump/cs2d/lighting/` (`TSmartLighting`, `TLightSource`, `TLightingRoom`, `TLightingStripe`,
`TLightingSprite(Floor)`, `ObstacleShadow(2)`, `SmartImageBuffer`, `LightFBOManager`), the frame `FUN_140281fd0`, the map
setup `FUN_1401d6790`, and the functions themselves run in Unicorn (`tools/agent/emu_light.py`); screenshots of the
original on fun_roleplay by day and at night (`tools/agent/lua/light_probe.lua`, `night_probe.lua`). Code:
`src/cs2d_light.c`, checked call for call against the original (`test_light_engine`, `tools/agent/light_check.sh`).
Fixtures depend on the map files: `maps/fun_roleplay.map` was replaced in cbd56be, so they were regenerated (2026-10-14).

- **Settings** (sys/config.cfg): `light` 0..5 (`DAT_140ddf185`; 0 = off; `SetQuality(light - 1)`: 1..3 buffers at half
  size (scale 2), 4 full, 5 full + `LIGHTFIXES`), `mapshadows` 0..4 (`DAT_140ddf190`: day shadows, floor sprites and
  occlusion need > 1, roof layers > 2, high-wall shadows > 3), `shadows` (day shadow floor `255 - shadows * 2.55`).
  Server: `sv_forcelight` (`[237,19]`; `[252,6]` u8) turns `light 0` into 1; `sv_daylighttime` (`[237,20]`; in `[252,6]`
  a u16 = value + 1 after `mp_hovertext`, a u8, the background string and 3 i32) replaces the map's sun angle.
- **Defaults** (static init 0x140305f79..0x14030604f): penumbra 55, occlusion on, night color 0, sun offset 0,
  ScreenWidth 850, TilesX 27; `DAT_140ddf418` is the game state (0 menu, 1 / 2 in a game, 4 editor).
- **Sun:** `ShadowRotation` = the map's daylight (225 unless the header holds 1000 + angle) or sv_daylighttime; it eases
  toward the target by 10 % x dt per frame when within 90 degrees, else jumps. Day = 180 < angle < 360.
- **Tiles** (`SetTile` from `cs2d_update_mapdata`): TileMap = tile + 1 (0x100 for an entity wall, -1 for tile 0);
  TileMapMode 2 wall (cell property 1), 1 obstacle (2), 0 anything else. The cell property is the tile mode, then
  Env_Breakable `ints[6]` (1 wall, 2 floor, 3 obstacle, else wall), closed Func_DynWall `ints[1]` (0 wall, 1 obstacle,
  2 / 3 the no-shadow modes, 4 the mode of tile `ints[0]`), solid buildings (wall). Heights = the map's tile heights,
  index 255 = 32.
- **Entities** (`FUN_1401d6790`): Env_Room x, y, w `ints[0]`, h `ints[1]`, roof height `ints[2]`, color `ints[3..5]`,
  edge mode `ints[6]` (roommap_invert frames mode * 8 + 0..7), roof shadow `ints[7]`, day color `1 - ints[8]`, layer
  `ints[9]`, offset `strs[0]` / `strs[1]`, transparency `strs[2]` (drawn only when <= 0.95). Env_Light at the tile center
  + (`strs[1]`, `strs[2]`), size min(`ints[0]`, 256), color `ints[1..3]`, angles `ints[4]` / `ints[5]` (0, 0 = 0..359),
  additive alpha `strs[0]`, rotation `strs[3]`, max size 512; entity state 1 = off; `Te.draw(2)` adds the speed `ints[6]` to
  the angles every frame. Env_LightStripe at + (`ints[7]`, `ints[8]`) tiles, w / h `ints[0..1]`, gfx_stream frame
  `ints[2]`, color `ints[3..5]`, day `1 - ints[6]`.
- **Frame** (`FUN_140281fd0`): `FUN_140262ed0` builds the light buffer first; floor, entities, items, then the **floor
  occlusion** (`DrawTilesFloor`), `Te.draw(1)`, players, the **light color** (`DrawLightsColorList`: each light's buffer
  added in color x alpha x 0.65), overhead, then the **light buffer** multiplied over the world before the HUD. Ours draws the player silhouette under the
  body instead of in the buffer (else the 4 px offset silhouette darkens the player); light 0: CPU shadows (`FUN_1401165a0`)
  before the players.
- **Light buffer** (`GenerateMainBuffer`, 854 x 480 / scale): white; by day `GenerateDayBuffer` + floor light sprites
  (added); rooms (alpha blend, their color); toning (multiply by `gfx_toning.png` row 0 at index
  ((ShadowRotation + 200) mod 360) / 360 x 1024, then add the night color); light sprites; each light's buffer added in
  its color; stripes added.
- **Day buffer:** every wall and obstacle casts the hexagon of its tile swept along the sun by
  `WallSize = (-h / 2 - h |cos(sun - 180)| / 2)`, black under the shade blend, with a blurred rim (`gfx_blur` quads on the
  edges where the neighbour height differs, `cs2d_draw_texturedpoly_3`); player silhouettes (black, 4 px away from the
  sun); obstacles and wall tops white again; with mapshadows > 3 walls above 18 px get a hexagon of `WallSize + 10`
  merged per row plus its rim; room roofs with a shadow cast the hexagon of the room (`-h / 2 - h / 2 |cos|`); roof
  layers 1..5 each shade and then whiten their area; void tiles white; at last + max(255 - night color, floor) everywhere.
- **Light buffers** (`UpdateLightBuffer`, 512 / scale, kept while on screen, redrawn on change): white; for every wall
  within size / 32 + 4 tiles `CalculateShadow` picks two silhouette corners by the light's direction and `DrawLineLight`
  draws the shadow polygon (2 x size long, closed 6 x size beyond the middle) with a 55 px penumbra triangle at each end
  unless a neighbour wall continues it (table `s_shadow_case`); walls white again; cone limits as black 5-degree wedges
  plus penumbra; multiplied by `gfx_light.png` stretched to 2 x size; black outside its square.
- **Floor occlusion** (`DrawTilesFloor`): pass 1 `gfx_shadowmap.png` on every non-wall tile from its wall neighbours,
  pass 2 `gfx_oclussionmap.png` on floor tiles from wall and obstacle neighbours; frames, rotations and mirrors per
  8-neighbour pattern come from the original (`tools/agent/gen_occlusion_table.py`).
- **Light sprites:** Env_Sprite with `strs[7]` "1" (white light sprite), alarms (Env_Object 2: flare1 in its color),
  Trigger_Use glow buttons (flare1 at 0.7); floor light sprites (by day): palm and tree leaves, white. An Env_Sprite whose
  blend is not light / shade / grayscale is added white to the day buffer (`ObstacleShadow2`), so roof sprites are lit.
- **Max2D quirks:** the SDL driver clamps colors to 0..255; blend 1 / 3 = alpha, 4 = add, 5 = multiply (ignores alpha);
  `cs2d_DrawTexturedPoly` emits every fan triangle twice, so the penumbra and blur textures apply squared.
- **Trigger_Use buttons** (`Te.draw` layer 1): `incbindata/buttons.bmp` frame `ints[0] - 1` (1..10), rotated by `ints[1]`;
  above 10 frame 1 plus a pulsing glow (11 / 16 red, 12 / 17 green, 13 blue, 14 yellow, 15 white).
- **Light pool** (`Tp_lightpool`): `GetLight(x, y, size, color, max, alpha)` (0x140039920) reuses the first pooled
  light with `MaxLightSize <= max` (moved, resized, recolored, re-alpha'd, on), else `CreateLight(..., 0, 359)`
  (appended to `TLightSource.List`; size capped at `MaxLightSize / 2`, also by `ChangeRadius`). `ReturnLight` = state
  0 + back to the pool. Code: `pool_get` / `pool_return`; Lua `<light>` images use it too.
- **Light particles** (Tp 0x1f, `cs2d_light_flash`): `Tp.CreateLight(x, y, r, g, b, size, shrink)` (0x14009ba30) needs
  x, y >= 1 on the map and not a wall cell, raises channels < 35 to 35, `GetLight(.., 256, 1.0)`; `Tp.draw` does
  `fx -= fy * g_dt`, `ChangeRadius(fx)` while > 16, else ReturnLight. Callers (all need particles > 3 and light on):
  firearm shot `Tshoot.Shoot` 0x140205d9d at the muzzle + Rand(-2, 2): (255, 255, 0), 100, 25, then `ChangeAlpha(0.7)`
  (0x140bd6cc0), not for style 4 (silencer on), only the first pellet; `Tp.explosion` (255, Rand(150, 255), 0, 180, 5)
  while explosion_count < 11; flashbang (Tpro.draw_ground 0x34) `Tp.Create(0x1f)` (255, 255, 0 unclamped, 100, 25)
  + CreateLight white 100 / 15. Not ported: lightning (55, 105, 255), laser / style 6 and turret (p8 92: 15, 35, 255)
  variants, flamethrower (Tp color, 100, 5), alarms (255, 0, 0, 80, 20 in FUN_1402553d0).
- **Flashlights** (`cs2d_light_flashlight`): map setup creates one per player, `CreateLight(-1000, -1000, 162, white,
  325, 0.0, 0, 359)`, LightType 1 (+0x6c), off. `Tpl.update` (0x14021e805): on when alive, not fogged, flashlight 1,
  mp_flashlight 1, not a T in Zombies; `ChangePosition(x + rx, y + ry)` (recoil kick), `ChangeRotation((int)rot)`.
  Alpha 0, so no color pass. `Tpl.draw` adds flare1 (255, 255, 200) alpha 0.4, rotation wrot * -0.2, 9 px ahead.
  Emulator: our buffers for a flashlight + a muzzle light match the original call for call (`*_dyn_*` fixtures).
- **Env_Sprite strs[7] "affect lighting"** is a light sprite (added white into the light buffer), not an occluder:
  light shadows (`CalculateShadow`) come only from wall cells (TileMapMode 2). Non-light / shade / grayscale sprites only
  keep their area out of the *day* shadows (ObstacleShadow2).
- **Entity count:** fun_roleplay has 9901 entities (812 light entities); our loader used to keep 2048
  (`CS2D_MAX_ENTITIES` is now 16384).
