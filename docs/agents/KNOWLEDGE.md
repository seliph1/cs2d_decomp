# Verified knowledge (shared agent memory)

Facts confirmed against the original binaries. Each one names its source:

- **REA**: Ghidra address in `CS2D.exe` (client) or `cs2d_dedicated.exe` (ded).
- **proxy**: a UDP capture of the original client.
- **Lua**: a probe on the dedicated server.
- **measured**: a calibrated value.

When you confirm or refute something, update this file. All multi-byte integers are little-endian.

## BlitzMax binary layout

- **String object:** `[class ptr][hash +8][length u32 +0xc][UTF-16 text +0x10]`. `search_strings`
  returns the text, so xref the object at text − 0x10.
- **Object:** the first qword points to its class (type table); methods are called through that table.
  The class `Tpl` (player) is at `0x140a18380`, and method +0x198 = `FUN_1401f7600` (move with collision).
- **Null object:** `&DAT_140de8f80` in the client.
- **Resource registry:** `docs/cs2d_gfx_descriptors.json`. Effect sprites embedded in `incbindata/` load in
  `0x14022e9f0`:

  | Sprite | Global |
  |---|---|
  | `bullet.png` | `0x140a0e758` |
  | `flare_x.png` | `0x140a0e750` |
  | `sparkleflare.png` | `0x140a0e748` |
  | `glowdot.bmp` | `0x140a0e018` |

- **Time factor:** `DAT_140a1b090` (1.0 per 60 Hz frame).

## Network: transport (Tudp)

- **Datagram header:** `[seq u16]`. An odd seq means unreliable; it may start with ACK/control triples
  (first byte 1..4, 3 bytes each). An even, non-zero seq means reliable, and the receiver ACKs it with
  `[own hdr][0x01][seq u16]`.
- **Messages:** several messages are concatenated in one payload. Every message length below is
  verified, because an unknown opcode makes the parser drop the rest of the datagram.

## Network: join

- **Handshake:**
  - `[252,0]` → `[252,1, name…]`
  - `[252,2,status,pid,map,secret]` → `[252,3,map sha256 hex]`
  - `[252,4,status]` → `[252,5,map,sha]`
  - `[252,6, map_name(RStr), server_name(RStr), …]`, then `[252,7,section,…]` snapshot chunks, ending with `[252,7,200,…]`.
- **Snapshot section 4:** the non-zero entity states, `{x u16, y u16, state u8}`.
- **Snapshot section 6 (ground projectiles, REA `0x1402c0000` 0x1402d1850):** `[252,7,6,count]`, then per ground projectile `{id u8, type u8, player u8, x u16, y u16}` (7 bytes each).
- **Client data (proxy, Lua):** after the snapshot the original sends
  `[252,10, 0x1c,0x02, size u16 (=4+zlib len), raw size u32 (=1024), zlib(32x32 spray)]`.
  **The server runs its `join` (Lua hook, player list, announcement to the others) only after this.**
  Our client sends a blank spray. The `[67,1]` seen after it is the spectator camera picking its first target
  (see "Spectator"): with nobody alive the original sends no `[67]` and the server still runs `join`
  (140.238.183.194:36969).
- **`[252,6]` settings (REA `0x1402d0560`, `sv_specmode` / `sv_fow` varied on the dedicated):**
  `[252,6,status, map str, server name str, u16, u16, roundtime, freezetime, c4timer, time i32, u8, u8]`, then,
  when more than 5 bytes remain: `maxplayers, sv_fow, sv_specmode, sv_gamemode, respawn delay, infammo, ...`.
- **Snapshot section 1 (players already in game, REA `0x1402cd9a0`, proxy):** `[252,7,1,count]`, then per player
  `id, name (u8 len, cipher key "name"), spray (key "spr"), usgn id i32, steam os u8, steam name, steam id i64,
  usgn name, team, spec state, look, score u16 (+1000), deaths u16, assists u16, mvp u16, rank, x u16, y u16,
  rot u16 (x 360/65536), health, armor, special, weapon, weapon mode, votekick, votemap u16`. The steam and usgn
  names were always empty (2 bytes each); u16 count + UTF-16 like `[240]` is assumed. The name cipher is the
  inverse of `cs2d_net_encrypt` (`cs2d_net_decrypt`).
- **Join fields (REA `FUN_1402ce7c0`, proxy):**
  - `[252,1]`: the password is encrypted with key `j01nk3y` (`0x140a3bf20`); the byte after the OS string is the
    map transfer mode `DAT_140ddf3dc` (0 normal, 1 save as `maps/temp.map`, 2 overwrite). Wrong password: `[252,2,1]`.
  - `[252,3, sha(str), enc secret, download u8]`: `sha` is the SHA-256 of `maps/<map>.map` (`""` when missing,
    `FUN_1402c9f40`); the last byte is the `download` setting of `sys/config.cfg` (`DAT_140a14f4c`; 6 in the shipped config).
  - The map is loaded at `[252,4,0]` (not at `[252,2]`), and `[252,5]` carries the recomputed checksum.
  - The original stays in the main menu during the handshake and the download (screenshot) and enters the game at
    `[252,4,0]`. Ours does the same: `connect` keeps the current scene, the lobby polls the socket, and
    `join_load_map` switches to the game scene on the server's map (no `de_dust2` in between).
- **Leave:** the client sends `[253,0]`. The other clients receive `[253,pid,reason]`; the server then
  drops what the player carried (`[24]`) and prints a `[226]` message.
- **Others joining:** `[248,pid,name(u8 len+text),18 bytes]`, followed by the spray relay
  `[28,pid,2,size u16,size bytes]`.

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
- **After the last file** the server sends `[252,2]` again; the client answers `[252,3]` with the new checksum and
  gets `[252,4,0]`. fun_roleplay: 231 files (2 min at ~19 kb/s), all byte-identical to the server's copies.
- **Window** (350×200, centered; Cancel 100×20 at (234,164) sends `[253,9]`): text at window +(16, 32 / 79 / 127 / 152),
  bars at y +55 (all files) and +105 (current file), 318×16 with a 1 px frame, black inside, `gfx/gui_shade.bmp`
  stretched over `(318 − 6) × fraction` at +3. Text and frame are (150,150,150) (measured on a screenshot).
  Lines: "Downloading data...", "`file` (n of total)" squeezed to 318 px, "read/total kb - p %", "Download Speed: N kb/s"
  (bytes received in the last 1000 ms, `DAT_140ddf5a8`).

## Network: client → server (proxy)

| Message | Bytes | Notes |
|---|---|---|
| Position | `[10 run / 11 walk][x u16][y u16]` | Unreliable, sent on every frame where the integer position changed |
| Rotation | `[70][angle u16 = deg·65536/360][aim distance/2 u8]` | Sent on change |
| Fire | `[7][seed u8]` | Unreliable. Melee always 0; firearms 0, 2, 3, 4, … (global counter across weapons). The ded stores it at player+0x58, reading 0 as 1 (ded `FUN_1401c8930`) |
| Select weapon | `[9][type][0]` | |
| Reload | `[16][1]` | |
| Drop | `[24][type][clip u16][reserve u16][0]` | |
| Buy | `[23][type][0]` | |
| Use | `[26][mode u8][target i32][x u16][y u16]` | Mode 100 = Trigger_Use with its tile; target ray at 0, 16 and 32 px along the aim |
| Team | `[20][team][look]` | `team`: 0 Spectator (`look = 5`), 1 T, 2 CT; `look`: `0..3` class skin or `4` Auto-Select skin |
| Projectile | `[27][type][x u16][y u16][dir u16 = deg·65536/360][flydist u16]` | Sent on throw/fire; flydist clamped to 385 px (0x181) for throwables |
| Spectate | `[67][player]` | Reliable. Followed player, 0 = free look; only sent while dead or Spectator |
| Spec position | `[32][x+32768 u16][y+32768 u16]` | Unreliable, once per second while dead or Spectator: the camera center |

## Network: server → client (proxy, Lua)

| Op | Layout | Meaning |
|---|---|---|
| 7 | `[7,pid,seed]` | Another player fired |
| 9 | `[9,pid,type,mode]` | Weapon held (e.g. Lua `setweapon`) |
| 15 | `[15,pid,x u16,y u16]` | setpos / teleport. The local player must apply it, or it keeps sending stale positions (rubberbanding) |
| 16 | `[16,pid,1 / 2]` | Reload started / finished (finished = clip refilled) |
| 17 | `[17,victim,attacker,hp,armor]` | Damage |
| 20 | `[20,pid,team,look]` | Team and skin broadcast (`team == 0` moves player to Spectator) |
| 21 | `[21,pid,x u16,y u16,weapon,armor,money u16,n,n×item]` | Spawn, with the whole inventory. prison_funst3r gives only the knife (50) |
| 22 | `[22,reason,n] + n×{pid,x u16,y u16,angle f32,weapon,armor,flag}` (12 bytes each) `+ {money u16, k, k×item}` | Round start. The trailer is the receiving player's inventory. `reason` = round end reason (see "Scoreboard") |
| 23 | `[23,pid,type,money u16,u8]` | Buy result; type 250 = refused (prison has no buy zone) |
| 24 | `[24,pid,type,clip u16,reserve u16,u8,item id u16,tile x u16,tile y u16,u8]` | Drop; the item lies on the player's tile |
| 25 | `[25,pid,item id u16,type,clip u16,reserve u16,u8]` | Collect |
| 27 | `[27,pid,type,x u16,y u16,dir u16,flydist u16]` | Projectile broadcast (11 bytes, REA `0x1402d3340`) |
| 31 | `[31,x u16,y u16,state]` | Entity event |
| 33 | `[33,type,item id u16,tile x u16,tile y u16,clip u16,reserve u16]` | Spawn item |
| 41 | `[41,id,pid]` | Kill/detonate projectile (3 bytes, REA `0x1402daf80`) |
| 42 | `[42,pid,field,value u16]` | Playerdata. Fields: 0 health, 1 score, 2 money, 3 armor, 4 deaths, 5 weapon given, 6 weapon taken, 8 max health, 9 assists, 10 MVPs (REA `FUN_1401cc9d0`) |
| 64 | `[64,t u16,ct u16]` | Team scores (`setteamscores`, REA `FUN_1402de440`) |
| 52 | `[52,pid,type,id u16,x u16,y u16,rot u32]` | Spawn ground projectile (13 bytes, REA `0x140060ee0`) |
| 55 | `[55,id,pid]` | Silent kill projectile (3 bytes, REA `0x1402dd7a0`) |
| 58 | `[58,pid,type,id,x+32768 u16,y+32768 u16,flydist u16,dir u16]` | Spawn flying projectile (12 bytes, REA `0x1400a91d0`) |
| 61 | `[61,pid,type,clip u16,reserve u16]` | Set ammo |
| 63 | `[63,id,pid,x+32768 u16,y+32768 u16]` | Move projectile (7 bytes, REA `0x14007dce0`) |
| 226 | `[226,1,u16,len u16,text,u8]` | Message |
| 240 | `[240,pid,mode,len u8,UTF-16LE (2×len bytes)]` | Chat & Server Info (`mode == 50`: `"1"` clear serverinfo, `"2"` open `"<server_name> - Info"` window; `mode == 51`: append UTF-16LE serverinfo line with optional `U+00A9` + 9 digits `RRRGGGBBB` color tag or `U+00BB` gold link) |
| 247 | `[247,n,ticks,u16,u16,n×{pid,ping u16}]` | Ping list (REA `FUN_140060f80`, proxy): server ticks, ping 0xfffe = bot, 0xffff = server |
| 229 | `[229, srv_ts i32]` | Clock Ping / Anti-speedhack (REA `0x1401d7690`, `0x1402e2ed0`, proxy): used when `sv_antiaccel > 0` (default); client replies unreliable `[229, srv_ts i32, cli_ts i32]` (9 bytes); server calculates RTT latency (`now - srv_ts`) into `player->ping` (`Tpl +0xd0`) and tracks `clock_diff` / `clock_ratio` |
| 249 | `[249, srv_ts i32]` | Normal In-Game Ping (REA `0x1401d9f10`, `0x1402e6080`): used when `sv_antiaccel == 0`; client replies unreliable `[249, srv_ts i32]` (5 bytes) |

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

