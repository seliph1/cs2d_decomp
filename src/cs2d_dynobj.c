#include "cs2d_dynobj.h"
#include "cs2d_entities.h"
#include "cs2d_map.h"
#include "cs2d_network.h"
#include "cs2d_platform.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
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

/*
 * Solid buildings (walls, Dispenser, turrets, supplies) cast a wall shadow like a wall tile. Not checked
 * against the original's shadow code (requested to match the look of solid walls); the Construction Site is
 * left out because it is flat.
 */
bool cs2d_dynobj_casts_shadow(int typ)
{
    return cs2d_dynobj_collision(typ) == 1 && typ != CS2D_DO_SITE;
}

/*
 * Tshoot.Shoot (0x1402073ed): a shot with damage > 0 that reaches one of these types throws debris
 * (Tp.breakable material 1): Barricade, Wall I..III, and below 16 the bits of 0x9b80 (Dispenser, Turret,
 * Supply, Dual / Triple Turret, Super Supply).
 */
bool cs2d_dynobj_hit_debris(int typ)
{
    if (typ == CS2D_DO_BARRICADE || (typ >= CS2D_DO_WALL_1 && typ <= CS2D_DO_WALL_3)) return true;
    return typ >= 0 && typ < 16 && ((0x9b80u >> typ) & 1u);
}

/* Rewrites the object overlay of one tile and recomputes its collision (tiles < entities < objects) */
static void refresh_tile(cs2d_dynobj_world_t *w, int tx, int ty)
{
    cs2d_map_t *map = w->map;
    if (!map || !map->object_collision) return;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return;
    uint8_t col = 0, shadow = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active || (int)(o->x * 0.03125f) != tx || (int)(o->y * 0.03125f) != ty) continue;
        uint8_t c = cs2d_dynobj_collision(o->typ);
        if (c) col = c;
        if (cs2d_dynobj_casts_shadow(o->typ)) shadow = 1;
    }
    map->object_collision[(size_t)ty * map->width + (size_t)tx] = col;
    if (map->object_shadow) map->object_shadow[(size_t)ty * map->width + (size_t)tx] = shadow;
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
    memset(w->tweens, 0, sizeof(w->tweens));
    w->res_count = 0;
    w->res_received = 0;
    w->map = map;
    w->version++;
    if (!map || !map->collision) return;
    size_t total = (size_t)map->width * map->height;
    free(map->object_collision);
    map->object_collision = (uint8_t *)calloc(total, 1);
    free(map->object_shadow);
    map->object_shadow = (uint8_t *)calloc(total, 1);
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
        if (!o->active || o->typ == CS2D_DO_IMAGE) continue;
        if ((int)(o->x * 0.03125f) == tx && (int)(o->y * 0.03125f) == ty) return o;
    }
    return NULL;
}

int cs2d_dynobj_count(const cs2d_dynobj_world_t *w)
{
    int n = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) n += w->objs[i].active;
    return n;
}

/* Hover text (FUN_1402a12d0, mp_hovertext bit 8): |camx - x + mx - 16| < 17 on both axes, first in list order */
const cs2d_dynobj_t *cs2d_dynobj_hovered(const cs2d_dynobj_world_t *w, float wx, float wy)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        const cs2d_dynobj_def_t *d = o->active ? cs2d_dynobj_def(o->typ) : NULL;
        if (!d || d->category != 1) continue;
        if (fabsf(wx - 16.0f - o->x) < 17.0f && fabsf(wy - 16.0f - o->y) < 17.0f) return o;
    }
    return NULL;
}

/*
 * Client branch (DAT_140ddf400 == 0, health is a percent): values above 100 are the upgrade progress, shown
 * as 100 % in (0, 510, 0) plus "Upgrade n%"; otherwise the percent clamped to >= 1 in ((100 - p) * 5.1, p * 5.1, 0).
 * Max2D SetColor clamps the channels to 0..255.
 */
