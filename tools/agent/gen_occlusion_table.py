"""Builds the occlusion table of TSmartLighting.DrawTilesFloor (CS2D.exe 0x14010abe0) by running it in Unicorn.

usage: python -I tools/agent/gen_occlusion_table.py CS2D.exe > table.txt

DrawTilesFloor draws, for every non-wall tile on screen, frames of gfx_shadowmap.png ('s', 6 frames) and
gfx_oclussionmap.png ('o', 11 frames), both midhandled, chosen from the walls among the 8 neighbours. This runs the
original on a 5x5 map for the 256 neighbour patterns (bit i = neighbour NB[i]) and prints, per pattern, the draws of
the center tile as (image, frame, rotation, scale x, scale y). The rows are pasted into src/cs2d_light.c.
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu_light as EL
from emu_cs2d import Cs2dEmu
emu = Cs2dEmu(sys.argv[1] if len(sys.argv) > 1 else 'CS2D.exe')
for addr, name in EL.IMAGES.items():
    img = emu.alloc(0x40); emu.set_ptr(addr, img); emu.images[img] = name
G = EL.G
emu.set_f32(G['CameraX'], 0.0); emu.set_f32(G['CameraY'], 0.0)
emu.set_i32(G['OCCLUSION'], 1); emu.set_i32(G['TilesX'], 27)
emu.set_i32(G['MapWidth'], 5); emu.set_i32(G['MapHeight'], 5)
arr = emu.array_2d('i', 5, 5, lambda x, y: 0)
emu.set_ptr(G['TileMapMode'], arr)
off = 0x30
NB = [(-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)]
for mask in range(256):
    walls = {(2+dx, 2+dy) for i,(dx,dy) in enumerate(NB) if mask >> i & 1}
    for x in range(5):
        for y in range(5):
            emu.set_i32(arr + off + (x*5+y)*4, 2 if (x,y) in walls else 0)
    log = emu.call(EL.F_TILES_FLOOR, [])
    rot = 0.0; sx = sy = 1.0; draws = []
    for it in log:
        if it[0] == 'SetRotation': rot = it[1]
        elif it[0] == 'SetScale': sx, sy = it[1], it[2]
        elif it[0] == 'SetTransform': rot, sx, sy = it[1], it[2], it[3]
        elif it[0] == 'DrawImage':
            if abs(it[2]-80) < 0.01 and abs(it[3]-80) < 0.01:
                draws.append((it[1][0], it[4], int(rot), int(sx), int(sy)))
        elif it[0] != 'SetBlend': draws.append(('?',)+tuple(it))
    print(mask, draws, flush=True)