## Movement and collision

Source: REA `FUN_1402563a0` (input) and `FUN_1401f7600` (collision); proxy.

- **Speed:** (2 + weapon bonus) × time factor × 1.2 per 60 Hz frame, about 144 px/s; walking halves it.
  Movement uses 8-direction vectors.
- **Collision box:** 24×24, resolved on X first and then Y, stepping 1 px while the leading corners at
  ±13 / ±12 are free. Tile modes 1..9 block.
- **Sliding:** the original **slides** along walls at the parallel component (W+D ≈ 101 px/s vs D ≈ 138 px/s).
- **Camera:** locked to the player.

## Entities

Source: Lua fixture, 331 samples.

- **State:** 0 = active / visible / closed, 1 = inactive / hidden / open. Triggering toggles the state.
- **Trigger_Delay:** state 1 while counting down, back to 0 when it fires.
- **Trigger_Once:** goes to state 1 after firing.
- **Env_Item:** toggles and spawns its item.
- **Env_Sound:** toggles only when looping (`ints[1]` = 0).
- **Func_GameAction:** 0 draw, 1 T win, 2 CT win, 3 restart in X s, 4 kill Ts, 5 kill CTs, 6 kill all, 7 load map.
- **Round start:** the round-end intermission is 3 s. Round start is deterministic: every state goes back
  to 0, then the Trigger_Start entities fire. Entity states are not resent at round start.
- **Dedicated quirk:** after some round restarts the dedicated stops broadcasting entity changes. The
  original client shows the same divergence.

## Env_Breakable and shots

Source: Lua, proxy and measured, on prison_funst3r.

- **Damage:** a bullet does the weapon damage. The AK-47 (22) breaks 280 health on shot 13. The knife (45)
  breaks the 10 health stone in one hit. The server sends nothing on hits that do not break.
- **Reach:** the knife (range 15) reaches 20 px but not 21; the AK-47 (range 300) reaches 310 px but not 320.
  The code uses range + 6 and range + 15 with 1 px ray steps.
- **What stops a bullet:** walls (collision 1) and closed DynWalls; obstacles do not. A shot fired from
  inside a breakable hits that breakable.
- **Explosion:** `ints[3]` = 1, range `ints[4]` in px, power `ints[5]`. A player at distance 0 took 50
  (range 1, power 50). The fall-off `power × (1 − d/range)` is assumed.
- **Break visuals (frame captures):**
  - the tile splits into 4 quarters that drift, grow slightly and fade, keeping their orientation;
  - material fragments (`gfx/fragments.bmp`) fly back towards the shooter;
  - glass with `strs[0]` = −0.4 is drawn at alpha 0.6 (the sign is ignored).

## Shot effects

Source: REA `FUN_140148f40`, plus frame captures.

- **Muzzle flash** (`incbindata/flare_x.png`):
  - Scaled by `DAT_140dde9d8` (0.1, set at `0x140306d2b`) × 5 and stretched along the aim.
  - Life starts at 1 and drops 0.4 per frame.
  - A big yellow light surrounds the shooter.
  - Flash object: +0x08 angle offset, +0x0c x-scale, +0x10 life, +0x18 player, +0x20 distance,
    +0x24 angle, +0x28 mode (1 = attached to a player), +0x30 object. Constructor `FUN_14001e300`.
- **Tracer:** a thin yellow line from the muzzle to the impact that leaves `glowdot` sparks along the path.
  Sparks also appear at wall hits.
- **Shells** (`gfx/shells.bmp`, 4 frames of 8×8): ejected to the right of the aim; they stay on the floor.
- **Muzzle offset table** `0x1409f5d20` (`init_weapons`, read with `tools/agent/weapontables.py`; not used yet, we
  still draw at 30 px for primary weapons and 22 px for pistols): pistols 1..6 19; M3 / XM1014 28; MP5 22, TMP 20, P90 22,
  Mac 10 20, UMP45 22; AK-47 26, SG552 30, M4A1 26, Aug 30, Scout 30, AWP 33, G3SG1 / SG550 30, Galil / Famas 24;
  M249 28; Laser 30, Flamethrower 25, RPG / Rocket / Grenade Launcher 30; Knife, Machete, Wrench, Claw 25; Chainsaw 30;
  Portal Gun 30; M134 32, FN F2000 23, 92 27.
- **Firearm visual recoil kickback** (REA `init_weapons` `0x1402305db..0x1402340a9`, `Tpl.attack` `0x1402166a7`,
  `Tpl.updateAll` `0x14021d1b1`, `Tpl.draw` `0x14012131c..0x1401213cc`):
  - Per-weapon integer kickback table at `0x1409eda18`:
    - `1` px: MP5 (20), TMP (21), P90 (22), Mac 10 (23), UMP45 (24), SG552 (31), M4A1 (32), Aug (33), M134 (90), FN F2000 (91)
    - `2` px: USP (1), Glock (2), P228 (4), Elite (5), Five-Seven (6), AK-47 (30), Scout (34), Galil (38), Famas (39), M249 (40), RPG (47), Rocket Launcher (48), Grenade Launcher (49)
    - `4` px: Deagle (3), G3SG1 (36), SG550 (37)
    - `5` px: M3 (10), XM1014 (11), Portal Gun (88)
    - `6` px: AWP (35), Laser (45)
  - On shot, `player->recoil` (`Tpl +0x308`) is set to `(float)visual_recoil[id] + Rnd(0.0, 1.0)`.
  - Each 60 Hz frame, `player->recoil` decays by `0.5` (`30.0 px/s`, `0x140bd69e8 = 0.5f`) down to `0.0`.
  - In `Tpl.draw`, the player sprite is displaced backward along `-angle`:
    `sx -= sin(angle) * recoil`, `sy += cos(angle) * recoil`.
- **Melee weapon swing animation** (REA `Tpl.slash` `0x14018cebc`, `Tpl.attack2` `0x1401fdd37`, Wrench `0x1401fad67`,
  `Tpl.updateAll` `0x14021d68a..0x14021d6d2` & `0x14021da21`, `Tpl.draw` `0x14012146b` & `0x14012299b`,
  `Tp.create` `0x14004b4e0`, `Tp.drawAll` `0x140265220`):
  - On any melee swing except Chainsaw (`85`, `0x14018ce92`), `player->slash_timer` (`Tpl +0x31c`) is set to `8.0f`.
  - `player->draw_angle` (`Tpl +0x24c`) is `player->angle + 6.0f * player->slash_timer` (`0x140bd69e4 = 6.0f`),
    starting at `angle + 48°` and sweeping linearly back to `angle` as `slash_timer` decays by `1.0` per 60 Hz frame (`60.0/s`).
  - Both the torso sprite (`0x14012146b`) and held weapon sprite (`0x14012299b`) use `draw_angle` for rotation and forward offset
    (`16.5 px` for firearms `0x14012168e`, `13.5 px` for melee/bomb `0x1401229b5`, `12.5 px` for grenades `0x1401227ab`).
  - Spawns `Tp` type `13` (`gfx/knifeslash.bmp`, `0x140a0dcd8`, `maskMode = 3`, `midhandle = 1`) at `(pos.x, pos.y)`
    rotated by `angle`, drawn with additive blend (`LIGHTBLEND = 4`), alpha `1.0` fading by `0.07` per 60 Hz frame (`4.2/s`).

## Spectator

Source: REA `FUN_14025aa60` (camera), `FUN_1401698a0` (target picker), `FUN_1401682f0` (`[67]`), `FUN_14025a270`
(`[32]`), `FUN_14029ac10` (HUD), `FUN_140298bf0` (HUD button), `FUN_140299470` (name box), command handlers at
`0x1401b58c6` / `0x1401b5980`; proxy and screenshots of the original on prison_funst3r with two bots and on
140.238.183.194:36969. Code: `src/cs2d_spectate.c`, `src/cs2d_scene_game.c`.

- **sv_specmode** (`0x140a14ee8[1]`): 0 Nothing (camera at -100000, box "Server does not allow Spectating"),
  1 Everything, 2 Own Team only (a team member follows only its side, CT and VIP together; a Spectator anybody).
  Live changes: `[237, setting, value]`; settings 1, 11 and 12 carry a string, 2..10 and 13..24 an i32; 3 is
  sv_specmode.
