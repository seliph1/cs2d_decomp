#include "cs2d_dynobj.h"
#include "cs2d_entities.h"
#include "cs2d_network.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD 0.017453292519943295

/* ---------------------------------------------------------------------------
 * Type table (FUN_140235390): name, category, price, max health, kill reward, limit, upgrade points, upgrade to
 * --------------------------------------------------------------------------- */

static const cs2d_dynobj_def_t s_defs[CS2D_DYNOBJ_TYPES] = {
    [CS2D_DO_BARRICADE]     = { "Barricade",           1, 300,   150,     50,   1000, 2,   3  },
    [CS2D_DO_BARBED_WIRE]   = { "Barbed Wire",         1, 500,   300,     0,    1000, 0,   0  },
    [CS2D_DO_WALL_1]        = { "Wall I",              1, 1000,  500,     100,  1000, 10,  4  },
    [CS2D_DO_WALL_2]        = { "Wall II",             1, 2000,  1000,    200,  1000, 10,  5  },
    [CS2D_DO_WALL_3]        = { "Wall III",            1, 3000,  2000,    300,  1000, 0,   0  },
    [CS2D_DO_GATE_FIELD]    = { "Gate Field",          1, 1500,  1000,    0,    5,    0,   0  },
    [CS2D_DO_DISPENSER]     = { "Dispenser",           1, 5000,  1000,    1500, 1,    0,   0  },
    [CS2D_DO_TURRET]        = { "Turret",              1, 5000,  500,     1000, 1,    75,  11 },
    [CS2D_DO_SUPPLY]        = { "Supply",              1, 5000,  1000,    1000, 1,    200, 15 },
    [CS2D_DO_SITE]          = { "Construction Site",   1, 20000, 100,     0,    1,    0,   0  },
    [CS2D_DO_DUAL_TURRET]   = { "Dual Turret",         1, 20000, 750,     1000, 1,    100, 12 },
    [CS2D_DO_TRIPLE_TURRET] = { "Triple Turret",       1, 20000, 1000,    1000, 1,    0,   0  },
    [CS2D_DO_TELE_ENTRANCE] = { "Teleporter Entrance", 1, 3000,  500,     500,  1,    0,   0  },
    [CS2D_DO_TELE_EXIT]     = { "Teleporter Exit",     1, 3000,  500,     500,  1,    0,   0  },
    [CS2D_DO_SUPER_SUPPLY]  = { "Super Supply",        1, 20000, 1500,    1000, 1,    0,   0  },
    [CS2D_DO_MINE]          = { "Mine",                2, 0,     50,      0,    1000, 0,   0  },
    [CS2D_DO_LASER_MINE]    = { "Laser Mine",          2, 0,     1,       0,    50,   0,   0  },
    [CS2D_DO_ORANGE_PORTAL] = { "Orange Portal",       2, 0,     1000000, 0,    1,    0,   0  },
    [CS2D_DO_BLUE_PORTAL]   = { "Blue Portal",         2, 0,     1000000, 0,    1,    0,   0  },
    [CS2D_DO_NPC]           = { "NPC",                 3, 0,     10,      0,    0,    0,   0  },
    [CS2D_DO_IMAGE]         = { "Image",               4, 0,     0,       0,    0,    0,   0  },
};

const cs2d_dynobj_def_t *cs2d_dynobj_def(int typ)
{
    if (typ < 0 || typ >= CS2D_DYNOBJ_TYPES || !s_defs[typ].name) return NULL;
    return &s_defs[typ];
}

/* ---------------------------------------------------------------------------
 * Collision
 * --------------------------------------------------------------------------- */

/*
 * cs2d_update_mapdata (0x1401413b0) writes the building cell props after the tiles and entities: Barricade 6
 * (obstacle), Construction Site 5 (wall at floor level), walls / Dispenser / turrets / supplies 1 (wall).
 * Barbed Wire, Gate Field and teleporters leave the tile walkable.
 */
uint8_t cs2d_dynobj_collision(int typ)
{
    switch (typ) {
        case CS2D_DO_BARRICADE:
            return 2;
        case CS2D_DO_WALL_1: case CS2D_DO_WALL_2: case CS2D_DO_WALL_3: case CS2D_DO_DISPENSER:
        case CS2D_DO_TURRET: case CS2D_DO_SUPPLY: case CS2D_DO_DUAL_TURRET: case CS2D_DO_TRIPLE_TURRET:
        case CS2D_DO_SUPER_SUPPLY: case CS2D_DO_SITE:
            return 1;
        default:
            return 0;
    }
}

