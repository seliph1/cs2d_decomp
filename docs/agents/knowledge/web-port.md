Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Web/Wasm portability (Emscripten)

Source: our own tree (`src/`, `include/`, the asset folders) and the toolchain's official documentation — **not** the
original binaries, so nothing in this section is evidence about `CS2D.exe`. Toolchain facts are quoted with their URL.

- **What would be ported:** Emscripten compiles source code, not PE. The target is our 48 files / 25,553 lines
  (`src/` + `include/`, two gcc lines in `build.bat`), never `CS2D.exe` (BlitzMax + GDI+ + DirectX) or the dedicated.
- **Platform surface (measured, `src/` + `include/`):**
  - `windows.h` / `winsock2.h` include sites: `cs2d_bind.c:2`, `cs2d_ui.c:3`, `main_client.c:1-2`, plus headers —
    `include/cs2d_render.h:4` (unguarded), `include/cs2d_client.h:4-7` (`_WIN32`), `include/cs2d_serverlist.h:4`
    (**unguarded `winsock2.h`**). Transitively 7 files include `cs2d_client.h` (which pulls `cs2d_render.h:10` and
    `cs2d_serverlist.h:16`), plus `cs2d_light.c`, `cs2d_render.c`, `cs2d_scoreboard.c`.
  - Win32 calls: `GetTickCount` 31 (double-click and caret blink in `cs2d_ui.c:112,1639,1871,1916,1942,1955,1986`),
    `socket`/`sendto`/`recvfrom`/`ioctlsocket`/`setsockopt`/`WSAStartup` 24, `FindFirstFileA`/`FindNextFileA`/`FindClose`
    6, `MultiByteToWideChar` 5, `LoadLibraryA` 2 (gdiplus, shlwapi), `CreateDirectoryA` 1.
  - Sockets live in exactly 3 files: `cs2d_client.c:587` (`AF_INET, SOCK_DGRAM, IPPROTO_UDP`), `cs2d_scene_game.c`,
    `cs2d_serverlist.c:301-322` (FIONBIO + `SO_BROADCAST` LAN scan) with `recvfrom` at `:393`. **No TCP anywhere.**
  - `VK_*` codes (13 distinct, ~31 uses) are the game's key identifiers and leak into UI/binds (`cs2d_bind.c:100-104`).
  - No threading (`SDL_CreateThread`/`pthread`/`CreateThread` = 0), no `Sleep`/`system`/`popen`/`fork`; `getenv` twice
    (`cs2d_client.c:35,526`, `CS2D_NET_LOG`).
  - SDL references: `main_client.c` 54, `cs2d_client.c` 29, `cs2d_light.c` 14, `cs2d_ui.c` 10, `cs2d_render.c` 10,
    `cs2d_scene_game.c` 1 (~118 total).
  - Main loop is blocking: `while (g_running)` + `SDL_PollEvent` (`main_client.c:248`) with a clamped
    `SDL_GetPerformanceCounter` delta (`:327-331`); the web needs a mainloop callback instead.
  - Tests are GL-free (pure functions only: `tests/test_cs2d.c:2405-2448`, `:3103-3116`) and already are the CI entry
    point (`.github/workflows/build.yml`, `windows-latest` + MSYS2 + SDL3).
