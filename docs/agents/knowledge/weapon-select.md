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
  in (100,0,0) when empty; column 15 wide. Infinite ammo shows "clip | INF" (0762).
- **Bindings** (`sys/controls.cfg`, `bind "<key>" "<command>"`): `mwheelup` invprev, `mwheeldown` invnext, `0`..`9`
  slot10 / slot1..slot9.
