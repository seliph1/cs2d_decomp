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
    CS2D_FX_KNIFESLASH = 7,  /* gfx/knifeslash.bmp melee swing arc (Tp type 13, 0x14004b4e0 & 0x140265220) */
    CS2D_FX_SMOKE      = 8,  /* Smoke / gas puff */
    CS2D_FX_FIRE       = 9,  /* Fire / flame particle */
    CS2D_FX_SPARKLE    = 10, /* Bright flare sparkle */
    CS2D_FX_BLOOD      = 11, /* Gut bomb blood drop / splat */
    CS2D_FX_SNOW       = 12, /* Snowball snow puff */
    CS2D_FX_LASER      = 13, /* Laser beam: Tp.CreateLine width 3 in the laser colour */
    CS2D_FX_FLARE      = 14  /* Tp type 2: flare3 additive (muzzle flare, C4 beep, flashbang) */
} cs2d_fx_kind_t;

typedef struct {
    cs2d_fx_kind_t kind;
    float          x, y;       /* World position (center) */
    float          x2, y2;     /* Tracer end */
    float          vx, vy;     /* px/s */
    float          drag;       /* Speed lost per second as a fraction (0: debris default 2.5) */
    float          rot, vrot;  /* Degrees, degrees/s */
    float          life, max_life;
    float          size;       /* Drawn size in px */
    float          r, g, b;
    int            frame;      /* Fragment frame / tile index */
    int            quarter;    /* Tile part: 0 TL, 1 TR, 2 BL, 3 BR */
    float          alpha;      /* Flare: start alpha (Tp.a), drawn x life / max_life */
    bool           light_once; /* Flare made while the light engine runs: one frame of light sprite, then freed */
    bool           queued;     /* light_once flare already handed to the light engine (cs2d_render_fx) */
} cs2d_fx_particle_t;

#define CS2D_FX_MAX_SHELLS 64

typedef struct cs2d_fx_s {
    cs2d_fx_particle_t p[CS2D_FX_MAX];
    int                count;
    cs2d_fx_particle_t shells[CS2D_FX_MAX_SHELLS]; /* Ring buffer: cases stay on the floor */
    int                shell_next;
    uint32_t           seed;
    int                explosion_count;   /* Tp.explosion_count: + 5 per explosion, - 1 per Tp.draw call (8 a frame) */
} cs2d_fx_t;

void cs2d_fx_clear(cs2d_fx_t *fx);

/* Bullet hit an Env_Breakable of material (ints[2]: 0 wood, 1 stone, 2 metal, 3 flesh, 4 glass, 5 nothing)
 * at (x, y); angle_deg is the shot direction (fragments bounce back towards the shooter) */
void cs2d_fx_breakable_hit(cs2d_fx_t *fx, float x, float y, int material, float angle_deg);

/* Bullet hit a building at (x, y) (Tshoot.Shoot -> Tp.breakable material 1): gray fragments thrown back
 * towards the shooter and onwards along the shot */
void cs2d_fx_building_hit(cs2d_fx_t *fx, float x, float y, float angle_deg);

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

/* The bullet trail of one shot from the muzzle (mx, my) to (ex, ey), with impact sparks when hit_wall: what
 * cs2d_fx_shot draws besides the flash and the case; each shotgun pellet draws one */
void cs2d_fx_tracer(cs2d_fx_t *fx, float mx, float my, float angle_deg, float ex, float ey, bool hit_wall);

/*
 * Laser shot (Tshoot.Shoot style 2, 0x140207092 / 0x14020871a): a line from the muzzle (mx, my) to (ex, ey), width 3,
 * alpha Rnd(0.4, 0.6) fading by 0.03 per 60 Hz frame, in the colour of the laser mode (0 green, 1 red, 2 yellow,
 * 3 blue), a few glowing particles of that colour along the beam and sparkles at the end
 */
void cs2d_fx_laser(cs2d_fx_t *fx, float mx, float my, float ex, float ey, int mode);

/*
 * Flamethrower shot (Tshoot.Shoot style 3, 0x140207144): a fire particle every 5 px of the path past the first 20 px,
 * moving at 1 px per frame within 50 deg of the aim, blue near the nozzle, then purple, then orange
 */
void cs2d_fx_flame(cs2d_fx_t *fx, float px, float py, float angle_deg, float dist);