/* Rewrites the object overlay of one tile and recomputes its collision (tiles < entities < objects) */
static void refresh_tile(cs2d_dynobj_world_t *w, int tx, int ty)
{
    cs2d_map_t *map = w->map;
    if (!map || !map->object_collision) return;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return;
    uint8_t col = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active || (int)(o->x * 0.03125f) != tx || (int)(o->y * 0.03125f) != ty) continue;
        uint8_t c = cs2d_dynobj_collision(o->typ);
        if (c) col = c;
    }
    map->object_collision[(size_t)ty * map->width + (size_t)tx] = col;
    cs2d_ents_refresh_tile(map, tx, ty);
    map->entity_solid_version++;
}

static void touch(cs2d_dynobj_world_t *w, const cs2d_dynobj_t *o)
{
    w->version++;
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(o->typ);
    if (d && d->category == 1) refresh_tile(w, (int)(o->x * 0.03125f), (int)(o->y * 0.03125f));
}

/* ---------------------------------------------------------------------------
 * List
 * --------------------------------------------------------------------------- */

void cs2d_dynobj_init(cs2d_dynobj_world_t *w, cs2d_map_t *map)
{
    memset(w->objs, 0, sizeof(w->objs));
    w->map = map;
    w->version++;
    if (!map || !map->collision) return;
    size_t total = (size_t)map->width * map->height;
    free(map->object_collision);
    map->object_collision = (uint8_t *)calloc(total, 1);
}

void cs2d_dynobj_clear(cs2d_dynobj_world_t *w)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active) continue;
        o->active = false;
        touch(w, o);
    }
}

cs2d_dynobj_t *cs2d_dynobj_get(cs2d_dynobj_world_t *w, int id)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        if (w->objs[i].active && w->objs[i].id == id) return &w->objs[i];
    }
    return NULL;
}

/* Tdo.getatpos: the object whose tile is (tx, ty) */
cs2d_dynobj_t *cs2d_dynobj_at_tile(cs2d_dynobj_world_t *w, int tx, int ty)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &w->objs[i];
        if (o->active && (int)(o->x * 0.03125f) == tx && (int)(o->y * 0.03125f) == ty) return o;
    }
    return NULL;
}

int cs2d_dynobj_count(const cs2d_dynobj_world_t *w)
{
    int n = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) n += w->objs[i].active;
    return n;
}

/* Tdo.getnextfreeid: the lowest unused id from 1 */
static int next_free_id(cs2d_dynobj_world_t *w)
{
    for (int id = 1; id < 65536; id++) {
        if (!cs2d_dynobj_get(w, id)) return id;
    }
    return -1;
}

cs2d_dynobj_t *cs2d_dynobj_add(cs2d_dynobj_world_t *w, int id, int typ, int x, int y)
{
    if (id == -1) {
        id = next_free_id(w);
        if (id < 0) return NULL;
    } else {
        cs2d_dynobj_kill(w, id, NULL); /* Tdo.free(id) */
    }
    cs2d_dynobj_t *o = NULL;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        if (!w->objs[i].active) { o = &w->objs[i]; break; }
    }
    if (!o) return NULL;
    memset(o, 0, sizeof(*o));
    o->active = true;
    o->id     = (uint16_t)id;
    o->typ    = (uint8_t)typ;
    o->x      = (float)x;
    o->y      = (float)y;
    o->target = 0;
    touch(w, o);
    return o;
}

/* Tdo.inibuilding (client side): a building on tile (tx, ty) at 100 % */
static cs2d_dynobj_t *ini_building(cs2d_dynobj_world_t *w, int id, int player, int typ, int mode, int tx, int ty)
{
    cs2d_dynobj_t *o = cs2d_dynobj_add(w, id, typ, tx << 5, ty << 5);
    if (!o) return NULL;
    o->player    = player;
    o->mode      = mode;
    o->health    = 100;
    o->countdown = 0;
    if (typ == CS2D_DO_TURRET) {
        o->rot  = (float)(1 + rand() % 360); /* Rand(360, 1) */
        o->rrot = o->rot;
    } else if (typ == CS2D_DO_SUPPLY) {
        o->mode = 1;
    }
    return o;
}

