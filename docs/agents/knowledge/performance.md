Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Performance (measured 2026-10-12, desktop, 850x480)

Measured on this machine with `sys/config.cfg` at its shipped maximum (`light 5`, `mapshadows 4`,
`shadows 50`). Method: temporary per-section timers in the `main_client.c` loop (events / update /
render / `glFinish` / `end_frame` / `SDL_Delay`) plus a native-pacing mode that ran N frames and
printed the split; the instrumentation was removed again (see `WORKLOG.md`).

| scene (entities) | VSync OFF | VSync ON |
|---|---|---|
| `prison_funst3r` (1609) | 167-176 FPS, 5.7-6.1 ms/frame | 59.6 FPS |
| `de_dust2` (1404) | 163-176 FPS | — |
| `fun_roleplay` (9902) | 72-98 FPS, 10.2-14 ms/frame | — |

- Steady-state split (`prison_funst3r`, VSync off): events 0.008 | update 0.02 | render 4.4 |
  `SDL_Delay(1)` 1.49 ms; `end_frame` (flush + `SDL_GL_SwapWindow`) 0.03-0.05 ms.
- **The original locks its own frame at ~65 FPS** (`15.384615 ms` = 1000/65, `Delay()` inside the wait
  loop of `docs/dump/cs2d/_global/1402f0000.c:2284`), VSync or not. An uncapped 167 FPS is therefore
  *faster* than the original; our VSync-off build has **no frame cap at all**, so it pegs a core.
- With VSync **on**, `render` reports 15.2 ms instead of 4.4 ms: that is the vblank wait, which the
  driver defers to the next GL call (the swap itself returns in 0.04 ms), not a CPU stall inside the
  renderer. A measurement must not read that as a renderer regression.
- `SDL_Delay(1)` costs 1.8 ms and a swap with interval 0 presents in 0.49 ms (2029 FPS) on this
  machine, so neither the loop's delay nor the swap is a cap here (SDL3 uses a high-resolution
  waitable timer; the old 15.6 ms `Sleep` granularity does not apply).
- **First frame after a map is set up freezes for ~0.1 ms per map entity** — 133 ms `prison_funst3r`,
  140 ms `de_dust2`, **1447 ms `fun_roleplay`**. The cost is inside `cs2d_client_render` and is not
  affected by `light 0`, `mapshadows 0` or `drawradar 0`: it is the lazy per-entity sprite load
  (`cs2d_render.c` ~1100, no image cache in front of it). The original builds its images at map load,
  behind the loading screen.
- **Tooling trap:** the `--capture` harness paces itself with `SDL_Delay(16)` per frame, so its
  "avg ms/frame" is CPU+GPU work only and its FPS is the harness's, never the game's; it also resets
  the stats at every step, so the first-frame freeze never appears in its output. To measure real
  pacing, run without `--capture` (see `WORKLOG.md` for the removed temporary mode, or add a new one).
- **Sprite cache ceiling (lifted 2026-10-13, `src/cs2d_atlas.c`, `src/cs2d_render.c`):** the fixed
  64-entry array `CS2D_MAX_SPRITE_CACHE` was replaced with a dynamic cache `bank->sprite_cache`
  populated from map atlas pages at map load. Maps with >64 distinct sprite paths (`fun_roleplay` has
  157 distinct sprite/mask pairs) no longer drop any sprites. Lookup remains linear `_stricmp` but is
  performed against pre-loaded textures in memory with no lazy disk I/O.