void cs2d_dynobj_hover_health(const cs2d_dynobj_t *o, int *percent, int *upgrade, int rgb[3])
{
    int p = o->health, up = 0, r, g;
    if (p > 100) {
        up = p - 100;
        p  = 100;
        r  = 0;
        g  = 510;
    } else {
        if (p < 1) p = 1;
        g = (int)((float)p * 5.1f);
        r = (int)((float)(100 - p) * 5.1f);
    }
    *percent = p;
    *upgrade = up;
    rgb[0] = r > 255 ? 255 : r;
    rgb[1] = g > 255 ? 255 : g;
    rgb[2] = 0;
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
    o->m_x    = (float)x;
    o->m_y    = (float)y;
    o->target = 0;
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(typ);
    if (d && d->category == 3) {
        o->health = 1;  /* NPC: a 32 x 32 Tdoc at -16, -16 */
        o->hit_mode = 1;
        o->hit_xo = -16;
        o->hit_yo = -16;
        o->hit_w = 32;
        o->hit_h = 32;
    } else if (d && d->category == 4) {
        o->color[0] = o->color[1] = o->color[2] = 255;
        o->alpha  = 1.0f;
        o->scalex = 1.0f;
        o->scaley = 1.0f;
    }
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

/*
 * Tdo.inlasermine (0x14000aa30 in cs2d_dedicated.exe): checks whether tile (tx, ty) intersects the laser mine beam.
 * Target is the beam span in tiles (0..6).
 * Facing direction (rot):
 *   0: down  (+Y) -> tx == mx && ty >= my && ty < my + target
 *   1: left  (-X) -> ty == my && tx <= mx && tx > mx - target
 *   2: up    (-Y) -> tx == mx && ty <= my && ty > my - target
 *   3: right (+X) -> ty == my && tx >= mx && tx < mx + target
 */
bool cs2d_dynobj_in_lasermine(const cs2d_dynobj_t *o, int tx, int ty)
{
    if (!o || o->typ != CS2D_DO_LASER_MINE) return false;
    int mx = (int)(o->x * 0.03125f);
    int my = (int)(o->y * 0.03125f);
    if (mx != tx && my != ty) return false;
    if (o->target <= 0) return false;
    int dir = (int)o->rot;
    if (dir == 0) { /* down (+Y) */
        return (tx == mx && ty >= my && ty < my + o->target);
    } else if (dir == 1) { /* left (-X) */
        return (ty == my && tx <= mx && tx > mx - o->target);
    } else if (dir == 2) { /* up (-Y) */
        return (tx == mx && ty <= my && ty > my - o->target);
    } else if (dir == 3) { /* right (+X) */
        return (ty == my && tx >= mx && tx < mx + o->target);
    }
    return false;
}

/*
 * FUN_14028e310 / FUN_14028f880: steps up to 5 tiles ahead along the laser mine's facing direction.
 * If a wall, map border or solid building blocks the beam, the beam stops before that tile.
 * Sets o->target to the total span in tiles (1..6), where 1 means only the mine tile itself.
 */
void cs2d_dynobj_update_lasermine(cs2d_dynobj_world_t *w, const cs2d_map_t *map, cs2d_dynobj_t *o)
{
    if (!o || o->typ != CS2D_DO_LASER_MINE || o->mode != 1) return;
    int mx = (int)(o->x * 0.03125f);
    int my = (int)(o->y * 0.03125f);
    int dir = (int)o->rot;
    int dx = 0, dy = 0;
    if (dir == 0) dy = 1;       /* down (+Y) */
    else if (dir == 1) dx = -1; /* left (-X) */
    else if (dir == 2) dy = -1; /* up (-Y) */
    else if (dir == 3) dx = 1;  /* right (+X) */
    else return;

    int target = 6;
    for (int i = 1; i <= 5; i++) {
        int tx = mx + dx * i;
        int ty = my + dy * i;
        if (map) {
            if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) {
                target = i;
                break;
            }
            if (cs2d_map_prop_class(cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty)) != 1) {
                target = i;
                break;
            }
            if (map->object_collision && map->object_collision[(size_t)ty * map->width + tx]) {
                target = i;
                break;
            }
        }
        if (w) {
            const cs2d_dynobj_t *other = cs2d_dynobj_at_tile(w, tx, ty);
            if (other && other->id != o->id && other->typ != CS2D_DO_IMAGE) {
                target = i;
                break;
            }
        }
    }
    o->target = target;
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
    if (typ == CS2D_DO_NPC) o->m_rot = o->rot; /* (health = 0x1409ed7f0[type] is not read: the client shows none) */
    return o;
}

/*
 * Tdo.kill case 0x1e: the die sound of an NPC, by its `player` type (REA). 1 zombie 0x140a0d8d8, 2 headcrab
 * 0x140a0d608, 3 snark 0x140a0d618, 4 vortigaunt 0x140a0d5f8, 5 soldier = one element of the array 0x140a0d958
 * chosen with Rand(0, length - 1); any other value plays nothing. The globals were resolved to files from the
 * loader 0x14030cc00 (in each block the `lea` of the name belongs to the sound stored in the *next* global):
 * d8d8 = "sfx/player/zm_die.wav", d608 = "sfx/npc/headcrab_die.wav", d618 = "sfx/npc/snark_die.wav",
 * d5f8 = "sfx/npc/vortigaunt_die.wav", d958 = the array of "sfx/player/die[1..3].wav".
 * (The ground blood and the snark's particle fountain behind DAT_140ddf192, config "gore", are not drawn yet.)
 */
static const char *npc_die_sound(int type)
{
    switch (type) {
        case 1: return "sfx/player/zm_die.wav";
        case 2: return "sfx/npc/headcrab_die.wav";
        case 3: return "sfx/npc/snark_die.wav";
        case 4: return "sfx/npc/vortigaunt_die.wav";
        case 5: {
            static const char *const die[3] = { "sfx/player/die1.wav", "sfx/player/die2.wav", "sfx/player/die3.wav" };
            return die[rand() % 3];
        }
        default: return NULL;
    }
}

bool cs2d_dynobj_kill(cs2d_dynobj_world_t *w, int id, cs2d_dynobj_fx_t *fx)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(w, id);
    if (!o) return false;
    o->active = false;
    touch(w, o);
    for (int i = 0; i < CS2D_DYNOBJ_TWEENS; i++) {
        if (w->tweens[i].used && w->tweens[i].id == o->id) w->tweens[i].used = false;
    }
    if (fx) {
        /* Tdo.kill: Tp.explosion(x + 16, y + 16, 32, 5) for these types */
        switch (o->typ) {
            case CS2D_DO_DISPENSER: case CS2D_DO_TURRET: case CS2D_DO_SUPPLY: case CS2D_DO_DUAL_TURRET:
            case CS2D_DO_TRIPLE_TURRET: case CS2D_DO_TELE_ENTRANCE: case CS2D_DO_TELE_EXIT: case CS2D_DO_SUPER_SUPPLY:
                fx->kind = 1;
                fx->x = o->x + 16.0f;
                fx->y = o->y + 16.0f;
                break;
            case CS2D_DO_ORANGE_PORTAL: case CS2D_DO_BLUE_PORTAL: /* Tp 0x1d sx 0.8 at the centre */
                fx->kind = (o->typ == CS2D_DO_ORANGE_PORTAL) ? 3 : 4;
                fx->x = o->x + 16.0f;
                fx->y = o->y + 16.0f;
                break;
            case CS2D_DO_NPC: /* the die sound at the NPC (Tdo.kill case 0x1e), no explosion */
                fx->kind = 6;
                fx->x = o->x;
                fx->y = o->y;
                fx->sound = npc_die_sound(o->player);
                break;
            default:
                break;
        }
    }
    return true;
}