- **Renderer (as of 2026-10-12): GL 3.3 core + shaders — 0 fixed-function, 1 shader program.** The old
  fixed-function inventory (`glBegin`/`glEnd` 13, `glVertex2f` 36, `glMatrixMode`/`glOrtho`/`glPushMatrix`,
  `glColor4f`, `glAlphaFunc`, `GL_QUADS`, `GL_ALPHA_TEST`) is gone; `src/cs2d_render.c` and `src/cs2d_light.c`
  contain no fixed-function call. `src/cs2d_gl.c` loads the GL 3.3 entry points from `SDL_GL_GetProcAddress`
  (opengl32.dll only exports GL 1.1) and owns the drawing path:
  - one shader: `frag = texture(u_tex, uv) * vertex colour`, with `discard` when `u_alpha_ref >= 0` — the
    replacement for `glAlphaFunc`, used by the player day-shadow silhouette (`cs2d_gl_shadow_*`);
  - one streaming VAO/VBO; the batcher flushes on a texture/blend/alpha/scissor/target change.
    `cs2d_gl_set_blend` maps to `glBlendFuncSeparate` pairs: alpha (SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
    ONE/ONE_MINUS_SRC_ALPHA), additive (SRC_ALPHA/ONE, ZERO/ONE), multiply (ZERO/SRC_COLOR, ZERO/ONE), none.
    A 1x1 white texture makes `tex = 0` a solid colour, so one shader covers everything;
  - the render targets: `cs2d_gl_bind_target`/`unbind_target` (colour-only FBO, saved viewport + CPU ortho)
    for the light engine's main/buffer passes.
  Still on linked GL 1.1–1.3 calls (all core in 3.3): texture management (`glGenTextures`/`glTexImage2D` +
  hand-built mipmaps/`GL_LINEAR_MIPMAP_LINEAR`), `glScissor`, `glDepthFunc`/`glDepthMask`/`glColorMask`,
  `glClear`, `glViewport`, `glGetIntegerv`, `glReadPixels` (the screenshot reads `GL_RGB` and swaps to BGR,
  no `GL_BGR_EXT`). One GL context (`cs2d_render_init`); the swap interval is VSync, applied by
  `cs2d_render_set_vsync` from `sys/config.cfg` "vsync" (default 1); core profile 3.3 +
  4x MSAA + depth 24.
  - Emscripten: because the renderer is now shader-based, `-sLEGACY_GL_EMULATION` / `-sGL_FFP_ONLY` are no
    longer required for it; WebGL2 covers FBO + `blendFuncSeparate` + shaders.
  - GL-free trace: the light engine (`cs2d_light.c`) keeps its `s_trace` path, so `emu_light.py` and the
    suite's "light engine" comparison still run with no window (the 4 scenes pass).
- **Image decoding is portable as of 2026-10-12; only the text engine still uses GDI+.** The image half of
  the paragraph that used to sit here was 11 `gdiplus.dll` functions (`GdipCreateBitmapFromFile`,
  `GdipCreateBitmapFromStream`, `GdipGetImageWidth` / `GdipGetImageHeight`, `GdipBitmapGetPixel`,
  `GdipDisposeImage`, plus the `shlwapi` `SHCreateMemStream` used for the `.ico`). `src/cs2d_image.c` now
  decodes every asset with the vendored public-domain `third_party/stb_image.h`; `cs2d_render.c` only applies
  the mask mode and uploads. Measured facts it had to cover:
  - 408 `gfx/**/*.png`: 207 are 8-bit RGBA, 148 8-bit RGB, 46 8-bit palette, 2 grey+alpha, 1 4-bit palette
    (`gfx/lighting/gfx_shadowmap.png`), 1 interlaced, 2 16-bit RGBA and 1 whose IHDR bit depth is 0; those
    five odd ones all live under `gfx/c4/`, which the client never loads.
  - 317 BMPs: 144 are 32-bit BI_RGB, 124 24-bit BI_RGB, 48 8-bit palette — and **`gfx/hud_symbols.bmp` is
    not a BMP at all: its bytes start with the PNG signature** (`89 50 4E 47`). The container therefore has
    to be sniffed from the bytes, never from the extension.
  - The 4th byte of a 32-bit BI_RGB BMP is alpha for both GDI+ and stb_image, so `has_embedded_alpha`
    reflects whatever is in there (`gfx/player/ct1.bmp`: 0 with a handful of 33 / 106 / 165 / 208).
    **Pillow drops that byte and forces A = 255**, so it is not a usable reference for those files; the BMP
    reference in `test_image_decode` comes from a direct reader instead.
  - `cs2d.ico` holds 9 frames; the one used is the 256x256 32-bit frame, which is **PNG-compressed inside
    the ICO**. Frames are therefore picked by area, instead of asking GDI+ to choose.
  - No JPEG decoder is needed yet: the 13 `gfx/backgrounds/*.jpg` are referenced nowhere in `src/` (only the
    `gfx/backgrounds/` extension filter of `cs2d_transfer.c:136` mentions the folder).
  - `test_image_decode` locks 9 assets (PNG, palette, 24/32-bit BMP, ICO) against an independent decode:
    FNV-1a over the R,G,B,A bytes in row-major order, which is the byte order the GL uploader consumes.
  - **MinGW trap when adding any decoder:** `stb_image.h` keeps some settings in `__thread`
    (`STBI_THREAD_LOCAL`). GCC implements `__thread` through emutls, whose runtime is built on winpthreads,
    so linking it silently adds a `libwinpthread-1.dll` import that is *not* shipped next to the exes — the
    program then dies with `0xC0000135` ("libwinpthread-1.dll was not found") before `main()`. `cs2d_image.c`
    compiles it with `STBI_NO_THREAD_LOCALS`; after that both exes import only KERNEL32, msvcrt, OPENGL32,
    SDL3 and WS2_32. Check the same thing for every future vendored decoder / rasterizer.
