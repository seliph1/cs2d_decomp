#include "cs2d_customtile.h"
#include "cs2d_map.h"
#include "cs2d_entities.h"
#include <stdlib.h>
#include <string.h>

void cs2d_settile_defaults(cs2d_settile_t *t, int x, int y, int tile)
{
    memset(t, 0, sizeof(*t));
    t->x = (uint16_t)x;
    t->y = (uint16_t)y;
    t->tile = (uint8_t)tile;
    t->r = t->g = t->b = 255;
}

uint8_t cs2d_settile_blend(const char *dir, const char *type)
{
    static const char *const dirs[8] = { "top", "topright", "right", "bottomright", "bottom", "bottomleft", "left",
                                         "topleft" };
    if (!dir || !type || !dir[0] || !type[0] || atoi(type) < 1) return 0;
    int d = 0;
    for (int i = 0; i < 8; i++) {
        if (strcmp(dir, dirs[i]) == 0) d = i;   /* BlitzMax string compare: case-sensitive */
    }
    return (uint8_t)(d - 8 + atoi(type) * 8);
}

uint8_t cs2d_customtile_modifier(const cs2d_settile_t *t)
{
    uint8_t mod = (t->rot == 1 || t->rot == 2 || t->rot == 3) ? t->rot : 0;
    if (t->blend != 0) mod += 0x40;
    if ((t->r & t->g & t->b) != 0xff) mod += 0x80;
    return mod;
}

int cs2d_settile_read(const uint8_t *buf, int len, int pos, cs2d_settile_t *out)
{
    if (pos + 6 > len) return -1;
    const uint8_t *p = &buf[pos + 1];
    cs2d_settile_defaults(out, p[0] | (p[1] << 8), p[2] | (p[3] << 8), p[4]);
    if (len - (pos + 6) < 6) return 6;
    out->rot = p[5];
    out->r = p[6];
    out->g = p[7];
    out->b = p[8];
    out->overlay = p[9];
    out->blend = p[10];
    return 12;
}

void cs2d_customtile_clear(cs2d_map_t *map)
{
    if (map) map->custom_tile_count = 0;
}

static int find_entry(const cs2d_map_t *map, int x, int y)
{
    for (uint32_t i = 0; i < map->custom_tile_count; i++) {
        if (map->custom_tiles[i].x == x && map->custom_tiles[i].y == y) return (int)i;
    }
    return -1;
}

const cs2d_customtile_t *cs2d_customtile_get(const cs2d_map_t *map, int x, int y)
{
    if (!map) return NULL;
    int i = find_entry(map, x, y);
    return i >= 0 ? &map->custom_tiles[i] : NULL;
}

/*
 * The Tmap_d a tile got from the map file (FUN_140113d50: one only for modifier & 0xc0 = 0x40 or 0x80) as
 * { frame, r, g, b, overlay }; false = none. tile_data keeps the blend frame (0x40) or the overlay (0x80) in d[3].
 */
static bool map_data(uint8_t mod, const uint8_t *d, uint8_t out[5])
{
    memset(out, 0, 5);
    if ((mod & 0xc0) == 0x40) {
        out[0] = d[3];
        return true;
    }
    if ((mod & 0xc0) == 0x80) {
        out[1] = d[0];
        out[2] = d[1];
        out[3] = d[2];
        out[4] = d[3];
        return true;
    }
    return false;
}

static bool same_data(const uint8_t a[5], const cs2d_settile_t *t)
{
    return a[0] == t->blend && a[1] == t->r && a[2] == t->g && a[3] == t->b && a[4] == t->overlay;
}

/* Writes the new Tmap_d: the renderer reads d[3] as the blend frame for 0x40, so it keeps the blend there */
static void write_data(uint8_t *d, uint8_t mod, const cs2d_settile_t *t)
{
    d[0] = t->r;
    d[1] = t->g;
    d[2] = t->b;
    d[3] = ((mod & 0xc0) == 0x40) ? t->blend : t->overlay;
}

void cs2d_customtile_begin(cs2d_customtile_batch_t *b)
{
    memset(b, 0, sizeof(*b));
}

