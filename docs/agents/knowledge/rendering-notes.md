Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Rendering notes

- Roof parapet shadow: none (checked in `FUN_140260330`).
- Shadow occlusion halos merge with max.
- Entities in state ≠ 0 are not drawn.
- **Info_BombSpot (type 5):** The original CS2D renders NO floor decals on the map for bomb spots. The letters "A" and "B" exist only as radar icons (`Info_RadarIcon`) in the radar / minimap HUD.
- **Player shadow pass order:** World floor shadows (`cs2d_render_map_shadows` / CPU shadow chunks) and the lighting buffer (`cs2d_light_draw_buffer`) are rendered *before* the players. This ensures the floor receives the day shadow silhouettes and world shadows while players are rendered cleanly on top, preventing player bodies from being darkened by ~50% from an overhead multiply pass.
- With the light engine on (`light` > 0, the shipped config) the day shadows come from the light buffer (see
  "Light engine"); the CPU shadow map below is only used with `light 0` and was never compared with the original's
  `mapshadows 1` path (`FUN_1401165a0`).
- The shadow map is partitioned into 16x16-tile chunks (256x256 texels each). When `map->entity_solid_version`
  changes (DynWall toggles or player buildings change), fast delta tracking marks only the affected localized chunks dirty,
  avoiding full-map software rasterization freezes and dynamic heap reallocations. Visible dirty chunks are lazily generated
  and updated into GPU textures.
- **Player shadows (REA `Tpl.draw 0x140121160 / 0x140122298`):**
  - Drawn when `shadows` (`DAT_140a14f7b`) is not 0 (Tpl.draw 0x140121160; `playershadows` does not gate it).
  - Image: `incbindata/shadow.bmp` (`PTR_bbNullObject_140a0e038`, 32x32 circular soft shadow loaded with maskMode 3).
  - Geometry: Centered on player position `(sx, sy)` including recoil backward kickback, rotation 0.0 deg.
  - Scale: `1.5x` (`32.0f * 1.5f = 48.0f` px width/height).
  - Color & Alpha: Black `(0, 0, 0)`, alpha `p0 * 0.5` where `p0` is 1, or `|sin(ms / 10)| * 0.1 + 0.05` under the stealth
    suit (armor 206): not the `shadows` percentage.
  - Also applied to hostages (`Info_Hostage`, `Thostage.draw 0x140147090`) with the same 48x48 scale 1.5.
  - With the light engine the original also puts each player's silhouette into the day shadows (`FUN_140112340`, see
    "Light engine"); our client does exactly that when `cs2d_light_day_shadows()`.
  - **Without the light engine our client differs on purpose (user request, 2026-10-12):** players cast their silhouette (legs, torso, held
    weapon) shifted along the map sun, with the obstacle shadow length formula for h = 6 px
    (`dz = -3 - 6 |sin(sun - 180)| / 2`; sun 225 -> (+3.6, +3.6) px). Two passes: the parts mark texels with alpha
    >= 0.5 in the depth buffer (color writes off), then one black quad at the shadow alpha fills `GL_EQUAL` pixels, so
    overlaps leave no seams (`set_player_shadow_offset`, `cs2d_render_player`). Hostages keep the blob.
- **Ground item & projectile shadows (REA `Titem.draw 0x1401483a0`, `Te.draw 0x140260000`, `Tpro.draw 0x140288702`):**
  - Triggered when `itemshadows != 0` and `shadows > 0` (`DAT_140ddf183` and `DAT_140a14f7b`).
  - Circular ground shadow: `incbindata/shadow.bmp` at scale 1.0 (32x32 px) in black `(0,0,0)` at `(x, y)` with alpha `shadows * 0.01f`.
  - Item silhouette drop shadow: Item image (`tex_weapons_d[type]` or projectile sprite) rendered in black `(0,0,0)` with alpha `shadows * 0.01f` offset by `+1.0 px, +1.0 px` at `(x + 1.0f, y + 1.0f)`.
  - Normal item drawn at `(x, y)` in full color above both shadows.

### Loading screen "Loading Map" (LoadScreen state 3)
- `cs2d_load_map` 0x1401da1e0 sets state 3, progress 0; every `cs2d_loadscreen_status` 0x1401d6510 / `SetTitle`
  redraws+presents a full frame and adds `1/PROCESS_MAPS` (value not recovered). Steps: Loading Map, Loading Map Header,
  Generating Background, Loading Tileset, Initializing Tileset/Map Tiles, Loading Map Tiles/Modifiers, Loading Entities,
  Counting Map Mission Entities, Updating Map Data, Checking Map Integrity, Generating Tile Effects/Blending,
  Initializing Lighting, Caching Entities.
- Draw (`DrawLoadingScreen` 0x1401d1610, `DrawWindow` 0x14012bee0): window 350x200 centered, title + 0-3 dots,
  step text 0xbc centered at bar_center-42; bar frame 204x14 0x40, 202x12 black, fill 200*p x10 0x3c, edges 0x60/0x20.
  Ours: `cs2d_render_loadscreen` (no menu background / corner images yet).
- Draw order (cs2d_draw_world 0x140290310): floor, map layer 3 (prop layer 2 = obstacles), Tp 3, Tpl.drawAll, Tpro.draw_flying, Tp 4, map layer 4 (prop layer 3 = walls, tile height 32), Tdo walls, Te pass 2, overwall, Tp 5/7/6. Walls cover the players. Tp 0x1b (smoke cloud) is created in list 7 (Tp.Create), i.e. after the walls; screenshots of CS2D.exe confirm the smoke is over the walls.
