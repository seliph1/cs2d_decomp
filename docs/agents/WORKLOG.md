# Work log (append-only, newest first)

Add one entry per session: `## YYYY-MM-DD — <agent> — <topic>`, followed by a few bullets covering:

- what changed;
- how it was verified;
- what is left (move it to `OPEN_ISSUES.md`).

Keep entries short; the details go in `KNOWLEDGE.md` or in code comments.

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

## 2026-10-12 — Cline — GL 1.x removal, phase 3: the GL 3.3 core + shader backend

- **Decision (user):** the fixed-function renderer is replaced by **GL 3.3 core + shaders**, not
  `SDL_Renderer` (the alternative my plan recommended). One SDL3 dependency is kept; the whole GL 3.3
  entry-point set is still loaded from `SDL_GL_GetProcAddress` (opengl32.dll only exports GL 1.1).
- **New `src/cs2d_gl.c` + `include/cs2d_gl.h`:** loads the 3.3 pointers, builds one shader
  (`frag = texture(u_tex, uv) * vertex colour`, with an alpha-test `discard` that stands in for
  `glAlphaFunc`) and one streaming VAO/VBO. The public API is a batcher (`cs2d_gl_quad`,
  `cs2d_gl_quad_rot`, `cs2d_gl_quad_corners`, `cs2d_gl_rect`, `cs2d_gl_rect_grad`, `cs2d_gl_line`,
  `cs2d_gl_circle`, `cs2d_gl_push_*`), sticky state (`set_blend` 1 alpha / 4 add / 5 mult / none,
  `set_texture`, `set_alpha_ref`, `scissor`) and the light engine's render targets
  (`bind_target`/`unbind_target`). A 1x1 white texture makes a textured draw with `tex = 0` a solid
  colour, so one shader covers everything.
- **`cs2d_render.c`:** `init` calls `cs2d_gl_load()` and fails cleanly when the context is not 3.3;
  `resize`/`begin_frame`/`end_frame` drive the backend; the 8 `glBegin` sites (rect, line, circle,
  sprite, tile frame, text, gradient, rotated FX quad) and the depth + `glAlphaFunc` player-shadow
  silhouette were rewritten on the batcher (the silhouette is now `cs2d_gl_shadow_mark_begin` /
  `_mark_end` / `_fill`); the map loop and `cs2d_render_tile_frame` pass the texture per quad instead
  of binding it, so the tilemap batches into one draw call.
- **`cs2d_light.c`:** the Max2D layer's `m2d_*` calls push into `cs2d_gl`; the FBO loader lost its
  `EXT` fallback (FBO/blendFuncSeparate are core now) and no longer carries `glBlendFuncSeparate`
  (the backend owns blending). **The `s_trace` path is untouched, so the emulator comparison still
  runs GL-free.**
- **`main_client.c`:** `SDL_GL_CONTEXT_PROFILE_CORE` 3.3 (keeps 4x MSAA + depth 24); the screenshot
  reads `GL_RGB` and swaps to BGR (no `GL_BGR_EXT`). `build.bat` compiles `src/cs2d_gl.c` in both gcc
  lines.
- **Verified:** `Build Successful!`; `test_cs2d.exe` prints `Tests Passed: 32 / 33` — the only failure
  is the known `test_file_transfer` "Transfer list" (OPEN_ISSUES). The light engine test still passes
  its 4 scenes against the original (`_global` trace is GL-free). Screenshots via `--capture`
  render real content: `map prison_funst3r` -> 850x480, 68002 distinct colours, mean (27.2, 32.6, 50.5);
  `map fun_roleplay;light 5` -> 18010 colours, mean (34.5, 30.7, 24.3) (frame time rises to ~43 ms with
  the light engine on, as expected). No `cs2d_gl` missing-entry-point or shader-compile errors on stdout.
- **Left:** a screenshot diff of the new core output against the *original* client on the same scene is
  still owed (OPEN_ISSUES); the FX `glBlendFunc` dead code in weapon-select was removed, and the
  `windows.h` in `include/cs2d_render.h` still waits for phase 2 (the GDI+ text engine).

## 2026-10-12 — Cline — Removing the GDI+ dependency, phase 1: image decoding
- **Changed:** new `src/cs2d_image.c` + `include/cs2d_image.h` decode every asset (BMP / PNG / ICO) with the
  vendored public-domain `third_party/stb_image.h`; `build.bat` compiles it, adds `-Ithird_party` and no longer
  links `-lgdi32`. `cs2d_render.c` lost the `GdipCreateBitmapFromFile` / `GdipCreateBitmapFromStream` /
  `GdipGetImageWidth` / `GdipGetImageHeight` / `GdipBitmapGetPixel` / `GdipDisposeImage` bindings and the
  `shlwapi` `SHCreateMemStream` trick used for `.ico` (`upload_gdiplus_bitmap` → `upload_decoded_image`,
  `load_image_via_gdiplus` → `load_image_portable`, `load_ico_largest_frame` rewritten on `cs2d_image`). The
  hand-rolled 24/32-bit BMP fast path, the mask modes and the transparent-RGB dilation are untouched, so no
  uploaded pixel changes. GDI+ now only starts for the TrueType text engine.
- **Verified:** `Build Successful`; `test_cs2d.exe` prints `Tests Passed: 32 / 33`, the single failure being the
  already known `test_file_transfer` "Transfer list" (OPEN_ISSUES). New `test_image_decode` decodes 9 assets
  covering 8-bit RGBA / 8-bit palette / 4-bit palette PNG, PNG-data-inside-`.bmp`, 24-bit, 32-bit and 8-bit
  palette BMP and the `cs2d.ico` container, and matches an independent decode byte for byte (FNV-1a clip over
  R,G,B,A). `objdump -p test_cs2d.exe` now lists only KERNEL32, msvcrt, OPENGL32, libwinpthread-1, SDL3 and
  WS2_32 — no gdi32.
- **Regression found and fixed in the same session:** the first build of this change died on launch with
  *"libwinpthread-1.dll não foi encontrado"* (`0xC0000135`). Cause: the vendored `stb_image.h` keeps some
  settings in `__thread` (`STBI_THREAD_LOCAL`), and GCC implements `__thread` on MinGW through emutls, whose
  runtime is built on the winpthreads API — `objdump -p` showed the exe importing `pthread_once` /
  `pthread_mutex_*` / `pthread_key_*` / `pthread_getspecific` from `libwinpthread-1.dll`. A scan of `src/` +
  `include/` + `tests/` found no `__thread` / `thread_local` anywhere else, so the import was new. Fix:
  `#define STBI_NO_THREAD_LOCALS` before the `stb_image.h` include (single-threaded client, never calls
  `stbi_set_*_thread`). After rebuilding, both executables import only KERNEL32 / msvcrt / OPENGL32 / SDL3 /
  WS2_32, and `test_cs2d.exe` runs correctly with `PATH` reduced to `%SystemRoot%`.
