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
    CS2D_FX_EXPLOSION = 2, /* explosion.png animation */
    CS2D_FX_TRACER    = 3, /* Bullet path: line from the muzzle to the impact */
    CS2D_FX_GLOWDOT   = 4, /* incbindata/glowdot.bmp left along the bullet path / impact sparks */
    CS2D_FX_MUZZLE     = 5, /* incbindata/flare_x.png at the muzzle (+ yellow light around the shooter) */
    CS2D_FX_SHELL      = 6, /* gfx/shells.bmp case flying out of the weapon (then lies on the floor) */
    CS2D_FX_KNIFESLASH = 7  /* gfx/knifeslash.bmp melee swing arc (Tp type 13, 0x14004b4e0 & 0x140265220) */
} cs2d_fx_kind_t;

typedef struct {
    cs2d_fx_kind_t kind;
    float          x, y;       /* World position (center) */
    float          x2, y2;     /* Tracer end */
    float          vx, vy;     /* px/s */
    float          rot, vrot;  /* Degrees, degrees/s */
    float          life, max_life;
    float          size;       /* Drawn size in px */
    float          r, g, b;
    int            frame;      /* Fragment frame / tile index */
    int            quarter;    /* Tile part: 0 TL, 1 TR, 2 BL, 3 BR */
} cs2d_fx_particle_t;

#define CS2D_FX_MAX_SHELLS 64

typedef struct cs2d_fx_s {
    cs2d_fx_particle_t p[CS2D_FX_MAX];
    int                count;
    cs2d_fx_particle_t shells[CS2D_FX_MAX_SHELLS]; /* Ring buffer: cases stay on the floor */
    int                shell_next;
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

/*
 * A firearm shot (CS2D.exe 0x140148f40 draws the muzzle flashes: flare_x scaled by 0.1 * 5,
 * stretched along the aim, fading by 0.4 per 60 Hz frame). (px, py) is the shooter, angle the aim
 * (0 = north, clockwise), (ex, ey) where the bullet stopped; hit_wall adds impact sparks.
 * shell_frame selects the gfx/shells.bmp case (-1: none).
 */
void cs2d_fx_shot(cs2d_fx_t *fx, float px, float py, float angle_deg, float muzzle_dist,
                  float ex, float ey, bool hit_wall, int shell_frame);

/*
 * Melee weapon slash arc (CS2D.exe Tpl.slash 0x14018cf63 -> Tp.create type 13 at 0x14004b4e0,
 * drawn in Tp.drawAll at 0x140265220): gfx/knifeslash.bmp centered at (px, py), rotated by
 * angle_deg, additive blend (LIGHTBLEND = 4), alpha 1.0 fading by 0.07 per 60 Hz frame (4.2/s).
 */
void cs2d_fx_knifeslash(cs2d_fx_t *fx, float px, float py, float angle_deg);

void cs2d_fx_update(cs2d_fx_t *fx, float dt);

#endif /* CS2D_FX_H */