/* Laser mine: rot becomes the beam direction 0..3 (FUN_1401431e0 at 0x1401433b0) */
static void laser_mine_dir(cs2d_dynobj_t *o)
{
    double s = sin((o->rot + 180.0) * DEG2RAD);
    double c = cos((o->rot + 180.0) * DEG2RAD);
    float dir;
    if (fabs(-c) < fabs(s)) dir = (s <= 0.0) ? 3.0f : 1.0f;
    else                    dir = (c < 0.0) ? 2.0f : 0.0f;
    o->rot    = dir;
    o->mode   = 1;
    o->target = -1;
}

cs2d_dynobj_t *cs2d_dynobj_spawn(cs2d_dynobj_world_t *w, int id, int typ, int x, int y, float rot,
                                 int mode, int team, int player)
{
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(typ);
    if (!d) return NULL;
    cs2d_dynobj_t *o = (d->category == 1) ? ini_building(w, id, player, typ, mode, x / 32, y / 32)
                                          : cs2d_dynobj_add(w, id, typ, x, y);
    if (!o) return NULL;
    o->rot    = rot;
    o->mode   = mode;
    o->team   = team;
    o->player = player;
    o->health = 100;
    if (typ == CS2D_DO_LASER_MINE) laser_mine_dir(o);
    return o;
}

bool cs2d_dynobj_kill(cs2d_dynobj_world_t *w, int id, cs2d_dynobj_fx_t *fx)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(w, id);
    if (!o) return false;
    o->active = false;
    touch(w, o);
    if (fx) {
        /* Tdo.kill: Tp.explosion(x + 16, y + 16, 32, 5) for these types */
        switch (o->typ) {
            case CS2D_DO_DISPENSER: case CS2D_DO_TURRET: case CS2D_DO_SUPPLY: case CS2D_DO_DUAL_TURRET:
            case CS2D_DO_TRIPLE_TURRET: case CS2D_DO_TELE_ENTRANCE: case CS2D_DO_TELE_EXIT: case CS2D_DO_SUPER_SUPPLY:
                fx->kind = 1;
                fx->x = o->x + 16.0f;
                fx->y = o->y + 16.0f;
                break;
            default:
                break;
        }
    }
    return true;
}

void cs2d_dynobj_spawn_map_buildings(cs2d_dynobj_world_t *w)
{
    cs2d_map_t *map = w->map;
    if (!map) return;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type != CS2D_ENT_ENV_BUILDING || map->entity_state[i] != 0) continue;
        if (e->x < 0 || e->y < 0 || (uint32_t)e->x >= map->width || (uint32_t)e->y >= map->height) continue;
        /* FUN_140190e10: type ints[0], team ints[1], rotation ints[2] */
        cs2d_dynobj_spawn(w, -1, e->int_settings[0], e->x << 5, e->y << 5, (float)e->int_settings[2], 0,
                          e->int_settings[1], 0);
    }
}

/* ---------------------------------------------------------------------------
 * Messages
 * --------------------------------------------------------------------------- */

static int rd16(const uint8_t *b) { return b[0] | (b[1] << 8); }

/*
 * Tdo.getdata (0x1401edb50), client branch. Each case reads its data only when enough bytes remain,
 * so the dispatcher's position must follow the same rule.
 */
