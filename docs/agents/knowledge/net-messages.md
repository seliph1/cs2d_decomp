Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Network: client → server (proxy)

| Message | Bytes | Notes |
|---|---|---|
| Position | `[10 run / 11 walk][x u16][y u16]` | Unreliable, sent on every frame where the integer position changed |
| Move + rot + aim | `[71 run / 72 walk][x u16][y u16][rot u16][aim/2 u8]` | 7 bytes after the opcode (ded `FUN_1401d3fb0` / `FUN_1401d4430`; proxy `47 10 04 6b 05 c1 d6 87`), sent while moving and turning. Misreading the length makes the leftover bytes run as opcodes |
| Rotation | `[70][angle u16 = deg·65536/360][aim distance/2 u8]` | Sent on change |
| Fire | `[7][seed u8]` | Unreliable. Melee always 0; firearms 0, 2, 3, 4, … (global counter across weapons). The ded stores it at player+0x58, reading 0 as 1 (ded `FUN_1401c8930`) |
| Select weapon | `[9][type][0]` | |
| Reload | `[16][1]` | |
| Drop | `[24][type][clip u16][reserve u16][0]` | |
| Kill dropped weapon | `[40][id u16]` | Reliable (`Tudp_con.Createpacket(1,0)`). `Titem.fadeout` (0x14004cd20) removes the item locally and sends `wbyte(0x28) + wshort(id)` (0x14004cec2) for every connected player. The client only does this while it runs the server; a network client waits for the server's own `[40]` (see the server → client table) |
| Buy | `[23][type][0]` | |
| Use | `[26][mode u8][target i32][x u16][y u16]` | Always sent on E (FUN_14014f820). Mode 100 = Trigger_Use with its tile; 1/2 = hostage use/release (target = hostage id), 3 = defuse (target = defuse kit); nothing found = mode 0 with zeros, still reported to the Lua `use` hook (verified, `use_probe.lua`). Target ray at 0, 16 and 32 px along the aim |
| Team | `[20][team][look]` | `team`: 0 Spectator (`look = 5`), 1 T, 2 CT; `look`: `0..3` class skin or `4` Auto-Select skin |
| Projectile | `[27][type][x u16][y u16][dir u16 = deg·65536/360][flydist u16]` | Sent on throw/fire; flydist clamped to 385 px (0x181) for throwables |
| Spectate | `[67][player]` | Reliable. Followed player, 0 = free look; only sent while dead or Spectator |
| Process | `[30][mode]` | 1 plant start, 2 plant abort, 4 defuse abort (`FUN_1401cb510`, `FUN_1401f5f60`) |
| Spec position | `[32][x+32768 u16][y+32768 u16]` | Unreliable, once per second while dead or Spectator: the camera center |
| Flashlight | `[57][state]` | Reliable (proxy `flashlight_probe.lua`). Bind `flashlight` (F): `FUN_140167440(id, 1 - flashlight, 1)`, sent only while alive in a team, `mp_flashlight` 1 and not a T in Zombies; the flag changes on the server's echo |

## Network: server → client (proxy, Lua)