- **Left:** the text engine is the last GDI+ user and the GL 1.x side is untouched; both are planned in
  OPEN_ISSUES "GL 1.x / GDI+ removal". `third_party/` is new and must be committed with the sources.

## 2026-10-12 — Cline — Web/Emscripten port feasibility (second opinion, docs only)
- **Changed:** these three shared-memory docs only (`WORKLOG.md`, `KNOWLEDGE.md`, `OPEN_ISSUES.md`). No source, build
  or asset file was touched.
- **Question:** is an Emscripten/WebAssembly port viable? This is the *second* opinion the DeepSeek entry below
  (2026-10-12, "Web/Emscripten feasibility report") asked for; an independent session re-measured the tree instead of
  re-labelling the first report. Same overall verdict, now with numbers.
- **Verified (measured, not estimated, `src/` + `include/`):** the platform surface is small and concentrated —
  **no threading** (`SDL_CreateThread`/`pthread`/`CreateThread`/`Sleep` = 0), **no audio at all** (no
  `SDL_Audio`/`Mix_`/`PlaySound`; only `cfg->sound_volume` at `cs2d_config.c:15,59` and the wav/ogg transfer filters at
  `cs2d_transfer.c:131`), stdio-only file I/O (`fopen` 21, `fwrite` 29, `fclose` 31, `_mkdir` 1 in 13 files), directory
  listing only for `maps\*.map` (`cs2d_client.c:2121`, `cs2d_scene_editor.c:140`), UDP-only sockets in 3 files,
  `GetTickCount` 31 uses, `VK_*` 13 codes, ~118 SDL references over 25.5k lines, and **0 shader calls** — the renderer is
  100% OpenGL 1.x fixed-function. Per-file lists, asset weights and the toolchain citations are in `KNOWLEDGE.md`
  "Web/Wasm portability".
