# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

Only the 5 newest entries live here; when adding one, move the oldest to the top of `worklog/archive.md` (do not read the archive in full; grep it).

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

## 2026-10-13 — Antigravity — Sprite atlasing for entity sprite set & lifted 64-sprite cache ceiling

- **Changed:**
  - Implemented `include/cs2d_atlas.h` and `src/cs2d_atlas.c`: shelf packing (First-Fit Decreasing) of map
    entity sprites (`Env_Sprite` 22, `Env_Image` 28) into 2048x2048 atlas pages at map load (`game_load_map`,
    `editor_load_map`).
  - Added 4 px extruded margin padding (`CS2D_ATLAS_PAD = 4`) around every cell to eliminate neighbour bleed with `GL_LINEAR`.
  - Maintained separate atlas pages for `mask_mode` 1 (normal / magenta key) and `mask_mode` 3 (black mask / additive).
  - Extended `cs2d_texture_t` with `src_x`, `src_y` page offsets while strictly preserving logical unpadded dimensions in `width` and `height` so camera culling and entity learn size logic remain intact.
  - Replaced the fixed 64-entry `CS2D_MAX_SPRITE_CACHE` ceiling with a dynamically sized sprite cache in `cs2d_gfx_bank_t`.
  - Updated sprite draw UV calculations across `cs2d_render.c` and `cs2d_light.c` to offset UVs by `(src_x, src_y)`.
- **Verified:**
  - `build.bat` clean; `test_cs2d.exe` passed 35 / 36 (added `test_sprite_atlas_packer` covering >64 sprites, no overlaps, UV round-trips, and map atlas loading without truncation).
  - Screenshots at `tp 44 25` (`screens/atlas_before.bmp` vs `screens/atlas_after.bmp`) are 100% byte-identical (1,225,014 / 1,225,014 bytes equal).
  - First-frame freeze eliminated: map load builds atlas in **172.35 ms** for 156 sprites across 2 pages; first frame render max time dropped from **1447 ms** to **7.7 ms** (187x improvement).
  - GPU time per frame dropped from **1.912 ms** to **1.750 ms** (-8.5%) with light, and from **1.433 ms** to **1.317 ms** (-8.1%) with `light=0,mapshadows=0`.
  - Draw calls and texture switches dropped from 672.7 / 296.2 to **664.7** / **288.3** in steady-state, and down to **472.8** / **104.1** at `tp 44 25`.
- **Left:**
  - Light engine internal texture atlas (273 draw calls, 256 texture switches with light on).
  - Unconditional 128x128 radar minimap upload on every frame.
  - VSync OFF frame cap.

## 2026-10-13 — Cline — Why our client uses ~20 % GPU where the original uses 0.6 %

- **Question (user):** Task Manager shows ~20 % GPU on `fun_roleplay` for our client, 0.6 % for the
  original. Reproduced first: while our client ran, the whole desktop's `engtype_3D` total was
  **17.85 %**, of which **17.76 %** was our pid — the attribution is not a Task-Manager artifact.
- **Method:** the counter Task Manager itself uses, `\GPU Engine(*)\Utilization Percentage`
  (`engtype_3D`, per pid), sampled once a second. Two new tools: `tools/agent/perf_gpu_matrix.ps1`
  (steady state, rewrites `sys/config.cfg` per case and restores it) and
  `tools/agent/perf_gpu_frames.ps1` + `tools/agent/perf_frames.sh` (a `--capture:"map …;!wait 600"`
  run = 636 frames, so GPU busy time / frames gives ms of GPU per frame). New `CS2D_MSAA` env gate in
  `src/main_client.c` (default unchanged: 4x) is the axis one of the runs compares.
- **Measured** (850x480, `fun_roleplay`, shipped `light 5 mapshadows 4 shadows 50`): **22.7 % GPU
  with VSync on** (13.8 % with every light setting off), **42-46 % with VSync off** where the CPU
  goes past one core; per frame **0.24 ms** (main menu) against **2.0-2.5 ms** (drawn map; MSAA 4x vs
  `CS2D_MSAA=0` is inside the run-to-run spread, MSAA does cost ~1.1 ms of CPU). The original's 0.6 %
  is ~0.1 ms of GPU per frame, so our world frame is 20-40x heavier in GPU terms.
