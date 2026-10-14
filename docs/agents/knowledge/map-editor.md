Part of the shared knowledge; index and source conventions in `docs/agents/KNOWLEDGE.md`.

## Map editor

Source: REA `FUN_1401a85f0` (GUI builder), `FUN_1402bf960` (editor loop), `FUN_14019cce0` (mode switch),
`TileProp.Get 0x140010450`, the user's screenshots of the original. Code: `src/cs2d_editor.c` (model),
`src/cs2d_scene_editor.c` (scene).

- **Tile modifier byte (`TileProp.Get`):** bits 0-1 rotation (1 = 90, 2 = 180, 3 = 270), bits 2-5 brightness level b
  (color x b * 0.1; b = 0 means 100 %), `(mod & 0xc0) == 0x80` color from the per-tile `Tmap_d` (r, g, b, overlay),
  `0x40` blend frame. Our render used to darken by 0.15 per level and draw `mod - 128` as an overlay: both wrong.
  The map file stores the extra bytes (`0x40`: frame; `0x80`: r, g, b, overlay); `cs2d_map_t.tile_data` keeps them,
  and load -> save is byte-identical on maps whose header already says "heights 2" (fy_shipment: 700 colored tiles).
- **Brush (tile list `+0x58` tile, `+0x5c` modifier, `+0x60` color):** a click in the list selects the tile and
  clears the modifier. Rotation button: rotation + 1 (icon 15 + rotation). "BR: n": level + 1, 9 -> 0, label level * 10
  or 100. Color "Okay": white clears `0x80`, else `(mod & 0x3f) | 0x80`.
- **Painting:** one tile (`FUN_1402bd6d0`) copies tile + modifier (+ color when `mod >= 0x40`); fill (`FUN_1402bdcc0`)
  is a 4-neighbour flood fill over the same tile and modifier; every change goes to `Teditor_actionlist` (undo).
- **Sidebar** (window "sidebar" at (0, -32), 152 wide): icons 20x20 from `gfx/gui_icons.bmp` (24 frames of 16x16):
  - row 1 at y 7: New 1 (x 5), Load 2 (27), Save 3 (49), Map Properties 4 (83), Display Settings 10 (105), Quit 0 (127);
  - row 2 at y 32: Tiles 8, Entities 9, Tools 4, then (tiles mode only) Tile 5 (83), Rect 6 (105), Fill 7 (127);
  - tooltips are sys/language 0421..0432;
  - tile list (5, 58): 5 columns of 25 px (32 px tile at scale 0.78125), (h - 110) / 25 rows, tiles at 200/255 gray
    (255 hovered), selection = additive yellow |sin| * 0.5, tooltip "Tile #n, <mode name>";
  - bottom: "Tile Properties" (5, h - 47) 142x17, rotation (5, h - 25), "BR" (27, h - 25) 60 wide, color box
    (90, h - 25) 57x20;
  - entities mode: filter combo (5, 58) 142 wide ("All|Info|Environment|Generate|Function|Trigger", 0433), list
    (5, 83) 142 x (h - 88).
- **Measured colors (screenshots):** sidebar 54; icon button inside 27, frame 150; grid (255,255,0) at 25 %; hovered
  tile frame (191,191,0) from 2 px outside to 1 px inside with a (255,255,0) line on the edge; status "Tile Position
  x|y - Tile #n" white at 70 % at (156, h - 18); entity type names in the type color at 70 %.
- **Windows:**
  - Tile Properties (155, 5) 480x470: "<" (16,32) 20x32, tile image (41,32), ">" (78,32), label (108,38), 16 options
    from (16, 74) every 20 px, "3D Height (pixels):" (314,160) + box (314,180) 150 wide, checks 3D Flip Y / 3D Mirror /
    Non-Rotated. at (314, 220 / 240 / 260), "Save as default Properties" (16,409) 250 wide, "Load default
    Properties" (16,434), Close (364,434).
  - Color (95, 144) 451x193: preview (16,32) 45x60, sliders (165, 32 / 52 / 72) 270 wide, 16 colors 48x20 at
    (16 + 53 i, 102 / 127), Okay (230,157), Cancel (335,157).
  - Load / Save Map (155, 5) (w - 160) x (h - 10); Display Settings (155, 5) 300x220; Map Settings (155, 5) 480x300.
- **Tile properties:** option -> mode `1,2,3,4,0,10,11,12,13,14,15,16,50,51,52,53` (`FUN_1402b85f0`; "Deadly -
  Explosion" is 51 and "Toxic" 52, unlike the map format text). 3D modifier: Flip Y +1, Mirror +2, Non-Rotated +4.
  Names `FUN_14019ba30` (5 "Floor Wall", 6 "Obstacle Wall").
- **`gfx/tiles/<tileset>.inf`:** the first line is the version:
  - "...Counter-Strike 2D Tileset Informations": one text line per tile;
  - "V 2.0": a mode byte per tile; "V 3.0": mode u8 + height i32;
  - "Unreal Software's CS2D Tileset Informations V 4.0": mode u8 + height u16 + 3D modifier u8 (what "Save as default
    Properties" writes, `FUN_1402b7860`).
  Without heights: walls 32, obstacles 16, else 0. Unknown modes are skipped.
- **Entities:** tables filled by the init code at `0x140318856` (read with `tools/agent/initarrays.py`):
  - full names `0x1409ecf78` (equal to `cs2d_entity_type_name`), short labels `0x1409e9450` (T, CT, Spr, Brkbl, ...),
    zone radius `0x1409f75f8` (Info_T / Info_CT / Info_BombSpot 2, Info_RescuePoint 1);
  - colors `0x1409e9db0` (loop at `0x140318ee7`): Info_T (255,0,0), Info_CT (0,0,255), Env 21..35 (128,255,0),
    Gen 50..53 (255,200,0), Func 70..73 (255,128,255), Trigger 90..96 (100,200,255), other Info (255,255,0);
  - `Te.drawineditor 0x14026a260`: `incbindata/shadow.bmp` at scale 1.2 in black, gui_icons frame 9 in the type
    color (additive), a pulsing shadow at scale 0.8, the short label in the small font at (+19, +21), zones as an
    additive yellow 10 % square; hovering shows the full name.
