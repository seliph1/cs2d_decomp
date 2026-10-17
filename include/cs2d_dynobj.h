#ifndef CS2D_DYNOBJ_H
#define CS2D_DYNOBJ_H

/*
 * Dynamic objects (class Tdo in CS2D.exe): buildings (Construction game mode, Env_Building), mines, portals,
 * NPCs and Lua images. The server owns them; the client keeps a copy from the join snapshot [252,7,5] and
 * the messages [34] build, [35] remove, [36] mode, [37] health and [49] spawn.
 *
 * Fields follow the Tdo debug scope (tools/agent/bbscope.py CS2D.exe Tdo): id +0x8, typ +0xc, health +0x10,
 * mode +0x14, team +0x18, player +0x1c, x +0x20, y +0x24, rot +0x28, countdown +0x58, rrot +0x5c,
 * rotvar +0x64, target +0x68. x / y are the top-left pixel of the object's tile.
 */

#include "cs2d_types.h"
#include <stdbool.h>
#include <stdint.h>

#define CS2D_DYNOBJ_MAX     1024
#define CS2D_DYNOBJ_TYPES   41   /* Type table size (FUN_140235390 fills 1..23, 30 and 40) */

typedef enum {
    CS2D_DO_BARRICADE      = 1,
    CS2D_DO_BARBED_WIRE    = 2,
    CS2D_DO_WALL_1         = 3,
    CS2D_DO_WALL_2         = 4,
    CS2D_DO_WALL_3         = 5,
    CS2D_DO_GATE_FIELD     = 6,
    CS2D_DO_DISPENSER      = 7,
    CS2D_DO_TURRET         = 8,
    CS2D_DO_SUPPLY         = 9,
    CS2D_DO_SITE           = 10, /* Construction Site: mode holds the type it becomes */
    CS2D_DO_DUAL_TURRET    = 11,
    CS2D_DO_TRIPLE_TURRET  = 12,
    CS2D_DO_TELE_ENTRANCE  = 13,
    CS2D_DO_TELE_EXIT      = 14,
    CS2D_DO_SUPER_SUPPLY   = 15,
    CS2D_DO_MINE           = 20,
    CS2D_DO_LASER_MINE     = 21,
    CS2D_DO_ORANGE_PORTAL  = 22,
    CS2D_DO_BLUE_PORTAL    = 23,
    CS2D_DO_NPC            = 30,
    CS2D_DO_IMAGE          = 40
} cs2d_dynobj_type_t;

/*
 * Per-type table (FUN_140235390). category 0x1409f7c40: 1 building (tile aligned, changes the map collision),
 * 2 mine / portal, 3 NPC, 4 image. price 0x1409f3930, max_health 0x1409f6298, kill_reward 0x1409f71b8,
 * limit 0x1409f2608 (per player), upgrade_points 0x1409f57c8 and upgrade_to 0x1409f57c0 (wrench hits).
 */
typedef struct {
    const char *name;       /* sys/language 0401.. (0x1409f79f8), English */
    int         category;
    int         price;
    int         max_health;
    int         kill_reward;
    int         limit;
    int         upgrade_points;
    int         upgrade_to;
} cs2d_dynobj_def_t;

typedef struct {
    bool     active;
    uint16_t id;
    uint8_t  typ;
    int      health;     /* Percent on the client (Tdo.healthpercent), 100 when spawned */
    int      mode;
    int      team;
    int      player;
    float    x, y;       /* Pixels, top-left of the tile for buildings */
    float    rot;
    float    rrot;       /* Turret rest angle */
    float    rotvar;     /* Turret idle sweep phase (degrees) */
    int      target;     /* Turret target player ([36,id,0,target]) */
    int      countdown;  /* Turret: frames left of the shooting burst */
    int      shots;      /* Turret bursts fired since the client last read it (each fires every barrel) */
    int      upgrade;    /* Building: Wrench upgrade points (Tdo.upgrade, +0x6c); NPC Vortigaunt: charge 1..89 */
    /* Server-side timers (Tdo.t1 / Tdo.t2, absolute ms of cs2d_server_t). 0 = inactive */
    uint32_t t1_ms;      /* Construction Site: the moment it becomes the building stored in mode */
    uint32_t t2_ms;      /* Teleporter Entrance: [36,id,5] + mode 1 five seconds after the build */
    uint32_t scan_ms;    /* Turret: next target scan (cs2d_sv_step_buildings) */

    /* NPC (typ 30): player holds the NPC type 1 zombie, 2 headcrab, 3 snark, 4 vortigaunt, 5 soldier */
    float    m_x, m_y;   /* Movement target (+0x104 / +0x108, [36,id,7]); Tdo.add sets it to the spawn point */
    float    m_rot;      /* Target angle (+0x10c) */

    /* Lua image (typ 40, Tdo.iniimage 0x14022b9f0): mode 0 floor, 1 top, 2 HUD, 3 super top, 4 background,
     * 101..132 / 133..164 / 201..232 / 233..263 follow player (mode - 100 / 132 / 200 / 232) */
    char     path[128];  /* Long path (Trestable.longversion), with an optional "<m>" / "<a>" / "<b>" / "<n>" mask */
    uint8_t  color[3];
    float    alpha;
    float    scalex, scaley;
    int      blend;      /* 0 alpha, 1 light, 2 shade, 3 solid (imageblend) */
    int      frame;      /* Current frame (imageframe - 1) */
    int      frames;     /* Frame count once the image is loaded (cs2d_dynobj_set_frames), 0 unknown */
    int      shadow;     /* +0x44 (imageparam / snapshot), 0 casts an obstacle shadow */
    int      hit_mode;   /* Tdoc hit zone ([59]): mode 0 none, xo / yo / w / h */
    int      hit_xo, hit_yo, hit_w, hit_h;
} cs2d_dynobj_t;

