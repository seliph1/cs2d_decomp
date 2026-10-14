Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

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
