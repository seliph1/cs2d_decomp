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
| 22 | `[22,round,n] + n×{pid,x u16,y u16,angle f32,weapon,armor,flag}` (12 bytes each) `+ {money u16, k, k×item}` | Round start. The trailer is the receiving player's inventory |
| 23 | `[23,pid,type,money u16,u8]` | Buy result; type 250 = refused (prison has no buy zone) |
| 24 | `[24,pid,type,clip u16,reserve u16,u8,item id u16,tile x u16,tile y u16,u8]` | Drop; the item lies on the player's tile |
| 25 | `[25,pid,item id u16,type,clip u16,reserve u16,u8]` | Collect |
| 31 | `[31,x u16,y u16,state]` | Entity event |
| 33 | `[33,type,item id u16,tile x u16,tile y u16,clip u16,reserve u16]` | Spawn item |
| 42 | `[42,pid,field,value u16]` | Playerdata. Fields: 0 health, 2 money, 3 armor, 5 weapon given, 6 weapon taken, 8 max health |
| 61 | `[61,pid,type,clip u16,reserve u16]` | Set ammo |
| 226 | `[226,1,u16,len u16,text,u8]` | Message |
| 240 | `[240,pid,mode,len u8,UTF-16LE (2×len bytes)]` | Chat & Server Info (`mode == 50`: `"1"` clear serverinfo, `"2"` open `"<server_name> - Info"` window; `mode == 51`: append UTF-16LE serverinfo line with optional `U+00A9` + 9 digits `RRRGGGBBB` color tag or `U+00BB` gold link) |
| 229 / 247 / 249 | | Pings; reply to 249 with the same 5 bytes |

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
- **Not recovered:** the per-weapon muzzle offset table `PTR_DAT_1409f5d20`. We use 30 px for primary
  weapons and 22 px for pistols (calibrated).
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
- **Turret** (`FUN_14028c630`): idle `rot = rrot + 60 sin(rotvar)`, rotvar += 1 per frame; while `countdown` runs it aims
  at the target (`atan2(tx - 16 - x, y + 16 - ty)`, within 445 x 260 px) and fires while countdown > 10.
- **Respawn modes** (DM, TDM, Construction, Zombies): joining a team leaves the player dead; a left click between the HUD
  bands sends `[39]` unless a living player is within 20 px of the cursor; the server answers `[21]`. While
  `mp_respawndelay` (`[252,6]` byte after sv_gamemode) runs the HUD shows 0767 in (200, 0, 0).
- **Kill:** `[19, victim, killer, weapon, u8, x u16, y u16]` (9 bytes).

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

## Center messages

`Thud_msg.add_center` / `draw_center` (0x14024d230): shown 5 s (`DAT_140a14c74`, default 5), from y = h / 2 - 10 n - 50,
a repeated text restarts its timer.

## Rendering notes

- Roof parapet shadow: none (checked in `FUN_140260330`).
- Shadow occlusion halos merge with max.
- Entities in state ≠ 0 are not drawn.
- The shadow cache is rebuilt when `map->entity_solid_version` changes.