- **Still GDI+ today — the TrueType text engine** (`cs2d_render.c` font block): `GdipCreateFont` at
  13 / 10 / 13-bold / 15 px Liberation Sans and 18 px Source Sans Pro Black (unit 2 = pixels),
  `GdipSetTextRenderingHint(4)` (unhinted anti-alias), `GdipMeasureString`, `GdipDrawString` and string format
  flags `0x0800 | 0x1000 | 0x4000` (measure trailing spaces, no wrap, no clip). The plan is to replace it with
  the already vendored `third_party/stb_truetype.h`, comparing advances string by string first.
- **File system:** stdio only (`fopen`/`fread`/`fwrite`/`fclose`, one `_mkdir`, in 13 files); writes at
  `cs2d_editor.c:242`, `cs2d_map.c:315`, `cs2d_transfer.c:518`, `main_client.c:71` (BMP screenshot). Emscripten backs
  libc I/O with its own FS and adds IDBFS for persistence (`-lidbfs.js` + `SDL_EMSCRIPTEN_PERSISTENT_PATH`,
  auto-persist). **Case matters:** 111 files under `gfx/` and 245 under `sys/` have uppercase letters in their names
  (e.g. `gfx/c4/.../Kitchen table.png`, with spaces) versus 0 under `maps/`, so `CASE_INSENSITIVE_FS=1` ("like Windows
  and macOS") is needed. SDL 3.4.2 (installed locally; `SDL_platform_defines.h:230-237` defines
  `SDL_PLATFORM_EMSCRIPTEN`) already provides `SDL_EnumerateDirectory` / `SDL_GlobDirectory` to replace the two
  `FindFirstFileA` sites.
- **Assets:** `gfx/` 761 files / 31.15 MB (408 png, 317 bmp, 13 jpg, 5 ttf, 13 inf, 1 ttc), `sfx/` 241 / 11.31 MB
  (unused: there is no audio), `maps/` 82 / 2.67 MB (average 33.3 KB), `sys/` 352 / 7.46 MB. Fonts: `liberationsans.ttf`
  350 KB, `sourcesans.ttf` 148 KB, `thai.ttf` 113 KB, plus the CJK set `chinese.ttc` 5.2 MB + `japanese.ttf` 6.0 MB +
  `korean.ttf` 4.7 MB. 98 distinct `gfx/...` literal paths exist in `src/`, 83 of them on disk, 4.10 MB in total; the
  other 15 are `%s`/`%d` templates. Some literals do not exist at all (`gfx/player/zombie.bmp`,
  `gfx/weapons/reload.bmp`, `silencer.bmp`, `m4a1s.bmp`) and fail silently, so a preload list must be generated from
  disk, not from the code.
- **Networking constraint (quoted):** "Direct UDP communication is not available in browsers"; "Emscripten does not
  provide a C/C++ API for interacting with WebRTC" / "WebTransport"
  (emscripten.org/docs/porting/networking.html). The supported route is the POSIX-socket proxy
  (`-lwebsocket.js -sPROXY_POSIX_SOCKETS -pthread -sPROXY_TO_PTHREAD`), which "can be slow" and is "mostly useful for
  developing testing infrastructure and debugging"; `NODERAWSOCKETS` is Node-only, and WebTransport is HTTP/3/QUIC and
  needs an HTTP/3 server. Consequence: keep the CS2D bytes identical and carry them through a gateway that owns a local
  UDP socket.
- **Keyboard focus:** `SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT` (SDL 3.2.0+) binds keyboard input to the canvas and "should
  be set before creating a window".
