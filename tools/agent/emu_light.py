"""Runs the ORIGINAL light engine (TSmartLighting, CS2D.exe) on a scene and prints its draw calls.

usage: python -I tools/agent/emu_light.py CS2D.exe <scene.txt> <part> [cam_x cam_y]
  part: floor  - TSmartLighting.DrawLayer1 -> DrawTilesFloor (occlusion pass, drawn after the items)
        day    - TSmartLighting.GenerateDayBuffer
        main   - TSmartLighting.Update: the light buffers and GenerateMainBuffer (the whole light buffer)
        color  - TLightSource.DrawLightsColorList (additive pass after the players)

The scene file is written by our client (`lightdump` console command, cs2d_light_dump_scene): it holds the
TSmartLighting state (tile maps, rooms, lights, stripes, settings). The output uses the same line format as
our trace (cs2d_light_set_trace), so tools/agent/light_diff.py can compare both.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I does not add the script folder
from emu_cs2d import Cs2dEmu, NULL_OBJ, HOOKS  # noqa: E402

# TSmartLighting globals (bbscope TSmartLighting)
G = dict(Initialized=0x140DDEEC0, CameraX=0x140DDEEA4, CameraY=0x140DDEEA0, SETTING_MODE=0x140DDEE94,
         GLOBALBRIGHTNESS=0x140DDEE70, DAYSHADOWBRIGHTNESS=0x140DDEE80, DAYSHADOWLIGHTBRIGHTNESS=0x140DDEE30,
         NIGHT_R=0x140DDEE54, NIGHT_G=0x140DDEE50, NIGHT_B=0x140DDEE4C, PENUMBRA=0x140DDEE98,
         OCCLUSION=0x140DDEE34, SHADOWSPRITE=0x140DDEE90, LIGHTFIXES=0x140DDEE8C, DAYSHADOWS=0x140DDEE38,
         SUNANGLE=0x140DDEE48, PLAYERSHADOWS=0x140DDEE58, MainBuffer=0x140DDEEC8, SCALE_NIGHTBUFFER=0x140DDEED4,
         SCALE_LIGHTS=0x140DDEE9C, MapWidth=0x140DDEEBC, MapHeight=0x140DDEEB8, ShadowRotation=0x140DDEE68,
         ShadowRotationSet=0x140DDEE6C, TileMap=0x140DDEEB0, TileMapMode=0x140DDEEA8, TileSetHeight=0x140DDEE78,
         TileSetCount=0x140DDEE5C, TilesX=0x140DDEE3C, TilesY=0x140DDEE28, ScreenWidth=0x140DDEED8,
         ScreenHeight=0x140DDEED0, WallSize=0x140DDEE40, DayToningColor=0x140DDEE60)
LISTS = dict(TLightSource=0x140DDEDB0, TLightingRoom=0x140DDED90, TLightingStripe=0x140DDED80,
             EntityShadow=0x140DDE118, Tdo=0x140DDED68, ObstacleShadow=0x140DDE170, ObstacleShadow2=0x140DDE178,
             LightFBOManager=0x140DDEDA0, Tpro=0x140DDECB0)
ROOM_GRID = 0x140DDED88
SPRITE_COUNT = 0x140DDE1C8
SPRITE_FLOOR_COUNT = 0x140DDE168
CLASS = dict(TLightingRoom=0x140A09460, TLightSource=0x140A0A2A0, TLightingStripe=0x140A09040)
IMAGES = {0x140A0DF98: 'light', 0x140A0DF90: 'flashlight', 0x140A0DF88: 'penumbra', 0x140A0DF80: 'roommap',
          0x140A0DF78: 'roommap_invert', 0x140A0DF70: 'stream', 0x140A0DF68: 'blur', 0x140A0DF60: 'shadowmap',
          0x140A0DF58: 'occlusion'}
B_LIGHT = 0x140DDF185      # "light" setting (byte)
B_PLAYERSHADOWS = 0x140DDF184
I_MAPSHADOWS = 0x140DDF190
I_RENDER3D = 0x140DDF084
PROFILERS = (0x140A1C850, 0x140A1C848)

F_TILES_FLOOR = 0x14010ABE0
F_DAY_BUFFER = 0x140260330
F_MAIN_BUFFER = 0x1402626D0
F_UPDATE_LIGHTS = 0x1400346D0
F_COLOR_LIST = 0x14004F670
F_UPDATE = 0x140262890          # TSmartLighting.Update
F_DRAW_LAYER1 = 0x14010E4B0     # TSmartLighting.DrawLayer1
G_DT = 0x140A1B090              # time factor (1.0 per 60 Hz frame)
F_PULL_FBO = 0x1400814B0
F_DOWNCAST = 0x1408C3280
SKIP = {0x140112340: 'playershadows', 0x14025F0E0: 'tdoshadow', 0x1401483B0: 'projectileshadows'}

FBO_SLOTS = {0x98: 'drawbuffer', 0xa0: 'drawbufferrect', 0xa8: 'bind', 0xb0: 'clear', 0xb8: 'unbind'}


def load_scene(path):
    sc = dict(rooms=[], lights=[], stripes=[], toning=[0] * 1024)
    lines = iter(open(path).read().split('\n'))
    for line in lines:
        p = line.split()
        if not p:
            continue
        k = p[0]
        if k == 'map':
            sc['w'], sc['h'] = int(p[1]), int(p[2])
        elif k == 'cells':
            # next h lines: w pairs "tilemap,mode"
            tm, md = {}, {}
            for y in range(sc['h']):
                row = next(lines).split()
                for x, c in enumerate(row):
                    a, b = c.split(',')
                    tm[(x, y)] = int(a)
                    md[(x, y)] = int(b)
            sc['tilemap'], sc['mode'] = tm, md
        elif k == 'theight':
            sc['theight'] = [float(v) for v in p[1:]]
        elif k == 'toning':
            sc['toning'] = [int(v, 16) for v in p[1:]]
        elif k == 'room':
            sc['rooms'].append([float(v) if '.' in v else int(v) for v in p[1:]])
        elif k == 'light':
            sc['lights'].append([float(v) if '.' in v else int(v) for v in p[1:]])
        elif k == 'stripe':
            sc['stripes'].append([float(v) if '.' in v else int(v) for v in p[1:]])
        else:
            sc[k] = [float(v) if '.' in v else int(v) for v in p[1:]]
    return sc


class LightEmu(Cs2dEmu):
    def __init__(self, exe):
        super().__init__(exe)
        self.hook_slots = 0
        self.lists = {}
        self.enums = {}
        self.fbo_id = {}
        self.hook(F_DOWNCAST, self._downcast)
        self.hook(F_PULL_FBO, self._pull_fbo)
        for addr, name in SKIP.items():
            self.hook(addr, lambda e, n=name: e.log_append(('call', n)))
        noop = self.fake_object('noop', 0x20, {})
        # static 12-float polygon buffer of cs2d_draw_poly_3 / _5 (PTR_bbEmptyArray_140a1bd78)
        self.set_ptr(0x140A1BD78, self.array_1d('f', [0.0] * 12))
        for p in PROFILERS:
            self.set_ptr(p, noop)

    def stub(self, fn):
        """A fresh code address in the hook page that runs fn(emu)."""
        self.hook_slots += 1
        a = HOOKS + 0x1000 + 0x10 * self.hook_slots
        self.hook(a, fn)
        return a

    def fake_object(self, name, size, slots, default=None):
        """Object whose vtable slots 0..0x200 run slots[off](emu) (or log 'name.slot')."""
        vt = self.alloc(0x200)
        for off in range(0, 0x200, 8):
            fn = slots.get(off)
            if fn is None:
                fn = default or (lambda e, o=off: e.log_append(('call', '%s.%x' % (name, o))))
            self.set_ptr(vt + off, self.stub(fn))
        obj = self.alloc(max(size, 0x10))
        self.set_ptr(obj, vt)
        return obj

    # ---- lists
    def make_list(self, addr, items):
        def enum(e):
            en = e.fake_object('enum', 0x10, {0x78: lambda e2: e2.ret_int(1 if e2.enums[eid] else 0),
                                              0x80: lambda e2: e2.ret_int(e2.enums[eid].pop(0))})
            eid = en
            e.enums[eid] = list(items)
            e.ret_int(en)
        lst = self.fake_object('list', 0x20, {0x130: enum}, default=lambda e: e.ret_int(0))
        self.set_ptr(addr, lst)
        self.lists[addr] = items

    def _downcast(self, e):
        obj, cls = e.ix(0), e.ix(1)
        clas = struct.unpack('<Q', e.uc.mem_read(obj, 8))[0] if obj >= 0x10000 else 0
        e.ret_int(obj if clas == cls else NULL_OBJ)

    # ---- light FBOs
    def _pull_fbo(self, e):
        size, lid = e.i32(0), e.i32(1)
        n = len(self.fbo_id) + 1
        name = 'buf%d' % n

        def slot(off):
            def fn(e2):
                op = FBO_SLOTS.get(off, 'fbo.%x' % off)
                if op in ('drawbuffer', 'drawbufferrect'):
                    e2.log_append((op, name, round(e2.fx(1), 3), round(e2.fx(2), 3), round(e2.fx(3), 3),
                                   round(e2.fx(4), 3)))
                elif op == 'clear':
                    e2.log_append((op, name))
                else:
                    e2.log_append((op, name))
            return fn
        fbo = self.fake_object(name, 0x20, {off: slot(off) for off in FBO_SLOTS})
        self.set_i32(fbo + 0x10, size)
        self.set_i32(fbo + 0x14, size)
        mgr = self.alloc(0x20)
        self.set_ptr(mgr, NULL_OBJ)
        self.set_i32(mgr + 8, 0)
        self.set_i32(mgr + 0xc, size)
        self.set_i32(mgr + 0x10, lid)
        self.set_ptr(mgr + 0x18, fbo)
        self.fbo_id[mgr] = name
        e.ret_int(mgr)

    # ---- scene
    def setup(self, sc, cam_x, cam_y):
        w, h = sc['w'], sc['h']
        s = sc.get('settings', [5, 4, 1, 0])
        light, mapshadows, playershadows = s[0], s[1], s[2]
        f = self.set_f32
        i = self.set_i32
        i(G['Initialized'], 1)
        i(G['SETTING_MODE'], light - 1)
        f(G['CameraX'], float(cam_x))
        f(G['CameraY'], float(cam_y))
        i(G['GLOBALBRIGHTNESS'], sc.get('brightness', [0])[0])
        i(G['DAYSHADOWBRIGHTNESS'], sc.get('dayshadowbrightness', [128])[0])
        nc = sc.get('nightcolor', [0, 0, 0])
        i(G['NIGHT_R'], nc[0])
        i(G['NIGHT_G'], nc[1])
        i(G['NIGHT_B'], nc[2])
        f(G['PENUMBRA'], 55.0)
        i(G['OCCLUSION'], 1)
        i(G['SHADOWSPRITE'], 1 if light == 5 else 0)
        i(G['LIGHTFIXES'], 1 if light == 5 else 0)
        i(G['DAYSHADOWS'], 0)
        f(G['SUNANGLE'], float(sc.get('sunangle', [0])[0]))
        i(G['PLAYERSHADOWS'], 1)
        scale = 1.0 if light >= 4 else 2.0
        f(G['SCALE_NIGHTBUFFER'], scale)
        f(G['SCALE_LIGHTS'], scale)
        i(G['MapWidth'], w)
        i(G['MapHeight'], h)
        rot = float(sc.get('shadowrot', [225])[0])
        f(G['ShadowRotation'], rot)
        f(G['ShadowRotationSet'], rot)
        tm, md = sc['tilemap'], sc['mode']
        self.set_ptr(G['TileMap'], self.array_2d('i', w, h, lambda x, y: tm[(x, y)]))
        self.set_ptr(G['TileMapMode'], self.array_2d('i', w, h, lambda x, y: md[(x, y)]))
        th = list(sc['theight']) + [0.0] * (256 - len(sc['theight']))
        th[255] = 32.0
        self.set_ptr(G['TileSetHeight'], self.array_1d('f', th))
        i(G['TileSetCount'], sc.get('tilesetcount', [len(sc['theight'])])[0])
        i(G['TilesX'], 27)
        i(G['TilesY'], 15)
        f(G['ScreenWidth'], 850.0)
        f(G['ScreenHeight'], 480.0)
        self.set_ptr(G['WallSize'], self.array_1d('i', [0] * 0x400))
        self.set_ptr(G['DayToningColor'], self.array_1d('i', [(v ^ 0x80000000) - 0x80000000
                                                              for v in sc['toning']]))
        self.uc.mem_write(B_LIGHT, bytes([light]))
        self.uc.mem_write(B_PLAYERSHADOWS, bytes([playershadows]))
        i(I_MAPSHADOWS, mapshadows)
        i(I_RENDER3D, 0)
        i(SPRITE_COUNT, 0)
        i(SPRITE_FLOOR_COUNT, 0)
        for addr, name in IMAGES.items():
            img = self.alloc(0x40)
            self.set_ptr(addr, img)
            self.images[img] = name
        main = self.fake_object('main', 0x20, {
            0xa8: lambda e: e.log_append(('bind', 'main')),
            0xb0: lambda e: e.log_append(('clear', 'main', round(e.fx(1), 3), round(e.fx(2), 3),
                                          round(e.fx(3), 3), round(e.fx(4), 3))),
            0xb8: lambda e: e.log_append(('unbind', 'main'))})
        self.set_ptr(G['MainBuffer'], main)
        # rooms: x y w h height r g b shadow mode daycolor layer offx offy transparency state
        rooms = []
        for r in sc['rooms']:
            o = self.alloc(0x48)
            self.set_ptr(o, CLASS['TLightingRoom'])
            for off, v in zip((0x8, 0xc, 0x10, 0x14, 0x28, 0x1c, 0x20, 0x24, 0x30, 0x18, 0x34, 0x44, 0x3c, 0x40),
                              r[0:14]):
                i(o + off, int(v))
            f(o + 0x38, float(r[14]))
            i(o + 0x2c, int(r[15]))
            rooms.append(o)
        # lights: x y size r g b alpha astart aend rotation maxsize state type
        lights = []
        for n, l in enumerate(sc['lights']):
            o = self.alloc(0x78)
            self.set_ptr(o, CLASS['TLightSource'])
            i(o + 0x14, int(l[0]))
            i(o + 0x18, int(l[1]))
            f(o + 0x1c, float(l[2]))
            f(o + 0x8, float(l[3]))
            f(o + 0xc, float(l[4]))
            f(o + 0x10, float(l[5]))
            f(o + 0x44, float(l[6]))
            i(o + 0x58, int(l[7]))
            i(o + 0x5c, int(l[8]))
            f(o + 0x48, float(l[9]))
            i(o + 0x70, int(l[10]))
            i(o + 0x64, int(l[11]))
            i(o + 0x6c, int(l[12]) if len(l) > 12 else 0)
            self.set_ptr(o + 0x50, NULL_OBJ)
            i(o + 0x68, n + 1)
            lights.append(o)
        # stripes: x y w h rot height r g b day alpha state
        stripes = []
        for st in sc['stripes']:
            o = self.alloc(0x38)
            self.set_ptr(o, CLASS['TLightingStripe'])
            for off, v in zip((0x8, 0xc, 0x10, 0x14), st[0:4]):
                f(o + off, float(v))
            i(o + 0x18, int(st[4]))
            i(o + 0x34, int(st[5]))
            i(o + 0x20, int(st[6]))
            i(o + 0x24, int(st[7]))
            i(o + 0x28, int(st[8]))
            i(o + 0x2c, int(st[9]))
            f(o + 0x1c, float(st[10]))
            i(o + 0x30, int(st[11]))
            stripes.append(o)
        self.make_list(LISTS['TLightingRoom'], rooms)
        self.make_list(LISTS['TLightSource'], lights)
        self.make_list(LISTS['TLightingStripe'], stripes)
        for k in ('EntityShadow', 'Tdo', 'ObstacleShadow', 'ObstacleShadow2', 'LightFBOManager', 'Tpro'):
            self.make_list(LISTS[k], [])


def fmt(item):
    op = item[0]
    rest = []
    for v in item[1:]:
        if isinstance(v, list):
            rest.extend('%.3f' % x for x in v)
        elif isinstance(v, float):
            rest.append('%.3f' % v)
        else:
            rest.append(str(v))
    names = {'SetColor': 'color', 'SetBlend': 'blend', 'SetAlpha': 'alpha', 'SetRotation': 'rotation',
             'SetScale': 'scale', 'SetTransform': 'transform', 'SetOrigin': 'origin', 'DrawRect': 'rect',
             'DrawImage': 'image', 'DrawImageRect': 'imagerect', 'DrawPoly': 'poly',
             'DrawTexturedPoly': 'tpoly'}
    return ' '.join([names.get(op, op)] + rest)


def main():
    exe, scene, part = sys.argv[1], sys.argv[2], sys.argv[3]
    sc = load_scene(scene)
    cam = sc.get('camera', [0, 0])
    if len(sys.argv) > 5:
        cam = [float(sys.argv[4]), float(sys.argv[5])]
    emu = LightEmu(exe)
    emu.setup(sc, cam[0], cam[1])
    if part == 'floor':
        log = emu.call(F_DRAW_LAYER1, [])
    elif part == 'tilesfloor':
        log = emu.call(F_TILES_FLOOR, [])
    elif part == 'day':
        log = emu.call(F_DAY_BUFFER, [])
    elif part == 'dayargs':
        hook_day_primitives(emu)
        log = emu.call(F_DAY_BUFFER, [])
    elif part == 'main':
        emu.set_f32(G_DT, 1.0)
        log = emu.call(F_UPDATE, [])
    elif part == 'color':
        emu.call(F_UPDATE_LIGHTS, [])
        # FUN_140281fd0 around the call: SetBlend(4), SetTransform(0, 1, 1) ... SetBlend(3)
        log = [('SetBlend', 4), ('SetTransform', 0.0, 1.0, 1.0)] + emu.call(F_COLOR_LIST, []) + [('SetBlend', 3)]
    else:
        raise SystemExit('unknown part ' + part)
    for item in log:
        print(fmt(item))



def raw_args(e, n):
    """First n Win64 arguments as (int, float) pairs (registers and stack)."""
    out = []
    for i in range(n):
        out.append('%d/%.3f' % (e.i32(i), e.fx(i)))
    return out


def hook_day_primitives(emu):
    """Log the calls GenerateDayBuffer makes to its shadow primitives instead of running them."""
    for addr, name, n in ((0x14025E420, 'poly3', 10), (0x14025F8D0, 'poly5', 10), (0x14025FFF0, 'fff0', 10)):
        emu.hook(addr, lambda e, nm=name, k=n: e.log_append(tuple([nm] + raw_args(e, k))))


if __name__ == '__main__':
    main()