/* Tdo.add defaults for Env_NPC: health 0x1409ed7f0[type] (CS2D.exe init 0x14031a195: the array is
 * {0, 150, 30, 20, 200, 100, ...}; index 0 is unused). Type 1 zombie, 2 headcrab, 3 snark,
 * 4 vortigaunt, 5 soldier. */
static const int s_npc_health[6] = { 0, 150, 30, 20, 200, 100 };

int cs2d_dynobj_npc_health(int type)
{
    if (type <= 0 || type >= (int)(sizeof(s_npc_health) / sizeof(s_npc_health[0]))) return 0;
    return s_npc_health[type];
}

/* FUN_140190e10: rebuild the map's dynamic objects from its entities in entity order. Both the server
 * (round start) and the client (round start / offline map load) run this, so the Tdo ids agree:
 * Env_Building -> building typ ints[0], team ints[1], rot ints[2]; Env_NPC -> typ 30 at the tile
 * center, rot ints[0], `player` = NPC type ints[1], health ints[2] (0 = the type default), and
 * ints[3] == 1 means "do not spawn on start". Every object remembers its entity tile (Tdo.entity). */
void cs2d_dynobj_spawn_map_objects(cs2d_dynobj_world_t *w)
{
    cs2d_map_t *map = w->map;
    if (!map) return;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (map->entity_state[i] != 0) continue;
        if (e->x < 0 || e->y < 0 || (uint32_t)e->x >= map->width || (uint32_t)e->y >= map->height) continue;

        if (e->type == CS2D_ENT_ENV_BUILDING) {
            /* FUN_140190e10: type ints[0], team ints[1], rotation ints[2] */
            cs2d_dynobj_t *o = cs2d_dynobj_spawn(w, -1, e->int_settings[0], e->x << 5, e->y << 5,
                                                 (float)e->int_settings[2], 0, e->int_settings[1], 0);
            if (o) {
                o->entity[0] = 1;
                o->entity[1] = e->x;
                o->entity[2] = e->y;
            }
        } else if (e->type == CS2D_ENT_ENV_NPC && e->int_settings[3] == 0) {
            int npc_type = e->int_settings[1];
            if (npc_type <= 0 || npc_type >= (int)(sizeof(s_npc_health) / sizeof(s_npc_health[0]))) continue;
            cs2d_dynobj_t *o = cs2d_dynobj_spawn(w, -1, CS2D_DO_NPC, (e->x << 5) + 16, (e->y << 5) + 16,
                                                 (float)e->int_settings[0], 0, 0, npc_type);
            if (o) {
                o->health = (e->int_settings[2] > 0) ? e->int_settings[2] : s_npc_health[npc_type];
                o->entity[0] = 1;
                o->entity[1] = e->x;
                o->entity[2] = e->y;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Messages
 * --------------------------------------------------------------------------- */

static int rd16(const uint8_t *b) { return b[0] | (b[1] << 8); }

/*
 * FUN_1401ed310 (client branch): an NPC attacks player pid. The attack spins the sprite (rotvar 40 / 20, decaying 5
 * per frame), shows a knife slash (Tp 13) at the NPC along rot and plays the type's sound; the Vortigaunt's kind 1
 * starts the 90 frame ranged charge and kind 2 fires it. Then the NPC turns to face the player (m_rot).
 * Sounds (loader 0x14030cc00): zm_attack[] (0x140a0d8b8), npc/headcrab_attack (d610), snark_attack (d620),
 * vortigaunt_attack (d600), vortigaunt_charge (d5f0), vortigaunt_ranged (d5e8), weapons/mp5 (d808).
 */
static void npc_attack(cs2d_dynobj_world_t *w, cs2d_dynobj_t *o, int pid, int kind, cs2d_dynobj_fx_t *fx)
{
    static const char *zm_attack[4] = { "sfx/player/zm_attack1.wav", "sfx/player/zm_attack2.wav",
                                        "sfx/player/zm_attack3.wav", "sfx/player/zm_attack4.wav" };
    cs2d_dynobj_fx_t f = { 5, o->x, o->y, o->rot, false, NULL, 0 };
    switch (o->player) {
        case 1: o->rotvar = 40.0f; f.slash = true; f.sound = zm_attack[rand() % 4]; break;
        case 2: o->rotvar = 20.0f; f.slash = true; f.sound = "sfx/npc/headcrab_attack.wav"; break;
        case 3: o->rotvar = 20.0f; f.slash = true; f.sound = "sfx/npc/snark_attack.wav"; break;
        case 4:
            if (kind == 0) {
                o->rotvar = 40.0f;
                f.slash = true;
                f.sound = "sfx/npc/vortigaunt_attack.wav";
            } else if (kind == 1) {
                o->upgrade = 1;
                f.sound = "sfx/npc/vortigaunt_charge.wav";
                o->m_x = o->x;
                o->m_y = o->y;
            } else if (kind == 2) {
                o->upgrade = 0;
                f.shot = 2;     /* Tdo.shoot(none, x, y, 0xf9, 40, style 2) */
                f.sound = "sfx/npc/vortigaunt_ranged.wav";
            }
            break;
        case 5: f.sound = "sfx/weapons/mp5.wav"; f.shot = 1; break; /* Tdo.shoot(none, x, y, 0xf8, 5, style 0) */
        default: break;
    }
    if (fx) *fx = f;
    if (pid >= 1 && pid <= CS2D_MAX_PLAYERS && w->players) {
        const cs2d_player_t *p = &w->players[pid];
        o->m_rot = (float)(atan2((double)(p->pos.x - o->x), (double)(o->y - p->pos.y)) * 57.29577951308232);
    }
}

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
            o->m_x = o->x;
            o->m_y = o->y;
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
            if (avail < 6) return 0;
            o->m_x   = (float)rd16(b);
            o->m_y   = (float)rd16(b + 2);
            o->m_rot = (float)rd16(b + 4);
            return 6;
        case 8:
            if (avail < 1) return 0;
            o->team = b[0];
            if (o->team == 0) o->player = 0;
            return 1;
        case 9: /* NPC attack (FUN_1401ed310): target player, attack kind */
            if (avail < 2) return 0;
            npc_attack(w, o, b[0], b[1], fx);
            return 2;
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

/*
 * FUN_140283120 (client): turn towards m_rot by at most turn degrees while more than 3 degrees away, else walk
 * towards (m_x, m_y) at speed px per frame while more than 3 px away. Returns true while the NPC turns or walks.
 * (The tile change checks on barbed wire, gate fields and mines are server side and left out.)
 */
static bool npc_move(cs2d_dynobj_t *o, float speed, float turn)
{
    if (fabsf(o->m_rot - o->rot) > 3.0f) {
        float d = fmodf(o->m_rot - o->rot, 360.0f);
        if (d < -180.0f) d += 360.0f;
        if (d > 180.0f) d -= 360.0f;
        if (fabsf(d) <= turn) o->rot = o->m_rot;
        else o->rot += (d > turn) ? turn : (d < -turn ? -turn : d);
        return true;
    }
    if (fabsf(o->m_x - o->x) <= 3.0f && fabsf(o->m_y - o->y) <= 3.0f) return false;
    double a = atan2((double)(o->m_x - o->x), (double)(o->y - o->m_y));
    o->x = (float)((double)o->x + sin(a) * (double)speed);
    o->y = (float)((double)o->y - cos(a) * (double)speed);
    if (fabsf(o->x - o->m_x) < speed && fabsf(o->y - o->m_y) < speed) {
        o->x = o->m_x;
        o->y = o->m_y;
    }
    return true;
}

/*
 * FUN_140283f20 (client branch), once per frame: speed / turn per type from the call sites (0x140bd7150 ...):
 * zombie 1 / 5, headcrab 2.5 / 5, snark 6 / 4, vortigaunt 7 / 20, soldier 3 / 7. The Vortigaunt's ranged charge
 * counts to 90 frames. The attack spin (rotvar) is not touched here: Tdo.draw_ground case 0x1e decays it 5 per
 * *draw* (render_npc, so a culled NPC keeps its spin).
 */
static void update_npc(cs2d_dynobj_t *o)
{
    static const float speed[6] = { 0.0f, 1.0f, 2.5f, 6.0f, 7.0f, 3.0f };
    static const float turn[6]  = { 0.0f, 5.0f, 5.0f, 4.0f, 20.0f, 7.0f };
    if (o->player < 1 || o->player > 5) return;
    if (npc_move(o, speed[o->player], turn[o->player])) {
        o->countdown = (o->player == 5) ? 0 : 10;
        return;
    }
    if (o->player == 4 && o->upgrade > 0) {
        o->countdown = 0;
        if (++o->upgrade == 90) o->upgrade = 0;
    }
}

void cs2d_dynobj_update(cs2d_dynobj_world_t *w, const cs2d_player_t *players, float frames)
{
    w->players = players;
    w->frame_acc += frames;
    while (w->frame_acc >= 1.0f) {
        w->frame_acc -= 1.0f;
        /* Ttween mode 3 (rotate constantly): rot += speed on every update */
        for (int i = 0; i < CS2D_DYNOBJ_TWEENS; i++) {
            const cs2d_dynobj_tween_t *t = &w->tweens[i];
            if (!t->used || t->mode != 3) continue;
            cs2d_dynobj_t *o = cs2d_dynobj_get(w, t->id);
            if (o) o->rot += t->rot[1];
        }
        for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
            cs2d_dynobj_t *o = &w->objs[i];
            if (!o->active) continue;
            if (o->typ == CS2D_DO_TURRET || o->typ == CS2D_DO_DUAL_TURRET || o->typ == CS2D_DO_TRIPLE_TURRET) {
                update_turret(o, players);
            } else if (o->typ == CS2D_DO_BARBED_WIRE && o->mode > 0) {
                o->mode--; /* Tdo.draw_ground: the wobble decays one step per frame */
            } else if (o->typ == CS2D_DO_NPC) {
                update_npc(o);
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
    int r = cs2d_dynobj_can_build_at(w, ptx, pty, tx, ty, own_tile_ok);
    if (r != 1 || !players) return r;
    float cx = (float)(tx * 32 + 16), cy = (float)(ty * 32 + 16);
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &players[i];
        if (i == self_id || !p->alive || p->team == CS2D_TEAM_SPEC || p->health < 1) continue;
        if (fabsf(p->pos.x - cx) < 32.0f && fabsf(p->pos.y - cy) < 32.0f) return -1;
    }
    return 1;
}

int cs2d_dynobj_can_build_at(const cs2d_dynobj_world_t *w, int ptx, int pty, int tx, int ty, bool own_tile_ok)
{
    const cs2d_map_t *map = w->map;
    if (!map || !map->collision) return 0;
    if (abs(tx - ptx) >= 2 || abs(ty - pty) >= 2) return 0;
    if (tx == ptx && ty == pty && !own_tile_ok) return 0;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return 0;
    /* g_prop_class[cellprop] == 1: a free floor tile (FUN_14014ef60). Not "our collision == 0": a deadly
     * tile is walkable (collision 3) and an entity may have raised the collision of a floor tile. */
    if (cs2d_map_prop_class(cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty)) != 1) return 0;
    if (entity_blocks(map, tx, ty) || area_blocks(map, tx, ty, own_tile_ok)) return 0;
    if (cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)w, tx, ty)) return 0;
    return 1;
}

int cs2d_dynobj_count_type(const cs2d_dynobj_world_t *w, int player, int team, int typ, bool all_teams)
{
    /* Tdo.counttype clamps the player's team to 2 before the comparison */
    int want = team < 0 ? 0 : (team > 2 ? 2 : team);
    int n = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active || o->player != player || o->typ != typ) continue;
        if (all_teams || o->team == want) n++;
    }
    return n;
}

