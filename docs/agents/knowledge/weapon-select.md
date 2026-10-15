Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

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
  in (100,0,0) when empty; column 15 wide. Infinite ammo shows "clip | INF" (0762); slot 6 without a count shows
  "0 x - Remote" (0763).
- **Bindings** (`sys/controls.cfg`, `bind "<key>" "<command>"`): `mwheelup` invprev, `mwheeldown` invnext, `0`..`9`
  slot10 / slot1..slot9.
- **Our model** (2026-10-14): `cs2d_player_t.owned[id]` is the item list (`Titem.list`); `inventory[slot]` is only the
  slot's main item (last given / held). The HUD lists `owned` by slot and order; "n x" is `clip_ammo[id]`.
- **Counts** (`Titem` +0x14 ammo, +0x18 ammoin; "n x" = ammoin = our clip_ammo): `Titem.give(pid, type, ammo,
  ammoin)` reuses the player's item of that type; -1 = defaults (ammoin 0x140a017a8: Flashbang 2, Snowball 10, Mine /
  Laser Mine 3, Medikit 5, other grenades 1; ammo 0x140a017b0: 0 for all of them). `[23]` buy of 0x33..0x36 / 0x59
  (1402008xx): owned -> ammoin + 1, else give(0, 1). Pickup `[25]` (client path of FUN_14020ce60): only HUD
  slot 4 already owned: ammoin + n > max -> ammoin = max, and if n - added > 0 `Titem.freedropped` is skipped (the
  ground item stays, its count unchanged on the client); else ammoin += n. Other items: give, ammoin / ammo = the
  packet's values, the ground item freed. (The host path, DAT_140ddf400, also lowers the ground item's ammoin.) Proxy (de_dust, dedicated): three `[23,52,0]` -> two `[23,1,52,money]` answers, no ammo
  message, the third ignored; our client then shows 2 x.

## Ground item hover text (FUN_14029ffb0)
- mp_hovertext bit 16 (FUN_1402a12d0, always checked last): items with id > 0, visible, tile center within 16 px of the
  cursor; one gui_c box at cursor y + 25 - 12, line height max(text + 2, 16), lines sorted by key ascending.
- Key = min(price * 0.005, 99) + offset: clip < 1 → +100 green (slot 0) / +0 yellow; reserve < 1 → "name (clip)"
  +300 orange (255,150,30) slot 4 / +200 yellow; guns "name (clip|reserve)" +500 (230,200,10) slot 2 / +600 yellow,
  "name (empty)" red (0799) at 0 ammo, "name (clip|INF)" (0762) with mp_infammo (0x140a14e38[1]).
- 63: "Bomb" (0798) blinking red/yellow on sin(ms/2), key 1000; 55 red 990; 70 red / 71 blue (50,150,255) 980.
- g_lang offsets: g_lang[k].scales + data_start + d → entry 777 + ((k - 0xac) * 36 + d + 4) / 8.
- Main HUD under mp_infammo (FUN_140295190 ~0x14029575x): clip at w - 6.5 u (normal 6.25), hud_nums '|' at w - 3.25 u,
  then hud_symbols frame 11 (infinity) at w - 2.25 u instead of the reserve. Weapon select slot 1/2: "clip | INF" (0762).
- Main HUD clock (0x1402955b8) is skipped entirely when mp_roundtime (0x140a14ea0[1], [252,6] byte after the two u16
  scores) is 100 = infinite rounds; the spectator HUD draws frame 11 instead.
- Round clock per game mode: table 0x1409f77e8 indexed by sv_gamemode (0x140a14e48[1]), filled at 0x140318264.. as
  1, 0, 0, 0, 1: only Standard and Zombies draw the clock (main and spectator HUD); DM / TDM / Construction don't.
- Pickup auto switch: FUN_14020ce60 tail calls FUN_1401caca0(id, type, 0xff, 1) (hold + send [9, type, mode]) for the local player when DAT_140ddf196 (Automatic Weapon Switch option, assumed from the options menu load at 1401b0000) is on and the type has a HUD slot (0x140a017b8). Bots: bestweapon instead.