static void batch_add(cs2d_customtile_batch_t *b, int x, int y)
{
    if (b->count == 0 || x < b->x0) b->x0 = x;
    if (b->count == 0 || y < b->y0) b->y0 = y;
    if (b->count == 0 || x > b->x1) b->x1 = x;
    if (b->count == 0 || y > b->y1) b->y1 = y;
    b->count++;
}

void cs2d_customtile_addcheck(cs2d_map_t *map, cs2d_customtile_batch_t *b, const cs2d_settile_t *t)
{
    if (!map || !map->tiles || !map->modifiers || !map->tile_data) return;
    if (t->tile > map->tile_count || t->x >= map->width || t->y >= map->height) return;
    size_t idx = (size_t)t->y * map->width + t->x;
    uint8_t *d = &map->tile_data[idx * 4];
    uint8_t mod = cs2d_customtile_modifier(t);
    uint8_t cur[5];

    int e = find_entry(map, t->x, t->y);
    if (e < 0) {
        /* Already like this: nothing to do */
        if (map->tiles[idx] == t->tile && map->modifiers[idx] == mod && map_data(mod, d, cur) && same_data(cur, t)) return;
        /* Tcustomtile.add: remember the map values, then change the tile */
        if (map->custom_tile_count == map->custom_tile_cap) {
            uint32_t cap = map->custom_tile_cap ? map->custom_tile_cap * 2 : 64;
            cs2d_customtile_t *grown = (cs2d_customtile_t *)realloc(map->custom_tiles, cap * sizeof(*grown));
            if (!grown) return;
            map->custom_tiles = grown;
            map->custom_tile_cap = cap;
        }
        cs2d_customtile_t *c = &map->custom_tiles[map->custom_tile_count++];
        c->x = t->x;
        c->y = t->y;
        c->old_tile = map->tiles[idx];
        c->old_mod = map->modifiers[idx];
        memcpy(c->old_data, d, 4);
        map->tiles[idx] = t->tile;
        map->modifiers[idx] = mod;
        write_data(d, mod, t);
        batch_add(b, t->x, t->y);
        return;
    }

    map->tiles[idx] = t->tile;
    map->modifiers[idx] = mod;
    write_data(d, mod, t);
    /* Back to the map values: the map's Tmap_d returns and the entry goes. Without one in the map, only the
     * default data (white, no overlay, no blend) counts as the same */
    cs2d_customtile_t *c = &map->custom_tiles[e];
    if (c->old_tile == t->tile && c->old_mod == mod) {
        bool restore;
        if (!map_data(c->old_mod, c->old_data, cur)) {
            restore = (t->r & t->g & t->b) == 0xff && t->overlay == 0 && t->blend == 0;
        } else {
            restore = same_data(cur, t);
        }
        if (restore) {
            memcpy(d, c->old_data, 4);
            map->custom_tiles[e] = map->custom_tiles[--map->custom_tile_count];
        }
    }
    batch_add(b, t->x, t->y);
}

bool cs2d_customtile_end(cs2d_map_t *map, cs2d_customtile_batch_t *b, int bounds[4])
{
    int n = b->count;
    b->count = 0;
    if (!map || n == 0 || !map->collision) return false;
    /* cs2d_update_mapdata(1, 1, 1, x0 - 1, y0 - 1, x1 + 1, y1 + 1), clamped to the map */
    int x0 = b->x0 - 1 < 0 ? 0 : b->x0 - 1;
    int y0 = b->y0 - 1 < 0 ? 0 : b->y0 - 1;
    int x1 = b->x1 + 1 >= (int)map->width ? (int)map->width - 1 : b->x1 + 1;
    int y1 = b->y1 + 1 >= (int)map->height ? (int)map->height - 1 : b->y1 + 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            size_t idx = (size_t)y * map->width + (size_t)x;
            uint8_t col = cs2d_map_mode_collision(map->tile_modes[map->tiles[idx]]);
            if (map->base_collision) {
                /* Cell prop of the tile, then the DynWall / Breakable / building overrides */
                map->base_collision[idx] = col;
                cs2d_ents_refresh_tile(map, x, y);
            } else {
                map->collision[idx] = col;
            }
        }
    }
    map->entity_solid_version++;   /* The light engine re-reads its tiles (SetTile FUN_14005c150) */
    bounds[0] = x0;
    bounds[1] = y0;
    bounds[2] = x1;
    bounds[3] = y1;
    return true;
}
