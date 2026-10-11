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
  `[252,10, 0x1c,0x02, size u16 (=4+zlib len), raw size u32 (=1024), zlib(32x32 spray)]`, then `[67,1]`.
  **The server runs its `join` (Lua hook, player list, announcement to the others) only after this.**
  Our client sends a blank spray.
- **Leave:** the client sends `[253,0]`. The other clients receive `[253,pid,reason]`; the server then
  drops what the player carried (`[24]`) and prints a `[226]` message.
- **Others joining:** `[248,pid,name(u8 len+text),18 bytes]`, followed by the spray relay
  `[28,pid,2,size u16,size bytes]`.

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

## Server Info and Team Selection UI

Source: reference screenshots from `CS2D.exe` + dedicated `opcode 240` (`FUN_1400d5190`) and `opcode 20`.

- **Window frame:** both `"<server_name> - Info"` and `"Select Team"` are `600×440` centered at `(125, 20)` on `850×480` with a 1 px horizontal divider below the title bar.
- **`"<server_name> - Info"`:** `553×374` scrollable text box (`ServerInfoBox`) + `15×374` scrollbar on the right (`ServerInfoScrollbar`) + `568×20` `Close` button at the bottom (`BtnServerInfoClose`). When joining a server, `"<server_name> - Info"` renders first and closing it reveals `"Select Team"`.
- **`"Select Team"` (`M` key):** `240×25` option buttons starting at `(205, 120)` with `5 px` vertical gap:
  - Main page: `1 Terrorists (N)` and `2 Counter-Terrorists (N)` open skin subcategories; after a `25 px` empty slot, `5 Auto-Select` and `6 Spectator` select immediately (no subcategory); `0 Close` at `(205, 420)`.
  - Subcategories (`"Select Team - Terrorists"` / `"Select Team - Counter-Terrorists"`): `1..4` skins (`Phoenix Connexion`, `L337 Krew`, `Arctic Avengers`, `Guerilla Warfare` / `Seal Team 6 (DEVGRU)`, `German GSG-9`, `UK SAS`, `French GIGN`) with per-skin player counts `(N)`, `25 px` empty slot, `5 Auto-Select`, `Back` (`y = 390`), `0 Close` (`y = 420`).

## Rendering notes

- Roof parapet shadow: none (checked in `FUN_140260330`).
- Shadow occlusion halos merge with max.
- Entities in state ≠ 0 are not drawn.
- The shadow cache is rebuilt when `map->entity_solid_version` changes.