/* Ttween (Lua tween_* on images, [54] with target 200 + id and [252,7,8]) */
#define CS2D_DYNOBJ_TWEENS 256
typedef struct {
    bool     used;
    uint8_t  mode;       /* 0 move, 1 move + rotate, 2 rotate, 3 rotate constantly, 4 alpha, 5 color, 6 scale,
                          * 7 frame, 8 frame animation */
    uint16_t id;
    uint32_t start_ms;
    int      duration;
    float    x[2], y[2], rot[2], alpha[2], sx[2], sy[2];
    uint8_t  rgb[2][3];
    uint8_t  frame[2];
} cs2d_dynobj_tween_t;

#define CS2D_RESTABLE_MAX 512

typedef struct cs2d_dynobj_world_s {
    cs2d_dynobj_t objs[CS2D_DYNOBJ_MAX];
    cs2d_map_t   *map;
    uint32_t      version;   /* Bumped on every add / remove / type change */
    float         frame_acc; /* 60 Hz frames not yet simulated by cs2d_dynobj_update */
    const cs2d_player_t *players; /* players[1..CS2D_MAX_PLAYERS] of the last cs2d_dynobj_update (NPC aim) */
    cs2d_dynobj_tween_t tweens[CS2D_DYNOBJ_TWEENS];
    /* Trestable client table ([252,7,10] resource paths): "?" + u16 index stands for paths[index] */
    char          res[CS2D_RESTABLE_MAX][96];
    int           res_count;
    int           res_received;
} cs2d_dynobj_world_t;

/* Effects the client should show after a message (kill explosion, build sound) */
typedef struct {
    int   kind;              /* 0 none, 1 explosion (Tp.explosion r 32 on kill), 2 built (sfx/player/build.wav),
                              * 3 / 4 orange / blue portal closed (Tp 0x1d sx 0.8), 5 NPC attack (FUN_1401ed310),
                              * 6 NPC died (Tdo.kill case 0x1e: the die sound of the NPC type) */
    float x, y;
    float rot;               /* kind 5: knife slash (Tp 13) angle, slash false for none */
    bool  slash;
    const char *sound;       /* kind 5 / 6: sound at (x, y), NULL for none */
    int   shot;              /* kind 5: Tdo.shoot along rot (c_offset 15, c_dist 400): 0 none, 1 style 0 bullet
                              * (soldier, weapon 0xf8), 2 style 2 beam (Vortigaunt ranged, weapon 0xf9) */
} cs2d_dynobj_fx_t;

const cs2d_dynobj_def_t *cs2d_dynobj_def(int typ);

/* Binds to a map (or NULL) and removes every object */
void cs2d_dynobj_init(cs2d_dynobj_world_t *w, cs2d_map_t *map);
/* Tdo.freeall: [22] round start, map change, disconnect */
void cs2d_dynobj_clear(cs2d_dynobj_world_t *w);

cs2d_dynobj_t *cs2d_dynobj_get(cs2d_dynobj_world_t *w, int id);
cs2d_dynobj_t *cs2d_dynobj_at_tile(cs2d_dynobj_world_t *w, int tx, int ty);
int            cs2d_dynobj_count(const cs2d_dynobj_world_t *w);

/* FUN_1402a12d0 (mp_hovertext bit 8): the building (category 1) whose tile center is within 17 px of the world point */
const cs2d_dynobj_t *cs2d_dynobj_hovered(const cs2d_dynobj_world_t *w, float wx, float wy);
/* Health line of that hover text: percent, upgrade progress (0 = no "Upgrade n%" line) and color 0..255 */
void cs2d_dynobj_hover_health(const cs2d_dynobj_t *o, int *percent, int *upgrade, int rgb[3]);