- **Entity index & view culling (added 2026-10-12, `src/cs2d_render.c` "Entity index & view culling"):**
  measured with a `.map` parse: `fun_roleplay` is 360x512 tiles (184,320) with 9901 entities (`22`=2112
  Env_Sprite, `71`=1907 Func_DynWall, `23`=769 Env_Sound, `93`=659 Trigger_Use, `33`=561 Env_Light, …) and
  only **22 entities are inside a 27x15 tile view on average** (0.2 %; min 0, max 212, over all 166,332
  camera positions). `prison_funst3r` (125x230, 1608 entities) sees 27, `de_dust2` (101x88, 1403) 81. The five
  map passes therefore dispatched **5 x entity_count = 49,505** entities per frame for ~20 draws. They now
  walk one cached list per pass (ascending entity index, so the draw order is the original sweep's) and skip
  entities whose draw box (tile centre ± the margin) cannot touch the view. Margins: 2 tiles for the 32x32 /
  tile-aligned types (hostage, bomb spot, both flags, item, decal, Breakable, DynWall, NPC, Use button, and
  32 x 1.42 / 2 = 23 px in general), `margin_for_size(w, h, offx, offy)` for an `Env_Sprite` with an explicit
  `ints[0]` / `ints[1]` size, 0 = never culled for `Env_Object` and texture sized `Env_Image`. A texture sized
  `Env_Sprite` / `Env_Image` is still drawn on the first frame and reports the size it used
  (`cs2d_render_entity_learn_size`), so it is culled from the second frame on. `test_entity_culling` holds the
  numbers: fun_roleplay 9901 → 10,357 list entries, **998 considered per frame** (902 once the sprite sizes
  are learned) instead of 49,505; de_dust2 218 instead of 7015.

### GPU cost of a frame (measured 2026-10-13, same machine, 850x480 from `res 850x480`)

Metric: the counter Task Manager uses, `\GPU Engine(*)\Utilization Percentage`, filtered to our
`cs2d_client.exe` pid and `engtype_3D` (so it is the same number the user sees), plus a run with a
known frame count so it can be converted to ms of GPU time per frame. Tools:
`tools/agent/perf_gpu_matrix.ps1` (steady state, config matrix) and
`tools/agent/perf_gpu_frames.ps1` + `perf_frames.sh` (`--capture:"map <map>;!wait 600"` = 636 frames,
GPU busy / frames). Both restore `sys/config.cfg`.

Steady state, normal loop, `fun_roleplay`, VSync as noted, 4-5 one-second samples:

| settings | GPU(3D) avg (max) | GPU ms/frame at 60 FPS | CPU |
|---|---|---|---|
| `light 5 mapshadows 4 shadows 50 vsync 1` (shipped) | **22.7%** (25.5%) | ~3.8 ms | 36 % |
| `light 0 mapshadows 0 shadows 0 vsync 1` | **13.8%** (18.2%) | ~2.3 ms | 21 % |
| `light 5 mapshadows 4 shadows 50 vsync 0` | **42.3%** (43.6%) | CPU bound | 114 % |
| `light 0 mapshadows 0 shadows 0 vsync 0` | **45.7%** | CPU bound | 81 % |

Frame-count-controlled runs (`fun_roleplay`, shipped settings, GPU busy / 636 frames):

| scene | GPU ms/frame | CPU ms/frame (client's own report) |
|---|---|---|
| main menu (no map) | **0.24** | — |
| `fun_roleplay`, MSAA 4x | 2.22 / 2.48 (two runs) | 8.6 / 11.1 |
| `fun_roleplay`, `CS2D_MSAA=0` | 2.02 | 7.5 |

Reading, and what it rules out:

- **The cost is the world pass, not the present path**: the menu frame (window shown, HUD drawn,
  swap every frame) costs 0.24 ms of GPU — a fully drawn `fun_roleplay` frame costs 2-2.5 ms.
- **The light engine + map shadows are ~40 % of the steady-state GPU load** (22.7 % → 13.8 % with
  every light setting off) and ~15 points of CPU (36 % → 21 %).
- **MSAA 4x is not the main cost**: 2.22/2.48 ms with 4x against 2.02 ms without is inside the
  run-to-run spread; it does save ~1.1 ms of *CPU* per frame. `CS2D_MSAA=0` (new, `src/main_client.c`)
  is the axis to re-measure on.
- **VSync OFF has no frame cap at all** (main loop `SDL_Delay(1)`), so the GPU% nearly doubles and the
  CPU goes past one core; the original locks ~65 FPS either way. This is the single biggest lever if
  the user runs with VSync off, and it is invisible with VSync on.
- **The original's 0.6 % is ~0.1 ms of GPU per frame** (0.6 % x 16.7 ms), i.e. our drawn frame is
  **20-40x heavier in GPU terms** even at the same frame rate. What is not attributed yet: the number
  of draw calls per frame (the batcher flushes on every texture/mode change, so one call per distinct
  sprite), the radar's unconditional 128x128 `glTexImage2D` every frame
  (`ensure_minimap_texture`, `cs2d_render.c:3542`, called from the HUD draw at `:3643`), and the
  per-entity sprite draws themselves. Measure those before optimising.

### Batcher statistics / draw calls per frame (measured 2026-10-13)

`CS2D_DRAWCALLS=1` (`src/main_client.c`, counts in `src/cs2d_gl.c`) prints what the batcher did,
averaged over the run (`tools/agent/perf_gpu_frames.ps1 -DrawCalls`, `-Config 'key=val,…'` to also
change a setting):

| scene / setting | draws/frame | verts/frame | tex_switch/frame | mode_switch | max batch | GPU ms/frame |
|---|---|---|---|---|---|---|
| main menu (no map) | 22.0 | 138 | 21.0 | 0 | 12 | 0.24-0.26 |
| `fun_roleplay`, `light 5 mapshadows 4` | **699.4** | 32,557 | **320.0** | 0 | 6,210 | 2.30-2.48 |
| `fun_roleplay`, `light 0 mapshadows 0` | **426.2** | 14,348 | 63.6 | 0 | 3,312 | 1.74 |

Reading:

- **The batcher is already a LÖVE-style SpriteBatch, and the tileset already uses it**: one
  streaming 65,536-vertex VBO, one `glDrawArrays` per flush, and the flush happens only on a texture
  change, a primitive-mode change, an alpha-reference change or a full batch. The 32x32 tiles are one
  texture with per-frame UVs, so the whole visible tile grid goes out in **one** call (`max_batch`
  6,210 vertices = ~1,000 quads); `overflow/frame` is 0, so the vertex budget is never the limit.
- **What limits it is a state change per sprite, not geometry**: 699 calls for only 32,557 vertices is
  an **average batch of 47 vertices (~12 quads)**. 320 of those 699 calls are texture switches —
  every `Env_Sprite` / `Env_Image` is its own `gfx/...` file, so consecutive sprites of different
  files cannot merge. That is the same limitation LÖVE documents: a SpriteBatch only merges quads that
  share the underlying image, which is why it wants an atlas.
- **The light engine is the largest single block**: turning it off removes 273 of the 699 calls and
  256 of the 320 texture switches (`light 0` vs the shipped settings above). Its passes draw from
  several distinct images (room map, roof layers, occlusion, blur, stripes) with many small quads,
  and its FBO binds flush the batch.
- **The remaining ~360 calls (light off: 426 calls vs 64 texture switches) are not texture switches
  either, and primitive-mode switches are 0** — they are blend-mode toggles (additive sprites) and the
  explicit flushes of the light/target path, i.e. one call per state change again.
- **Draw order cannot simply be sorted for batching**: the sprite passes follow the original's sweep
  order (painter's order), so reordering to group by state would change what covers what. Only
  *adjacent* same-state draws can merge — which is exactly what atlasing the sprite set buys, without
  touching the order.

### Entity sprite atlasing & batching wins (measured 2026-10-13)

Implemented shelf packing (First-Fit Decreasing, shelf height = tallest item in shelf) of map entity
sprites (`Env_Sprite` 22, `Env_Image` 28) into 2048x2048 atlas pages at map load (`game_load_map` and
`editor_load_map`).

Key invariants:
- **Logical size preserved:** `tex->width` and `tex->height` remain the logical unpadded dimensions of the
  sprite; `tex->id` points to the atlas page, and `tex->src_x` / `tex->src_y` specify the sprite's offset.
  Camera culling and margins depend on `tex->width/height` being the sprite's dimensions, not the page's.
- **Separate mask pages:** `mask_mode` 1 (normal / magenta key) and `mask_mode` 3 (black mask / additive)
  do not share cells or pages.
- **Bleed protection:** 4 px extruded margin (`pad = 4`) around every packed cell prevents neighbour bleed
  under `GL_LINEAR` filtering.
- **Painter's order strictly preserved:** no draw order sorting; adjacent same-state draws merge naturally.

Measured results on `fun_roleplay` (shipped config vs after):
- **Draw calls & texture switches:**
  - With light (`light 5 mapshadows 4`): `draws/frame` dropped from 672.7 to **664.7**, `tex_switch/frame` from 296.2 to **288.3**.
  - Without light (`light 0 mapshadows 0`): `tex_switch/frame` dropped from 84.4 to **80.4**.
  - In sprite-dense areas (`tp 44 25`): `tex_switch/frame` dropped from 320 to **104.1**, `draws/frame` to **472.8**.
- **GPU time per frame (636-frame run via `perf_gpu_frames.ps1`):**
  - With light: **1.912 ms/frame** → **1.750 ms/frame** (-8.5% GPU time).
  - Without light: **1.433 ms/frame** → **1.317 ms/frame** (-8.1% GPU time).
- **First-frame freeze:**
  - Map load time: atlas built and uploaded in **172.35 ms** for 156 items across 2 pages (55 ms in GL-free tests).
  - First rendered frame time: dropped from **1447 ms** to **7.7 ms** (187x improvement, freeze eliminated).
- **Visual fidelity:**
  - Screenshot comparison at `tp 44 25`: byte-for-byte identical (1,225,014 / 1,225,014 bytes equal).
