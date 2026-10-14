Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Server browser (Find Servers)

Source: REA (`CS2D.exe`) and proxy captures of the original against `cs2d_dedicated.exe -offline`
(`tools/agent/browser_proxy.py`). Code: `src/cs2d_serverlist.c`.

- **Function names:** BlitzMax debug decls (kind 6/7, `{kind, name, type tag, address}`, 40-byte records in
  `.data`) name most functions: `pingServers` `0x1403012d0`, `Tgui_sl.draw` `0x14016b5d0`, `draw_stats`
  `0x1401083f0`, `offlineserverlist_load` `0x1400f25d0`, `loadfavs` `0x140083730`, `loadrecent` `0x140085090`.
  Class `Tsv` (debug scope `0x1409f04a0`) lists every field with its offset.
- **Out-of-band messages:** client dispatcher at `0x1402d22c0`, jump table `0x140bc8010` for opcodes 0xF0..0xFC
  (0xFA ping `0x1402d24f0`, 0xFB info `0x1402d23c8`).
- **Requests (exact bytes):**
  - info: `[01 00][03][tick u16][FB][mode][1018 u16]` (9 bytes). `tick` is the time since the client started, in
    100 ms units. Mode 1 goes to each listed server; mode 2 is broadcast to 255.255.255.255:36963 every ~5 s
    (LAN port `DAT_140a14ca4`).
  - ping: `[01 00][FA][millisecs i32]` (7 bytes); the server echoes the same 7 bytes.
- **Info reply:** `[01 00][FB][mode][flags][name u8 len + text][map u8 len + text][players][max]([gamemode])[bots][flags2]`.
  - `flags`: 0 password, 1 U.S.G.N. only, 2 fog, 3 friendly fire, 4 same version (request carried 1018),
    5 game mode byte present, 6 Lua, 7 force light.
  - `flags2`: 0 recoil, 1 off-screen damage, 2 server transfers.
  - Offline prison_funst3r: `fb 01 10 0b "CS2D Server" 0e "prison_funst3r" 00 0c 00 02`.
- **Scheduler (`pingServers`):** each server is queried every 3 s, with up to 3 pings (`pingc`) and up to 5 info
  requests (`inforeqs`, stops at the first reply). A new entry n waits n × 30 ms (`DAT_140a14ca8`).
  `Tsv.refresh` resets the info counters and sets `pingc` = 2 (one more ping).
  - ping = average of the last two non-zero samples.
  - After 3 unanswered requests the original asks U.S.G.N. to poke the server (`[0x12][ip][port]`, `FUN_140301220`).
- **Dedicated flood guard:** the dedicated drops some info requests (`FUN_1400d4c90(ip, 0xFB, 60, 60000)`,
  `FUN_1400d5210(…, 500, 5)`). Often the first request from an address is dropped, which is why the client
  retries. Pings are always answered.
- **`sys/core/svl.cache`:** a comment line, then 6-byte records `{ip i32 LE, port u16 LE}`. The IP int keeps
  the first octet in the MSB, so the bytes are d, c, b, a: `ae 03 e6 17 65 90` = 23.230.3.174:36965.
  - The original loads the cache at startup ("Cached servers added to list: N").
  - When U.S.G.N. gives no list within 15 s, it clears the Internet list and reloads the cache: it prints
    "WARNING: Failed to load online serverlist within 15 sec! …", then "WARNING: Using cached list (servers: N)",
    and the title becomes "Servers - using cached list - see console". The entries then start their 3 pings and
    5 requests again.
- **Window (measured at 850×480):**
  - Layout: window (5,5) 840×470; tabs y 30..50; column header 50..75; list box (21,75)–(838,433) with 15 px
    rows (23 visible) and a 15 px scrollbar.
  - Buttons, 20 px high at y 439: Filters x 21 (100 wide), Refresh/Stop x 126 (100), Connect to IP x 604 (120),
    Connect x 729 (100).
  - Row columns: flag icons (`gfx/gui_server.bmp`, 9 frames of 10×10) at x + 4 + 13k; mode letter at +94; Lua "s"
    at +100 (115,110,255); name at +109 (max 225 px); map at +401 (max 188 px, half brightness when the map is
    not in `maps/`); players at +693; bots at +731 (200,200,0); 10 latency bars (`gfx/netspeed.bmp`) at +757,
    4 px apart.
  - Latency bars: f = ping / 500; lit = ceil(10 (1 − f)); color (510 f, 510 (1 − f), pingblend). pingblend is
    255 on a fresh echo and drops 25 per frame.
  - Players color: 0 players at half brightness; more humans than bots green (0,175,0); full red (175,0,0).
  - Game mode letters S D T C Z, colors (175,175,50) (200,100,100) (200,125,50) (100,150,175) (100,175,100).
  - Order: human players (players − bots), descending.
  - Stats (top right, small font): "N Servers (answered)", "H Players (H)", "B Bots (B)".
  - The first click on a row selects it; clicking the selected row joins. Hovering a row shows an `ip:port` tooltip.