/* Portal Gun shot (FUN_1401fc9f0, Tp 0x1d sx 0.5): a flash 24 px ahead, orange (200, 100, 0) for mode 1, blue
 * (50, 100, 255) for mode 2 */
void cs2d_fx_portal_flash(cs2d_fx_t *fx, float px, float py, float angle_deg, int mode);
/* Tp 0x1d at (x, y), scale sx, portal colour of mode (1 orange, 2 blue): a portal closing (Tdo.kill, sx 0.8) */
void cs2d_fx_portal_glow(cs2d_fx_t *fx, float x, float y, float sx, int mode);

/*
 * Melee weapon slash arc (CS2D.exe Tpl.slash 0x14018cf63 -> Tp.create type 13 at 0x14004b4e0,
 * drawn in Tp.drawAll at 0x140265220): gfx/knifeslash.bmp centered at (px, py), rotated by
 * angle_deg, additive blend (LIGHTBLEND = 4), alpha 1.0 fading by 0.07 per 60 Hz frame (4.2/s).
 */
void cs2d_fx_knifeslash(cs2d_fx_t *fx, float px, float py, float angle_deg);

/* Projectile & field visual effects */
void cs2d_fx_smoke_puff(cs2d_fx_t *fx, float x, float y, float r, float g, float b, float size, float speed);
void cs2d_fx_fire_puff(cs2d_fx_t *fx, float x, float y, float size);
void cs2d_fx_sparkle(cs2d_fx_t *fx, float x, float y, float r, float g, float b);
void cs2d_fx_blood_splat(cs2d_fx_t *fx, float x, float y, int count);
void cs2d_fx_snow_puff(cs2d_fx_t *fx, float x, float y, int count);

/*
 * Tp type 2 (Tp.Create 0x14004ac20 case 2, Tp.draw 0x140263cc0 case 2): gfx/sprites/flare3.bmp, additive, rotation rot,
 * scale sx, color (r, g, b) 0..1, alpha a fading by fade per 60 Hz frame. While the light engine runs it is not drawn
 * in the world: for one frame it is a light sprite (TLightingSprite.DrawSprite flare3 in its color x a), then freed.
 */
void cs2d_fx_flare(cs2d_fx_t *fx, float x, float y, float r, float g, float b, float a, float sx, float rot, float fade);
/* Tshoot.Shoot's muzzle flare (first pellet of style 0 / 4): Tp 2 at the muzzle + Rand(-2, 2) with the Tp.Create
 * colors Rand(230, 255), Rand(150, 230), Rand(0, 20), fade 0.3, a Rnd(0.7, 0.9), sx Rnd(0.5, 1) (silenced: Rnd(0.1, 0.2)) */
void cs2d_fx_muzzle_flare(cs2d_fx_t *fx, float mx, float my, float angle_deg, bool silenced);

/*
 * Tshoot.Shoot's muzzle effects of a style 0 / 4 shot's first pellet at the muzzle (mx, my): the Tp 2 flare, then with
 * particles > 3 and the light engine on Tp.CreateLight at the muzzle + Rand(-2, 2), (255, 255, 0) raised to (255, 255,
 * 35), radius 100 shrinking 25 per frame, and ChangeAlpha(0.7) (0x140205d9d, 0x140bd6cc0). Silenced shots (style 4)
 * get a small flare and no light. Players, turrets (Tdo.shoot weapon 0xfd) and NPC soldiers (0xf8) alike.
 */
void cs2d_fx_muzzle_light(cs2d_fx_t *fx, float mx, float my, float angle_deg, bool silenced);
/*
 * Tshoot.Shoot style 2 (laser, 0x140208735..0x1402057e9): a light at the muzzle + Rand(-2, 2) in the color of the beam's
 * last glow particle (Tp 0xe: mode 0 (0..50, 100..255, 0), 1 (100..255, 0..75, 0), 2 (100..255, 100..255, 0),
 * 3 (0, 0..75, 100..255)), channels raised to 35, radius 100 shrinking 5 per frame
 */
void cs2d_fx_laser_light(cs2d_fx_t *fx, float mx, float my, int mode);

void cs2d_fx_update(cs2d_fx_t *fx, float dt);

#endif /* CS2D_FX_H */