- **Mode** (`DAT_140ddf2f8`): 0 nothing, 1 Free Look, 2 Follow Player; target `DAT_140ddf2f4`. Mode 0 becomes 2
  under sv_specmode 1 or 2. Bindings (`sys/controls.cfg`): `leftctrl` = `specmode`, `space` = `specplayer`.
  - `specmode` (only sv_specmode 1): free -> follow sends `[67,target]`, follow -> free sends `[67,0]`.
  - `specplayer` (no HUD window open; the Server Info window blocks it): in follow mode it picks the next player
    (which sends `[67,n]`), then sends `[67,n]` again; the proxy shows `43 02 43 02`.
- **Picker:** living players in `Tpl.list` order. dir 0 keeps a valid target, else takes the first one; next
  wraps to the first; previous from the second entry (index <= 1) wraps to the last (quirk). Nobody alive:
  target 0 and no `[67]`.
- **Free look:** 10 px per 60 Hz frame per held movement key (`0x140bd6d88`), x 0.5 with walk; the integer
  camera is truncated after each axis step.
- **Follow** (`Tmap_scroll.toplayer_smoothly`): within 32 px (`maxDelta`) on both axes the camera moves
  0.3 x the distance per frame (`smoothScrollFactor`); farther, it jumps.
- **Showcase** (`FUN_140060b10`; flag `DAT_140ddf328` set at join, cleared by any team menu button): every 5 s of
  the 1 s tick, with no T or CT player and a HUD window open, the camera jumps to a random Info_T, then an Info_CT
  (alternating; the first one is random).
- **HUD (850x480):**
  - Black bands at alpha 0.55, y 0..40 and h-40..h. "Spectating" (150) centered at y = 40 - text height - 3.
  - A dead team member also gets "You are dead!" at y 0, pulsing red (`|sin(ms/10 deg)| x 155 + 100`); respawn
    modes show "You are dead! - Click to respawn!" in (255, `|sin(ms/5 deg)| x 255`, 0).
  - Buttons 25 high at y h-33: Menu x 5 w 46, Map x 56 w 37, Fog x 98 w 37 (enabled with sv_fow and
    sv_specmode 1), name box x 140 w w-280, mode button x w-135 w 130 ("Spectate Nothing" / "Free Look" /
    "Follow Player"; enabled in free look, and in follow under sv_specmode 1).
  - Button: inside (50) at alpha 0.6, frame 200 hovered / 150 / 100 disabled, additive `gui_shade` 0.3 / 0.1;
    text white hovered / 200 / 150, or the team color (T 255,25,0; CT 50,150,255) at alpha 1 / 0.8 / 0.5.
  - Name box: "-" (disabled) without a target or in free look. Otherwise an avatar square ("?" without one),
    the name in the team color (white in Deathmatch), `hud_symbols` frame 0 + health and frame 1 + armor
    (additive (255,255,32); value color ((100-v) x 5.1, v x 5.1, 0); armors 201..206 by name). Under
    sv_specmode 2 an enemy shows only its name. Left click = next player, right click = previous.

## Server Info and Team Selection UI

Source: reference screenshots from `CS2D.exe` + dedicated `opcode 240` (`FUN_1400d5190`) and `opcode 20`.

- **Window frame:** both `"<server_name> - Info"` and `"Select Team"` are `600×440` centered at `(125, 20)` on `850×480` with a 1 px horizontal divider below the title bar.
- **`"<server_name> - Info"`:** `553×374` scrollable text box (`ServerInfoBox`) + `15×374` scrollbar on the right (`ServerInfoScrollbar`) + `568×20` `Close` button at the bottom (`BtnServerInfoClose`). When joining a server, `"<server_name> - Info"` renders first and closing it reveals `"Select Team"`.
- **`"Select Team"` (`M` key):** `240×25` option buttons starting at `(205, 120)` with `5 px` vertical gap:
  - Main page: `1 Terrorists (N)` and `2 Counter-Terrorists (N)` open skin subcategories; after a `25 px` empty slot, `5 Auto-Select` and `6 Spectator` select immediately (no subcategory); `0 Close` at `(205, 420)`.
  - Subcategories (`"Select Team - Terrorists"` / `"Select Team - Counter-Terrorists"`): `1..4` skins (`Phoenix Connexion`, `L337 Krew`, `Arctic Avengers`, `Guerilla Warfare` / `Seal Team 6 (DEVGRU)`, `German GSG-9`, `UK SAS`, `French GIGN`) with per-skin player counts `(N)`, `25 px` empty slot, `5 Auto-Select`, `Back` (`y = 390`), `0 Close` (`y = 420`).

## Map editor

Source: REA `FUN_1401a85f0` (GUI builder), `FUN_1402bf960` (editor loop), `FUN_14019cce0` (mode switch),
`TileProp.Get 0x140010450`, the user's screenshots of the original. Code: `src/cs2d_editor.c` (model),
`src/cs2d_scene_editor.c` (scene).

- **Tile modifier byte (`TileProp.Get`):** bits 0-1 rotation (1 = 90, 2 = 180, 3 = 270), bits 2-5 brightness level b
  (color x b * 0.1; b = 0 means 100 %), `(mod & 0xc0) == 0x80` color from the per-tile `Tmap_d` (r, g, b, overlay),
  `0x40` blend frame. Our render used to darken by 0.15 per level and draw `mod - 128` as an overlay: both wrong.
  The map file stores the extra bytes (`0x40`: frame; `0x80`: r, g, b, overlay); `cs2d_map_t.tile_data` keeps them,
  and load -> save is byte-identical on maps whose header already says "heights 2" (fy_shipment: 700 colored tiles).
- **Brush (tile list `+0x58` tile, `+0x5c` modifier, `+0x60` color):** a click in the list selects the tile and
  clears the modifier. Rotation button: rotation + 1 (icon 15 + rotation). "BR: n": level + 1, 9 -> 0, label level * 10
  or 100. Color "Okay": white clears `0x80`, else `(mod & 0x3f) | 0x80`.
- **Painting:** one tile (`FUN_1402bd6d0`) copies tile + modifier (+ color when `mod >= 0x40`); fill (`FUN_1402bdcc0`)
  is a 4-neighbour flood fill over the same tile and modifier; every change goes to `Teditor_actionlist` (undo).
- **Sidebar** (window "sidebar" at (0, -32), 152 wide): icons 20x20 from `gfx/gui_icons.bmp` (24 frames of 16x16):
  - row 1 at y 7: New 1 (x 5), Load 2 (27), Save 3 (49), Map Properties 4 (83), Display Settings 10 (105), Quit 0 (127);
  - row 2 at y 32: Tiles 8, Entities 9, Tools 4, then (tiles mode only) Tile 5 (83), Rect 6 (105), Fill 7 (127);
  - tooltips are sys/language 0421..0432;
  - tile list (5, 58): 5 columns of 25 px (32 px tile at scale 0.78125), (h - 110) / 25 rows, tiles at 200/255 gray
    (255 hovered), selection = additive yellow |sin| * 0.5, tooltip "Tile #n, <mode name>";
  - bottom: "Tile Properties" (5, h - 47) 142x17, rotation (5, h - 25), "BR" (27, h - 25) 60 wide, color box
    (90, h - 25) 57x20;
  - entities mode: filter combo (5, 58) 142 wide ("All|Info|Environment|Generate|Function|Trigger", 0433), list
    (5, 83) 142 x (h - 88).
- **Measured colors (screenshots):** sidebar 54; icon button inside 27, frame 150; grid (255,255,0) at 25 %; hovered
  tile frame (191,191,0) from 2 px outside to 1 px inside with a (255,255,0) line on the edge; status "Tile Position
  x|y - Tile #n" white at 70 % at (156, h - 18); entity type names in the type color at 70 %.
- **Windows:**
  - Tile Properties (155, 5) 480x470: "<" (16,32) 20x32, tile image (41,32), ">" (78,32), label (108,38), 16 options
    from (16, 74) every 20 px, "3D Height (pixels):" (314,160) + box (314,180) 150 wide, checks 3D Flip Y / 3D Mirror /
    Non-Rotated. at (314, 220 / 240 / 260), "Save as default Properties" (16,409) 250 wide, "Load default
    Properties" (16,434), Close (364,434).
  - Color (95, 144) 451x193: preview (16,32) 45x60, sliders (165, 32 / 52 / 72) 270 wide, 16 colors 48x20 at
    (16 + 53 i, 102 / 127), Okay (230,157), Cancel (335,157).
  - Load / Save Map (155, 5) (w - 160) x (h - 10); Display Settings (155, 5) 300x220; Map Settings (155, 5) 480x300.
