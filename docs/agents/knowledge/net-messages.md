Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

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