| Op | Layout | Meaning |
|---|---|---|
| 7 | `[7,pid,seed]` | Another player fired |
| 9 | `[9,pid,type,mode]` | Weapon held (e.g. Lua `setweapon`) |
| 15 | `[15,pid,x u16,y u16]` | setpos / teleport. The local player must apply it, or it keeps sending stale positions (rubberbanding) |
| 16 | `[16,pid,mode]` | Reload: 1 started, 2 clip refilled, 4 one shell (M3 / XM1014), 3 / 100 aborted (FUN_140214780; mode 4 from the host branch, not captured). Client fallback `[16][101]` after ping x 1.5 ms without a 4 |
| 17 | `[17,victim,attacker,hp,armor]` | Damage |
| 20 | `[20,pid,team,look]` | Team and skin broadcast (`team == 0` moves player to Spectator) |
| 21 | `[21,pid,x u16,y u16,weapon,armor,money u16,n,n×item]` | Spawn, with the whole inventory. prison_funst3r gives only the knife (50) |
| 22 | `[22,reason,n] + n×{pid,x u16,y u16,angle f32,weapon,armor,special}` (12 bytes each) `+ {money u16, k, k×item}` | Round start. The trailer is the receiving player's inventory. `reason` = round end reason (see "Scoreboard") |
| 23 | `[23,pid,type,money u16,u8]` | Buy result; type 250 = refused (prison has no buy zone) |
| 24 | `[24,pid,type,clip u16,reserve u16,u8,item id u16,tile x u16,tile y u16,u8]` | Drop; the item lies on the player's tile |
| 25 | `[25,pid,item id u16,type,clip u16,reserve u16,u8]` | Collect |
| 26 | `[26,pid,mode,data i32,x u16,y u16]` | Use echo (hostage 1/2, defuse 3); see `hostages-bomb.md` |
| 29 | `[29,hostage,kind,a u16,b u16]` | Hostage: 0 target tile, 2 rescued, 3 hurt, 4 killed (`FUN_1402d93f0`) |
| 30 | `[30,pid,mode]` (+ `x u16,y u16,item u16,timer u8` for 3) | Process: 1/2 plant start/abort, 3 planted, 4 defuse abort, 6 explosion |
| 27 | `[27,pid,type,x u16,y u16,dir u16,flydist u16]` | Projectile broadcast (11 bytes, REA `0x1402d3340`) |
| 31 | `[31,x u16,y u16,state]` | Entity event |
| 33 | `[33,type,item id u16,tile x u16,tile y u16,clip u16,reserve u16]` | Spawn item |
| 40 | `[40, id u16]` | The server says a ground item is gone (its own drop timer, see "Ground item lifetime"). Our client removes it; `remove_ground_item` |
| 41 | `[41,id,pid]` | Kill/detonate projectile (3 bytes, REA `0x1402daf80`) |
| 42 | `[42,pid,field,value u16]` | Playerdata. Fields: 0 health, 1 score, 2 money, 3 armor, 4 deaths, 5 weapon given, 6 weapon taken, 8 max health, 9 assists, 10 MVPs (REA `FUN_1401cc9d0`) |
| 64 | `[64,t u16,ct u16]` | Team scores (`setteamscores`, REA `FUN_1402de440`) |
| 52 | `[52,pid,type,id u16,x u16,y u16,rot u32]` | Spawn ground projectile (13 bytes, REA `0x140060ee0`) |
| 55 | `[55,id,pid]` | Silent kill projectile (3 bytes, REA `0x1402dd7a0`) |
| 57 | `[57,pid,state]` | Flashlight switched (3 bytes, recv `FUN_1402dda70` -> `FUN_140167440(pid, state, 0)`): sets Tpl.flashlight (+0x300), plays `sfx/player/flashlight.wav` at 0.5. Also special byte bit 32 (`setspecialbyte`) |
| 58 | `[58,pid,type,id,x+32768 u16,y+32768 u16,flydist u16,dir u16]` | Spawn flying projectile (12 bytes, REA `0x1400a91d0`) |
| 61 | `[61,pid,type,clip u16,reserve u16]` | Set ammo |
| 63 | `[63,id,pid,x+32768 u16,y+32768 u16]` | Move projectile (7 bytes, REA `0x14007dce0`) |
| 226 | `[226,1,u16,len u16,text,u8]` | Message |
| 240 | `[240,pid,mode,len u8,UTF-16LE (2×len bytes)]` | Chat & Server Info (`mode == 50`: `"1"` clear serverinfo, `"2"` open `"<server_name> - Info"` window; `mode == 51`: append UTF-16LE serverinfo line with optional `U+00A9` + 9 digits `RRRGGGBBB` color tag or `U+00BB` gold link) |
| 247 | `[247,n,ticks,u16,u16,n×{pid,ping u16}]` | Ping list (REA `FUN_140060f80`, proxy): server ticks, ping 0xfffe = bot, 0xffff = server |
| 229 | `[229, srv_ts i32]` | Clock Ping / Anti-speedhack (REA `0x1401d7690`, `0x1402e2ed0`, proxy): used when `sv_antiaccel > 0` (default); client replies unreliable `[229, srv_ts i32, cli_ts i32]` (9 bytes); server calculates RTT latency (`now - srv_ts`) into `player->ping` (`Tpl +0xd0`) and tracks `clock_diff` / `clock_ratio` |
| 249 | `[249, srv_ts i32]` | Normal In-Game Ping (REA `0x1401d9f10`, `0x1402e6080`): used when `sv_antiaccel == 0`; client replies unreliable `[249, srv_ts i32]` (5 bytes) |
| 65 | `[65, x u16 + 32768, y u16 + 32768, len u8, path]` | Positional sound (REA `FUN_1402de5f0` -> `FUN_1401e3890(x, y, path, 0)`, proxy 157.90.170.75:10003): Lua servers send `player/build.wav` right before a `spawnobject` `[49]` in the same datagram. Not handling it dropped the `[49]` |

## Ground item lifetime (the drop timer behind `[40]`)

Source: the tail block both `Titem.draw` overloads share (0x140147b80 and 0x1401483a0).

- The step is `droptimer = droptimer + 1` and the fade at `DAT_140a14d94 <= droptimer` (`fadeout(id)`), but the
  whole statement is gated by `DAT_140ddf540 != 0 && DAT_140ddf400 != 0 && (0x140a14e48[..] & 0xfb) != 0 &&
  dropped == 1 && droptimer != 10000`, where:
  - `DAT_140ddf540` is the main loop's 1000 ms timer (0x140236801, 140230000.c:6800): 1 for the single frame that
    closes a second, 0 for the rest, so the timer counts **seconds**;
  - `DAT_140ddf400` is 1 only while this instance runs the server (create server 1401e0000.c:1499, cleared on join
    1401d0000.c:128 / 1402c0000.c:6747), so a network client never expires a ground item itself;
  - the third gate indexes a settings array (0x140a14e48); the setting is not identified yet (assumed on - only the
    host can reach it).
- `DAT_140a14d94` = 15, so a dropped item lives **15 s** of the server's clock; a Lua `fadeout` hook returning 1
  pins `droptimer` at 10000 and stops the timer. Map / server spawned (`[33]`) items have `dropped = 0` and never
  age.
- `Titem.fadeout` (0x14004cd20) removes the item and broadcasts `[40, id u16]` to every connected player, which is
  how a client learns an item is gone. Ported as `cs2d_ground_item_step` + the `[40]` receive branch in
  `src/cs2d_client.c`; the user-visible bug of the old per-frame port (a dropped weapon faded in ~90 ms at 167 FPS)
  is locked by `test_random_and_dropwep`.

