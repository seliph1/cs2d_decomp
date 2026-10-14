# Open issues / next steps

Keep this file current: remove items when they are done (and log them in `WORKLOG.md`), and add what
you discover. Sign items that are being worked on (`[in progress: <agent>]`) so two agents do not take
the same one.

## Known gaps
- **Gen_* entities (2026-10-13, `src/cs2d_gen.c`):** not screenshot-compared with CS2D.exe. Missing: the thunder
  sound (no audio system; `gen.thunder` counts claps), wind on Tp 22 / 0x20 (not spawned by us), screen shake
  from explosions / Lua (only Info_Quake feeds `gen.shake`),
  the FOW hiding (`FUN_140146fd0`) and the lighting sprites (`TLightingSprite::DrawSprite` when DAT_140ddf185).
  Settings gating rain / snow / effects (DAT_140a1b068, 140a1b05c, 140ddf540) are assumed on, "particles" = 4.
  Thunder decoded as a 10..30 *frame* countdown (assembly 0x14027525b), which seems very frequent: verify on the
  original. smokepuff masks (mode 3) are our choice. Spawns follow 60 Hz, the original spawns per rendered frame.
- **Loading screen (2026-10-13):** `cs2d_render_loadscreen` lacks the menu background and DrawWindow corner
  images/spinner; progress uses our 6 steps, not the original `PROCESS_MAPS`. Not compared to a screenshot yet.
- **First-frame freeze when a map is set up (RESOLVED 2026-10-13):** resolved by preloading
  and packing map entity sprites into atlas pages at map load (`game_load_map`, `cs2d_gfx_atlas_load_map`).
  The 1447 ms freeze on `fun_roleplay` was transferred to load time (172.35 ms in `game_load_map` to load,
  decode, dilate and pack 156 sprites into 2 atlas pages of 2048x2048), reducing the first drawn frame
  time from 1447 ms to **7.7 ms** (187x improvement).
- **Cache ceiling (RESOLVED 2026-10-13):** the fixed 64-entry array `CS2D_MAX_SPRITE_CACHE` was replaced
  with a dynamically resized sprite cache (`sprite_cache`) backed by map texture atlas pages. Maps with
  >64 distinct entity sprites (`fun_roleplay` has 157 distinct sprite/mask pairs) keep every sprite without
  silently dropping any. Verified by regression test `test_sprite_atlas_packer`.
- **Info_TileFX (2026-10-14, `cs2d_tilefx.c`):** not screenshot-compared with CS2D.exe. Not applied to tile frames
  drawn outside the map pass (Env_Breakable / Func_DynWall, tile blending sources); the frame clock is the global
  tick instead of a per-effect timer; built on the first draw, not at map load; ignores DAT_140ddf17f (as tile blending).
- **Entity culling (RESOLVED 2026-10-14):** `Env_Image` and `Env_Object` now start unculled and learn their
  drawn size (`cs2d_render_entity_learn_size`, `env_object_learn`), like `Env_Sprite`. `fun_roleplay` at (0,0):
  78 entities considered per frame once learned (test_entity_culling). Not re-profiled on GPU.
- **No frame cap with VSync off (measured 2026-10-13):** the original locks its frame at ~65 FPS
  (`15.384615 ms`, `docs/dump/cs2d/_global/1402f0000.c:2284`) whether VSync is on or off; our loop only
  does `SDL_Delay(1)` (~1.5 ms), so it runs at 167 FPS and pegs a CPU core. Measured on `fun_roleplay`:
  VSync ON 22.7 % GPU / 36 % CPU, VSync OFF **42-46 % GPU / 81-114 % CPU** (the CPU past one core).
  Decide whether to reproduce the original's cap; it is the biggest lever for the GPU number the user
  sees whenever VSync is off. Numbers: `KNOWLEDGE.md` "Performance" (2026-10-13).
