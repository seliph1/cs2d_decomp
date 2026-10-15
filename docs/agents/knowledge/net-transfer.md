Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Network: join-time file transfer

Source: REA `FUN_1402cd970` (`[252,50]` handler), `Ttrans_recv` (`ini 0x1400f43a0`, `add 0x1400f4460`,
`finish 0x1402cc1a0`), `Tgui_joinmen.draw 0x140301640`; proxy captures of the original and of our client against the
modded C4 dedicated (`tools/agent/capture_transfer.sh`, map fun_roleplay). Code: `src/cs2d_transfer.c`.

- **When:** only when the `[252,3]` map checksum does not match (missing or different map). Then the server answers
  `[252,4,13]` (open the "Downloading" window) and sends the list. With a matching checksum it answers `[252,4,0]`
  directly, for the original client too.
- **List:** `[252,50,1, n u16, n × str]` in chunks of 10, ended by `[252,50,1,0,0]`. It holds `sys/servertransfer.lst`,
  every file the map references (entities, tileset, `maps/<map>.txt` and `.lua`) and Lua-added files; names can repeat.
  `[252,50,3, n u16, n × {str, 8 × i32}]` lists files the server keeps in `sys/cache/transfer/<sha256>`.
- **Per name:** an entry with `>` is an HTTP download (`url > path`; `<spritesheet:…>` entries come out "Malformed").
  `mods/<mod>/…` is checked in `sys/cache/mods/<mod>/…`. A missing file is requested when `FUN_1402cb770` accepts it:
  - `maps/`: `.map`, `.txt` only (the `.lua` is refused);
  - `gfx/tiles/`, `gfx/backgrounds/`: images only;
  - `sfx/weapons/`: sounds, except `_clipin.` / `_clipout.` / `_roundin.`;
  - elsewhere: `bmp,jpg,jpeg,png` / `wav,ogg`; `download 2` refuses sounds, `download 3` images;
  - any path with `:/`, `./` or `..` is refused.
  An existing `.map` is requested again only in map mode 1 or 2.
- **Request:** `[252,50,2, n u16, n × str]`, a new message every 10 names, then `[252,50,2, 0 u16]` (`0xffff` when HTTP
  downloads are queued). Our 26 messages for fun_roleplay are byte-identical to the original's.
- **Files:** `[252,50,5, file str, total i32, len i32, bytes]`, then `[252,50,6, file, len, bytes]`…,
  `[252,50,7, file, len, bytes]`. The joined data is `[raw size i32][zlib]` (negative size = stored, `FUN_14022d2f0`).
- **Saving (`finish`):** extensions `bmp,jpg,jpeg,png,wav,ogg,map,txt,cfg,inf,lst` only, never `.lua`; no absolute
  path, no `./`, nothing under `sys/` except `sys/cache/transfer`. Missing folders are created ("Created directory").
  Each saved file is appended to `sys/core/dls.cache` as `"%d %b %Y" + path` (no separator, CRLF).
- **HTTP (`Thttp`, `0x1402cc9d0`):** `FUN_1402cb9e0` splits `src > dst`, trims; length ≤ 2 → "Malformed"; else
  queued (`addjob`, deduped on url + dest) when `FileType(dst) = 0` and `FUN_1402cb770 ≠ 0`. A queued job turns the
  `[252,50,2]` terminator into `0xffff`. `Tgui_joinmen.draw` menu 10 calls `Thttp.get(limit)` each frame once
  `dlftotal ≤ dlfnum`; limit = `download` 4..10 → 50/100/250/500 KB, 1/2/3 MB (`DAT_140bc89e0`, ×1024 / ×1000 as
  stored: 51200, 102400, 256000, 512000, 1048576, 2097152, 3145728), else 16 MB. State 0: strip `http://`, " " →
  `%20`, host up to the first `/`, port 80, blocking connect (11 failures → "HTTP Timeout"), `GET <file> HTTP/1.1`,
  `Host:`, `User-Agent: CS2D`, `Accept: */*`; status = 3 chars after the first space; `content-length: ` sets the
  size; size > limit → "File is too large and will not be downloaded"; 404 → "File not found (404)"; 301/302/303/307
  keep the job and retry the same url (the redirect is never followed); 200 checks each folder of the destination
  (`.`/`..` part, first part `sys`) and creates it. State 1: 4 KB per frame; then "File loaded (N b)" + dls.cache.
  When the list empties: `[252,51, Tgui_joinmen+0x68]` (10). Window: "Downloading data (HTTP)...", one bar at +62,
  dest at +87, "r/t kb - p %" at +107, "File n of total" at +132.
- **Transfer cache read-back:** `[252,50,3]` fills `lua_filetransfer.cache` (path → `FUN_1400a1750` text: each word
  as 8 **upper-case** hex digits). `Tres.add` (`0x1401ad1e0`) replaces a cached rpath with
  `sys/cache/transfer/<sha>` before loading the image / sound (ours: `cs2d_xfer_cache_resolve` in
  `cs2d_gfx_get_sprite_ex` and `snd_load`).
- **`[252,4,11]`** (local map differs): error window with the 3 map choices → map mode 1 (temp.map) / 2 (overwrite),
  join restarts (`join_menu_submit`); see net-join.md.
- **After the last file** the server sends `[252,2]` again; the client answers `[252,3]` with the new checksum and
  gets `[252,4,0]`. fun_roleplay: 231 files (2 min at ~19 kb/s), all byte-identical to the server's copies.
- **Window** (350×200, centered; Cancel 100×20 at (234,164) sends `[253,9]`): text at window +(16, 32 / 79 / 127 / 152),
  bars at y +55 (all files) and +105 (current file), 318×16 with a 1 px frame, black inside, `gfx/gui_shade.bmp`
  stretched over `(318 − 6) × fraction` at +3. Text and frame are (150,150,150) (measured on a screenshot).
  Lines: "Downloading data...", "`file` (n of total)" squeezed to 318 px, "read/total kb - p %", "Download Speed: N kb/s"
  (bytes received in the last 1000 ms, `DAT_140ddf5a8`).
