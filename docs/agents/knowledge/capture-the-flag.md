Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Capture the flag

Source: `Te.draw` case 0xf (0x140274e28), `FUN_140271b60`, `Tpl.draw` (`self->flag`), `Titem.give`.

- The flag at home is drawn by its `Info_CTF_Flag` entity (ints[0] 0 = T / red item 70, 1 = CT / blue item 71) while the
  entity state is 0: the item image (midhandle) at (x * 32 + 26, y * 32), waving: the pole through a 3 px viewport, then
  nine 4 px viewports every 2 px redraw it shifted by 2 sin((ms >> 2) + 30 i) - 2 sin((ms >> 2) + 30) in gray
  |sin| * 55 + 200 at alpha 0.8. The base always gets an additive flare1 in the team color (rotation ms / 15).
- Protocol (proxy, `tools/agent/lua/ctf_probe.lua`): taking a flag is `[25]` collect of type 70 / 71 with an item id the
  clients never got (the flag at home is only the entity), followed by `[31]` entity event (base x, y, state 1). Dying
  with it sends `[24]` drop of type 70 / 71 (a normal ground item). A return is `[31]` with state 0.
- Titem.give (FUN_14020ce60) case 0x46 / 0x47: the enemy team (70: team > 1, 71: team 1) gets `Tpl.flag = 1` (no
  inventory item), center message sys/language 0707 / 0708 (red 255,25,0 / blue 50,150,255), and 0709 in green to the
  carrier. Drop (FUN_1401f8350) clears it and shows 0704 / 0705. Tee.run on a CTF base going to state 0 shows
  "Red / Blue Flag returned". Captures clear every carrier (FUN_140199580).
- Draw order (our choice, screenshot comparison only): the waving base flag and the carried flag are drawn after the wall
  tiles so walls do not hide them.
- A taken flag (Tpl.flag): drawn 7 px behind along the aim, rotation weapon angle + 160,
  scale 0.8; a Terrorist carries the blue image, anybody else the red one. Dropped flags are ground items.
- ctf_cs2dfortress: CT flag (20, 17), T flag (20, 57). The remote server's Lua walls the CT flag in with 56 Wall III
  (fixture `tests/data/dynobj_snapshot_ctf_cs2dfortress.hex`).
