# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-13 — Antigravity — Fix player shadow order and remove BombSpot decals on floor

- **Changed:**
  - Removed floor rendering of `bspot_a.png` / `bspot_b.png` for `CS2D_ENT_INFO_BOMBSPOT` in `src/cs2d_render.c`:
    the original CS2D renders no floor decals for BombSpots on the game map; bombspot icons appear only on the radar / minimap.
  - Reordered the game scene render loop in `src/cs2d_scene_game.c`:
    - Moved the light buffer (`cs2d_light_draw_buffer()`) and world floor shadows (`cs2d_render_map_shadows()`) to render *before* the player drawing loop instead of after it.
    - Previously, `cs2d_light_draw_buffer()` was multiplied (blend mode 5) over the scene *after* players, multiplying the black player day shadow silhouette directly over the player's body and darkening it by ~50%.
    - With the new pass order, world shadows, day shadows and the player's floor shadow silhouette are rendered onto the floor first, and player bodies are rendered on top with their legs, torso and weapons clean and unshaded. Overhead passes (rooftop sprites and tree canopies) continue to render above players.
  - Separated CPU floor shadow chunk pass (`cs2d_render_map_shadows`) from `cs2d_render_map_overhead`, calling it before players in `src/cs2d_scene_game.c` and before overhead entities in `src/cs2d_scene_editor.c`.
- **Verified:**
  - `build.bat` compiles `test_cs2d.exe` and `cs2d_client.exe` without warnings or errors.
  - `test_cs2d.exe` regression suite: **37 / 37** tests passing.

## 2026-10-13 — Antigravity — Dynamic map list in New Game (Create Server) from maps/ directory

- **Changed:**
  - Replaced the hardcoded 10-map list in Create Server ("New Game") with a dynamic directory scanner
    (`cs2d_maplist_load` in `src/cs2d_client.c`, declared in `include/cs2d_client.h`).
  - Faithfully ported the original BlitzMax map scanner logic (`Tgui_ml::Load` 0x1400af1d0 and `FUN_1401a4610`):
    - Reads map prefix categories from `sys/maptypes.lst` (e.g. `as_`, `cs_`, `de_`, `dm_`, `ctf_`, etc.).
    - Scans `maps/*.map`, filters out `temp.map` and `autosave.map`, lowercases and strips extension.
    - Groups maps under their respective section header in `maptypes.lst` order, sorted alphabetically per group.
    - Remaining maps without matching prefixes are grouped under "Other Maps" (`sys/language` entry 0182).
  - Integrated `cs2d_ui_scrollbar` with fixed 300px list container height and mouse wheel scrolling support.
  - Selected map is stored by name (`srv_selected_map`), synced with Map Cycle view, and used for console `map <name>` execution.
  - Fixed duplicate `work_ms` declaration in `src/main_client.c`.
  - Added test `test_new_game_maplist` to `tests/test_cs2d.c` validating grouping, sorting, exclusions, and headers against `sys/maptypes.lst`.
- **Verified:**
  - `build.bat` compiles `test_cs2d.exe` and `cs2d_client.exe` cleanly.
  - `test_cs2d.exe` regression suite: all **37 / 37** tests passing.
- **Left:**
  - Map cycle editing (add/remove from cycle).

- 2026-10-13: chat input starting with '/' runs the rest as a console command (cs2d_client_handle_console_key); not yet checked against the original.
