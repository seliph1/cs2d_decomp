Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Env_Object and tile blending

Source: dump `Te.draw` (0x140271ee0), map draw `FUN_140281fd0`, editor list at 0x1402a8707, image loads at
0x140221404; `Ttileblend` (`updatetile` 0x1400ae560, `FUN_14005e2e0`), floor draw 0x140270a40, `gfx/blendmap.bmp`;
screenshots of the original (fun_roleplay). Code: `render_env_object_*`, `tileblend_*` in `src/cs2d_render.c`.

- **Draw order** (`FUN_140281fd0`, also the editor loop `FUN_1402bf960`): floor, `Te.draw(0)`, items, `Te.draw(1)`,
  players, `Te.draw(2)` (overhead).
- **Env_Object** ints[0]: 0 Palm, 1 Tree, 2 Alarm, 3 Sink, 4 Toilet, 5 Street Light, 6 Street Light (flicker); ints[1..3]
  color, ints[4] rotation, strs[0] size, strs[1] alpha, both read with bbStringToFloat (empty = 0; fun_roleplay's street
  lights have alpha "0", so only the lamp shows). Only state 0 is drawn (prison_funst3r's alarms start in state 1).
  - `incbindata/objects.bmp`: 10 frames of 32x32, midhandle: 0 sink, 1 toilet, 2 street lamp.
  - Layer 1: Sink / Toilet, frame type - 3, rotation (rot / 10) * 10, scale size, color, alpha, ALPHABLEND.
  - Layer 2, Palm: 8 `palmleave.png` (handle 15, 2) at scale size x 0.8, rotation k * 45 + rot + 3 sway(k * 20 + age).
  - Layer 2, Tree: 7 `treeleaves.png` (midhandle) at size x 30 px, angle k * 51 + rot + 2 sway(age + k * 50), rotation
    rot + 5 sway; then one at scale size x 1.2 on the center, rotation rot + 5 sin(age). sway = sin when k % 3 == 0,
    else cos. `Te.age` += 2 x time factor per frame (only while on screen).
  - Leaf shadows (palm and the 7 tree clumps) with itemshadows: black at shadows % x alpha, + 4 px along
    (cos, sin)(ShadowRotation - 180).
  - Layer 2, Alarm: `gfx/sprites/flare1.bmp` LIGHTBLEND, rotation rot, scale size, alpha |sin(ms / 3 degrees)| x alpha.
  - Layer 2, Street Light: `gfx/sprites/flare3.bmp` LIGHTBLEND, rotation ms / 150 + rot, scale size, alpha; type 6: when
    Rand(1, 5) == 1, alpha x Rnd(0, 0.8). Then objects frame 2, white, scale 1, rotation (rot / 10) * 10.
- **Tile blending:** a tile with `(mod & 0xc0) == 0x40` stores a blend frame (one byte after the modifier). The neighbour
  is taken in direction frame & 7 (0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left;
  outside the map = tile 0). The same tile as the neighbour clears the 0x40 flag. Otherwise the image is
  `own * (1 - f) + neighbour * f` per channel (alpha included), f = red of `gfx/blendmap.bmp` (256x160, 40 frames of
  32x32) frame `frame` at (x, y) / (tile size / 32), truncated to int. It is drawn with the tile's rotation and brightness.
  The blendmap is white on the side the direction points to (all 40 frames: >= 200 there, <= 55 opposite). The images are built at map
  load ("Generating Tile Blending") when `DAT_140ddf17f` is 1 (probably `tileanimations`, from the order of the
  settings at 0x1401b0989; not confirmed). de_aztec has 198 blended tiles, fun_roleplay 2700.