bool cs2d_dynobj_same_type_close(const cs2d_dynobj_world_t *w, int typ, float x, float y, float radius, int team)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active || (o->typ != typ && o->typ != CS2D_DO_SITE)) continue;
        float dx = x - o->x, dy = y - o->y;
        if (sqrtf(dx * dx + dy * dy) > radius) continue;
        if (team == 0) return true;
        if (team == 1) { if (o->team == 1) return true; }
        else if (o->team > 1) return true;
    }
    return false;
}

void cs2d_dynobj_refresh(cs2d_dynobj_world_t *w, cs2d_dynobj_t *o)
{
    if (o) touch(w, o);
}

int cs2d_dynobj_health_percent(const cs2d_dynobj_t *o)
{
    int p = o->health;
    if (p > 100) p = 100;
    if (p < 0) p = 0;
    return p;
}

int cs2d_dynobj_hpup_net_value(const cs2d_dynobj_t *o)
{
    const cs2d_dynobj_def_t *d = cs2d_dynobj_def(o->typ);
    int maxhp = (d && d->max_health > 0) ? d->max_health : 100;
    int v = (int)(((float)o->health / (float)maxhp) * 100.0f);
    if (v < 0) return 0;
    if (v >= 100 && d && d->upgrade_points > 0) {
        int u = (int)(((float)o->upgrade / (float)d->upgrade_points) * 100.0f);
        if (u > 100) u = 100;
        if (u < 0) u = 0;
        v = 100 + u;
    }
    return v;
}

