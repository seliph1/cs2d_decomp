# settile (Tcustomtile)

Code: `src/cs2d_customtile.c`. Test: `test_settile`. Probe: `tools/agent/lua/settile_probe.lua`.

## Messages (proxy, dedicated 2026-10-15)

- **[62]** `x u16, y u16, tile u8, rot, r, g, b, overlay, blend` (12 bytes with the opcode). The server always sends
  all of it (FUN_1401957f0); the client reads the 6 last bytes only when at least 6 bytes remain after the tile,
  else rot 0, rgb 255, overlay 0, blend 0 (FUN_140195b80). Ignored when the client is the server (DAT_140ddf400).
- **[252,7,9]** (FUN_1402ce7c0 case 6): Tcustomtile.list and .batch cleared, `count u8`, then count x 11 bytes
  `{x u16, y u16, tile, rot, r, g, b, overlay, blend}` through addcheck, one endbatch. Entries come newest first.
  The dedicated sends a **second `[252,7,9]` with count 0**, so the client's list is empty after the join (tiles keep
  their change). A **cut** entry (fewer than 11 bytes left) prints `ERROR: failed to load custom tile data` (red), keeps
  the entries read so far (its endbatch runs first) and takes the shared join abort `LAB_1402cf3ee`: joinstep 0, world
  teardown `FUN_1401d0160` + `Tgui_win::closeall`, then `Tgui_joinmen.errorprompt(0xc9)`. `0xc9` = **201**, "Connection
  problems! Failed to receive all server data." (lang 0396). No `[253]` then (the error window's Okay/Cancel sends `[253,9]`).
- Command (FUN_140195d00): `settile x y tile [rot] [r] [g] [b] [overlay] [blenddir] [blendtype]`, defaults 0 / 255 /
  255 / 255 / 0. blend = dir + (type - 1) * 8 with dir top 0, topright 1, right 2, bottomright 3, bottom 4,
  bottomleft 5, left 6, topleft 7 (case-sensitive, unknown = 0); 0 when either is empty or type < 1.
  So `top 1` gives blend 0 (no blending). Seen: `left 2` = 14, `topright 1` = 1, `bottom 3` = 20, `bottomleft 5` = 37.

## Apply (Tcustomtile.addcheck 0x1400aeaf0 / add 0x1400ae820)

- Ignored when tile > tilemax or x / y outside the map.
- modifier = rot (1, 2, 3, else 0) + 0x40 if blend != 0 + 0x80 if (r & g & b) != 255. No brightness bits.
- Tmap_d (fields frame +8, r +9, g +10, b +11, overlay +0xc) = {blend, r, g, b, overlay}, always created.
- Draw: color only for (mod & 0xc0) == 0x80 (TileProp.Get), blending only for == 0x40 (Ttileblend.updatetile);
  with 0xc0 neither (rotation still applies). Overlay is never drawn by CS2D.exe.
- No entry yet: if tile, modifier and an existing map Tmap_d all match, nothing happens; else `add` stores the old tile,
  modifier and Tmap_d (map files create a Tmap_d only for 0x40 / 0x80). With an entry: overwrite; when tile and
  modifier equal the old ones and the data equals the old Tmap_d (or there was none and the data is default), the old
  Tmap_d comes back and the entry goes. Lua `tile(x, y, "hascustomframe")` / `"originalframe"` read the server's list.
- endbatch (0x1401443e0): cs2d_update_mapdata(1, 1, 1, minx - 1, miny - 1, maxx + 1, maxy + 1): cell props
  (collision), entity / building overrides, light engine SetTile, Ttileblend.updatetile of the region.
- Map load clears the list (0x1401d0000 area); Tcustomtile.clear (restores old values) has no caller in CS2D.exe.
