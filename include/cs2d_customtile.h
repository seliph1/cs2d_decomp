#ifndef CS2D_CUSTOMTILE_H
#define CS2D_CUSTOMTILE_H

#include <stdbool.h>
#include <stdint.h>
#include "cs2d_types.h"

/*
 * settile: tiles changed at runtime (class Tcustomtile in CS2D.exe, 0x1400aeaf0 addcheck .. 0x1401443e0 endbatch).
 *
 * A change sets the tile frame and replaces the modifier byte (cs2d_map_t.modifiers) and the tile's Tmap_d:
 *   modifier = rotation (1..3, else 0) + 0x40 if blend != 0 + 0x80 unless r = g = b = 255 (no brightness bits);
 *   Tmap_d   = { frame = blend, r, g, b, overlay }.
 * The renderer then applies the color only for (modifier & 0xc0) == 0x80 and the blending only for 0x40
 * (TileProp.Get 0x140010450, Ttileblend.updatetile 0x1400ae560): with both, neither is drawn.
 * Tcustomtile.list keeps the map values of every changed tile; setting a tile back to them drops its entry.
 * A batch of changes then runs cs2d_update_mapdata (0x1401413b0) on their bounds plus one tile: cell props
 * (collision), the light engine's tiles, the wall shadows and the tile blending.
 *
 * Sources: [62] settile (FUN_140195b80) and the join snapshot section [252,7,9] (FUN_1402ce7c0 case 6), both
 * received only when the client is not the server; the server side is FUN_1401957f0 (apply + broadcast) and the
 * console command FUN_140195d00.
 */

/* One settile change, in the order of the [62] message */
typedef struct {
    uint16_t x, y;
    uint8_t  tile;
    uint8_t  rot;       /* 0..3 x 90 degrees */
    uint8_t  r, g, b;   /* 255, 255, 255 = no color */
    uint8_t  overlay;   /* Tmap_d.overlay (stored, never drawn by CS2D.exe) */
    uint8_t  blend;     /* Tmap_d.frame: blendmap frame, direction = blend & 7 (cs2d_settile_blend) */
} cs2d_settile_t;

/* Tcustomtile.batch: the tiles changed since cs2d_customtile_begin */
typedef struct {
    int count;
    int x0, y0, x1, y1;
} cs2d_customtile_batch_t;

/* The defaults of a short [62] / a command without the optional arguments */
void cs2d_settile_defaults(cs2d_settile_t *t, int x, int y, int tile);

/* FUN_140195d00: blend byte of the command's [blenddir] [blendtype]: dir (top 0, topright 1, right 2,
 * bottomright 3, bottom 4, bottomleft 5, left 6, topleft 7, else 0) + (type - 1) * 8; 0 without a direction
 * or with type < 1 (so "top 1" blends nothing either) */
uint8_t cs2d_settile_blend(const char *dir, const char *type);

/* The modifier byte of a change (Tcustomtile.addcheck) */
uint8_t cs2d_customtile_modifier(const cs2d_settile_t *t);

/* Parses [62, x u16, y u16, tile u8 (, rot, r, g, b, overlay, blend)] at buf[pos] (the opcode): the 6 optional bytes
 * are read when at least 6 bytes follow the tile (FUN_140195b80). Bytes used, or -1 when the message is cut */
int  cs2d_settile_read(const uint8_t *buf, int len, int pos, cs2d_settile_t *out);

/* Tcustomtile.list.Clear (map load, snapshot section 9): forgets the entries, keeps the tiles */
void cs2d_customtile_clear(cs2d_map_t *map);
/* Tcustomtile.get: the entry of a changed tile, NULL if the tile has its map values */
const cs2d_customtile_t *cs2d_customtile_get(const cs2d_map_t *map, int x, int y);

/* Tcustomtile.startbatch / addcheck / endbatch. addcheck ignores a tile frame above the tileset's last one or a
 * position outside the map. end updates the collision of the bounds plus one tile and bumps
 * map->entity_solid_version (light engine); it returns false when nothing changed, else the updated bounds
 * (inclusive) for cs2d_render_tiles_changed */
void cs2d_customtile_begin(cs2d_customtile_batch_t *b);
void cs2d_customtile_addcheck(cs2d_map_t *map, cs2d_customtile_batch_t *b, const cs2d_settile_t *t);
bool cs2d_customtile_end(cs2d_map_t *map, cs2d_customtile_batch_t *b, int bounds[4]);

#endif /* CS2D_CUSTOMTILE_H */
