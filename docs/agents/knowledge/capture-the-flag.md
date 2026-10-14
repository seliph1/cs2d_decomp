Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

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
