Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Advanced radar / minimap (TSmartRadar / TSmartMinimap)

Source: REA / dump `docs/dump/cs2d/TSmartRadar.c`, `TSmartMinimap.c`, `_global/140120000.c`
(`cs2d_draw_line_3` 0x1401262c0), `_global/140270000.c` (`FUN_14027c8c0`, `FUN_14027dff0`), `_global/140290000.c`
(`FUN_140291c58`). Code: `cs2d_render.c` (`ensure_minimap_texture`, radar blips in `cs2d_render_hud`).

- **Advanced radar (top-left circular HUD) = `TSmartRadar`.** `Init` (0x1400101b0): `BufferSize = 100`,
  `scale = 1.0`, `cx = cy = 50`. `SetMinimapScale` (0x140010210) sets `scale` and re-centers. `SetCamera(f)` sets
  `CameraX/Y` from the local player's pixel position (`Tpl +0x240`, x * 32).
- **Radar minimap image** is the actual map rendered at 1/16 scale (each 32 px tile -> 2 px): `FUN_14027c8c0`
  (0x14027c8c0), chunked 30x40 / 60x80 / 120x160 / 480x640, saved to `sys/cache/radars/<map>.png` + `.cache`;
  `CheckCache` (0x14027d460) loads it. `Update` (0x14027d870) draws it into a 100x100 buffer at `2*scale` px per tile,
  offset by `scale * CameraX * -0.0625` (so 16 world px = 1 radar px = half a tile), then a circular crop
  (`hud_radar_advanced.png`, alpha 0.7).
- **Blips** (`cs2d_draw_line_3`, 0x1401262c0) use `gfx/hud_radar_icons.png` (32x32, 24 frames,
  `PTR_bbNullObject_140a0de00`): frame 0 = player dot, 2 = dot (bomb spots), 3 = dot (hostage), 4 = death "X",
  8/9 = 'A'/'B' (Info_RadarIcon `ints[0]`). `DrawIcon` (0x140010370) packs the colour as RGBA (`R<<24|G<<16|B<<8|A`).
  Player colours (`FUN_140291c58`): local player red (pulsing), Terrorist `0xff9696ff` (light red), Counter-Terrorist
  `0x9696ffff` (light blue); enemies are hidden in team modes (radarhack check "SE02RD") and shown in Deathmatch
  (`team_game()` == 0). Escape/rescue/bomb spots pulse with `sin/cos` of the tick.
- **Full minimap (big map) = `TSmartMinimap`.** `Init` (0x1400865f0) computes `MapScale`/`MapLoadScale` from the map
  size so the whole map fits the screen height (`(height - 100) / (max dim * 32)`). `Draw` -> `FUN_1402696b0`
  (0x1402696b0) draws it at 0.4x (non-detail) or 0.95x (detail) of `MapScale`, plus players (`gfx/minimap_player.png`,
  `gfx/minimap_arrow.png`), entities, death icons, and the camera viewport rectangle. Generation `FUN_14027dff0` renders
  the map into `sys/cache/minimaps/<map>.png`.
- **Our radar (2026-10-12):** renders the real tileset into a full-map minimap (2 px/tile, box-averaged via
  `tileblend_find`) and crops it around the player's exact pixel position (smooth, sub-tile), at 16 world px/radar px.
  Player blips match the original colours; enemy blips hidden outside Deathmatch. Not reproduced: the full-screen
  `TSmartMinimap` overlay, and tile rotation/brightness are not applied to the radar minimap (2 px/tile makes them
  negligible).