- **GPU per frame, not yet optimised (measured 2026-10-13):** a drawn `fun_roleplay` frame costs
  **1.75 ms of GPU with light** (down from 1.91-2.48 ms before entity atlasing; 1.32 ms with `light 0 mapshadows 0`):
  ~40 % of the steady-state load is the light engine + map shadows (`light 5 mapshadows 4`); MSAA 4x is
  within the run-to-run spread but costs ~1.1 ms of CPU per frame (`CS2D_MSAA=0` in `src/main_client.c`
  turns it off). Still unattributed or pending optimization:
  - **light engine draw calls & texture switches:** 273 calls and 256 texture switches disappear
    with `light 0`; packing the light engine's internal texture set (room map, roof layers, occlusion,
    blur, stripes) remains the next atlasing target. Entity sprites (`Env_Sprite` / `Env_Image`) are
    already atlased (2026-10-13).
  - **the radar's unconditional upload** — `ensure_minimap_texture` (`cs2d_render.c:3481`) builds a
    128x128 RGBA buffer in a nested loop and calls `glTexImage2D` (new storage) on **every frame**, from
    the HUD draw at `cs2d_render.c:3643`; the minimap only changes when the player moves a tile;
  - **per-entity sprite draws** (up to ~1000 considered per frame on `fun_roleplay` after the entity
    index) and the per-entity margin decisions in `cs2d_render.c`.
- **Audio:** there is no audio system (material sounds, shots, `Env_Sound`).
- **Shots:** offline shots have no spread, and the reach of weapons other than the knife and the AK-47
  is not measured.
- **Muzzle offsets:** the per-weapon muzzle offset table (`0x1409f5d20` in `CS2D.exe`) is now recovered
  (KNOWLEDGE "Buy menu") but not used yet; `cs2d_fx` still uses the calibrated 30 / 22 px.
- **Fire rate, not measured:** the 39 ms firedelay tick comes from REA only; compare the `[7]` interval of the
  original on a proxy capture. Unknown whether semi-automatic weapons need a new click (we fire while held), and the
  global timer 0x140a1b008 that knife right click also sets to 1200 ms is not reconstructed.
- **Explosion damage fall-off:** assumed; only distance 0 was measured.
- **Fragments:** which fragment frames and tints belong to each material is inferred from captures.
- **Scoreboard, not reconstructed** (KNOWLEDGE "Scoreboard"):
  - Left click on a row to follow that player, the right-click context menu (`Thud_contextmenu`), the "Ticks" tooltip.
  - Voice / chat mute icons, country flags, Steam / U.S.G.N. avatars (always `incbindata/avatar.png`), the competitive line.
  - Small font: `PTR_bbNullObject_140a1ccb8` is assumed to be our 10 px font; not compared with a screenshot of the original.
  - Zombies: a zombie killer scores `DAT_140a14d7c` (we add 1). The client's local +300 / -3300 kill money is not applied.
  - Our list order is the player id; the original's `Tpl.list` is the join order (matters only for equal sortval).
- **Env_Object and tile blending, not reconstructed** (KNOWLEDGE "Env_Object and tile blending"):
  - `Te.age` is per entity and only advances on screen; we use one clock for every object.
  - Sink / Toilet are drawn on our ground pass, under the ground items (the original draws `Te.draw(1)` after items).
  - The 3D mode (`Tile3D`).
  - The setting `DAT_140ddf17f` that turns tile blending off is not read; we always blend. In the editor, painting does
    not re-run `updatetile` (we compute the neighbour every frame, so a cleared 0x40 flag never stays cleared).
  - `test_file_transfer` "Transfer list" fails while `maps/fun_roleplay.map` / `gfx/c4/` exist locally.
- **Kill feed:** `[19]` updates scores and deaths, but the kill messages (`Thud_msg.add_kill`) are not drawn.
- **Opcode 40:** "kill dropped weapon"; its format is unknown.
- **Round start and ground items:** whether dropped weapons are removed at round start is unknown.
- **Spray:** our client sends a blank spray logo.
- **Dedicated broadcast quirk:** after some round restarts the dedicated stops broadcasting entity
  changes; the original client shows the same, so it is server behaviour.
- **Weapon selection HUD, not reconstructed** (KNOWLEDGE "Weapon selection HUD"):
  - Our inventory holds one weapon per `cs2d_weapon_slot_t`, so slot 4 lists one grenade (the original lists HE,
    Flashbang and Smoke with their counts, e.g. "2 x").
  - "clip | INF" under infinite ammo (`0x140a14e38[1]`, language 0762) and "0 x - Remote" (0763, slot 6).
  - The HUD sounds (`0x140a0d628` / `d630` / `d640`), `lastinv`, and the gamepad path (`FUN_140239bd0`).
  - Bindings: only the weapon selection commands go through `sys/controls.cfg`; movement, reload, use, buy, drop and
    the spectator keys are still hardcoded in `game_handle_key`. `cs2d_bind_key_name` knows only digits, letters,
    space, leftctrl and leftshift. The HUD font scale is fixed at 1 (13 px).
