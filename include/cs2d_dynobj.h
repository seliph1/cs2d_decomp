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
} cs2d_dynobj_t;

typedef struct cs2d_dynobj_world_s {
    cs2d_dynobj_t objs[CS2D_DYNOBJ_MAX];
    cs2d_map_t   *map;
    uint32_t      version;   /* Bumped on every add / remove / type change */
    float         frame_acc; /* 60 Hz frames not yet simulated by cs2d_dynobj_update */
} cs2d_dynobj_world_t;

/* Effects the client should show after a message (kill explosion, build sound) */
typedef struct {
    int   kind;              /* 0 none, 1 explosion (Tp.explosion r 32 on kill), 2 built (sfx/player/build.wav) */
    float x, y;
} cs2d_dynobj_fx_t;

const cs2d_dynobj_def_t *cs2d_dynobj_def(int typ);

/* Binds to a map (or NULL) and removes every object */
void cs2d_dynobj_init(cs2d_dynobj_world_t *w, cs2d_map_t *map);
/* Tdo.freeall: [22] round start, map change, disconnect */
void cs2d_dynobj_clear(cs2d_dynobj_world_t *w);

cs2d_dynobj_t *cs2d_dynobj_get(cs2d_dynobj_world_t *w, int id);
cs2d_dynobj_t *cs2d_dynobj_at_tile(cs2d_dynobj_world_t *w, int tx, int ty);
int            cs2d_dynobj_count(const cs2d_dynobj_world_t *w);

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

/* Collision a building puts on its tile (cs2d_update_mapdata): 1 wall, 2 obstacle, 0 none */
uint8_t cs2d_dynobj_collision(int typ);
bool cs2d_dynobj_casts_shadow(int typ);

#endif /* CS2D_DYNOBJ_H */