/* Does a bullet hitting this type throw debris (Tshoot.Shoot)? */
bool cs2d_dynobj_hit_debris(int typ);

/* Tdo.add: a new object (replacing one with the same id; id -1 takes the next free id) */
cs2d_dynobj_t *cs2d_dynobj_add(cs2d_dynobj_world_t *w, int id, int typ, int x, int y);
/* FUN_1401431e0 (client side of [49]): buildings are snapped to their tile by Tdo.inibuilding */
cs2d_dynobj_t *cs2d_dynobj_spawn(cs2d_dynobj_world_t *w, int id, int typ, int x, int y, float rot,
                                 int mode, int team, int player);
/* Tdo.kill: removes the object; fx (may be NULL) gets the kill effect */
bool cs2d_dynobj_kill(cs2d_dynobj_world_t *w, int id, cs2d_dynobj_fx_t *fx);

/* Offline: the Env_Building entities of the map, as the server spawns them at round start (FUN_140190e10) */
void cs2d_dynobj_spawn_map_buildings(cs2d_dynobj_world_t *w);

/*
 * Server messages. Each returns the bytes consumed from buf[pos] (the opcode included), or -1 when the
 * message is truncated. fx may be NULL.
 *   [35, id u16]                                                           remove
 *   [36, id u16, sub, ...]                                                 mode / data (Tdo.getdata)
 *   [37, id u16, health u8]                                                health percent (<= 200)
 *   [49, id u16, typ, x u16, y u16, rot u16 (x 360/65536), mode, team, player]  spawn
 */
int cs2d_dynobj_net_message(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int pos, cs2d_dynobj_fx_t *fx);

/* [252,7,5]: count u8, then {id u16, typ, health, mode, x u16 (+32768), y u16 (+32768), player, team, rot f32} */
bool cs2d_dynobj_read_snapshot(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos);

/*
 * Per 60 Hz frame (Tdo.draw_wall / draw_ground animation state): turret aim and idle sweep, barbed wire
 * wobble. players[1..CS2D_MAX_PLAYERS].
 */
void cs2d_dynobj_update(cs2d_dynobj_world_t *w, const cs2d_player_t *players, float frames);

/*
 * FUN_14014ef60: can the player (tile ptx, pty) build on tile (tx, ty)? 1 yes, 0 no (0694 "you can't build
 * here"), -1 another living player is within 32 px of the tile center (0695). own_tile_ok = param_4.
 */
int cs2d_dynobj_can_build(const cs2d_dynobj_world_t *w, const cs2d_player_t *players, int self_id,
                          int ptx, int pty, int tx, int ty, bool own_tile_ok);

/* The same test without the player proximity part: 1 free, 0 not (the dedicated has no cs2d_player_t) */
int cs2d_dynobj_can_build_at(const cs2d_dynobj_world_t *w, int ptx, int pty, int tx, int ty, bool own_tile_ok);

/* Tdo.counttype (0x140035680): objects of `typ` owned by `player`, with the same team unless all_teams */
int cs2d_dynobj_count_type(const cs2d_dynobj_world_t *w, int player, int team, int typ, bool all_teams);

/*
 * Tdo.sametypetoclose (0x14004a2b0): is an object of `typ`, or a Construction Site (10), within `radius` px of
 * (x, y)? team 0 counts every team, 1 only team 1, > 1 only team > 1 (the CT side). Buildings do not count.
 */
bool cs2d_dynobj_same_type_close(const cs2d_dynobj_world_t *w, int typ, float x, float y, float radius, int team);

/* Re-applies the tile collision and the object pass after a type / position change (the server's update_mapdata) */
void cs2d_dynobj_refresh(cs2d_dynobj_world_t *w, cs2d_dynobj_t *o);

/* Tdo.healthpercent (0x1400052f0): the health as a 0..100 percent (the join snapshot and the hover text) */
int cs2d_dynobj_health_percent(const cs2d_dynobj_t *o);
/*
 * Tdo.hpupnetvalue (0x140005350): the byte of [37]: the health percent, or 100 + the upgrade progress.
 * It divides by the type's max health, so it belongs to the dedicated, which keeps Tdo.health in points
 * (the client keeps the percent and stores the [37] byte as it is).
 */
int cs2d_dynobj_hpup_net_value(const cs2d_dynobj_t *o);

/*
 * FUN_1401fc9f0 (server side): may a portal open on tile (tx, ty)? A free floor tile (not tile property 15) with no
 * object, Tdo.checkentity / Tdo.checkareaentity(.., 1).
 */