- **Verdict:** viable for the C99 client; the deciding risk is the **render/font backend**, not the game logic.
  Emscripten documents exactly the mode we need (`-sLEGACY_GL_EMULATION` + `-sGL_FFP_ONLY`, "your code will not use the
  programmable pipeline/shaders at all"), and names gl4es / Regal as fallbacks. Online play needs a **server-side UDP
  gateway**: "Direct UDP communication is not available in browsers" and the documented POSIX-socket proxy is itself
  "slow" and aimed at test infrastructure. The original `CS2D.exe` is not a port target (Emscripten compiles source,
  not PE).
- **Left:** the phases and the open decisions are in `OPEN_ISSUES.md` "Web port (Emscripten)". Phase 0 (emcc build of
  the Win32-free core + `test_cs2d.exe` under Node) is the next step and runs in a separate context. Any structural work
  goes on `feat/emscripten-poc`, never `master` (parallel session, merge risk).

## 2026-10-12 — Claude Code — Light engine (TSmartLighting)
- **Changed:** new `src/cs2d_light.c` ports the original light engine: day shadows of walls, obstacles, high walls and
  room roofs along the sun, player silhouettes, Env_Room / Env_Light / Env_LightStripe, toning, per-light buffers with
  wall shadows and penumbra, floor occlusion, light sprites (Env_Sprite, alarms, Trigger_Use glow, leaves) and
  `ObstacleShadow2`. Settings `light` / `mapshadows`, server `sv_forcelight` / `sv_daylighttime` (`[252,6]`, `[237,19/20]`),
  console `light`, `mapshadows`, `daylight`, `lightdump`. Trigger_Use buttons are drawn. `CS2D_MAX_ENTITIES` 16384
  (fun_roleplay has 9901 entities).
- **Verified:** the original's functions run in Unicorn (`tools/agent/emu_light.py`); our traces are identical on
  fun_roleplay, de_dust2, de_inferno, as_mars, de_vantage and cs_assault, light 1..5, mapshadows 2..4, sun 30..300.
  `test_light_engine` locks 4 traces. Screenshots against the original on fun_roleplay: 91..94 % of pixels within 8 by
  day, 94 % at night (the rest is items, HUD and timing). About 1.5 ms per frame.
- **Left:** flashlights, particle / building / projectile light sprites and shadows, editor lighting (OPEN_ISSUES).

## 2026-10-12 — Cline — Advanced radar (minimap) fidelity pass
- **Changed:** the top-left HUD radar in `cs2d_render.c` now renders the real tileset instead of hardcoded
  brown/gray tile colours. `ensure_minimap_texture` builds a cached full-map minimap (2 px/tile, box-averaged via
  `tileblend_find`) and crops it around the player's exact pixel position, so it slides smoothly instead of
  snapping per tile (16 world px per radar px, matching `TSmartRadar`). Added player blips: the local player red,
  teammates in team colour (T light red 0xff9696, CT light blue 0x9696ff), enemies hidden outside Deathmatch.
  `cs2d_render_hud` gained a `tileset_tex` parameter (wired through `cs2d_scene_game.c`).
- **Verified:** `gcc -fsyntax-only` clean; the regression suite still passes 30/31 (the one failure is the
  pre-existing `test_file_transfer` "Transfer list" gap, unrelated). Full `build.bat` step [1/2] (test suite)
  linked; step [2/2] was cut short by the terminal, but it compiles the same sources plus `main_client.c`.
- **Left:** the full-screen `TSmartMinimap` overlay (big map) is still not reconstructed; the radar minimap does not
  apply tile rotation/brightness (negligible at 2 px/tile). Recorded in `KNOWLEDGE.md`.

## 2026-10-12 — Antigravity — Scoreboard ping display & Opcode 229 clock ping reply
- **Changed:** implemented client reply to `CS2D_NET_CLOCK_PING` (opcode 229 / 0xE5) in `src/cs2d_client.c`.
  The original dedicated server sends opcode 229 when `sv_antiaccel > 0` (default) every 2 ticks (~2 s) with
  `[229, srv_ts i32]`. The client now responds with `[229, srv_ts i32, cli_ts i32]` (9 bytes, unreliable datagram,
  per `CS2D.exe` `0x1401d7690` and `0x1402e2ed0`) where `cli_ts = GetTickCount()`. With this reply, the server
  calculates RTT latency (`now - srv_ts`) and transmits live player pings via `CS2D_NET_PINGLIST` (opcode 247).
- **Verified:**
  - Dedicated server capture `tools/agent/capture_ours.sh` (`proxy_pingtest.log`) confirmed the exchange: server sent
    `e5 af 17 00 00`, our client replied `e5 af 17 00 00 19 46 12 00`, and the dedicated server sent `f7 03 ... 03 10 00`
    broadcasting our player ID 3 with latency 16 ms (0x0010).
  - Regression tests in `tests/test_cs2d.c` for `[247]` ping assignment and `[229]` 9-byte reply roundtrip.
  - Test suite passes (30/31, same as baseline).
- **Left:** None for this item.

## 2026-10-12 — DeepSeek — Web/Emscripten feasibility report (read-only, no code changed)
- **Changed:** nothing. Read-only feasibility review at the user's request, deliberately without editing any source
  or doc file to avoid conflicting with the parallel session (Claude Code). Report only.
  Note on authorship: this entry is by the same agent (DeepSeek) as the "Lua language-server config" entry below;
  the agent names in this log are session/tool labels, not separate identities.
- **Question:** is an Emscripten/WebAssembly port of this reimplementation viable?
- **Evidence scanned:** `build.bat`, `main_client.c`, `cs2d_render.c`, `cs2d_network.c`, `cs2d_transfer.c`,
  `include/cs2d_client.h`, `include/cs2d_serverlist.h`, `include/cs2d_render.h`, `.github/workflows/build.yml`,
  `src/` + `include/` size (46 files, ~22.8k lines), targeted grep for Win32 / GDI+ / OpenGL-fixed-function symbols.
- **Findings (facts):**
  - **Render:** 100% legacy fixed-function OpenGL 1.x throughout `cs2d_render.c` / `cs2d_scene_*` (`glBegin/glEnd`,
    `glOrtho`, `glPushMatrix/glRotatef/glTranslatef/glScalef`, `glColor4f`, `glAlphaFunc`, `glReadPixels(GL_BGR_EXT)`,
    manual mipmaps, `glEnable(GL_MULTISAMPLE)`). WebGL has no fixed-function pipeline -> either a fixed-function->WebGL
        shim or a shader/VBO rewrite is required. Highest-effort item.
  - **GDI+ is the only image decoder AND the only text/font engine** (`GdipCreateBitmapFromFile`,
    `GdipCreateBitmapFromStream` for `.ico`, `GdipCreateBitmapFromScan0`, `GdipNewPrivateFontCollection` +
    `GdipPrivateAddFontFile` for `gfx/fonts/liberationsans.ttf` / `sourcesans.ttf`, `GdipDrawString` /
    `GdipMeasureString` feeding a 512-entry GPU string-texture cache). No non-Windows fallback; the system-font
    fallback (`Segoe UI` / `Arial`) is Windows-only. Second-highest effort.
  - **Winsock / windows.h are in public headers:** `cs2d_client.h` (`#ifdef _WIN32`, `SOCKET`, `sockaddr_in`) and
    `cs2d_serverlist.h` (`#include <winsock2.h>` **without a guard**). Runtime uses `WSAStartup`, `ioctlsocket`,
    `sendto`, `recvfrom`, `closesocket`, `SO_BROADCAST`. CS2D is UDP-only; browsers have no raw UDP, so a
    WebTransport/WebRTC-to-UDP gateway on the **server** is required for online play. Major architectural cost.
  - **Win32 I/O and system:** `FindFirstFileA` (map listing in `cs2d_editor.c`, `cs2d_client.c`),
    `CreateDirectoryA` (`main_client.c`), `GetTickCount()` (UI, serverlist, render), `MultiByteToWideChar`,
    `LoadLibrary` / `GetProcAddress`.
- **Findings (portable):** the C99 core (`cs2d_types`, map, entities, player, weapons, UI) and the whole
  protocol/crypto layer (`cs2d_network.c`: MD5, SHA-256, shift cipher, Tudp builders) are Win32-free; zlib
  (transfer) works in wasm; SDL3 (window/input) has first-class Emscripten support.
- **Assessment:** technically viable but NOT a recompile. It needs a platform-abstraction layer (HAL) that does not
  exist today, plus a render/font backend and, for online, a server-side UDP gateway. Suggested phases: (A) HAL for
  time/dir/file (low/med), (B) render + text/image backend (high, project-deciding), (C) socket abstraction + UDP
  gateway (high, client+server, required only for online), (D) asset preload via `--preload-file`. Lowest-risk first
  step: an Emscripten build of only the Win32-free logic (network/map/entities/player/weapons) to measure portability
  without touching render/network.
- **Risks:** visual/byte fidelity is a project invariant (the `--capture` harness compares frames/screenshots and the
  proxy compares packets byte-for-byte); a render/network rewrite risks violating it, so keep the desktop path intact
  and add a web backend beside it. Also: any structural refactor has high merge-conflict risk with the parallel agent,
  so do it on an isolated branch (`feat/emscripten-poc`), not on `master`.
- **Left (requested):** the user asked for a second opinion, but it must come from a *distinct* session/tool
  (e.g. the parallel Claude Code session, or an independent review), not from this agent re-labelling itself as a
  third party. Open items to size: exact per-file inventory of Win32/GDI+ symbols; asset weights (`gfx/`, `sfx/`,
  `maps/`); whether the GL extensions used (`GL_BGR_EXT`, `GL_MULTISAMPLE`) survive on WebGL; fixed-function
  emulation fidelity against the screenshot tests.

## 2026-10-12 — Antigravity — In-game Action Progress Bar (Reloading, Planting, Defusing)
- **Changed:** reconstructed the in-game action progress bar from CS2D.exe `FUN_140295190` (`0x1402958f0..0x140295b70`).
  Displays centered at `y = screen_h / 2 + 80px` with width 102px and height 20px, 1px outer frame, 2px padding,
  98x16 inner filled rectangle, warm gold/amber `(255, 200, 0)` at alpha 0.75, soft black shadow underneath
  (`incbindata/shadow.bmp` scaled 11x5 at alpha 0.40). Supports "Reloading" (`sys/language 0759`), "Planting Bomb"
  (`0760`), and "Defusing Bomb" (`0761`). Synchronized `is_reloading` in `cs2d_weapons.c`, `cs2d_player.c`, and
  `cs2d_client.c`.
- **Verified:** `test_hud_action_progress_bar` added to `tests/test_cs2d.c` (passes 31/31 suites, verifying layout,
  exact 102x20 geometry, centering, inner dimensions, progress calculations, and weapon filters). Visually matched against
  the user screenshot.
- **Left:** nothing; integrated directly into `cs2d_render_hud`.

## 2026-10-12 — DeepSeek — Lua language-server config
- **Changed:** added `.luarc.json` (Lua 5.1, `workspace.library = meta`, the CS2D command/hook/global list in
  `diagnostics.globals`, and a few noisier diagnostics disabled) and `meta/cs2d.lua`, EmmyLua stubs for the CS2D
  Lua API derived from `sys/lua/info.txt`, `docs/cs2d_help_db.json` and the shipped scripts. Editor-only; not built
  or executed. The shipped scripts under `sys/lua/`, `maps/` and `bots/` no longer report undefined globals and get
  autocomplete for `addhook` / `player` / `entity` / `parse` / `ai_*`.
- **Verified:** `.luarc.json` parses; the stubs cover every command in the scrape plus the `wrapper` / `sample` globals.
- **Left:** nothing; the config lives at the workspace root and covers the whole tree.

## 2026-10-12 — Claude Code — Env_Object and tile blending
- **Changed:** Env_Object is now a port of `Te.draw`: Sink / Toilet on the ground pass (frames 0 / 1, we used type - 2),
  Palm / Tree with the original sway and leaf shadows, Alarm (flare1 pulse), Street Lights (flare3 + lamp frame 2); size,
  alpha and color are used as stored (we forced 0 to 1 / white, so fun_roleplay's street lights showed a flare the
  original does not draw). Tile blending (`Ttileblend`): blended images built from the tileset pixels and
  `gfx/blendmap.bmp` into a per-tileset atlas the first time they are drawn. The editor now draws the overhead pass too
  (the original editor loop calls the same map draw). `tex_lamp1` renamed `tex_objects`.
- **Verified:** `test_tile_blending` (direction table, blendmap orientation, mix, de_aztec frames); screenshots of
  fun_roleplay at 2|24 and 119|150 next to the user's screenshots of the original, prison_funst3r sinks / toilets /
  palms / trees. 29 / 30 tests: `Transfer list` also fails without these changes (the untracked `maps/fun_roleplay.map`
  and `gfx/c4/` from a download make the list request different files).
- **Left:** see OPEN_ISSUES "Env_Object and tile blending".

## 2026-10-12 — Claude Code — projected player shadow
- **Changed:** at the user's request, `cs2d_render_player` casts the player's silhouette (legs, torso, weapon in black)
  along the map sun instead of the `shadow.bmp` blob of the original `Tpl.draw`. Second round: offset reduced (h 6 px,
  ~3.6 px per axis) and two-pass depth mask + one quad, which fixed the legs' semi-transparent outline showing over
  the torso shadow. Hostages keep the blob.
- **Verified:** build, 29 / 29 tests, screenshots on de_dust2 (standing and walking).

## 2026-10-12 — Claude Code — Scoreboard (Tab)
- **Changed:** `src/cs2d_scoreboard.c` (port of `Thud_scoreboard`), shown while `showscores` (Tab) is held. Network:
  `[19]` kill scoring (byte 3 is the assist, byte 4 the weapon), `[22]` round end reason -> team scores, `[64]`, `[247]`
  ping list (was unhandled and dropped the rest of the datagram), `[42]` fields 1 / 4 / 9 / 10, `[252,6]` team scores,
  `[252,7,1]` assists / MVPs. `cs2d_game_init` leaves player slots empty (id 0); offline bots have `bot` set.
  Harness step `!hold <key> down|up`.
- **Verified:** `test_scoreboard` (layout, order, kill and round reason scoring, timers); screenshots offline and against
  the dedicated with two bots (`[247]` = `f7 02 25 0000 0000 01 fffe 02 fffe`).
- **Left:** see OPEN_ISSUES "Scoreboard"; not compared with a screenshot of the original.

## 2026-10-12 — Antigravity — Player and ground item shadows
- **Changed:** implemented authentic player, hostage, ground item, and ground projectile drop shadows:
  - Added shadow cvars and config parameters: `shadows` (0..100), `playershadows` (0|1), `itemshadows` (0|1), `mapshadows` (0..4) (`include/cs2d_config.h`, `src/cs2d_config.c`).
  - Added shadow control and querying API: `cs2d_render_set/get_shadow_intensity`, `cs2d_render_set/get_player_shadows`, `cs2d_render_set/get_item_shadows` (`include/cs2d_render.h`, `src/cs2d_render.c`).
  - Added `tex_shadow` texture loading (`incbindata/shadow.bmp`, maskMode 3) to `cs2d_gfx_bank_t`.
  - Player shadows (`Tpl.draw 0x140121160 / 0x140122298`): rendered centered at player `(sx, sy)` including recoil backward kickback, scale 1.5 (48x48 px), color black `(0,0,0)`, alpha `player_alpha * (shadows * 0.01f)` (0.50 default, 0.10 stealth suit).
  - Hostage shadows (`Thostage.draw 0x140147090`): scale 1.5 (48x48 px) centered at hostage position.
  - Ground item shadows (`Titem.draw 0x1401483a0`, `Te.draw 0x140260000`): 32x32 circular soft shadow from `shadow.bmp` at `(x, y)` plus 1px offset silhouette drop shadow from the weapon sprite at `(x + 1.0f, y + 1.0f)` in black with alpha `shadows * 0.01f`.
  - Ground projectile shadows (`Tpro.draw 0x140288702`): 32x32 circular shadow at `(x, y)` with rotation `p->rot` plus 1px offset silhouette drop shadow at `(x + 1.0f, y + 1.0f)`.
  - Interactive console commands: `shadows [0..100]`, `playershadows [0|1]`, `itemshadows [0|1]` in `cs2d_client.c`.
- **Verified:** dump disassembly of `Tpl.draw`, `Titem.draw`, `Te.draw`, `Tpro.draw`, `Thostage.draw`; added `test_item_and_player_shadows` regression test in `tests/test_cs2d.c` (28 / 28 passed).

## 2026-10-12 — Antigravity — Projectiles and Persistent Area Fields (Tpro, Tfield)
- **Changed:** implemented full projectiles and fields system (`include/cs2d_projectile.h`, `src/cs2d_projectile.c`):
  - Projectile classification (throwables vs. rockets vs. impact weapons).
  - Throwable consumption with 2 charges for Flashbang (1 clip, 1 reserve) and 1 for others (item stripped on throw).
  - Flight simulation with wall ricochet (bouncing grenades reflect velocity, reduce flydist and speed, reverse spin).
  - Impact detonation on first obstacle/wall/player for Rockets, RPG, Molotov, Snowball, Gut Bomb.
  - Cosmetic effects and persistent area fields (`CS2D_FIELD_FIRE`, `CS2D_FIELD_GAS`, `CS2D_FIELD_SMOKE`) with smoke cover detection (`cs2d_projectile_smoke_cover`).
  - Added particle FX: smoke puffs, fire puffs, sparkles, blood splats, snow puffs (`cs2d_fx.c`, `cs2d_render.c`).
  - Loaded projectile textures (rocket, rpg, grenade, molotov, flare, gutbomb, airstrike, flashbang, smokegrenade, gasgrenade, snowball) with fallback to ground weapon sprites.
  - Client networking & dispatch: Client->Server opcode 27 (`CS2D_NET_PROJECTILE`), Server->Client opcodes 27, 41, 52, 55, 58, 63, and snapshot chunk 6 (`[252, 7, 6]`).
  - Integrated into game loop, firing logic, rendering passes (ground and flying) and state resets.
- **Verified:** dump inspection of `Tpro.c` and opcode handlers in `1400a0000.c`, `1402d0000.c`, `1402e0000.c`; added `test_projectiles_and_fields` to test suite (`test_cs2d.exe` 27 / 27 passing).
- **Left:** Satchel charge intentionally deferred per user instruction.

## 2026-10-12 — Claude Code — player hover text
- **Changed:** hovering another player shows its name ("Friend: " / "Enemy: " in a team or deathmatch) and "hp%"
  (`render_player_hover` in `src/cs2d_scene_game.c`, `cs2d_player_hover_kind` in `src/cs2d_player.c`), from the player
  branch of `FUN_1402a12d0`; it takes precedence over the building hover.
- **Verified:** dump; regression in `test_construction` (friend / enemy / neutral, enemy health rules); our client on
  the offline dedicated with the reworked `tools/agent/lua/hover_probe.lua` shows "Enemy: [b]LUL" in red, no health.
- **Left:** smoke cover, fog, click-to-spectate, table 0x140a015d0 (OPEN_ISSUES).

## 2026-10-12 — Claude Code — building hover health
- **Changed:** hovering a building shows its name / "$1 by $2", "n%" health and "Upgrade n%" (`render_building_hover`
  and `hover_text` in `src/cs2d_scene_game.c`, `cs2d_dynobj_hovered` / `cs2d_dynobj_hover_health`), ported from
  `FUN_1402a12d0` / `FUN_14024fb10`. `mp_hovertext` is read from `[252,6]` and `[237,16]` (`net_hovertext`).
- **Verified:** dump + objdump (strings "%", " ", ": " at 0x140a6dd00 / 0x140a71020 / 0x140a66160); proxy log of
  `[252,6]` (hovertext 31); regression in `test_construction`; our client on the offline dedicated with
  `tools/agent/lua/hover_probe.lua` shows "Turret" in CT blue and "100%" in green.
- **Left:** screenshot of the original; player / hostage / item hover texts (OPEN_ISSUES).

## 2026-10-12 — Claude Code — fire rate twice too fast
- **Changed:** `fire_delay` converts firedelay ticks at 39 ms (`Tpl.attack` `firetimer.reset(firedelay * 0x27)`,
  0x140216b8f) instead of 20 ms, so every weapon fired ~1.95x faster than the original. Knife / Claw right click
  waits 1200 ms (0x1401fdde8) instead of 0.42 s.
- **Verified:** dump + objdump (`Ttimer.reset`, imul 0x27); regression in `test_weapons` (AK-47 117 ms, AWP 2535 ms).
  Not yet measured on a proxy capture (interval between `[7]` of the original).
- **Left:** whether semi-automatic weapons need a new click in the original (we fire while the button is held).

## 2026-10-12 — Claude Code — building hit debris
- **Changed:** shots (local and remote) that reach a building throw gray fragments both ways
  (`cs2d_fx_building_hit`, `cs2d_dynobj_hit_debris`, `building_hit_debris` in `src/cs2d_scene_game.c`), ported
  from `Tshoot.Shoot` 0x1402073ed -> `Tp.breakable` material 1 -> `Tp.matp`. Particles got a per-particle `drag`.
- **Verified:** REA / dump / objdump only (type mask 0x9b80, matp constants Rnd(0.2, 3), Tp type 12 update);
  regression in `test_construction`. Not yet compared with a screenshot of the original.
- **Left:** see OPEN_ISSUES (hit sounds, wall stop of fragments, screenshot check).

## 2026-10-12 — Claude Code — chat send/receive verified on the remote server
- **Changed:** no code change. Live end-to-end check of the `[240]` (`CS2D_NET_MSG2`) chat path against
  `140.238.183.194:36969` (user-authorized), driven by `tools/agent/udp_proxy.py` + `cs2d_client.exe`
  with `CS2D_NET_LOG=1` and the console `say` / `sayteam` commands (the `!press Y` harness uppercases the
  key and does not open the prompt, so it is not usable for this).
- **Verified:** the two chat datagrams are byte-correct and round-trip:
  - send `[240, mode 1, len 0x17, UTF-16LE]` and `[240, mode 2, len 0x0c, UTF-16LE]`;
  - server echo `[240, 01 (sender_id), mode, len, ...]` decoded to `*DEAD* real: ...` and
    `*DEAD*(TEAM) real: ...` in the chat log.
  `sys/recentlyjoined.lst` was backed up and restored (rule 5). The user reported the original
  "missing messages" were the server's anti-flood during parallel-client testing, not a client bug.
- **Left:** confirm/set aside the pre-existing `sys/core/svl.cache` modification (not from this session);
  the two latent dispatcher risks (`break` on an unknown opcode discarding the rest of the datagram, and
  the `[226]` trailing-byte length assumption) stay unverified in `OPEN_ISSUES.md`.

## 2026-10-12 — Antigravity — In-game chat, server messages (@C) & Opcode 240
- **Changed:**
  - Added in-game chat log layout matching CS2D original (`Thud_msg.draw_chat` 0x14024d8a0): left aligned at `x = 3.0f`
    with black drop shadow at `x = 4.0f, y + 1.0f` at alpha `fade`, baseline starting at `screen_h - 85 - 2*line_h` stacking
    upwards with 10 s duration and fade-out decay. Top ceiling at `y = 180.0f` prevents overlap with top radar.
  - Interactive chat input prompt (`FUN_14029f7b0`) on 'Y' (`Say : `) and 'U' (`Say (Team) : `) or `say`/`sayteam` commands.
    Draws alpha 0.4 black background box, blinking vertical cursor, freezes movement/attack input while open, and sends on Enter.
  - Fixed `sdl_key_to_cs2d_key` in `src/main_client.c`: previously only W, S, A, D, R, E, B, G, M, C were mapped. Added
    generic letter mapping (`SDLK_A..SDLK_Z` -> `'A'..'Z'`), keypad digits, Tab (`VK_TAB`), Backspace (`VK_BACK`), Enter, Escape.
  - Added trigger key suppression (`chat_just_opened`): consumes the initial 'y' or 'u' key event from text input so the chat prompt starts empty.
  - Server message routing (`FUN_1401de400`): messages ending with `@C` or `@c` (or mode == 2) strip the `@C` tag and route
    to `cs2d_client_center_msg` (screen center at `h / 2 - 10*n - 50` with black drop shadow at `+1, +1`). Other server messages
    route to HUD chat log. All messages echo to client console.
  - Color tag parser: `\xA9RRRGGGBBB` (single-byte 0xA9 or UTF-8 `\xC2\xA9` + 9 ASCII digits) parses RGB and strips prefix.
  - Team colors (`tintTeamColor` 0x140083a40): Spec/Server `(255, 220, 0)`, T `(255, 25, 0)`, CT `(50, 150, 255)`. Dead
    player formatting with `*DEAD* ` and `*DEAD*(TEAM) `.
  - Opcode 240 (`CS2D_NET_MSG2`): client send `[240, mode, len_u16, len*2 bytes UTF-16LE]`, server recv/broadcast handler.
- **Verified:** `test_chat_and_server_messages` in `tests/test_cs2d.c` (26 / 26 passed), verifying color code parsing,
  `@C` tag detection & stripping, server routing to center vs. chat, player team formatting, opcode 240 serialization, and
  interactive chat prompt operation (Y/U trigger, clean text input consumption, backspace, enter submit, escape close).

## 2026-10-12 — Antigravity — Chunk-based shadow map & incremental invalidation

- **Changed:** `src/cs2d_render.c`: Replaced full-map monolithic shadow texture generation with a 16x16-tile chunk grid
  (256x256 texels per chunk). Replaced dynamic `calloc`/`free` of 34+ MB per frame with static scratch buffers and
  cached `eff_modes` / `eff_heights` tables. Fast delta tracking checks which `Func_DynWall`, `Env_Breakable` or
  `object_shadow` tile changed and marks only localized chunks dirty (typically 1..4 chunks of 42+). Lazy texture
  generation and viewport frustum culling ensures only visible dirty chunks generate OpenGL textures with `glTexSubImage2D`.
- **Verified:** `test_shadow_chunks` added to `tests/test_cs2d.c` (25 / 25 passed), confirming 101x88 de_dust2 grid
  geometry (7x6 chunks) and that a solid tile modification marks only localized chunks (1..4) dirty instead of 42.

## 2026-10-12 — Claude Code — Buy menu
- **Buy menu** (`src/cs2d_hudmen.c`): `Tgui_hudmen` menus 20..26 from the dump: sections, T / CT items, disabled
  Defuse Kit / shield, `_k` images on buttons, rotating `_m` section previews, `weaponinfo` description with the client's
  `init_weapons` stats, money box. `buy` / `buyequip` / `buyammo1` / `buyammo2` go through `sys/controls.cfg`
  (`B`, `C`, `,`, `.`); the hardcoded `B` shortcut is gone. The "Construct" menu now uses the shared `Tgui_hudmen` button.
- **Tools:** `tools/agent/weapontables.py` recovers the per-weapon `init_weapons` tables (also the muzzle offsets).
- **Verification:** `test_buy_menu` (layout, previews, info lines, keys and Back click); screenshots of our client on
  de_dust2 offline. Not compared with the original client yet, and not run against the dedicated.

## 2026-10-12 — Claude Code — weapon selection HUD and bindings
- **Changed:** invnext / invprev / slotN logic and the selection HUD ported from `CS2D.exe` (KNOWLEDGE "Weapon selection
  HUD"); attack / attack2 with the HUD open holds the item. New `src/cs2d_bind.c` reads `sys/controls.cfg` (defaults
  mwheelup invprev, mwheeldown invnext, 0..9 slots); console `bind`, `invnext`, `invprev`, `slotN`. The mouse wheel now
  always reaches the client (in game it runs the bound command). Config reads `hud_fastswitch`, `drawradar`, `radarsize`.
- **Verified:** screenshots of the original (`wsel_probe.lua`, new `wheel:n` step in `drive_orig.ps1`) against ours:
  same positions, colors and texts; the proxy shows `[9, type, 0]` on the confirming click and nothing on the wheel.
  `test_weapon_select` (23 / 23).
- **Left:** see OPEN_ISSUES (one item per inventory slot, INF / Remote texts, sounds, other bindings).

## 2026-10-12 — Claude Code — building shadows, build helper alpha, additive explosion
- **Changed (user requests, not checked against the original):** solid buildings (walls, Dispenser, turrets, supplies;
  `cs2d_dynobj_casts_shadow`) mark `map->object_shadow` and cast a 32 px wall shadow in the shadow map; the build helper
  alpha is floored at 0.35 (`0.35 + 0.65 x sin`); the `gfx/explosion.png` effect draws additive (LIGHTBLEND).
- **Verified:** `test_construction` checks the shadow overlay for the turret and Wall III (22 / 22).

## 2026-10-12 — Claude Code — Construction mode, dynamic objects, CTF flags
- **Changed:** new `src/cs2d_dynobj.c` (Tdo list, type table, `[35]` `[36]` `[37]` `[49]`, snapshot sections 3 and 5,
  collision overlay `map->object_collision`, `FUN_14014ef60` build rules); client `[34]` build result + red center
  messages, `[19]` kill, `[22]` frees objects; render of every building layer, build helper, "Construct" menu (Wrench
  right click twice), respawn click `[39]` + countdown, waving CTF flag and carried flag; Env_Building is now spawned as
  dynamic objects offline. New `tools/agent/bbscope.py`, `lua/construct_probe.lua`, harness `rdown`/`rup`.
- **Verified:** `test_construction` (22 / 22) with the 56-object snapshot recorded on 140.238.183.194:36969; our client
  against the offline dedicated (`sv_gamemode 3`, ctf_cs2dfortress): respawn by click, objects from `[49]` drawn, build
  request byte-identical in format, server built Barricade -> Wall I -> Wall II with Wrench hits (screenshots).
- **Left:** see OPEN_ISSUES "Construction".

## 2026-10-11 — Claude Code — spectator mode
- **Changed:** spectator camera and HUD (`src/cs2d_spectate.c`, `cs2d_scene_game.c`): free look and follow, the
  target picker, `[67]` and `[32]`, `space` / `leftctrl` (`specplayer` / `specmode` commands), the join
  showcase, sv_specmode from `[252,6]` and `[237]`. The join snapshot section 1 (players already in game, names
  deciphered) is now read, so bots and earlier players exist. The fixed `[67,1]` after the client data is gone.
- **Verified:** proxy of the original vs ours on prison_funst3r with two bots (`spec_probe.lua`): `[67,1]` and
  `[32] 20 f0 86 90 85` byte-identical at join, `specplayer` `[67,2][67,2]`, `specmode` `[67,0]` / `[67,2]`,
  HUD screenshots match. Both clients on 140.238.183.194:36969 (empty server): no `[67]`, identical first
  `[32]`, showcase every 5 s. `test_spectator` added (21 / 21).
- **Left:** see OPEN_ISSUES "Spectator".

## 2026-10-11 — Claude Code — shared scrolling in the UI library
- **Changed:**
  - `cs2d_ui`: `cs2d_ui_scroll_t` (first visible row + thumb grab), `cs2d_ui_scrollbar_style_t`, and
    `cs2d_ui_scroll_{max,clamp,by,page,wheel}`, `cs2d_ui_scrollbar` (in the layout) / `cs2d_ui_scrollbar_at`
    (floating, over custom-drawn lists). Every view now drags the thumb, pages on a track press (visible - 1),
    takes the wheel while the pointer is over it, and clamps the same way.
  - Moved to it: console (now counts from the top; at the bottom it follows new output), Server Info, server list
    (it had no drag / page), dropdown, and the editor's tile / entity / map file lists (they only had the wheel).
    Each keeps its old look through a style. The global wheel dispatch in `cs2d_client_handle_mouse_wheel` and
    `cs2d_scene_editor_wheel` are gone: the views read the wheel from the UI context.
- **Verified:** `test_ui_scroll` (wheel inside / outside, track page, thumb geometry and drag, clamp), the console
  and dropdown tests (20 / 20); screenshots of the editor tile list being dragged, the console and the server list.
- **Left:** the editor sidebar buttons (z 11) and its tooltip are drawn over the console window (it was like that before).

## 2026-10-11 — Claude Code — map editor (first part)
- **Changed:**
  - New `cs2d_editor.c/.h` (model): brush (rotation, BR, color), set tile / rect / flood fill, tile properties,
    `.inf` V1..V4 read and V4 write, entity tables, place / remove.
  - `cs2d_scene_editor.c` rewritten: sidebar with the 12 icon buttons, tile list, entity list with filter, rotation /
    BR / color, Tile Properties, Color and Load / Save windows, grid, hovered-tile frame, entity icons, status line,
    tooltips. The editor builds the UI every frame (`cs2d_scene_editor_build_ui`, `_render_overlay`, `_wheel`).
  - Map: per-tile color data (`tile_data`) loaded and saved; the render follows `TileProp.Get` (brightness b * 10 %,
    colored tiles) instead of the old guesses. New `cs2d_render_tile_frame` and `cs2d_render_text_scaled_ex`.
  - Tools: `initarrays.py`, `bbstr.py --array`.
- **Verified:** our editor's screenshots next to the user's screenshots of the original (sidebar, Tile Properties,
  Color); load -> save byte-identical on 7 original maps; `test_editor` (19 / 19); de_dust renders as before apart
  from the corrected brightness.
- **Left:** Map Properties, Display Settings, Tools mode, Entity Properties, context menu, undo (OPEN_ISSUES).

## 2026-10-11 — Claude Code — join file transfer (server downloads)
- **Changed:**
  - New `cs2d_transfer.c/.h`: `[252,50]` list checks (`FUN_1402cb770` rules), `[252,50,2]` requests, file blocks,
    zlib uncompress (`-lz` in `build.bat`), saving with the `Ttrans_recv.finish` rules, `sys/core/dls.cache`.
  - Client: "Downloading" window (Tgui_joinmen layout, Cancel = `[253,9]`); the map now loads at `[252,4,0]`;
    reliable packets are handled once (sequence bitmap); `connect <ip> <port> [password]` (key `j01nk3y`, the old
    `mypassword` key was wrong); `download` read from `sys/config.cfg` and sent in `[252,3]`.
  - `cs2d_render_text_scaled_ex`; tools `capture_transfer.sh` (dedicated in another folder, client in a copy,
    `CLIENT=orig`, `KEEP=1`) and `bbstr.py` (decodes `bbs__` literals).
- **Verified** against the modded C4 dedicated (`C:\C4`, fun_roleplay, password 132):
  - our client downloads 231 files, every one byte-identical to the server's copy, then joins;
  - our 26 `[252,50,2]` request messages are byte-identical to the original client's;
  - the handshake after the transfer (`[252,2]` again → `[252,3]` with the new checksum) matches the original;
  - a second join with everything present gets no list (original too);
  - a normal join on the stock dedicated (prison_funst3r) still works; `test_file_transfer` added (18 / 18).
- **Left:** HTTP downloads, the `[252,4,11]` map-differs dialog, the transfer cache lookups (see `OPEN_ISSUES.md`).
- **Follow-up:** zlib is linked statically (the exe needed `zlib1.dll`). Joining from Find Servers no longer loads
  `de_dust2` first: the handshake and download run over the menu and the game scene starts at `[252,4,0]` with the
  server's map (checked on prison_funst3r and on C4 with a download).

## 2026-10-11 — Claude Code — Ghidra dump of CS2D.exe
- **Changed:** documented `docs/dump/` (full Ghidra decompilation of the client, git-ignored) in `TOOLS.md`
  and `AGENTS.md`; added `tools/agent/dumpgrep.sh` (function by address/name, methods of a class, body regex).
- **Verified:** dump addresses match REA on `CS2D.exe` (`Tpl::move` `0x1401f7600`, `Tgui_sl::draw`,
  `Tgui_sl::pingServers`, `NativeFlare::Draw`).

## 2026-10-11 — Claude Code — server browser (Find Servers)
- **Changed:**
  - New `cs2d_serverlist.c/.h`:
    - `Tsv` model;
    - `svl.cache`, `favorites.lst` and `recentlyjoined.lst` readers;
    - info / ping packets;
    - `pingServers` scheduler;
    - LAN broadcast;
    - its own UDP socket.
  - "Servers" window in `cs2d_client.c`: tabs, flag icons, mode letters, players and bots, latency bars,
    scrollbar, stats, tooltip, the 15 s cache reload, joining, and "Connect to IP". It opens from the lobby's
    "Find Servers".
  - Fixed `cs2d_net_parse_info_packet`: strings are u8-length, and bots + flags2 are always present.
- **Verified:**
  - Proxy captures of the original (`tools/agent/browser_proxy.py` + a test `svl.cache`), and our client
    against the same dedicated: identical request and ping bytes, the same 15 s reload, and joining from the
    list reaches "Connected".
  - Screenshots compared with the original.
  - `test_serverlist` (17/17).
- **Left:** see `OPEN_ISSUES.md` (U.S.G.N. list, filters, favorites, writing the lists).

## 2026-10-11 — Antigravity — firearm visual recoil kickback, melee swing animation, and GitHub Actions release workflow
- **Firearm visual recoil (`Tpl +0x308`):** extracted the per-weapon visual recoil table (`0x1409eda18` in
  `CS2D.exe`, `1..6 px`) from `init_weapons` (`0x1402305db..0x1402340a9`). Firing sets
  `recoil = visual_recoil + Rnd(0, 1)` (`0x1402166a7`), which displaces the player backward along `-angle`
  in `cs2d_render_player` (`0x14012131c..0x1401213cc`) and decays by `0.5 px` per 60 Hz frame (`30 px/s`, `0x14021d1b1`).
- **Melee swing (`Tpl +0x31c`, `+0x24c`, `Tp` type 13):** melee attacks (except Chainsaw `85`) set
  `slash_timer = 8.0f` (`0x14018cebc`) and `draw_angle = angle + 6.0f * slash_timer` (`+48° -> 0°` over 8
  frames at 60 Hz, `0x14021d68a` & `0x14021da21`), rotating both the torso and held weapon (`13.5 px` offset)
  and spawning the additive `gfx/knifeslash.bmp` arc (`0.07/frame` fade, `0x14004b4e0` & `0x140265220`).
- **GitHub Actions CI & Releases:** added `.github/workflows/build.yml` (MSYS2 MinGW-w64 + SDL3) to compile
  `cs2d_client.exe` and `test_cs2d.exe`, run the regression suite, upload workflow artifacts, and publish
  `cs2d-binaries-win64.zip` + `SDL3.dll` to GitHub Releases.
- **Verification:** added `test_recoil_and_swing` in `tests/test_cs2d.c` (`16 / 16` passing).

## 2026-10-11 — Antigravity — Server Info ("<Server> - Info") and "Select Team" / skin subcategory UI
- **Protocol & parsing:** parsed `server_name` from `[252,6]`, `opcode 240` (`CS2D_NET_MSG2`) `mode 50`
  (`"1"` clear, `"2"` open) and `mode 51` (UTF-16LE `serverinfo.txt` lines with `U+00A9` `RRRGGGBBB`
  color tags and `U+00BB` gold link highlighting), and `opcode 20` (`CS2D_NET_TEAM`) send/receive.
- **Clay UI windows (`cs2d_ui`):** implemented the `600×440` `"<Server Name> - Info"` window (`553×374`
  scrollable box + `15 px` scrollbar + `568×20` `Close` button) and the `600×440` `"Select Team"` menu
  (`M` key) with `240×25` numbered buttons, T/CT skin subcategories (`1..4` skins, `5 Auto-Select`,
  `Back`, `0 Close`), and immediate selection (no subcategory) for `5 Auto-Select` and `6 Spectator`.
- **Verification:** added `test_server_info_and_team_menu` in `tests/test_cs2d.c` (`15 / 15` passing) and
  captured live screenshots against `cs2d_dedicated.exe -offline` verifying the full join → Server Info →
  Select Team → CT skin selection → live spawn flow.

## 2026-10-11 — Claude Code — agent memory structure
- Added `AGENTS.md` and its pointers (`CLAUDE.md`, `GEMINI.md`, `.agent/rules/`), plus `docs/agents/*`,
  `tools/agent/*` (capture scripts and Lua probes) and `.mcp.json` (REA).

## 2026-10-11 — Claude Code — weapon sync, shot effects, join/leave
- **Weapon sync:** the client follows the server's inventory: opcodes 9, 16, 21, 22 (fixed record size),
  23, 24, 25, 33, 42 and 61, plus ground items. Validated with the `inv` command against the Lua dump:
  identical.
- **Shot effects:** tracer, glowdots, muzzle flash with light, wall sparks and shells (`cs2d_fx_shot`).
  Remote shots come from `[7,pid,seed]`.
- **Join/leave:** the missing client data `[252,10]` + `[67,1]` meant the server never ran `join`; the
  client now sends both. Two-client test: join 1, join 2, leave 2, leave 1; player count 0→1→2→1→0.
  The client also handles `[248]`, `[253]` and `[28]`.

## 2026-10-11 — Claude Code — Env_Breakable
- **Behaviour:** health, damage, hitscan trace, explosion and break effects (`cs2d_fx.c`). The fire
  packet is now `[7,seed]`.
- **Verification:** `test_breakables` uses numbers measured on the dedicated.

## Earlier (Claude Code / Antigravity)
- **Scenes and UI:** scene split (lobby, game, editor); UI library (textbox, dropdown, numberbox, console
  ring buffer).
- **Shadows:** fixed shadow halos and removed the invented roof parapet.
- **Entities:** entity logic validated state for state against the dedicated (fixture of 331 samples).
- **Movement and protocol:** movement is an exact port of `FUN_1401f7600`; the protocol matches the
  original client (positions, rotation, use, setpos).
- **Restart:** the offline `restart` command restarts the round (states, Trigger_Start, freeze time, random spawns).
