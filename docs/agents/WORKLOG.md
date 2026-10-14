# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- 2026-10-14 Claude: editor entity helpers (zones, Env_Light radius, LightStripe, Teleport line, trigger links) now hover-only / Space, per Te.drawineditor + Ted_tr.drawat.
- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

## 2026-10-14 — Claude — Editor Entity Properties, editor and spectator minimap
- **Changed:** `cs2d_ed_prop_*` (editor.c: per-type widget table of `FUN_1402aa220`, load / apply with its clamps
  and conversions), `cs2d_ed_minimap_build` (`FUN_14019d7c0`); editor scene: Entity Properties window (right click
  on an entity), rotation dial, color box -> Color window, minimap with click / drag navigation; spectator minimap
  (`FUN_140299d70`) toggled by the Map button, click moves the free-look camera. Render: `cs2d_texture_upload_rgba`,
  `cs2d_render_rect_add`.
- **Verified:** build, `test_editor_entity_props` (40 / 40); `--capture` of the editor on de_dust2 (minimap click
  moved the camera, Env_Object window, dial drag). The spectator minimap was not run against a server.
- **Left:** OPEN_ISSUES "Map editor" / "Spectator".

## 2026-10-13 — Claude — Editor Display Settings window, console over editor UI
- Client windows (console, ...) now build after the editor UI, so they draw on top of it.
- Display Settings window (`build_disp_window`), `sys/editor.cfg` load/save (`cs2d_ed_disp_*`), grid / shadows / entities gated; G toggles the grid.
- Verified: build, tests (`test_editor` cfg byte round trip). The `--capture` `!mouse` clicks did not reach the editor sidebar (Load Map did not open either), so no screenshot yet.

## 2026-10-13 — Claude — Gen_Particles / Gen_Sprites / Gen_Weather / Gen_FX + Info_NoWeather

- **Changed:** new `src/cs2d_gen.c` (+ `include/cs2d_gen.h`): spawn rules of `Te.draw` pass 2 and the `Tp` types
  1/3/12/14/17/19/20/21 in fixed 60 Hz steps; `cs2d_render_gen` / `cs2d_render_gen_flash` (render.c) with
  wave/snowflake/raindrop/smokepuff textures; hooked in `game_load_map`, the game update and `game_render`.
- **Verified:** `test_gen_entities` (39 / 39); our client screenshots of de_aztec_classic (rain + fog) and de_inferno
  (chimney smoke). Not compared to original screenshots yet.
- **Left:** see OPEN_ISSUES "Gen_* entities".

## 2026-10-13 — Claude — Map loading screen

- **Changed:** `cs2d_render_loadscreen` (render.c) draws+presents a "Loading Map" window with step text and progress
  bar (port of `LoadScreen::DrawLoadingScreen` 0x1401d1610 state 3); `game_load_map` calls it at each step.
- **Verified:** build + 38 / 38; not screenshot-compared.
- **Left:** see OPEN_ISSUES "Loading screen".

## 2026-10-13 — Claude — Env_Decal (TDecal) + entity checklist

- **Changed:** Env_Decal moved out of the ground pass into its own `CS2D_EPASS_DECAL`, drawn right after the tiles
  (`render_decals`): alpha = `strs[0]` as float with no invented default ("" = invisible), frame clamped, one decal
  per tile (first wins), entity_state ignored. Source: `Te.setup` 0x1401ad930 case 0x18, `TDecal::Add` 0x140005c70,
  `FUN_140110c60`. Added the implementation checklist to `knowledge/entities.md`.
- **Verified:** `test_env_decal` + updated `test_entity_culling`; 38 / 38. No screenshot comparison yet.
- **Left:** decals on wall/obstacle tiles are drawn in the original's wall/obstacle tile loops (layer 4 / 5);
  ours draws all after the single tile pass.