- **Tile properties:** option -> mode `1,2,3,4,0,10,11,12,13,14,15,16,50,51,52,53` (`FUN_1402b85f0`; "Deadly -
  Explosion" is 51 and "Toxic" 52, unlike the map format text). 3D modifier: Flip Y +1, Mirror +2, Non-Rotated +4.
  Names `FUN_14019ba30` (5 "Floor Wall", 6 "Obstacle Wall").
- **`gfx/tiles/<tileset>.inf`:** the first line is the version:
  - "...Counter-Strike 2D Tileset Informations": one text line per tile;
  - "V 2.0": a mode byte per tile; "V 3.0": mode u8 + height i32;
  - "Unreal Software's CS2D Tileset Informations V 4.0": mode u8 + height u16 + 3D modifier u8 (what "Save as default
    Properties" writes, `FUN_1402b7860`).
  Without heights: walls 32, obstacles 16, else 0. Unknown modes are skipped.
- **Entities:** tables filled by the init code at `0x140318856` (read with `tools/agent/initarrays.py`):
  - full names `0x1409ecf78` (equal to `cs2d_entity_type_name`), short labels `0x1409e9450` (T, CT, Spr, Brkbl, ...),
    zone radius `0x1409f75f8` (Info_T / Info_CT / Info_BombSpot 2, Info_RescuePoint 1);
  - colors `0x1409e9db0` (loop at `0x140318ee7`): Info_T (255,0,0), Info_CT (0,0,255), Env 21..35 (128,255,0),
    Gen 50..53 (255,200,0), Func 70..73 (255,128,255), Trigger 90..96 (100,200,255), other Info (255,255,0);
  - `Te.drawineditor 0x14026a260`: `incbindata/shadow.bmp` at scale 1.2 in black, gui_icons frame 9 in the type
    color (additive), a pulsing shadow at scale 0.8, the short label in the small font at (+19, +21), zones as an
    additive yellow 10 % square; hovering shows the full name.

## Construction mode and dynamic objects

Source: REA / dump (`Tdo` class, debug scope via `tools/agent/bbscope.py CS2D.exe Tdo`), proxy captures of the
original on 140.238.183.194:36969 ("UNLIMITED Construction", ctf_cs2dfortress) and of our client against the
offline dedicated with `sv_gamemode 3` + `tools/agent/lua/construct_probe.lua`. Code: `src/cs2d_dynobj.c`.

- **Tdo fields** (offset from the object start): id +0x8, typ +0xc, health +0x10, mode +0x14, team +0x18, player +0x1c,
  x +0x20, y +0x24 (top-left pixel of the tile), rot +0x28, t1 / t2 timers +0x48 / +0x50, countdown +0x58, rrot +0x5c,
  rotvar +0x64, target +0x68. The dump's `p[n].clas` offsets are n * 8 (BBObject is 8 bytes there).
- **Type table** (`FUN_140235390`; arrays: name 0x1409f2610 / localized 0x1409f79f8, category 0x1409f7c40, price
  0x1409f3930, max health 0x1409f6298, kill reward 0x1409f71b8, per-player limit 0x1409f2608, upgrade points 0x1409f57c8,
  upgrade to 0x1409f57c0): 1 Barricade $300 hp150 (2 hits -> 3), 2 Barbed Wire $500, 3 Wall I $1000 (10 -> 4), 4 Wall II
  $2000 (10 -> 5), 5 Wall III $3000, 6 Gate Field $1500 (limit 5), 7 Dispenser $5000, 8 Turret $5000 (75 -> 11), 9 Supply
  $5000 (200 -> 15), 10 Construction Site, 11 Dual Turret (100 -> 12), 12 Triple Turret, 13/14 Teleporter Entrance / Exit
  $3000, 15 Super Supply, 20 Mine, 21 Laser Mine, 22/23 portals, 30 NPC, 40 Image. Category 1 = building (tile aligned).
- **Messages (server -> client)**, all verified on captures:
  - `[49, id u16, typ, x u16, y u16 (pixels), rot u16 (x 360/65536), mode, team, player]` spawn (13 bytes; `FUN_140143890`).
  - `[35, id u16]` remove (`Tdo.kill`: explosion r 32 for types 7, 8, 9, 11..15).
  - `[36, id u16, sub, ...]` (`Tdo.getdata` 0x1401edb50): 0 turret target u8 (aim + 15-frame burst), 1 Construction Site
    finished (becomes the type stored in `mode`), 2 x,y u16, 3 rot f32, 4 new type u8 (upgrade, health 100), 5 hit
    (Barbed Wire wobble 60 frames / Laser Mine armed / Teleporter Entrance charged, sent 5 s after it is built),
    6 teleporter used, 7 NPC target 6 bytes, 8 team u8, 9 image 2 bytes. Each case reads its bytes only when they are
    there, and an unknown id reads nothing after `sub`.
  - `[37, id u16, value u8]` health percent; values 101..200 are the upgrade progress (Barricade 150, 200 -> `[36,id,4,3]`;
    Wall I 110 .. 200 in 10 Wrench hits), values > 200 are ignored.
  - `[34, pid, team, typ, tx u16, ty u16, mode, rot + 1000 u16, money u16, id u16]` (15 bytes) build result. A build
    creates a Construction Site (typ 10) whose mode is the requested type, then `[36,id,1]`; codes 0xf8..0xff refuse it
    (0716 not finished, 0715 not allowed on server, 0714 same type too close, 0713 limit, 0712 players too close, 0711 not
    allowed, 0562 insufficient funds, 0710 can't build here; `g_lang` element size 0x24 at 0x1401fbece).
  - Each Wrench hit on an own building costs $100 (`[42,pid,2,money]`).
  - Join snapshot `[252,7,5, count, {id u16, typ, health %, mode, x u16 + 32768, y u16 + 32768, player, team, rot f32}]`
    (15 bytes each); `[252,7,3]` lists ground items `{id u16, typ, tx u16, ty u16, clip u16, reserve u16}`.
  - `[22]` round start frees every object on the client (`Tdo.freeall`); the server respawns them with `[49]`.
- **Client -> server:** build `[34, typ, tx u16, ty u16, 0, rot + 1000 u16]` reliable (only Barbed Wire passes the
  player's rotation; the menu "X" buttons send tile 0xffff / 0xffff to remove that type); respawn `[39]` reliable.
- **Wrench right click** (`Tpl.attack2` 0x1401fd6c0): the first click shows `gfx/hud_buildhelper.png` (82x82, additive,
  alpha sin((ms >> 3) deg), top-left at tile * 32 - 25) on the player's tile (yellow) and the cursor tile (green / red);
  the second checks `FUN_14014ef60` and opens the "Construct" menu (0372) or prints 0694 / 0695 in red in the center.
- **FUN_14014ef60 (can build):** within one tile (Chebyshev) and not the player's own tile, floor cell class, no entity
  of type 0, 1, 2, 3, 15, 17, 21, 25, 29, 70, 71 on the tile, outside active `Info_NoBuildings` areas, no object on the
  tile; -1 when another living player is within 32 px of the tile center. ctf_cs2dfortress has `Info_NoBuildings` over
  both spawn bases (11,1) and (11,63), 19 x 11.
- **"Construct" menu** (`Tgui_hudmen` menu 60): hud menu window (125, 20) 600 x 440 (`0x1401aba29`), 240 px buttons at
  window + (16, y): 1 Barricade 30, 2 Barbed Wire 60, 3/4/5 Wall I/II/III 110/140/170, 6 Gate Field 220, 7 Turret 250,
  8 Dispenser 280, 9 Supply 310, Teleporter Entrance / Exit (no key) at x 314, y 280 / 310; "X" (25 px) at x 261 / 559
  for 6..9 and the teleporters; 0 Cancel at h - 46. Right text = price + "$".
  `Tgui_hudmen.button`: black at 0.3, frame 200 hovered / 150 / 50 disabled, additive gui_shade 0.3 / 0.1, key at +5
  (alpha 0.4), label at +20, right text at w - 5 (alpha 0.4).
- **Collision** (`cs2d_update_mapdata` 0x1401413b0, written after tiles and entities): Barricade cell 6 (obstacle),
  Construction Site 5, walls / Dispenser / turrets / supplies 1; Barbed Wire, Gate Field and teleporters stay walkable.
- **Drawing order** (`FUN_14028f890`): floor, `Tdo.draw_ground` (Barbed Wire rot + 90, Gate Field frame 5 additive team
  color |sin * 0.7| + 0.3 over a 2x shadow, Site frame 7, Teleporters 8 / 9 over a 2.3x shadow, mines), items,
  `Tdo.draw_obstacle` (Barricade frame 0), players, `Tdo.draw_wall` (walls frames 2..4; Dispenser / turrets / supplies on
  frame 6 with a team-colored flare1: Dispenser scale 2 rot ms/10, turret scale 1 + `gfx/weapons/turret*.bmp` at `rot`,
  supply scale 0.6 rot ms/15 alpha |sin|). Team colors (`Tdo.setteamcolor`): team game T red, CT blue, neutral yellow;
  otherwise own yellow, others green. `buildings.png` frames: 0 barricade .. 4 Wall III, 5 gate, 6 base plate, 7 site,
  8 / 9 teleporters.
- **Shadows (our choice, not from REA):** solid buildings except the Construction Site cast a 32 px wall shadow
  (`map->object_shadow`); the build helper alpha is floored at 0.35; explosions draw additive.
- **Turret** (`FUN_14028c630`): idle `rot = rrot + 60 sin(rotvar)`, rotvar += 1 per frame; while `countdown` runs it aims
  at the target (`atan2(tx - 16 - x, y + 16 - ty)`, within 445 x 260 px) and fires while countdown > 10.
- **Respawn modes** (DM, TDM, Construction, Zombies): joining a team leaves the player dead; a left click between the HUD
  bands sends `[39]` unless a living player is within 20 px of the cursor; the server answers `[21]`. While
  `mp_respawndelay` (`[252,6]` byte after sv_gamemode) runs the HUD shows 0767 in (200, 0, 0).
- **Kill:** `[19, victim, killer, assist, weapon, x u16, y u16]` (9 bytes; REA `FUN_1401eaf60` reads the 3rd byte as a
  player id and compares the 4th with 0xfa / 0xfc).
- **Hover text** (`FUN_1402a12d0`, called only with the cursor on the 850x480 screen): `mp_hovertext` (`0x140a14e00[1]`)
  is a bit mask, 1 | 2 players, 4 hostages, 8 buildings. It comes in `[252,6]` at maxplayers + 12 + both string
  lengths (31 with the dedicated's default config, proxy) and in `[237,16,i32]`. Bit 8: the first category 1 object
  in `Tdo.list` with `|mouse + cam - (x + 16)| < 17` on both axes. Lines through `FUN_14024fb10` (text centered on a
  point, shadow +1, black 0.4 box with `gfx/gui_c.bmp` corners 8 px, text width rounded to even + 8, max(text h + 2,
  16) high): at cursor y - 25 the type name, or "$1 by $2" (0777) when the owner is in game; colors: owner set and no
  team game -> own (255,255,0) / others (0,255,0), else team 1 (255,25,0), 2 (50,150,255), other yellow (unowned with
  another team value: no title). At y + 25 "n%": a client (`DAT_140ddf400 == 0`, set at join) reads health as a percent;
  above 100 it shows 100 % in (0,510,0) and "Upgrade n%" (0778 + " " + n + "%") at y + 45 in yellow, else clamps to
  >= 1 with ((100 - p) * 5.1, p * 5.1, 0). The listen server computes health / max health and upgrade points itself.
- **Player hover text** (same function, bits 1 | 2, tried before hostages and buildings, which it then skips): the first
  `Tpl.list` player that is not the local one, team >= 1, health >= 1, `Tpl.fog` (+0x3f8) 0, within 20 px of the cursor
  on both axes. Bit 1 title at y - 25, bit 2 "hp%" at y + 25 in ((100 - hp) * 5.1, hp * 5.1, 0). `0x140a015d0[sv_gamemode]
  == 0` (no teams): "Enemy: name" (0774 + ": ") in (255,0,0). Team game: no local player or a Spectator -> the name in
  (255,255,0); team mate (T with T, CT with VIP) -> "Friend: name" (0775) in (0,255,0); else enemy. An enemy covered by
  smoke (`Tpro.smokegren_cover`) gets nothing, and its health only shows under sv_specmode 1 while the flag 0x140a1afd8
  is 0 (the local player is not alive in a team; `FUN_1402559c0` sets it). Then `FUN_14029fc70`: a left click on the
  hovered player while that flag is 0 follows it as a spectator (specmode 1, or 2 for a team mate) and sends `[67]`.
- **Shot debris** (`Tshoot.Shoot` 0x1402073ed, client side for every shot): each tile the bullet enters is checked with
  `Tdo.getatpos`; an object of category 1 or 2 within 457 x 272 px of the shooter, of type 1, 3..5 or below 16 in the
  mask 0x9b80 (7, 8, 9, 11, 12, 15), hit with damage > 0, calls `Tp.breakable(x, y, 1, angle + 180)` and
  `Tp.breakable(x, y, 1, angle)` (a Wrench on an own building, `Tdo.sameteam` 0x140005460, repairs instead).
  `Tp.breakable` (0x1401197c0) material 1: a random sound of the array at 0x140a0d890, then 2 x `particles`
  (`DAT_140ddf182`, 4 in the shipped config) `Tp.matp(1, ...)` at impact + Rnd(-5, 5). `Tp.matp` (0x14004c710)
  material 1: another Rnd(-5, 5), frame Rand(4, 7), gray Rand(50, 150), speed Rnd(0.2, 3) px per frame along the
  direction (constants 0x140bd6b30 / p5 = 3). Tp type 12 (`Tp.Create` 0x14004ac20, `Tp.draw` 0x140263cc0): rotation
  Rand(1, 360), alpha Rnd(0.7, 1.2) - 0.01 per frame, scale Rnd(0.4, 0.7), alpha blend, speed - speed / 15 per
  frame, stops inside a wall tile; on layer 5 a fragment slower than 0.3 moves to another Tp list.

## Capture the flag

Source: `Te.draw` case 0xf (0x140274e28), `FUN_140271b60`, `Tpl.draw` (`self->flag`), `Titem.give`.

- The flag at home is drawn by its `Info_CTF_Flag` entity (ints[0] 0 = T / red item 70, 1 = CT / blue item 71) while the
  entity state is 0: the item image (midhandle) at (x * 32 + 26, y * 32), waving: the pole through a 3 px viewport, then
  nine 4 px viewports every 2 px redraw it shifted by 2 sin((ms >> 2) + 30 i) - 2 sin((ms >> 2) + 30) in gray
  |sin| * 55 + 200 at alpha 0.8. The base always gets an additive flare1 in the team color (rotation ms / 15).
- A taken flag is item 70 / 71 in the carrier's inventory: drawn 7 px behind along the aim, rotation weapon angle + 160,
  scale 0.8; a Terrorist carries the blue image, anybody else the red one. Dropped flags are ground items.
- ctf_cs2dfortress: CT flag (20, 17), T flag (20, 57). The remote server's Lua walls the CT flag in with 56 Wall III
  (fixture `tests/data/dynobj_snapshot_ctf_cs2dfortress.hex`).

## Weapon selection HUD and bindings

Source: REA / dump (`FUN_140166fb0` invnext / invprev, `FUN_1401cc180` slotN, `FUN_14018c580` highlighted item,
`FUN_140219820` / `FUN_14021b910` attack / attack2, the end of `FUN_140295190` draw), command dispatcher at
`0x1401b3e30`, settings at `0x1401ba7df`; screenshots and proxy of the original (`tools/agent/lua/wsel_probe.lua`,
`wheel:n` step). Code: `cs2d_invsel_*` in `src/cs2d_player.c`, `cs2d_render_weapon_select`, `src/cs2d_bind.c`.

- **State:** open slot `DAT_140ddf338` (0 = closed), highlighted index `DAT_140ddf334`. Tables from `init_weapons`
  (id in `DAT_140ddf57c`): HUD slot `0x140a017b8` (1 primary, 2 pistol, 3 melee, 4 grenades, 5 bomb / mines, 6 item 89;
  0 = not listed), order in the slot `0x1409f3ce0` (HE 0, Flashbang 1, Smoke 2, Flare 3, Gas 4, Molotov 5, Snowball 6,
  Air Strike 7; Mine 8, Laser Mine 9), ammo flag `0x140a017b0` (-1 for the Portal Gun). Items of a slot are listed by
  order, then `Titem.list` order.
- **invnext / invprev** (wheel): a closed HUD opens on the held weapon's slot and position, then steps; past the last
  item of a slot the next owned slot is taken (slots wrap 9 -> 0 -> 1; at most 101 steps). invprev enters a slot on its
  last item. Nothing is sent to the server.
- **slotN** (`slot10` = slot 0): with `hud_fastswitch` (`DAT_140ddf19e`, 1 in the shipped config) and one item in the
  slot, that item is held at once; otherwise the slot opens on item 0, and the same key again steps (wrapping).
- **attack / attack2** with the HUD open: holds the highlighted item (`[9, type, 0]`, proxy), resets the fire timer to
  400 ms, closes the HUD, and fires nothing (no `[7]`).
- **Draw** (s = HUD font height / 13 = 1 at 850x480): slots 1..9 from x = `radarsize + 108` when `drawradar` != 0
  (`DAT_140ddf19a` / `DAT_140ddf19b`), else 8, at y 3. Open slot: `gfx/hud_slotheads.bmp` frame slot - 1 additive
  (200,200,0); a `gfx/hud_slot.bmp` row (150x40) per item from y 15 every 46, additive (200,200,0) / (100,50,0); the
  `_d.bmp` image midhandled at +21 (shadow +23, black 0.4; half size above 64 px wide) turning 3 degrees per frame
  (melee: the other way, on a 5 px circle); name at +45 (+43 for slots 4, 6), y + 3, squeezed to 95 px; y + 18
  "clip | reserve" (slots 1, 2) or "n x" (4, 6). Text (255,255,0) highlighted / (220,180,0), shadow black 0.8 at +1.
  Column 155 wide. Closed slots: a 10x10 additive (100,50,0) square per item every 12 px under the number, the number
  in (100,0,0) when empty; column 15 wide. Infinite ammo shows "clip | INF" (0762).
- **Bindings** (`sys/controls.cfg`, `bind "<key>" "<command>"`): `mwheelup` invprev, `mwheeldown` invnext, `0`..`9`
  slot10 / slot1..slot9.

## In-game Action Progress Bar (Reloading, Planting, Defusing)

Source: CS2D.exe dump `FUN_140295190` (`0x1402958f0..0x140295b70`), `Tpl.update` (`0x14021e35a`), `Tpl.reload` (`+0x320`),
`Tpl.proc` (`+0x340`), `Tpl.proctimer` (`+0x348`), language entries 0759..0761. Code: `src/cs2d_render.c`.

- **Trigger conditions:**
  - `Reloading` (`sys/language 0759`): `player->reload == 1` / `player->reload_timer > 0` on firearms (`wep->clip_size > 0` and `wep->slot != CS2D_SLOT_MELEE`). Progress: `1.0f - (player->reload_timer / wep->reload_time)`.
  - `Planting Bomb` (`sys/language 0760`): `player->proc == 1` (`player->is_planting`). Total time 3000 ms (`0.00033333333`). Progress: `player->plant_progress / 3.0f`.
  - `Defusing Bomb` (`sys/language 0761`): `player->proc == 2` (no kit, 10000 ms, `0.0001`) or `player->proc == 3` (with kit, 5000 ms, `0.0002`). Progress: `player->defuse_progress / (has_kit ? 5.0f : 10.0f)`.
- **Dimensions & Geometry (0x140295980):**
  - Font scale: `scale = font_h / 13.0f = 1.0f` (`DAT_140a1ccc8 * 0.07692308`).
  - Bar dimensions: `w = (int)(scale * 102.0f) = 102 px`, `h = (int)(scale * 20.0f) = 20 px`.
  - Border: `border = max(1, (int)scale) = 1 px`.
  - Position: `x = screen_w / 2 - w / 2`, `y = screen_h / 2 + (int)(scale * 80.0f)`.
  - Inner fill: `inner_w = w - border * 4 = 98 px`, `inner_h = h - border * 4 = 16 px`, `inner_x = x + border * 2`, `inner_y = y + border * 2`.
- **Styling & Colors:**
  - Background: `incbindata/shadow.bmp` (`bank->tex_shadow`) midhandled at `(screen_w / 2, y + h / 2)`, scaled `(scale * 11.0f, scale * 5.0f)` in black at alpha 0.40.
  - Text: Centered at `screen_w / 2`, `y = bar_y - text_h - (int)(scale * 4.0f)`. Color: warm amber/gold `(255, 200, 0)` (`DAT_140a14c6c * 1.64`), alpha 0.75.
  - Border & Inner fill: Same `(255, 200, 0)` color, alpha 0.75. Fill width: `inner_w * progress`.

## Buy menu

Source: dump `Tgui_hudmen` (`update 0x14002eb80`, `button 0x140129620`, `fbutton 0x14012a2c0`, `box 0x14012aaf0`,
`moneyinfo 0x140108010`, `drawBuySectionPreview 0x140111b10`, `weaponinfo 0x14013bfd0`, `buyWpnButton 0x140202670`,
`draw 0x14021f6d0` menus 0x14..0x1a), command handlers at `0x1401b546d` (buy) / `0x1401b5530` (buyammo1) /
`0x1401b55c7` (buyequip), `FUN_1401fff70` (buy request), `init_weapons` tables. Code: `src/cs2d_hudmen.c`.

- **g_lang strings:** `g_lang[N].scales + data_start + k` is `sys/language` entry `(N * 36 + k + 28) / 8` (Price = 0242,
  Close = 0031, Back = 0032 check out).
- **Commands** (`sys/controls.cfg`: `B` buy, `C` buyequip, `,` buyammo1, `.` buyammo2), only while `DAT_140ddf418` is
  1..2 (this is the game state, not the team: 0 menu, 1 / 2 in a game, 4 map editor, set at 0x1401d0af5): `buy` sets menu 20, `buyequip` menu 26 (`FUN_14014ee60` opens the window), both skipped when the
  setting `0x140a14e20[1]` starts with "-"; `buyammo1/2` buy item 61 / 62 at once.
- **Buying** (`FUN_1401fff70(pid, item, 0, 1, 0)`): sends `[23, item, 0]`; the menu stays open (the button only flushes
  the keys). Autobuy / rebuy go through the same function with local checks (`DAT_140ddf2e0`, not reconstructed).
- **Window:** 600 x 440 centered. Buttons 240 x 25 at x 16, key = number key (`DAT_140ddf340`):
  - menu 20 "Buy" (0308): 1 Handgun (0309) y 30, 2 Shotgun 60, 3 Sub Machine Gun 90, 4 Rifle 120, 5 Machine Gun 150, 6
    Primary Ammo 210, 7 Secondary Ammo 240, 8 Equipment (0316) 270, 0 Close (0031) h-46; no key: Auto-Buy (0318) and
    Re-Buy Previous (0317) at x w-256, y h-76 / h-46. The section buttons show the `_k.bmp` of Glock / USP, M3, MP5,
    AK-47 / M4A1, M249, Kevlar+Helm (T / CT by `Tpl.team == 1`).
  - 21..26 "Buy - <section>" (`" - "`): items every 30 px from y 30, Back (0032, no key) h-76, 0 Close h-46.
    Handgun 1..6; Shotgun 10, 11; SMG key 1 Mac 10 (T) / MP5, then TMP, P90, UMP45; Rifle Galil / Famas, AK-47 / M4A1,
    SG552 / Aug at 30..90, then Scout, AWP, G3SG1, SG550 at 150..240; MG M249; Equipment Kevlar, Kevlar+Helm,
    Flashbang, HE, Smoke, Defuse Kit (enabled when the map has Info_BombSpot, `DAT_140ddf078`, and team >= 2), Night
    Vision, Tactical Shield (team > 1), Flare.
- **Button:** inside black 0.3; frame 200 hovered / 150 / 50 disabled; gui_shade additive 0.3 / 0.1; text 255 / 200 /
  100 at y + 12 - h / 2; key at +5 (alpha 0.4), label at +20 (+5 without key); an image is scaled to fit 46 x 22 (max 1),
  centered on x + 42, additive 0.75, and moves the label to +70 (+55); right text at w - 5 (alpha 0.4). Items:
  name, `_k.bmp`, `"<price> $"`.
- **Images** (`0x140220000` loader, name without spaces / dashes, lower case, maskMode 2): `_k.bmp` 0x1409f3d18
  (top-left), `_m.bmp` 0x1409f3d20 (int midhandle), `_d.bmp` 0x1409f3d28.
- **Hovering a section** (menu 20): its `_m` images circle window + (430, 180), shadow +2 black 0.3, step 360 / n (int),
  turning 3 degrees per frame (`rot += g_dt * 3`). Radii: handgun 35 (1..6), shotgun 30, SMG 40 (T 23,20,22,24; CT
  20,21,22,24), rifle 55 (T 38,30,31,34..37; CT 39,32,33,34..37), MG / ammo at the center, equipment 55 (57,58,52,51,56,
  53,59,41,54).
- **Hovering an item** (`weaponinfo`): `_m` at (430, 180) and 3x at (430, 110) (shadow +6, +4 when taller than 35 or
  wider than 100, +3 when wider than 150 or taller than 55), `_d` when there is no `_m`; `_k` at (285, 35) additive (100);
  box (275, 215) 305 x 190; lines in (150) at x 280, values right aligned at 575, y 220 + 20 n: Name (0199), Price (0242,
  label and value red when money < price), then for HUD slots 1 / 2: Damage, Ammo `clip|reserve`, Rate of Fire
  `60000 / (delay * 40)` rounds/min, Range `<0248..0251> (range)` (< 21, < 101, < 301), Weight (0 Normal, > 0 Light,
  < 0 Heavy 0876), Accuracy (0 Perfect, 1 Good, < 4 Average, < 6 Bad, else Very Bad, `$1` = value), a y 380 note
  (Silencer 1 / 32, Burst-Mode 2 / 39, "5 bullets" damage x 5 for 10 / 11, 1x Zoom 31 / 33, 2x Zoom 34..37). Equipment
  has its own notes (0266..0278); Kevlar 65 / Kevlar+Helm 100 AP.
- **Client stats** (`init_weapons`, `tools/agent/weapontables.py`): damage 0x1409f62a0, clip 0x140a017a8, reserve
  0x140a017b0, fire delay 0x1409f2600, range 0x1409f62a8, accuracy 0x1409f5d18, weight 0x1409efd18 (float), zoom damage
  0x1409efd10 / 0x1409efd08. Range, accuracy and weight differ from the dedicated's table (M3 range 50, AWP weight -0.5).
- **Fire and reload timers** (`Tpl.firetimer` / `Tpl.reloadtimer`, `Ttimer.reset(ms)`: time = ms + `g_time_ms`):
  `Tpl.attack` (`FUN_1402164d0`, imul 0x27 at 0x140216b8f) re-arms the fire timer with firedelay x **39 ms** (AK-47 3
  -> 117 ms); the client also reads `firedelay` from `sys/weapons.cfg` into 0x1409f2600 (`FUN_140175f40`, clamped
  2..255). Knife / Claw right click (`Tpl.attack2`, 0x1401fdde8 / 0x1401fde66) waits 1200 ms. Reload is two stages of
  reload x 5 ms each (`FUN_140214780` states 1 -> 4 -> 2), reload x 10 ms in total.
- **moneyinfo:** box (514, 30) 70 x 25, `"<money> $"` right aligned at 579, y 33, in (200).

## In-game chat and server messages

Source: dump `Thud_msg` (`add_center 0x14009d2c0`, `draw_center 0x14024d230`, `add_chat 0x140117a90`,
`draw_chat 0x14024d8a0`, `tintTeamColor 0x140083a40`), dispatcher `FUN_1401de400` (0x1401d0000.c),
chat prompt `FUN_14029f7b0`, opcode 240 handler `FUN_1402c8100` (`recv 240 msg`). Code: `src/cs2d_client.c`, `src/cs2d_scene_game.c`.

- **Chat log layout (`Thud_msg.draw_chat` 0x14024d8a0):**
  - Anchor: `base_y = screen_h - 85 - (line_height * 2)`, stacking upwards (`y -= line_height`).
  - Coordinates: text at `x = 3.0f`, black drop shadow at `x = 4.0f, y + 1.0f` with alpha `fade`.
  - Duration: 10 seconds (`hud_chattime 10`, `DAT_140a14c7c * 1000`).
  - Fade out: after 10 s, `fade -= dt * 0.05f` until `fade <= 0.0f`, then removed.
  - Radar ceiling: lines above `y = 180.0f` fade out to prevent collision with top radar.
- **Chat input prompt (`FUN_14029f7b0`):**
  - Opened via `say` (bind "Y", mode 1, prompt `"Say : "`) and `sayteam` (bind "U", mode 2, prompt `"Say (Team) : "`).
  - Drawn at `base_y` with alpha 0.4 black background box, drop shadow (+1, +1), and blinking vertical cursor.
  - Traps text input, Esc cancels, Enter sends via `cs2d_client_send_chat`. Stops movement and weapon firing while open.
  - SDL key translation: SDL3 defines `SDLK_A..SDLK_Z` as ASCII `'a'..'z'` (0x61..0x7a); `sdl_key_to_cs2d_key` maps
    all letter keys to uppercase `'A'..'Z'` so `sys/controls.cfg` binds and switch handlers match.
  - `chat_just_opened` flag suppresses the initial text input event corresponding to the opening key ('y' / 'u') so the
    prompt starts with a clean empty input buffer.
- **Team colors (`tintTeamColor` 0x140083a40):**
  - Spectator / Server: `(255, 220, 0)` (CS2D gold)
  - Terrorist: `(255, 25, 0)` (red/orange)
  - Counter-Terrorist: `(50, 150, 255)` (blue)
  - Dead player formatting: prefix `*DEAD* <name>: ` (mode 1) and `*DEAD*(TEAM) <name>: ` (mode 2).
- **Server messages & tag `@C` (`FUN_1401de400`):**
  - Server messages from `sv_msg`, `sv_msg2`, `msg`, `msg2`, or opcode 226/240.
  - If message ends with `@C` or `@c` (or mode == 2): tag `@C` is stripped, routed to `Thud_msg.add_center` (screen center).
  - Otherwise routed to `Thud_msg.add_chat` (HUD chat log).
  - All messages are echoed to the console (`cs2d_con_print` / `cs2d_client_console_print`).
  - Color tag: leading `\xA9RRRGGGBBB` (`U+00A9` / `\xC2\xA9` + 9 ASCII digits) sets line RGB and strips the 10-char prefix.
- **Center messages (`Thud_msg.draw_center` 0x14024d230):**
  - Centered horizontally: `x = (screen_w - text_w) / 2`.
  - Origin $Y$: `y = screen_h / 2 - 10 * count - 50`.
  - Text drop shadow at `x + 1, y + 1` (black, alpha `fade`).
  - Duration: 5 seconds (`DAT_140a14c74`, default 5). Same text restarts its timer.
- **Network opcode 240 (`CS2D_NET_MSG2`):**
  - Client send: `[240, mode u8 (1=say, 2=sayteam), char_len u16 LE, char_len * 2 bytes UTF-16LE]`.
  - Server recv & broadcast: `[240, sender_id u8, mode u8, char_len u16 LE, char_len * 2 bytes UTF-16LE]`.


## Scoreboard

Source: dump `Thud_scoreboard` (`draw 0x140251220`, `drawBase 0x140250170`, `drawTeamHeader 0x140124cd0`,
`drawTeamColumns 0x140124ff0`, `drawPlayer 0x14012e4b0`, `drawWithTeams 0x140130450`, `drawWithoutTeams 0x1401309d0`,
spectators `FUN_1401066d0`), scope `0x1409df4e0`, kill `FUN_1401eaf60`, round start `FUN_1402520b0`; screenshots of our
client offline and against the dedicated. Code: `src/cs2d_scoreboard.c`.

- **Key:** `bind "tab" "showscores"` sets `DAT_140ddf480` while held; the HUD draws `Thud_scoreboard.draw(1)` after the chat.
- **Settings (shipped):** columns Latency, Deaths, Assists, Score + avatars, flags, MVPs on (`0x140a14fd0..0x140a14fe8` = 1);
  money, K / D, ids, ranks, shortcuts off (.bss). Size setting `0x140ddf1e0` = 0.
- **Window:** resolution table (widths `0x140bd7860`, heights `0x140bd78a0`: 640x480, 850x480, 800x600, 1060x600, ...);
  a screen width at index i > 1 uses entry 1 (i = 2, 3), `0x140bc8a40[i - 4]` (4..11) or i, minus 20, centered when it
  fits; otherwise the screen minus 10 px on every side (850x480: 830x460 at (10, 10)). Black 0.55, `gui_c.bmp` 16 px corners.
- **Sizes** (HUD font 13 < 15): 15 px font, small 12, scale 1.1538461; value column 48 (+2 gap), team header 26, gap 2,
  row 21, icon 17, column (rw - 32) / 2 - 4 = 395; rows per column ceil(...) = 17.
- **Top line** (y = ry + 5): server name (255,220,0) at rx + 16, ticks (small, red < 20, yellow < 30, else 180) after it,
  map name (150,255,0) right-aligned at rx + rw - 16. Then y += 15 + scale * 8.
- **Team game:** CT (50,150,255) left "Counter-Terrorist Forces" (0353), T (255,25,0) right "Terrorist Forces" (0352);
  Zombies: "Survivors" (0880) / "Zombies" (0879) in green. Header: 0.2 bar, white name (squeezed to 75 %), team score on
  the right, "n Players" (0201, "1 Player") small at 0.7. Without teams: one green "Players" header over both columns.
- **Rows:** sortval = score * 500000 + assists * 5000 + mvp * 500 - deaths, descending. Row bar 0.1 / 0.03 alternating,
  the local player +50 brighter at 0.3. Dead: name gray 110 at 0.8, `hud_symbols` frame 12 fading yellow -> 200 over
  130 frames, deaths white -> row color. Score / assists flash for 97.5 frames after a change (green, yellow without teams).
  Latency colored like the server browser; offline the local player "Server" (0281), bots "Bot" (0229).
- **Team scores:** `[252,6]` u16 T, u16 CT after the server name; `[64]`; client-side `+1` at `[22]` by reason:
  1 << reason in 0x1004010040101000 T, 0x804 / 21 / 22 / 0x2008020080000000 CT, 4 and 5 reset all scores.
- **Kill scoring (client):** victim deaths + 1 unless weapon 0xfc; T vs CT/VIP killer + 1; otherwise (not self, weapon not
  0xff / 72 / 73 / 76 / 77 / 87) team game - 1, no teams + 1.
- **Spectators:** "Spectators (n): " (0791) + names with ", " (80), alternating 110 / 150, anchored to the window bottom.

## Env_Object and tile blending

Source: dump `Te.draw` (0x140271ee0), map draw `FUN_140281fd0`, editor list at 0x1402a8707, image loads at
0x140221404; `Ttileblend` (`updatetile` 0x1400ae560, `FUN_14005e2e0`), floor draw 0x140270a40, `gfx/blendmap.bmp`;
screenshots of the original (fun_roleplay). Code: `render_env_object_*`, `tileblend_*` in `src/cs2d_render.c`.

- **Draw order** (`FUN_140281fd0`, also the editor loop `FUN_1402bf960`): floor, `Te.draw(0)`, items, `Te.draw(1)`,
  players, `Te.draw(2)` (overhead).
- **Env_Object** ints[0]: 0 Palm, 1 Tree, 2 Alarm, 3 Sink, 4 Toilet, 5 Street Light, 6 Street Light (flicker); ints[1..3]
  color, ints[4] rotation, strs[0] size, strs[1] alpha, both read with bbStringToFloat (empty = 0; fun_roleplay's street
  lights have alpha "0", so only the lamp shows). Only state 0 is drawn (prison_funst3r's alarms start in state 1).
  - `incbindata/objects.bmp`: 10 frames of 32x32, midhandle: 0 sink, 1 toilet, 2 street lamp.
  - Layer 1: Sink / Toilet, frame type - 3, rotation (rot / 10) * 10, scale size, color, alpha, ALPHABLEND.
  - Layer 2, Palm: 8 `palmleave.png` (handle 15, 2) at scale size x 0.8, rotation k * 45 + rot + 3 sway(k * 20 + age).
  - Layer 2, Tree: 7 `treeleaves.png` (midhandle) at size x 30 px, angle k * 51 + rot + 2 sway(age + k * 50), rotation
    rot + 5 sway; then one at scale size x 1.2 on the center, rotation rot + 5 sin(age). sway = sin when k % 3 == 0,
    else cos. `Te.age` += 2 x time factor per frame (only while on screen).
  - Leaf shadows (palm and the 7 tree clumps) with itemshadows: black at shadows % x alpha, + 4 px along
    (cos, sin)(ShadowRotation - 180).
  - Layer 2, Alarm: `gfx/sprites/flare1.bmp` LIGHTBLEND, rotation rot, scale size, alpha |sin(ms / 3 degrees)| x alpha.
  - Layer 2, Street Light: `gfx/sprites/flare3.bmp` LIGHTBLEND, rotation ms / 150 + rot, scale size, alpha; type 6: when
    Rand(1, 5) == 1, alpha x Rnd(0, 0.8). Then objects frame 2, white, scale 1, rotation (rot / 10) * 10.
- **Tile blending:** a tile with `(mod & 0xc0) == 0x40` stores a blend frame (one byte after the modifier). The neighbour
  is taken in direction frame & 7 (0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left;
  outside the map = tile 0). The same tile as the neighbour clears the 0x40 flag. Otherwise the image is
  `own * (1 - f) + neighbour * f` per channel (alpha included), f = red of `gfx/blendmap.bmp` (256x160, 40 frames of
  32x32) frame `frame` at (x, y) / (tile size / 32), truncated to int. It is drawn with the tile's rotation and brightness.
  The blendmap is white on the side the direction points to (all 40 frames: >= 200 there, <= 55 opposite). The images are built at map
  load ("Generating Tile Blending") when `DAT_140ddf17f` is 1 (probably `tileanimations`, from the order of the
  settings at 0x1401b0989; not confirmed). de_aztec has 198 blended tiles, fun_roleplay 2700.

## Rendering notes

- Roof parapet shadow: none (checked in `FUN_140260330`).
- Shadow occlusion halos merge with max.
- Entities in state ≠ 0 are not drawn.
- With the light engine on (`light` > 0, the shipped config) the day shadows come from the light buffer (see
  "Light engine"); the CPU shadow map below is only used with `light 0` and was never compared with the original's
  `mapshadows 1` path (`FUN_1401165a0`).
- The shadow map is partitioned into 16x16-tile chunks (256x256 texels each). When `map->entity_solid_version`
  changes (DynWall toggles or player buildings change), fast delta tracking marks only the affected localized chunks dirty,
  avoiding full-map software rasterization freezes and dynamic heap reallocations. Visible dirty chunks are lazily generated
  and updated into GPU textures.
- **Player shadows (REA `Tpl.draw 0x140121160 / 0x140122298`):**
  - Drawn when `shadows` (`DAT_140a14f7b`) is not 0 (Tpl.draw 0x140121160; `playershadows` does not gate it).
  - Image: `incbindata/shadow.bmp` (`PTR_bbNullObject_140a0e038`, 32x32 circular soft shadow loaded with maskMode 3).
  - Geometry: Centered on player position `(sx, sy)` including recoil backward kickback, rotation 0.0 deg.
  - Scale: `1.5x` (`32.0f * 1.5f = 48.0f` px width/height).
  - Color & Alpha: Black `(0, 0, 0)`, alpha `p0 * 0.5` where `p0` is 1, or `|sin(ms / 10)| * 0.1 + 0.05` under the stealth
    suit (armor 206): not the `shadows` percentage.
  - Also applied to hostages (`Info_Hostage`, `Thostage.draw 0x140147090`) with the same 48x48 scale 1.5.
  - With the light engine the original also puts each player's silhouette into the day shadows (`FUN_140112340`, see
    "Light engine"); our client does exactly that when `cs2d_light_day_shadows()`.
  - **Without the light engine our client differs on purpose (user request, 2026-10-12):** players cast their silhouette (legs, torso, held
    weapon) shifted along the map sun, with the obstacle shadow length formula for h = 6 px
    (`dz = -3 - 6 |sin(sun - 180)| / 2`; sun 225 -> (+3.6, +3.6) px). Two passes: the parts mark texels with alpha
    >= 0.5 in the depth buffer (color writes off), then one black quad at the shadow alpha fills `GL_EQUAL` pixels, so
    overlaps leave no seams (`set_player_shadow_offset`, `cs2d_render_player`). Hostages keep the blob.
- **Ground item & projectile shadows (REA `Titem.draw 0x1401483a0`, `Te.draw 0x140260000`, `Tpro.draw 0x140288702`):**
  - Triggered when `itemshadows != 0` and `shadows > 0` (`DAT_140ddf183` and `DAT_140a14f7b`).
  - Circular ground shadow: `incbindata/shadow.bmp` at scale 1.0 (32x32 px) in black `(0,0,0)` at `(x, y)` with alpha `shadows * 0.01f`.
  - Item silhouette drop shadow: Item image (`tex_weapons_d[type]` or projectile sprite) rendered in black `(0,0,0)` with alpha `shadows * 0.01f` offset by `+1.0 px, +1.0 px` at `(x + 1.0f, y + 1.0f)`.
  - Normal item drawn at `(x, y)` in full color above both shadows.

## Light engine (TSmartLighting)

Source: dump `docs/dump/cs2d/lighting/` (`TSmartLighting`, `TLightSource`, `TLightingRoom`, `TLightingStripe`,
`TLightingSprite(Floor)`, `ObstacleShadow(2)`, `SmartImageBuffer`, `LightFBOManager`), the frame `FUN_140281fd0`, the map
setup `FUN_1401d6790`, and the functions themselves run in Unicorn (`tools/agent/emu_light.py`); screenshots of the
original on fun_roleplay by day and at night (`tools/agent/lua/light_probe.lua`, `night_probe.lua`). Code:
`src/cs2d_light.c`, checked call for call against the original (`test_light_engine`, `tools/agent/light_check.sh`).

- **Settings** (sys/config.cfg): `light` 0..5 (`DAT_140ddf185`; 0 = off; `SetQuality(light - 1)`: 1..3 buffers at half
  size (scale 2), 4 full, 5 full + `LIGHTFIXES`), `mapshadows` 0..4 (`DAT_140ddf190`: day shadows, floor sprites and
  occlusion need > 1, roof layers > 2, high-wall shadows > 3), `shadows` (day shadow floor `255 - shadows * 2.55`).
  Server: `sv_forcelight` (`[237,19]`; `[252,6]` u8) turns `light 0` into 1; `sv_daylighttime` (`[237,20]`; in `[252,6]`
  a u16 = value + 1 after `mp_hovertext`, a u8, the background string and 3 i32) replaces the map's sun angle.
- **Defaults** (static init 0x140305f79..0x14030604f): penumbra 55, occlusion on, night color 0, sun offset 0,
  ScreenWidth 850, TilesX 27; `DAT_140ddf418` is the game state (0 menu, 1 / 2 in a game, 4 editor).
- **Sun:** `ShadowRotation` = the map's daylight (225 unless the header holds 1000 + angle) or sv_daylighttime; it eases
  toward the target by 10 % x dt per frame when within 90 degrees, else jumps. Day = 180 < angle < 360.
- **Tiles** (`SetTile` from `cs2d_update_mapdata`): TileMap = tile + 1 (0x100 for an entity wall, -1 for tile 0);
  TileMapMode 2 wall (cell property 1), 1 obstacle (2), 0 anything else. The cell property is the tile mode, then
  Env_Breakable `ints[6]` (1 wall, 2 floor, 3 obstacle, else wall), closed Func_DynWall `ints[1]` (0 wall, 1 obstacle,
  2 / 3 the no-shadow modes, 4 the mode of tile `ints[0]`), solid buildings (wall). Heights = the map's tile heights,
  index 255 = 32.
- **Entities** (`FUN_1401d6790`): Env_Room x, y, w `ints[0]`, h `ints[1]`, roof height `ints[2]`, color `ints[3..5]`,
  edge mode `ints[6]` (roommap_invert frames mode * 8 + 0..7), roof shadow `ints[7]`, day color `1 - ints[8]`, layer
  `ints[9]`, offset `strs[0]` / `strs[1]`, transparency `strs[2]` (drawn only when <= 0.95). Env_Light at the tile center
  + (`strs[1]`, `strs[2]`), size min(`ints[0]`, 256), color `ints[1..3]`, angles `ints[4]` / `ints[5]` (0, 0 = 0..359),
  additive alpha `strs[0]`, rotation `strs[3]`, max size 512; entity state 1 = off; `Te.draw(2)` adds the speed `ints[6]` to
  the angles every frame. Env_LightStripe at + (`ints[7]`, `ints[8]`) tiles, w / h `ints[0..1]`, gfx_stream frame
  `ints[2]`, color `ints[3..5]`, day `1 - ints[6]`.
- **Frame** (`FUN_140281fd0`): `FUN_140262ed0` builds the light buffer first; floor, entities, items, then the **floor
  occlusion** (`DrawTilesFloor`), `Te.draw(1)`, players, the **light color** (`DrawLightsColorList`: each light's buffer
  added in color x alpha x 0.65), overhead, then the **light buffer** multiplied over the world before the HUD.
- **Light buffer** (`GenerateMainBuffer`, 854 x 480 / scale): white; by day `GenerateDayBuffer` + floor light sprites
  (added); rooms (alpha blend, their color); toning (multiply by `gfx_toning.png` row 0 at index
  ((ShadowRotation + 200) mod 360) / 360 x 1024, then add the night color); light sprites; each light's buffer added in
  its color; stripes added.
- **Day buffer:** every wall and obstacle casts the hexagon of its tile swept along the sun by
  `WallSize = (-h / 2 - h |cos(sun - 180)| / 2)`, black under the shade blend, with a blurred rim (`gfx_blur` quads on the
  edges where the neighbour height differs, `cs2d_draw_texturedpoly_3`); player silhouettes (black, 4 px away from the
  sun); obstacles and wall tops white again; with mapshadows > 3 walls above 18 px get a hexagon of `WallSize + 10`
  merged per row plus its rim; room roofs with a shadow cast the hexagon of the room (`-h / 2 - h / 2 |cos|`); roof
  layers 1..5 each shade and then whiten their area; void tiles white; at last + max(255 - night color, floor) everywhere.
- **Light buffers** (`UpdateLightBuffer`, 512 / scale, kept while on screen, redrawn on change): white; for every wall
  within size / 32 + 4 tiles `CalculateShadow` picks two silhouette corners by the light's direction and `DrawLineLight`
  draws the shadow polygon (2 x size long, closed 6 x size beyond the middle) with a 55 px penumbra triangle at each end
  unless a neighbour wall continues it (table `s_shadow_case`); walls white again; cone limits as black 5-degree wedges
  plus penumbra; multiplied by `gfx_light.png` stretched to 2 x size; black outside its square.
- **Floor occlusion** (`DrawTilesFloor`): pass 1 `gfx_shadowmap.png` on every non-wall tile from its wall neighbours,
  pass 2 `gfx_oclussionmap.png` on floor tiles from wall and obstacle neighbours; frames, rotations and mirrors per
  8-neighbour pattern come from the original (`tools/agent/gen_occlusion_table.py`).
- **Light sprites:** Env_Sprite with `strs[7]` "1" (white light sprite), alarms (Env_Object 2: flare1 in its color),
  Trigger_Use glow buttons (flare1 at 0.7); floor light sprites (by day): palm and tree leaves, white. An Env_Sprite whose
  blend is not light / shade / grayscale is added white to the day buffer (`ObstacleShadow2`), so roof sprites are lit.
- **Max2D quirks:** the SDL driver clamps colors to 0..255; blend 1 / 3 = alpha, 4 = add, 5 = multiply (ignores alpha);
  `cs2d_DrawTexturedPoly` emits every fan triangle twice, so the penumbra and blur textures apply squared.
- **Trigger_Use buttons** (`Te.draw` layer 1): `incbindata/buttons.bmp` frame `ints[0] - 1` (1..10), rotated by `ints[1]`;
  above 10 frame 1 plus a pulsing glow (11 / 16 red, 12 / 17 green, 13 blue, 14 yellow, 15 white).
- **Entity count:** fun_roleplay has 9901 entities (812 light entities); our loader used to keep 2048
  (`CS2D_MAX_ENTITIES` is now 16384).

## Projectiles and Persistent Fields (Tpro, Tfield)

Source: REA `CS2D.exe` class `Tpro` (`0x14001c900`, `add 0x1400a8b30`, `addground 0x140030120`, `spawnprojectile 0x1400a9190`, `spawngroundprojectile 0x140060ee0`, `remove 0x1402daf80`, `kill 0x1402dd7a0`), `Tpl.attack / throw` (`0x140217d80`). Code: `src/cs2d_projectile.c`, `include/cs2d_projectile.h`.

- **Throw Distance:** Aim distance clamped to max 385 px (`0x181` in `FUN_140217d80`).
- **Throwables:** HE Grenade (51), Flashbang (52), Smoke Grenade (53), Flare (54), Gas Grenade (72), Molotov Cocktail (73), Snowball (75), Air Strike (76), Gut Bomb (86).
  - All disappear from player inventory upon throwing except **Flashbang (52)** which starts with 2 charges (1 in clip, 1 reserve).
- **Rockets / Missiles:** Rocket Launcher (48) and RPG-7 (47) fire as weapon ammo.
- **Flight & Wall Collision:**
  - Bouncing Grenades (HE, Flashbang, Smoke, Gas): bounce off map walls and obstacle tiles. Reflection reverses axis velocity component, reduces remaining `flydist *= 0.5f`, speed `speed *= 0.65f`, and reverses spin `rot_speed = -rot_speed`. Detonates/activates at end of lifetime.
  - Impact Projectiles (Rocket, RPG, Molotov, Snowball, Gut Bomb): stop and detonate immediately upon hitting the first obstacle (map wall, breakable, building, or player).
    - Molotov: explosion + spawns persistent `CS2D_FIELD_FIRE` (radius 45 px, duration 20s).
    - Snowball: spawns snow puff particles.
    - Gut Bomb: spawns blood particles.
    - Gas Grenade: grenade bounce + spawns persistent `CS2D_FIELD_GAS` (radius 64 px, duration 15s).
    - Smoke Grenade: grenade bounce + spawns persistent `CS2D_FIELD_SMOKE` (radius 75 px, duration 30s) providing smoke cover.
    - Flare: settles on ground, stays for ~60s emitting bright sparkle particles, then disappears silently.
    - Air Strike: stays on ground for ~7s emitting marker smoke puffs, then creates massive cluster explosions.
- **Visuals:** Items without dedicated projectile sprite fallback to their dropped ground sprite (`tex_weapons_d[type]`).
- **Network Opcodes:**
  - Client -> Server: `[27, type u8, x u16, y u16, dir u16 (deg*65536/360), flydist u16]` (10 bytes).
  - Server -> Client: `[27, player u8, type u8, x u16, y u16, dir u16, flydist u16]` (11 bytes).
  - Spawn flying projectile: `[58, player u8, type u8, id u8, x_bias u16, y_bias u16, flydist u16, dir u16]` (12 bytes, signed coord bias 0x8000).
  - Spawn ground projectile: `[52, player u8, type u8, id u16, x u16, y u16, rot u32]` (13 bytes).
  - Move projectile: `[63, id u8, player u8, x_bias u16, y_bias u16]` (7 bytes).
  - Kill / Detonate projectile: `[41, id u8, player u8]` (3 bytes).
  - Silent kill projectile: `[55, id u8, player u8]` (3 bytes).
  - Snapshot section 6: `[252, 7, 6, count u8, count * { id u8, type u8, player u8, x u16, y u16 }]` (7 bytes each).

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
- **Sprite cache ceiling:** `cs2d_gfx_get_sprite` (`cs2d_render.c:1103`) is a fixed 64-entry array
  (`CS2D_MAX_SPRITE_CACHE`, `include/cs2d_render.h:11`) keyed by (path, maskMode), and returns `NULL`
  when full (`:1115`) — the entity is then simply not drawn. Lookup is a linear `_stricmp` scan, so
  the failed-lookup path is at most 64 string compares per entity (cheap); the two numbers above
  therefore need a re-measure with the load time counted explicitly before the freeze is attributed
  to image decode.
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