bool cs2d_dynobj_portal_tile(const cs2d_dynobj_world_t *w, int tx, int ty)
{
    const cs2d_map_t *map = w->map;
    if (!map || !map->collision) return false;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return false;
    /* g_prop_class[cellprop] == 1 (a floor) and cellprop != 15 (Metal: portals need a normal floor) */
    uint8_t m = cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty);
    if (cs2d_map_prop_class(m) != 1) return false;
    if (m == CS2D_TILE_FLOOR_METAL) return false;
    if (cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)w, tx, ty)) return false;
    return !entity_blocks(map, tx, ty) && !area_blocks(map, tx, ty, true);
}

cs2d_dynobj_t *cs2d_dynobj_place_portal(cs2d_dynobj_world_t *w, int player, int team, int mode,
                                        float px, float py, int x, int y, cs2d_dynobj_fx_t *fx)
{
    int typ = (mode == 1) ? CS2D_DO_ORANGE_PORTAL : CS2D_DO_BLUE_PORTAL;
    /* The player's previous portal of this colour closes first, even when the new one can't be placed */
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &w->objs[i];
        if (o->active && o->player == player && o->typ == typ) cs2d_dynobj_kill(w, o->id, fx);
    }
    if (fabsf(px - (float)x) > 457.0f || fabsf(py - (float)y) > 272.0f || x <= -32 || y <= -32) return NULL;
    int tx = (x < 0 ? x + 31 : x) >> 5, ty = (y < 0 ? y + 31 : y) >> 5;
    if (!cs2d_dynobj_portal_tile(w, tx, ty)) return NULL;
    return cs2d_dynobj_spawn(w, -1, typ, tx << 5, ty << 5, (float)(1 + rand() % 360), 0, team, player);
}

int cs2d_dynobj_teleport(cs2d_dynobj_world_t *w, int tx, int ty, int team, bool team_game, float *dx, float *dy,
                         int *used_id)
{
    if (used_id) *used_id = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active || (int)(o->x * 0.03125f) != tx || (int)(o->y * 0.03125f) != ty) continue;
        const cs2d_dynobj_t *dest = NULL;
        if (o->typ == CS2D_DO_ORANGE_PORTAL || o->typ == CS2D_DO_BLUE_PORTAL) {
            /* Case 0x16 / 0x17: the other colour of the same owner */
            int other = (o->typ == CS2D_DO_ORANGE_PORTAL) ? CS2D_DO_BLUE_PORTAL : CS2D_DO_ORANGE_PORTAL;
            for (int j = 0; j < CS2D_DYNOBJ_MAX && !dest; j++) {
                const cs2d_dynobj_t *p = &w->objs[j];
                if (p->active && p->typ == other && p->player == o->player) dest = p;
            }
            if (!dest) continue;
        } else if (o->typ == CS2D_DO_TELE_ENTRANCE) {
            /* Case 0xd: own team's entrance (any team when not a team game, or for team 0) */
            bool usable = (team == 1 && o->team == 1) || (team > 1 && o->team > 1) || team == 0 || !team_game;
            if (!usable) continue;
            if (o->mode != 1) return -1;
            /* The owner's own exit if there is one, else a random exit of the entrance's team */
            const cs2d_dynobj_t *exits[CS2D_DYNOBJ_MAX];
            int n = 0;
            for (int j = 0; j < CS2D_DYNOBJ_MAX; j++) {
                const cs2d_dynobj_t *p = &w->objs[j];
                if (!p->active || p->typ != CS2D_DO_TELE_EXIT) continue;
                if (o->team > 0 && p->team == o->team && p->player == o->player) { exits[0] = p; n = 1; break; }
                if (p->team == o->team) exits[n++] = p;
            }
            if (n == 0) return -2;
            dest = exits[rand() % n];
        } else {
            continue;
        }
        /* FUN_14014ccc0 (setpos): the tile centre of the destination */
        *dx = dest->x + 16.0f;
        *dy = dest->y + 16.0f;
        if (used_id) *used_id = o->id; /* the entrance that has to recharge ([36,id,6]) */
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Lua images (Tdo 40) and tweens
 * --------------------------------------------------------------------------- */