static int getdata(cs2d_dynobj_world_t *w, int id, int sub, const uint8_t *b, int avail, cs2d_dynobj_fx_t *fx)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(w, id);
    if (!o) return 0; /* Unknown object: nothing is read */
    switch (sub) {
        case 0: /* Turret target: aim and fire for 15 frames */
            if (avail < 1) return 0;
            o->target    = b[0];
            o->countdown = 15;
            return 1;
        case 1: { /* Construction site finished: becomes the building stored in mode */
            int team = o->team;
            float rot = o->rot;
            cs2d_dynobj_t *n = ini_building(w, o->id, o->player, o->mode, 0, (int)(o->x * 0.03125f), (int)(o->y * 0.03125f));
            if (n) {
                n->team = team;
                n->rot  = rot;
                if (fx) { fx->kind = 2; fx->x = n->x + 16.0f; fx->y = n->y + 16.0f; }
            }
            return 0;
        }
        case 2:
            if (avail < 4) return 0;
            o->x = (float)rd16(b);
            o->y = (float)rd16(b + 2);
            return 4;
        case 3:
            if (avail < 4) return 0;
            memcpy(&o->rot, b, 4);
            return 4;
        case 4: { /* Upgrade: new type at 100 % */
            int n = 0;
            if (avail >= 1) {
                o->typ    = b[0];
                o->health = 100;
                touch(w, o);
                n = 1;
            }
            if (fx) { fx->kind = 2; fx->x = o->x + 16.0f; fx->y = o->y + 16.0f; }
            return n;
        }
        case 5:
            if (o->typ == CS2D_DO_BARBED_WIRE) o->mode = 60;      /* hit wobble (sfx/weapons/barbedwire.wav) */
            else if (o->typ == CS2D_DO_LASER_MINE) o->mode = 1;
            else if (o->typ == CS2D_DO_TELE_ENTRANCE) o->mode = 1; /* charged */
            return 0;
        case 6:
            if (o->typ == CS2D_DO_TELE_ENTRANCE) o->mode = 0;     /* used: recharging */
            return 0;
        case 7: /* NPC movement target: m_x, m_y, m_rot */
            return (avail >= 6) ? 6 : 0;
        case 8:
            if (avail < 1) return 0;
            o->team = b[0];
            if (o->team == 0) o->player = 0;
            return 1;
        case 9: /* Image data (FUN_1401ed310) */
            return (avail >= 2) ? 2 : 0;
        default:
            return 0;
    }
}

int cs2d_dynobj_net_message(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int pos, cs2d_dynobj_fx_t *fx)
{
    const uint8_t *b = &buf[pos];
    int avail = len - pos - 1;
    switch (buf[pos]) {
        case CS2D_NET_DYNOBJ_REMOVE: /* FUN_1402da510 */
            if (avail < 2) return -1;
            cs2d_dynobj_kill(w, rd16(b + 1), fx);
            return 3;
        case CS2D_NET_DYNOBJ_MODE: { /* FUN_1402da670 */
            if (avail < 3) return -1;
            return 4 + getdata(w, rd16(b + 1), b[3], b + 4, avail - 3, fx);
        }
        case CS2D_NET_DYNOBJ_HEALTH: { /* FUN_1402da7f0: values above 200 are ignored */
            if (avail < 3) return -1;
            cs2d_dynobj_t *o = cs2d_dynobj_get(w, rd16(b + 1));
            if (o && b[3] <= 200) o->health = b[3];
            return 4;
        }
        case CS2D_NET_DO_SPAWN: { /* FUN_1402dc250 -> FUN_140143890 */
            if (avail < 12) return -1;
            cs2d_dynobj_spawn(w, rd16(b + 1), b[3], rd16(b + 4), rd16(b + 6), (float)rd16(b + 8) * 0.005493164f,
                              b[10], b[11], b[12]);
            return 13;
        }
        default:
            return -1;
    }
}

bool cs2d_dynobj_read_snapshot(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos)
{
    int p = *pos;
    if (p >= len) return false;
    int count = buf[p++];
    for (int k = 0; k < count; k++) {
        if (len - p < 15) { *pos = len; return false; } /* "failed to load dynamic object data" */
        const uint8_t *r = &buf[p];
        float rot;
        memcpy(&rot, r + 11, 4);
        /* The join snapshot uses Tdo.add directly: no turret rest angle, health is a percent */
        cs2d_dynobj_t *o = cs2d_dynobj_add(w, rd16(r), r[2], rd16(r + 5) - 0x8000, rd16(r + 7) - 0x8000);
        if (o) {
            o->health = r[3];
            o->mode   = r[4];
            o->player = r[9];
            o->team   = r[10];
            o->rot    = rot;
        }
        p += 15;
    }
    *pos = p;
    return true;
}

/* ---------------------------------------------------------------------------
 * Animation
 * --------------------------------------------------------------------------- */