- **Conclusion:** the cost is in the world pass (the menu proves the present path is cheap), ~40 % of
  it is the light engine + map shadows, and VSync OFF additionally removes any frame cap (the
  original locks 65 FPS). Numbers: `KNOWLEDGE.md` "Performance".
- **Then attributed the draw calls** (`CS2D_DRAWCALLS=1`): `fun_roleplay` = **699 calls/frame for
  32,557 vertices** (avg batch 47 verts), of which **320 are texture switches** (one `gfx/...` file per
  `Env_Sprite`/`Env_Image`) and 273 calls + 256 switches belong to the light engine; the tileset grid
  already goes out in **one** call (max batch 6,210 verts, 0 overflows). The remaining ~360 calls are
  blend toggles and target flushes. Same conclusion as LÖVE's `SpriteBatch` documentation: batching
  needs a shared image, so the next win is atlasing the sprite set — grouping by state is not an
  option because the sprite order is the original's painter's order. Tool: `perf_gpu_frames.ps1
  -DrawCalls [-Config 'key=val,…']`.
- **Left (moved to `OPEN_ISSUES.md`):** decide the frame cap, and re-measure before optimising the
  batcher/radar.

## 2026-10-12 — Cline — Entity index & view culling (5 sweeps per frame → one typed list per pass)

- **Measured first** (`.map` parse off-line, then the new test): `fun_roleplay` is **360x512 tiles with 9901
  entities**, and only **22 of them are inside a 27x15 tile view on average** (0.2 % — over all 166,332 camera
  positions, min 0, max 212). The five map passes (`src/cs2d_render.c`: tile-animation remap, Breakable /
  DynWall, ground actors, rooftop sprites, rooftop objects) swept all 9901 entities each, so **49,505
  dispatches per frame for ~20 draws**; `Env_Sound` (769), `Trigger_Delay` (509) and `Trigger_If` (387) were
  swept five times per frame without drawing anything.
- **Changed:** `src/cs2d_render.c` "Entity index & view culling": one cached list per pass, **ascending entity
  index** so the draw order of the original sweep is unchanged, rebuilt only when the map or its entity table
  changes (`map->entity_table_version`, new in `include/cs2d_types.h`, bumped by the loader in `cs2d_map.c`
  and by `cs2d_ed_entity_add` / `cs2d_ed_entity_remove`). Entities whose draw box (tile centre ±
  `cs2d_render_entity_margin` tiles) cannot touch the view are skipped before the string parse, the sprite
  lookup and the draw. `strs[4]` of `Env_Sprite` is parsed once into the index (was `atoi` per sprite per
  pass, 3x per frame) and `render_env_sprite_entity` takes the map + entity index now. A texture sized
  sprite/image reports the size it just drew (`cs2d_render_entity_learn_size`), so it is culled from the
  second frame on.