- **Buy menu, not reconstructed** (KNOWLEDGE "Buy menu" has what is done):
  - `autobuy` / `rebuy` only print a message (the original's `FUN_1401fff70` path with `DAT_140ddf2e0` / `2e4`).
  - The custom buy menu (`custombuy`, when the setting `0x140a14e20[1]` is longer than 2 characters) and the "menu
    disabled" case (it starts with "-"): the setting is not received yet, so the default menu always opens.
  - The radial menu variant (`DAT_140dde9ac`, `TRadialMenu`), the voxel weapon preview (`DAT_140a14ca0`), the button sound.
  - Offline, buying ignores buy zones and buy time (online the server decides and answers `[23, pid, 250]`).
  - Not compared with a screenshot of the original: the window frame / title reuse the "Construct" menu's colors.
- **Server browser, not reconstructed:**
  - U.S.G.N. serverlist download: the Internet tab always uses `sys/core/svl.cache`.
  - Filters dialog: the button only shows the count from `sys/filters.cfg`, and filters are not applied.
  - Right-click to add a favorite, and the "Add to Favorites" button in the middle of the footer.
  - Writing `favorites.lst` / `recentlyjoined.lst`: joined servers go to the Recently Joined tab in memory only.
  - Sorting by the other columns, and the player list request `[FB][5]`.
- **Server browser, not measured:**
  - The meaning of the "M" header icon (frame 5 of `gui_server.bmp`).
  - The "Connect to IP" dialog layout.
  - The hover / selection highlight alpha (calibrated by eye).
  - Long names: the original scales them down to fit (`SetScale(225 / width)`), ours clips them.
    `cs2d_render_text_scaled_ex` now does this (used by the download window).
- **Join file transfer, not reconstructed** (see KNOWLEDGE "join-time file transfer"):
  - HTTP downloads (`url > path` entries, `Thttp`): well-formed ones are skipped and we always end the request with
    `0`, never `0xffff`.
  - `[252,4,11]` (the local map differs from the server's): the original asks "save as temp.map / overwrite /
    cancel" and joins again with map mode 1 or 2. We only print the status; the mode stays 0.
  - Files cached under `sys/cache/transfer/<sha>` (`[252,50,3]`) are saved there, but nothing reads them back yet
    (the original resolves Lua image paths through that cache). The checksum text format of `FUN_1400a1750` is assumed
    to be 8 × `%08x`.
  - The spinner icon (`gfx/gui_load.bmp`) next to the "Downloading" title.
  - Our Tudp has no retransmission of our own reliable packets; the request burst (26 messages) only works because
    nothing is lost on a LAN.
- **Join messages that differ from the original (proxy, not fixed):** the original sends `[252,0]` (2 bytes, ours
  adds an empty string), and in `[252,1]` it sends `01` after the U.S.G.N. fields and the spray file name
  (`unrealsoftware.bmp`) where ours sends empty strings. The dedicated accepts both.
- **Spectator, not reconstructed** (KNOWLEDGE "Spectator" has what is done):
  - The Menu / Fog buttons open nothing (game menu, fog toggle). Map toggles the minimap; its hostage / flag / bomb /
    Dom_Point dots, the per-player fog test under sv_specmode 2 and the `TSmartMinimap` variant are missing.
  - `mp_spectatemouse`: the followed player's mouse cursor (`Tpl.mx/my`, `net_mspec`) is not drawn, and its
    network message is unknown. The kill cam (`Thud_killCam`) is not done.
  - Respawn game modes: "Click to respawn" sends nothing (`FUN_140298b30`), and the respawn countdown text
    (0767) is missing.
  - Avatars: the name box always shows the "?" placeholder (Steam / U.S.G.N. avatars, `DAT_140a14fe8`).
  - While spectating our HUD (timer, money, radar) is hidden; the original keeps its timer, money and minimap.
  - Clicking a player on the scoreboard or the minimap to follow it (`FUN_14029fc70`, `0x140299e7a`).
  - Snapshot section 1: the steam / usgn name strings are assumed to be u16 count + UTF-16 (always empty so far).
- **Map editor, not reconstructed yet** (KNOWLEDGE "Map editor" has the layouts):
  - Map Properties (Map Settings window), Display Settings, and the Tools / More Options mode (Distance, Path
    Distance, Pathfinder, Tile Blender, Decal Painter, minimap sizes, export, move map).
  - Entity Properties: opened by a right click on the entity (the original's context menu is missing); no decal
    preview (`Tgui_img` 0x140a0e850); Okay does not re-run `Te.setup` / `cs2d_update_mapdata` (nothing in our editor
    caches them). Not compared to screenshots of every type, only Env_Hurt's layout.
  - The tile context menu (right click), undo
    (`Teditor_actionlist`), tile blending (`0x40`), the unsaved-map and "save as default" confirmations.
  - New Map makes a 50x50 `cs2dnorm.bmp` map (size not measured); the 600 px/s scroll speed is a guess; the selection
    pulse speed (`_DAT_140ddeff8`) is calibrated by eye.
  - Entities are deleted with Delete over them (the original uses its context menu).
  - The overlay frame of colored tiles (`Tmap_d.overlay`) is loaded and saved but not drawn.
  - With the console open in the editor, the sidebar buttons ("Tile Properties", "BR", z 11) and the tile tooltip
    are drawn over the console window (z 10).
- **Scrolling, not measured:** wheel steps (3 lines for the console / Server Info / server list / map files,
  1 row for the dropdown and the editor lists) and the thumb hover color are our choices, not taken from `CS2D.exe`.
- **Map saver:** a map without daylight (0) is saved as 225 + 1000; maps saved with "heights 0 / 1" are re-saved with 2
  (like the 1.0.1.8 editor, not checked).

- **Construction / dynamic objects, not reconstructed or not measured** (KNOWLEDGE "Construction mode"):
  - No screenshot of the original's buildings / "Construct" menu yet: the menu window colors reuse our Tgui window,
    the "X" button (`Tgui_hudmen.fbutton`) is drawn like a small button, and the center message shade bar and fade
    (1 s) are calibrated. The team menu buttons sit at window + 80 in our UI while every `Tgui_hudmen.button` call
    passes x = 16: one of the two is wrong (check on a screenshot).
  - Not drawn: portals (22 / 23), NPCs (30), Lua images (40, `Tdo` image fields and `[252,7,7]` image data),
    laser mine beams, Construction Site smoke particles, dynamic lights; turret tracers use a calibrated 300 px reach.
  - Building hit debris (`cs2d_fx_building_hit`): no hit sound (array 0x140a0d890 not identified), fragments do not
    stop at walls, the `particles` setting is fixed at 4, and the look is not compared with a screenshot yet.
  - Building hover text (KNOWLEDGE "Construction mode"): not compared with a screenshot of the original; the text
    height uses our 15 px line height for Max2D `TextHeight`. Hostage (bit 4) hover text and the ground item hover
    (`FUN_14029ffb0`, `THudItemHover`) are not done.
  - Player hover text: no smoke cover check (no smoke grenades) and no `Tpl.fog` (no fog of war); the click on the
    hovered player to spectate it (`FUN_14029fc70`) is not done. `team_game()` stands for both `0x140a015d0` tests
    (buildings `== 1`, players `!= 0`): the table values are not read yet.
  - Not handled: teleporter use
    ("Select Teleporter Destination" 0528), Supply (`FUN_14028dba0` item menu, menu 61/62), Gate Field team blocking
    for movement, kill rewards and `mp_building_limit` / `mp_building_price` overrides.
  - `g_prop_class[cellprop] == 1` is approximated by our collision 0; the team-game table `0x140a015d0` is assumed
    (every mode but Deathmatch) and the respawn-mode table `0x1409f57b8` as modes 1..4.
  - `[252,7,3]` items and `[252,7,6]` ground projectiles are read, but `[252,7,7]` images and `[252,7,8]` tweens are not.
- **CTF:** captures, returns and scores (`[64]` setteamscores, `[226]` messages) are not shown on the HUD; the flag
  drawing follows the code but was not compared with a screenshot of the original.
- **Light engine, not reconstructed** (KNOWLEDGE "Light engine"; what exists matches the original call for call):
  - Flashlights (`TLightSource` type 1, the player's flashlight item) are never created.
  - Light sprites of particles, muzzle flashes, explosions, `TSparkle`, projectiles and buildings (`Tdo`), the shadows of
    buildings (`FUN_14025f0e0`) and of flying projectiles (`Tpro.draw_flying_shadow`).
  - The player silhouette in the day shadows leaves out the carried bomb / defuse kit; `ObstacleShadow` (player parts as
    obstacles) is not filled.
  - `DrawStripesLightColor`, `render3d` (`DAT_140ddf084`), and the map editor (the original lights the editor too; ours
    turns the engine off there).
  - Trigger_Use buttons are drawn on our ground pass, not on `Te.draw(1)`.
  - The `light 0` path (old CPU shadow map) was never compared with the original's `mapshadows 1`.
  - The sprite / obstacle queues (Env_Sprite, palm / tree leaves, alarms) are not covered by the emulator test (the scene
    file does not carry them); they were checked on screenshots only.

## GL 1.x / GDI+ removal (started 2026-10-12, independent of the Wasm port)
Goal: drop the two Windows-only backends the renderer still depends on. The UDP network layer is explicitly out
of scope, and this is *not* the Wasm port — the phases just happen to be the same work the port needs.
- **Phase 1 — image decoding: DONE (2026-10-12).** `src/cs2d_image.c` + `third_party/stb_image.h` replaced
  `GdipCreateBitmapFromFile` / `GdipCreateBitmapFromStream` / `GdipBitmapGetPixel` / `GdipDisposeImage` and the
  `shlwapi` `SHCreateMemStream` trick for `.ico`; `build.bat` no longer links `-lgdi32`. Locked by
  `test_image_decode` (9 assets, FNV-1a clip vs an independent decode); the measured format inventory is in
  KNOWLEDGE "Image decoding is portable".
- **Phase 2 — text: the last GDI+ user** (`cs2d_render.c` font block; sizes, flags and hint mode are in
  KNOWLEDGE "Still GDI+ today"). Replace with the already vendored `third_party/stb_truetype.h`:
  - put it in an isolated, GL-free module (`src/cs2d_font.c`) so measurement can be unit-tested with no window;
  - **the risk is metrics, not pixels:** every HUD and dialog measurement goes through
    `cs2d_render_text_width_ex` / `cs2d_render_text_prefix_width`, so the stb advances must equal the
    `GdipMeasureString` bounding box (including the measure-trailing-spaces flag) or the whole layout drifts;
  - measure before switching: dump `<font, string, bbox>` for a corpus of real UI strings from the *current*
    build, then compare stb_truetype advances string by string and lock the equal ones in a test;
  - keep the raster conventions: white text on opaque black, coverage read from the G channel, and the
    stem-darkening gamma (`pow(a, 0.62)` into `s_gamma_lut`).
- **Phase 3 — OpenGL 1.x: DONE (2026-10-12) via GL 3.3 core + shaders** (the user chose core 3.3 + shaders
  over `SDL_Renderer`; see WORKLOG). New `src/cs2d_gl.c` + `include/cs2d_gl.h` own the whole drawing path:
  the GL 3.3 entry points are loaded from `SDL_GL_GetProcAddress` (opengl32 only exports GL 1.1), one shader
  does `texture * vertex colour` with an alpha-test `discard` (replacing `glAlphaFunc`), and one streaming
  VAO/VBO is fed by a batcher. `cs2d_render.c` and `cs2d_light.c` no longer contain a single fixed-function
  call; the FBO loader kept only the core names. The entry-point inventory is in KNOWLEDGE "Renderer".
  - **Owed — pixel comparison:** the new core output has not yet been diffed against the *original* client
    on the same scene (or against the old fixed-function build). Screenshots so far are only checked to be
    non-black and plausibly coloured (`--capture`, 850x480). This is the verification portão of the phase.
  - **Light engine on core:** `glBlendFuncSeparate` + render-to-texture are core in 3.3 and the `emu_light.py`
    trace test still passes, but the on-screen light compositing (`m2d_draw_buffer`, `cs2d_gl_bind_target`)
    was only eyeballed via a `fun_roleplay` capture — a `light 0..5` / `mapshadows 0..4` screenshot diff is owed.
  - **Fallback removed:** the compatibility context is gone; a machine without a 3.3 core context fails to
    start (clear stderr message). Decide later whether to keep a legacy path.
  - `include/cs2d_render.h` still includes `<windows.h>` (unguarded) — it goes away with the phase 2 GDI+ text
    engine; `cs2d_gl.h` deliberately does not include it.
- **Practical note — the `libwinpthread-1.dll` trap (fixed 2026-10-12):** the first build of phase 1 died on
  launch with *"libwinpthread-1.dll não foi encontrado"* (`0xC0000135`). The vendored `stb_image.h` declares
  some of its settings as `__thread` (`STBI_THREAD_LOCAL`: `stbi__g_failure_reason`,
  `stbi__vertically_flip_on_load_local`, `stbi__unpremultiply_on_load_local`, `stbi__de_iphone_flag_local`),
  and GCC implements `__thread` on MinGW through **emutls**, whose runtime is built on the winpthreads API.
  `objdump -p` showed the exe importing `pthread_once` / `pthread_mutex_{init,lock,unlock}` /
  `pthread_key_create` / `pthread_getspecific` / `pthread_setspecific` from `libwinpthread-1.dll`, which is
  not shipped next to the executables. Fix: `#define STBI_NO_THREAD_LOCALS` before the `stb_image.h` include
  (single-threaded client, never calls `stbi_set_*_thread`). Both exes now import only KERNEL32 / msvcrt /
  OPENGL32 / SDL3 / WS2_32 — no extra DLL has to be shipped, and this is where alternative decoders
  (SDL_image, FreeType, a future JPEG decoder) have to be checked too.

## Web port (Emscripten) — not started
- **Decision (2026-10-12):** viable for the C99 client; the render/font backend is the deciding risk and online play
  needs a server-side UDP gateway. Evidence: `KNOWLEDGE.md` "Web/Wasm portability" and the `WORKLOG.md` entry of the same
  date (Cline, second opinion). No source file has been changed for it.
- **Open decisions:**
  - **Render:** resolved on the desktop — the renderer is now GL 3.3 core + shaders (`src/cs2d_gl.c`, see the
    GL 1.x removal section), so `-sLEGACY_GL_EMULATION` / `-sGL_FFP_ONLY` and gl4es / Regal are no longer
    candidates; the web build targets WebGL2, still to be proven by an in-browser screenshot comparison.
  - **Font metrics:** `stb_truetype`/FreeType must reproduce the `GdipMeasureString` advances that size the HUD, or the
    whole layout drifts. Measure on the desktop first, then compare string by string.
  - **Network:** gateway choice (WebSocket vs. WebTransport over a local UDP socket), where it runs, and how `[229]`
    latency and the LAN scan (impossible in a browser) are reported.
  - **Assets:** which set goes into `--preload-file` (the minimum referenced is ~4.1 MB of `gfx` + fonts + one map +
    `sys/config.cfg`/`controls.cfg`); the CJK fonts (16 MB) are the main candidate for on-demand loading.
  - **CI:** a separate `ubuntu-latest` + emsdk job (`.github/workflows/build.yml` is Windows-only today).
- **Obstacles inside the tree:** the two fixed `maps\*.map` scans (`cs2d_client.c:2121`, `cs2d_scene_editor.c:140`), the
  Windows-hardcoded harness paths (`main_client.c:348`), the case sensitivity of 356 asset names (111 under `gfx/`, 245
  under `sys/`), and the blocking `while (g_running)` main loop (`main_client.c:248`) that SDL3 replaces with a mainloop.
- **Phase 0 (cheapest, do first):** `emcc` build of the Win32-free core + `tests/test_cs2d.c` with `-sUSE_ZLIB=1`, run
  under Node, to measure portability before touching `cs2d_render.c` (`feat/emscripten-poc` branch).

## Verification debt
- **Original + our client, one session:** not re-run after the latest join/leave fix (the session ended
  in a BSOD). Run `twoplayer_probe.lua` with the original firing next to our client and check the remote
  shot effects on screen.