static float rdf32(const uint8_t *b)
{
    float f;
    memcpy(&f, b, 4);
    return f;
}

void cs2d_dynobj_longversion(const cs2d_dynobj_world_t *w, const char *in, int in_len, char *out, int max_out)
{
    /* "?" then the u16 index as two characters (idConversionBank, low byte first), then the rest of the path.
     * The index bytes may be 0, so the length comes with the string. */
    if (in_len > 2 && in[0] == '?') {
        int idx = (uint8_t)in[1] | ((uint8_t)in[2] << 8);
        if (idx < w->res_count && w->res[idx][0]) {
            snprintf(out, (size_t)max_out, "%s%.*s", w->res[idx], in_len - 3, in + 3);
            return;
        }
    }
    snprintf(out, (size_t)max_out, "%.*s", in_len, in);
}

/* Tudp_p string (u8 length + bytes) read raw into a resolved path; false when truncated */
static bool read_path(const cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *p, char *out, int max_out)
{
    if (*p >= len) return false;
    int n = buf[(*p)++];
    if (len - *p < n) return false;
    cs2d_dynobj_longversion(w, (const char *)buf + *p, n, out, max_out);
    *p += n;
    return true;
}

bool cs2d_dynobj_read_restable(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos)
{
    int p = *pos;
    if (p >= len) return false;
    int count = buf[p++];
    if (count == 0) {
        w->res_count = w->res_received < CS2D_RESTABLE_MAX ? w->res_received : CS2D_RESTABLE_MAX; /* createClientTable */
        *pos = p;
        return true;
    }
    for (int k = 0; k < count; k++) {
        if (p >= len) { *pos = len; return false; } /* "failed to load resource path data" */
        char path[96];
        cs2d_pkt_r_str(buf, len, &p, path, sizeof(path));
        if (w->res_received < CS2D_RESTABLE_MAX) {
            snprintf(w->res[w->res_received], sizeof(w->res[0]), "%s", path);
        }
        w->res_received++;
    }
    *pos = p;
    return true;
}

cs2d_dynobj_t *cs2d_dynobj_add_image(cs2d_dynobj_world_t *w, int id, const char *path, float x, float y, int mode,
                                     int player)
{
    cs2d_dynobj_t *o = cs2d_dynobj_add(w, id, CS2D_DO_IMAGE, (int)x, (int)y);
    if (!o) return NULL;
    o->mode   = mode;
    o->player = player;
    snprintf(o->path, sizeof(o->path), "%s", path);
    return o;
}

static cs2d_dynobj_t *get_image(cs2d_dynobj_world_t *w, int id)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(w, id);
    return (o && o->typ == CS2D_DO_IMAGE) ? o : NULL;
}

int cs2d_dynobj_image_message(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int pos)
{
    const uint8_t *b = &buf[pos];
    int avail = len - pos - 1;
    if (buf[pos] == CS2D_NET_IMAGEHITZONE) {
        /* FUN_14017d7e0 client side: mode 0 removes the zone; the server sends the offsets + 32768 */
        if (avail < 11) return -1;
        cs2d_dynobj_t *o = cs2d_dynobj_get(w, rd16(b + 1));
        if (o) {
            o->hit_mode = b[3];
            if (o->hit_mode) {
                o->hit_xo = rd16(b + 4) - 0x8000;
                o->hit_yo = rd16(b + 6) - 0x8000;
                o->hit_w  = rd16(b + 8);
                o->hit_h  = rd16(b + 10);
            }
        }
        return 12;
    }
    if (buf[pos] != CS2D_NET_DYNAMICIMAGE || avail < 1) return -1;
    int sub = b[1];
    const uint8_t *d = b + 2;
    avail--;
    cs2d_dynobj_t *o;
    switch (sub) {
        case 0: { /* FUN_14017c310 with the server's id */
            if (avail < 1) return -1;
            int p = pos + 2;
            char path[128];
            if (!read_path(w, buf, len, &p, path, sizeof(path)) || len - p < 8) return -1;
            const uint8_t *r = &buf[p];
            cs2d_dynobj_add_image(w, rd16(r), path, (float)(rd16(r + 2) - 0x8000), (float)(rd16(r + 4) - 0x8000),
                                  r[6], r[7]);
            return p + 8 - pos;
        }
        case 1: /* imagecolor */
            if (avail < 5) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) { o->color[0] = d[2]; o->color[1] = d[3]; o->color[2] = d[4]; }
            return 7;
        case 2: /* imagealpha */
            if (avail < 3) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) o->alpha = (float)d[2] * 0.003921569f;
            return 5;
        case 3: /* imageblend, clamped to 3 */
            if (avail < 3) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) o->blend = d[2] > 3 ? 3 : d[2];
            return 5;
        case 4: /* imagescale */
            if (avail < 10) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) { o->scalex = rdf32(d + 2); o->scaley = rdf32(d + 6); }
            return 12;
        case 5: /* imagepos */
            if (avail < 8) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) {
                o->x   = (float)(rd16(d + 2) - 0x8000);
                o->y   = (float)(rd16(d + 4) - 0x8000);
                o->rot = (float)rd16(d + 6) * 0.005493164f;
            }
            return 10;
        case 6: /* freeimage: Tdo.free(id, 40) only removes images */
            if (avail < 2) return -1;
            if (get_image(w, rd16(d))) cs2d_dynobj_kill(w, rd16(d), NULL);
            return 4;
        case 7: /* imageframe: 1-based, clamped to the frames of the loaded image */
            if (avail < 3) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) {
                int f = d[2] - 1;
                if (o->frames > 0 && f > o->frames - 1) f = o->frames - 1;
                o->frame = f < 0 ? 0 : f;
            }
            return 5;
        case 8: /* Physics parent (typ 4 platform / 6 object): no physics objects on our side */
            if (avail < 6) return -1;
            return 8;
        case 9: /* imageparam shadow */
            if (avail < 3) return -1;
            if ((o = get_image(w, rd16(d))) != NULL) o->shadow = d[2];
            return 5;
        default: /* FUN_140182240 reads nothing more */
            return 2;
    }
}