- **Verified:** new `test_entity_culling` in `tests/test_cs2d.c` — pass membership and ascending order,
  rebuild on `entity_table_version`, the culling invariant ("no entity whose draw box touches the view is
  dropped") over 17x22 cameras, the learned sprite size, and three real maps. Measured there: `fun_roleplay`
  9901 entities → 10,357 list entries and **998 considered per frame (49.6x fewer than the 49,505)**, 902 after
  the sprite sizes are learned; `de_dust2` 1403 → 2,907 entries, 218 considered (vs 7,015);
  `prison_funst3r` 1608 → 1,651 entries, 311 considered (vs 8,040). Suite 34 / 35 (the only failure is the
  known `test_file_transfer` "Transfer list" gap).
- **Left:** the freeze numbers of `OPEN_ISSUES.md` were **not** re-timed. An `Env_Sprite` with an explicit
  `ints[0]` / `ints[1]` size is now skipped *and not loaded* when it is off screen, even on the first frame,
  so expect the first frame to be cheaper on maps that size their sprites (which is what the 998 → 902
  difference after learning shows: those had no explicit size). A texture sized sprite / image is still drawn
  once before it can be culled, so the preload stays owed. `Env_Object` and texture sized `Env_Image` frames
  are never culled yet (see `OPEN_ISSUES.md`).



- **Question (user):** the game ran with less performance than expected with VSync disabled.
- **Method:** temporary per-section timers in the `main_client.c` loop (events / update / render /
  `glFinish` / `end_frame` / `SDL_Delay`) plus a native-pacing mode (`CS2D_PERF=<frames>`,
  `CS2D_PERF_VSYNC=0|1`, `CS2D_PERF_CMD="<console cmd>"`); all of it removed again before the session
  ended, so the tree only keeps the findings.
- **Measured (this machine, 850x480, `sys/config.cfg` at its shipped maximum, light 5 / mapshadows 4):**
  - **VSync OFF:** prison_funst3r 167-176 FPS (5.7-6.1 ms/frame), de_dust2 163-176 FPS,
    fun_roleplay 72-98 FPS (10.2-14 ms/frame).
  - **VSync ON:** 59.6 FPS (locked to the 60 Hz panel). The `render` number jumps 4.4 -> 15.2 ms
    there, but that is the deferred vblank wait landing in the next GL call — `end_frame` still
    returns in 0.04 ms and there is no stall.
  - Split at steady state (prison_funst3r, VSync off): events 0.008 | update 0.02 | render 4.4 |
    `SDL_Delay(1)` 1.49 ms.
  - **The original locks its own frame at ~65 FPS** (`15.384615 ms` = 1000/65,
    `docs/dump/cs2d/_global/1402f0000.c:2284`), so our uncapped 167 FPS is *faster* than the original.
  - `SDL_Delay(1)` costs 1.8 ms and a swap with interval 0 presents in 0.49 ms (2029 FPS) here, so
    neither the loop delay nor the swap is a cap on this machine.
- **Real defect found:** the first frame after a map is set up freezes for **~0.1 ms per map entity**
  (133 ms prison_funst3r / 140 ms de_dust2 / **1447 ms fun_roleplay**, 9902 entities). The cost is
  entirely inside `cs2d_client_render` and survives `light 0`, `mapshadows 0` and `drawradar 0`, so it
  is the lazy per-entity sprite load, not the light engine, the map shadows or the radar/minimap.
- **Method trap for the next session:** the `--capture` harness paces itself with `SDL_Delay(16)` per
  frame, so its "avg ms/frame" is CPU+GPU work only and its FPS is the harness's, never the game's — two
  runs of different length gave a false "36.8 FPS" before this was noticed. It also resets the stats at
  every step, which is why the first-frame freeze never shows up in its output.
- **Reverted:** a speculative streaming-VBO change in `cs2d_gl.c` (fresh write offset per batch +
  orphan on wrap). It left the vsync-on frame time unchanged, so the "BufferSubData stall" hypothesis
  was wrong; kept out rather than keeping an unproven rewrite.
- **Verified:** `Build Successful!`, `test_cs2d.exe` 33 / 34 (the known `test_file_transfer`
  "Transfer list" gap). `git status sys/` clean.


- **Changed:** the OpenGL swap interval is now a real setting instead of the hard-coded
  `SDL_GL_SetSwapInterval(1)`. Added `cs2d_render_set_vsync` / `cs2d_render_get_vsync`
  (`src/cs2d_render.c`, `include/cs2d_render.h`), a `vsync` field in `cs2d_config_t` (`sys/config.cfg`
  "vsync", default 1, `DAT_140a12960`) and the `vsync [0|1]` console command (a bare `vsync` toggles)
  in `src/cs2d_client.c`, with a `help` line. `cs2d_client_init` applies the config value at startup.
- **Verified:** `test_vsync_command` in `tests/test_cs2d.c` (config parse, getters/setters, the command
  and the bare toggle). Full suite 33 / 34; the only failure is the known `test_file_transfer`
  "Transfer list" gap (see `OPEN_ISSUES.md`).
- **Left:** the options menu still does not expose VSync (only the console and `sys/config.cfg` do).

