#ifndef CS2D_FX_H
#define CS2D_FX_H

#include "cs2d_types.h"

/*
 * Client side visual effects of Env_Breakable (recorded from CS2D.exe frame by frame):
 *   hit   - a few material fragments (gfx/fragments.bmp, 16 frames of 32x32) burst from the impact
 *   break - the tile image splits into its four 16x16 quarters that drift outward, spin and fade
 *           while fragments scatter; explosive breakables add an explosion (gfx/explosion.png,
 *           25 frames of 64x64)
 * Effects are cosmetic: the same calls run offline and when the server reports a break.
 */

#define CS2D_FX_MAX 512

typedef enum {
    CS2D_FX_FRAGMENT  = 0, /* fragments.bmp frame */
    CS2D_FX_TILE_PART = 1, /* Quarter of a map tile */
    CS2D_FX_EXPLOSION = 2  /* explosion.png animation */
} cs2d_fx_kind_t;

typedef struct {
    cs2d_fx_kind_t kind;
    float          x, y;       /* World position (center) */
    float          vx, vy;     /* px/s */
    float          rot, vrot;  /* Degrees, degrees/s */
    float          life, max_life;
    float          size;       /* Drawn size in px */
    float          r, g, b;
    int            frame;      /* Fragment frame / tile index */
    int            quarter;    /* Tile part: 0 TL, 1 TR, 2 BL, 3 BR */
} cs2d_fx_particle_t;

typedef struct cs2d_fx_s {
    cs2d_fx_particle_t p[CS2D_FX_MAX];
    int                count;
    uint32_t           seed;
} cs2d_fx_t;

void cs2d_fx_clear(cs2d_fx_t *fx);

/* Bullet hit an Env_Breakable of material (ints[2]: 0 wood, 1 stone, 2 metal, 3 flesh, 4 glass, 5 nothing)
 * at (x, y); angle_deg is the shot direction (fragments bounce back towards the shooter) */
void cs2d_fx_breakable_hit(cs2d_fx_t *fx, float x, float y, int material, float angle_deg);

/* Env_Breakable destroyed: tile parts of its tile image plus fragments, and an explosion if it explodes */
void cs2d_fx_breakable_break(cs2d_fx_t *fx, const cs2d_entity_t *e);

/* Explosion animation centered at (x, y) */
void cs2d_fx_explosion(cs2d_fx_t *fx, float x, float y, float range);

void cs2d_fx_update(cs2d_fx_t *fx, float dt);

#endif /* CS2D_FX_H */