static void update_turret(cs2d_dynobj_t *o, const cs2d_player_t *players)
{
    /* FUN_14028c630: while the burst lasts, aim at the target within 445 x 260 px */
    if (o->countdown != 0) {
        if (o->target >= 1 && o->target <= CS2D_MAX_PLAYERS && players) {
            const cs2d_player_t *t = &players[o->target];
            if (t->alive && t->team != CS2D_TEAM_SPEC && fabsf(o->x + 16.0f - t->pos.x) < 445.0f &&
                fabsf(o->y + 16.0f - t->pos.y) < 260.0f) {
                o->rot = (float)(atan2((double)((t->pos.x - 16.0f) - o->x), (double)((o->y + 16.0f) - t->pos.y)) * 57.29577951308232);
            }
        }
        /* Tdo.shoot (method +0x120) once per barrel while more than 10 frames remain */
        if (o->countdown > 10) o->shots++;
        o->countdown--;
        o->rrot   = o->rot;
        o->rotvar = 0.0f;
    }
    /* Idle: sweep +-60 degrees around the rest angle */
    o->rot = (float)((double)o->rrot + sin((double)o->rotvar * DEG2RAD) * 60.0);
    o->rotvar += 1.0f;
    if (o->rotvar > 360.0f) o->rotvar -= 360.0f;
}

void cs2d_dynobj_update(cs2d_dynobj_world_t *w, const cs2d_player_t *players, float frames)
{
    w->frame_acc += frames;
    while (w->frame_acc >= 1.0f) {
        w->frame_acc -= 1.0f;
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            cs2d_dynobj_t *o = &w->objs[i];
            if (!o->active) continue;
            if (o->typ == CS2D_DO_TURRET || o->typ == CS2D_DO_DUAL_TURRET || o->typ == CS2D_DO_TRIPLE_TURRET) {
                update_turret(o, players);
            } else if (o->typ == CS2D_DO_BARBED_WIRE && o->mode > 0) {
                o->mode--; /* Tdo.draw_ground: the wobble decays one step per frame */
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Building rules
 * --------------------------------------------------------------------------- */

/*
 * Tdo.checkentity: no building on Info_T / CT / VIP / Hostage, Info_CTF_Flag, Info_Dom_Point, Env_Item,
 * Env_Breakable, Env_Object, Func_Teleport and Func_DynWall tiles (bit mask 0x180000000004445 from type 15).
 */
static bool entity_blocks(const cs2d_map_t *map, int tx, int ty)
{
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x != tx || e->y != ty) continue;
        switch ((int)e->type) {
            case 0: case 1: case 2: case 3: case 15: case 17: case 21: case 25: case 29: case 70: case 71:
                return true;
            default:
                break;
        }
    }
    return false;
}

/* Tdo.checkareaentity: active Info_NoBuildings areas (ints[2] = 1 still allows param_4 builds) */
static bool area_blocks(const cs2d_map_t *map, int tx, int ty, bool own_tile_ok)
{
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type != CS2D_ENT_INFO_NOBUILDINGS || map->entity_state[i] != 0) continue;
        int ew = e->int_settings[0], eh = e->int_settings[1];
        if (ew < 1 || eh < 1 || tx < e->x || ty < e->y || tx >= e->x + ew || ty >= e->y + eh) continue;
        if (!own_tile_ok || e->int_settings[2] == 0) return true;
    }
    return false;
}

int cs2d_dynobj_can_build(const cs2d_dynobj_world_t *w, const cs2d_player_t *players, int self_id,
                          int ptx, int pty, int tx, int ty, bool own_tile_ok)
{
    const cs2d_map_t *map = w->map;
    if (!map || !map->collision) return 0;
    if (abs(tx - ptx) >= 2 || abs(ty - pty) >= 2) return 0;
    if (tx == ptx && ty == pty && !own_tile_ok) return 0;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return 0;
    /* g_prop_class[cellprop] == 1: a free floor tile (calibrated: our collision 0 stands for the floor class) */
    if (map->collision[(size_t)ty * map->width + (size_t)tx] != 0) return 0;
    if (entity_blocks(map, tx, ty) || area_blocks(map, tx, ty, own_tile_ok)) return 0;
    if (cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)w, tx, ty)) return 0;
    if (players) {
        float cx = (float)(tx * 32 + 16), cy = (float)(ty * 32 + 16);
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            const cs2d_player_t *p = &players[i];
            if (i == self_id || !p->alive || p->team == CS2D_TEAM_SPEC || p->health < 1) continue;
            if (fabsf(p->pos.x - cx) < 32.0f && fabsf(p->pos.y - cy) < 32.0f) return -1;
        }
    }
    return 1;
}