bool cs2d_dynobj_read_image_snapshot(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos)
{
    int p = *pos;
    if (p >= len) return false;
    int count = buf[p++];
    for (int k = 0; k < count; k++) {
        if (len - p < 19) { *pos = len; return false; } /* "failed to load dynamic object image data" */
        int id = rd16(buf + p);
        p += 2;
        char path[128];
        if (!read_path(w, buf, len, &p, path, sizeof(path))) { *pos = len; return false; }
        if (len - p < 16) { *pos = len; return false; }
        const uint8_t *r = &buf[p];
        /* r, g, b, alpha f32, scalex f32, scaley f32, blend, frame u16, parent typ (+ typid u16, mode), shadow */
        int q = 19;
        if (len - p < q) { *pos = len; return false; }
        if (r[18] != 0) q += 3;
        if (len - p < q + 1) { *pos = len; return false; }
        cs2d_dynobj_t *o = get_image(w, id);
        if (o) {
            snprintf(o->path, sizeof(o->path), "%s", path);
            o->color[0] = r[0];
            o->color[1] = r[1];
            o->color[2] = r[2];
            o->alpha  = rdf32(r + 3);
            o->scalex = rdf32(r + 7);
            o->scaley = rdf32(r + 11);
            o->blend  = r[15];
            o->frame  = rd16(r + 16);
            o->frames = 0;
            o->shadow = r[q];
        }
        p += q + 1;
        /* Hit zone flag, then {mode, xo u16 + 32768, yo u16 + 32768, w u16, h u16} */
        if (p >= len) { *pos = len; return false; }
        if (buf[p++] == 1) {
            if (len - p < 9) { *pos = len; return false; }
            if (o) {
                o->hit_mode = buf[p];
                o->hit_xo = rd16(buf + p + 1) - 0x8000;
                o->hit_yo = rd16(buf + p + 3) - 0x8000;
                o->hit_w  = rd16(buf + p + 5);
                o->hit_h  = rd16(buf + p + 7);
            }
            p += 9;
        }
    }
    *pos = p;
    return true;
}

/* Ttween.addtween for an image: replaces a tween of the same kind (moves / rotations / frame modes conflict) */
static bool tween_conflict(int a, int b)
{
    if (a == b || (a | b) < 2) return true;
    if (a != 0 && a < 4 && (unsigned)(b - 1) < 3) return true;
    return (a == 7 && b == 8) || (a == 8 && b == 7);
}

bool cs2d_dynobj_read_tween(cs2d_dynobj_world_t *w, const uint8_t *buf, int len, int *pos, uint32_t now_ms)
{
    /* Ttween.send: mode 0/1 = duration + x/y (+ rot for mode 1), mode 2 = duration + rot,
     * 3 = speed float, 4 = duration + alpha, 5 = duration + rgb, 6 = duration + scale,
     * 7 = duration + frame, 8 = duration + animation mode. */
    static const int body[9] = { 8, 10, 6, 4, 5, 7, 12, 5, 5 };
    int p = *pos;
    if (len - p < 3) return false;
    int mode = buf[p];
    int id = rd16(buf + p + 1) - 200;
    int n = mode < 9 ? body[mode] : 0;
    if (len - p - 3 < n) return false;
    const uint8_t *d = buf + p + 3;
    *pos = p + 3 + n;
    cs2d_dynobj_t *o = get_image(w, id);
    if (!o || mode > 8) return true;
    cs2d_dynobj_tween_t *t = NULL;
    for (int i = 0; i < CS2D_DYNOBJ_TWEENS; i++) {
        cs2d_dynobj_tween_t *e = &w->tweens[i];
        if (e->used && e->id == id && tween_conflict(e->mode, mode)) e->used = false;
        if (!e->used && !t) t = e;
    }
    if (!t) return true;
    memset(t, 0, sizeof(*t));
    t->used = true;
    t->mode = (uint8_t)mode;
    t->id = (uint16_t)id;
    t->start_ms = now_ms;
    if (mode != 3) memcpy(&t->duration, d, 4);
    switch (mode) {
        case 0:
        case 1:
            t->x[0] = o->x;
            t->y[0] = o->y;
            t->rot[0] = o->rot;
            t->x[1] = (float)(rd16(d + 4) - 0x8000);
            t->y[1] = (float)(rd16(d + 6) - 0x8000);
            if (mode == 1) t->rot[1] = (float)rd16(d + 8) * 0.005493164f;
            break;
        case 2:
            t->rot[0] = o->rot;
            t->rot[1] = (float)rd16(d + 4) * 0.005493164f;
            break;
        case 3:
            t->rot[1] = rdf32(d);
            break;
        case 4:
            t->alpha[0] = o->alpha;
            t->alpha[1] = (float)d[4] * 0.003921569f;
            break;
        case 5:
            memcpy(t->rgb[0], o->color, 3);
            memcpy(t->rgb[1], d + 4, 3);
            break;
        case 6:
            t->sx[0] = o->scalex;
            t->sy[0] = o->scaley;
            t->sx[1] = rdf32(d + 4);
            t->sy[1] = rdf32(d + 8);
            break;
        default: /* 7 / 8 */
            t->frame[0] = (uint8_t)o->frame;
            t->frame[1] = d[4];
            break;
    }
    return true;
}

static float lerp_angle(float a, float b, float k)
{
    float d = fmodf(b - a, 360.0f);
    if (d < -180.0f) d += 360.0f;
    if (d > 180.0f) d -= 360.0f;
    return d * k + a;
}