bool cs2d_dynobj_portal_tile(const cs2d_dynobj_world_t *w, int tx, int ty);
/*
 * FUN_1401fc9f0: a Portal Gun shot at world (x, y) of player (at px, py), mode 1 orange / 2 blue. Closes the
 * player's portal of that colour, then opens a new one (Tdo 22 / 23, rot Rand(360, 1)) on the tile when it is within
 * 457 x 272 px of the player and cs2d_dynobj_portal_tile. Returns it or NULL.
 */
cs2d_dynobj_t *cs2d_dynobj_place_portal(cs2d_dynobj_world_t *w, int player, int team, int mode,
                                        float px, float py, int x, int y, cs2d_dynobj_fx_t *fx);
/*
 * Tpl.tilepos (server side, cases 0xd / 0x16 / 0x17): a player of team entered tile (tx, ty). A portal leads to its
 * owner's other portal; a charged (mode 1) Teleporter Entrance of the player's team to the owner's exit, else a
 * random exit of its team. Returns 1 with the destination (tile centre) in dx / dy, 0 nothing, -1 entrance not
 * charged, -2 no exit. used_id (may be NULL) gets the id of the entrance that has to recharge ([36,id,6]).
 */
int cs2d_dynobj_teleport(cs2d_dynobj_world_t *w, int tx, int ty, int team, bool team_game, float *dx, float *dy,
                         int *used_id);

/*
 * Lua images [51, sub, ...] (FUN_140182240): 0 create {path str, id u16, x u16 + 32768, y u16 + 32768, mode, player},
 * 1 color {id, r, g, b}, 2 alpha {id, a / 255}, 3 blend {id, b}, 4 scale {id, sx f32, sy f32},
 * 5 pos {id, x + 32768, y + 32768, rot x 360 / 65536}, 6 free {id}, 7 frame {id, frame 1..}, 8 parent {id, typ,
 * typid u16, mode}, 9 shadow {id, s}. [59, id u16, mode, xo u16, yo u16, w u16, h u16] hit zone (FUN_14017d7e0).
 * Same contract as cs2d_dynobj_net_message.
 */
int cs2d_dynobj_image_message(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int pos);
/* [252,7,7] image data (FUN_1402ce7c0 case 4) for objects of the [252,7,5] snapshot */
bool cs2d_dynobj_read_image_snapshot(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos);
/* [252,7,10] resource paths (Trestable.receiveList; count 0 ends the table, Trestable.createClientTable) */
bool cs2d_dynobj_read_restable(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos);
/* Trestable.longversion: "?" + u16 index (+ a 3 character mask suffix) to the full path */
void cs2d_dynobj_longversion(const cs2d_dynobj_world_t *w, const char *in, int in_len, char *out, int max_out);

/* Lua image creation (FUN_14017c310): a Tdo 40 with white colour, alpha / scale 1 (Tdo.add category 4) */
cs2d_dynobj_t *cs2d_dynobj_add_image(cs2d_dynobj_world_t *w, int id, const char *path, float x, float y, int mode,
                                     int player);

/*
 * Ttween.receive (0x1400b4420) from buf[*pos] = mode: {mode, target u16, body} where target - 200 is the object id.
 * Body sizes: 0 {ms i32, x, y u16 + 32768}, 1 same, 2 {ms, rot u16}, 3 {speed f32}, 4 {ms, a}, 5 {ms, r, g, b},
 * 6 {ms, sx f32, sy f32}, 7 / 8 {ms, frame}. Returns false when truncated.
 */
bool cs2d_dynobj_read_tween(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos, uint32_t now_ms);
/* Ttween.update (0x14027fc50) for objects, by time; mode 3 turns once per call of cs2d_dynobj_update frames */
void cs2d_dynobj_update_tweens(cs2d_dynobj_world_t *w, uint32_t now_ms);
/* Image path kinds (Tdo.iniimage 0x14022b9f0) */
enum {
    CS2D_IMG_NONE = 0,  /* Nothing drawn (unknown "<...>" path) */
    CS2D_IMG_FILE,      /* file (+ sprite sheet cell_w x cell_h, Tres mask 0 / 1 / 3 / 4) */
    CS2D_IMG_TILE,      /* <tile:N>: arg = N (clamped to the tileset by the drawer) */
    CS2D_IMG_FLAG,      /* <flag:cc>: file = the lower case ISO code */
    CS2D_IMG_AVATAR,    /* <avatar_N>: arg = player 1..32, else 0 */
    CS2D_IMG_LIGHT      /* <light>: a white light source of radius 128, no image */
};
int cs2d_dynobj_image_kind(const char *path, char *file, int max_file, int *cell_w, int *cell_h, int *mask, int *arg);

/* Collision a building puts on its tile (cs2d_update_mapdata): 1 wall, 2 obstacle, 0 none */
uint8_t cs2d_dynobj_collision(int typ);
bool cs2d_dynobj_casts_shadow(int typ);

#endif /* CS2D_DYNOBJ_H */