void cs2d_dynobj_update_tweens(cs2d_dynobj_world_t *w, uint32_t now_ms)
{
    for (int i = 0; i < CS2D_DYNOBJ_TWEENS; i++) {
        cs2d_dynobj_tween_t *t = &w->tweens[i];
        if (!t->used) continue;
        cs2d_dynobj_t *o = get_image(w, t->id);
        if (!o) { t->used = false; continue; }
        int el = (int)(now_ms - t->start_ms);
        float k = t->duration > 0 ? (float)el / (float)t->duration : 1.0f;
        if (k >= 1.0f) k = 1.0f;
        int frames = o->frames > 0 ? o->frames : 1;
        switch (t->mode) {
            case 0: case 1:
                o->x = (t->x[1] - t->x[0]) * k + t->x[0];
                o->y = (t->y[1] - t->y[0]) * k + t->y[0];
                if (t->mode == 1) o->rot = lerp_angle(t->rot[0], t->rot[1], k);
                break;
            case 2: o->rot = lerp_angle(t->rot[0], t->rot[1], k); break;
            case 4: o->alpha = (1.0f - k) * t->alpha[0] + k * t->alpha[1]; break;
            case 5:
                for (int c = 0; c < 3; c++) {
                    o->color[c] = (uint8_t)((float)t->rgb[0][c] * (1.0f - k) + (float)t->rgb[1][c] * k);
                }
                break;
            case 6:
                o->scalex = t->sx[0] * (1.0f - k) + t->sx[1] * k;
                o->scaley = t->sy[0] * (1.0f - k) + t->sy[1] * k;
                break;
            case 7: {
                int f = (int)((float)t->frame[0] * (1.0f - k) + (float)t->frame[1] * k);
                if (f >= 0 && f < frames) o->frame = f;
                break;
            }
            case 8: /* Every duration ms: 0 forward loop, 1 backward loop, 2 / 3 ping-pong, 4 random */
                if (el > t->duration) {
                    t->start_ms = now_ms;
                    if (t->frame[1] == 0) { if (++o->frame >= frames) o->frame = 0; }
                    else if (t->frame[1] == 1) { if (--o->frame < 0) o->frame = frames - 1; }
                    else if (t->frame[1] == 2) { if (++o->frame >= frames) { o->frame = frames - 1; t->frame[1] = 3; } }
                    else if (t->frame[1] == 3) { if (--o->frame < 1) { o->frame = 0; t->frame[1] = 2; } }
                    else if (t->frame[1] == 4) o->frame = rand() % frames;
                }
                break;
            default: break;
        }
        /* Rotate constantly (3) and frame animations (8) never end */
        if (el > t->duration && t->mode != 3 && t->mode != 8) t->used = false;
    }
}

int cs2d_dynobj_image_kind(const char *path, char *file, int max_file, int *cell_w, int *cell_h, int *mask, int *arg)
{
    *cell_w = *cell_h = 0;
    *mask = 1;
    *arg = 0;
    file[0] = '\0';
    if (path[0] == '<') {
        /* Tdo.iniimage special paths (lower case compares) */
        if (_strnicmp(path, "<light", 6) == 0) return CS2D_IMG_LIGHT;
        if (_strnicmp(path, "<avatar_", 8) == 0) {
            int n = atoi(path + 8); /* bbStringToInt of the rest: "3>" -> 3 */
            *arg = (n >= 1 && n <= 32) ? n : 0;
            return CS2D_IMG_AVATAR;
        }
        if (strlen(path) > 7 && _strnicmp(path, "<flag:", 6) == 0) {
            /* Characters 6 and 7, lower case, looked up in TFlag.flags_map */
            snprintf(file, (size_t)max_file, "%c%c", tolower((unsigned char)path[6]), tolower((unsigned char)path[7]));
            return CS2D_IMG_FLAG;
        }
        if (_strnicmp(path, "<tile:", 6) == 0) { *arg = atoi(path + 6); return CS2D_IMG_TILE; }
        if (_strnicmp(path, "<spritesheet:", 13) != 0) return CS2D_IMG_NONE;
        /* <spritesheet:path:w:h[:m|a|b|n]>: "<" and ">" removed, split at ":" */
        char tmp[256], *part[6] = { 0 };
        int n = 0;
        for (const char *c = path; *c && n < (int)sizeof(tmp) - 1; c++) if (*c != '<' && *c != '>') tmp[n++] = *c;
        tmp[n] = '\0';
        n = 0;
        for (char *tok = strtok(tmp, ":"); tok && n < 6; tok = strtok(NULL, ":")) part[n++] = tok;
        if (n < 4) return CS2D_IMG_NONE; /* "ERROR: Spritesheet frame dimensions not set" */
        snprintf(file, (size_t)max_file, "%s", part[1]);
        *cell_w = atoi(part[2]);
        *cell_h = atoi(part[3]);
        if (n >= 5) {
            char m = (char)tolower((unsigned char)part[4][0]);
            *mask = m == 'm' ? 4 : m == 'a' ? 3 : m == 'n' ? 0 : 1;
        }
        return (*cell_w > 0 && *cell_h > 0) ? CS2D_IMG_FILE : CS2D_IMG_NONE;
    }
    /* "path<m>": the suffix picks the Tres mask (m 4 magenta, a 3 alpha, b 1 black, n 0 none), default 1 */
    snprintf(file, (size_t)max_file, "%s", path);
    size_t n = strlen(file);
    if (n > 3 && file[n - 3] == '<' && file[n - 1] == '>') {
        char m = (char)tolower((unsigned char)file[n - 2]);
        *mask = m == 'm' ? 4 : m == 'a' ? 3 : m == 'n' ? 0 : 1;
        file[n - 3] = '\0';
    }
    return file[0] ? CS2D_IMG_FILE : CS2D_IMG_NONE;
}
