#ifndef CS2D_GEN_H
#define CS2D_GEN_H

#include "cs2d_types.h"

/*
 * Generator entities Gen_Particles (50), Gen_Sprites (51), Gen_Weather (52) and Gen_FX (53).
 *
 * CS2D.exe spawns their particles in Te.draw (0x140271ee0, pass p3 = 2, once per rendered frame) and
 * moves / draws them as Tp particles in Tp.draw (0x140263cc0). Tp.Create (0x14004ac20) sets the defaults.
 * Everything here runs in fixed 60 Hz steps: g_dt (0x140a1b090) is 1 per 60 Hz frame, and the per-frame
 * spawns of the original are paced the same way. Info_NoWeather (80) zones suppress rain and snow.
 *
 * Tp types used (= cs2d_gen_particle_t.typ):
 *    1 gfx/sprites/wave.bmp        Gen_FX waves, rain splashes on water (Tp list 1, under the players)
 *    3 incbindata/smokepuff0..3    Gen_FX smoke / steam / fire smoke (list 5)
 *   12 gfx/fragments.bmp           Gen_Sprites (list 5)
 *   14 gfx/sprites/flare3.bmp      Gen_FX fire (list 5)
 *   17 one pixel                   Gen_Particles (list 5)
 *   19 gfx/sprites/snowflake.bmp   Gen_Weather snow, screen space (list 6)
 *   20 gfx/sprites/raindrop.bmp    Gen_Weather rain, screen space (list 6)
 *   21 incbindata/smokepuff0..3    Gen_Weather fog / dust / smog (list 7)
 * Draw order after the overhead entities: list 5, list 7, list 6 (FUN_1402802xx), then the screen flash.
 */

#define CS2D_GEN_MAX       8192
#define CS2D_GEN_MAX_ENTS  1024 /* fun_roleplay: 384 Gen_*, 179 Info_NoWeather */

typedef enum {
    CS2D_GEN_LAYER_FLOOR   = 0, /* Tp lists 1 / 2: drawn on the floor before the players */
    CS2D_GEN_LAYER_TOP     = 1, /* Tp list 5 */
    CS2D_GEN_LAYER_FOG     = 2, /* Tp list 7 */
    CS2D_GEN_LAYER_WEATHER = 3  /* Tp list 6 (screen space) */
} cs2d_gen_layer_t;

typedef struct {
    uint8_t typ;      /* Tp type (see above) */
    uint8_t layer;    /* cs2d_gen_layer_t */
    float   x, y;     /* World px; types 19 / 20: offset from the screen centre at depth 30 */
    float   fx, fy;   /* Speed (px per frame); type 19: heading (deg) and its turn rate */
    float   sx, sy;   /* Scale; sy: type 3 alpha loss per frame, types 19 / 20 depth */
    float   rot, rots;
    float   a;        /* Alpha (may exceed 1; drawing clamps) */
    float   age;
    int     frame;
    float   r, g, b;  /* 0..255 */
} cs2d_gen_particle_t;

typedef struct cs2d_gen_s {
    cs2d_gen_particle_t p[CS2D_GEN_MAX];
    int                 count;

    int                 ents[CS2D_GEN_MAX_ENTS];   /* Gen_* entity indices of the map */
    int                 ent_count;
    int                 counter[CS2D_GEN_MAX_ENTS]; /* Te.ints[9]: thunder countdown / Gen_FX last spawn (ms) */
    int                 noweather[CS2D_GEN_MAX_ENTS]; /* Info_NoWeather indices */
    int                 noweather_count;

    float               flash;     /* White screen flash (0x140a17ff0, drawn by FUN_140131710) */
    int                 thunder;   /* Thunder claps so far (the sound is not played yet) */
    float               step_acc;  /* Seconds not yet simulated */
    int                 time_ms;   /* Simulated g_time_ms */
    uint32_t            seed;
} cs2d_gen_t;

/* Binds to a map: collects its Gen_* and Info_NoWeather entities and drops every particle */
void cs2d_gen_init(cs2d_gen_t *g, const cs2d_map_t *map);

/*
 * Advances dt seconds in 60 Hz steps: each step spawns from the active (state 0) generators near the
 * camera (cam = top-left of the screen, half_w / half_h = half the screen) and moves every particle.
 */
void cs2d_gen_update(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                     float half_w, float half_h, float dt);

/* One 60 Hz step (cs2d_gen_update calls it; exposed for the tests) */
void cs2d_gen_step(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y, float half_w, float half_h);

/* Whether the screen centre lies in an Info_NoWeather zone (ints[0] x ints[1] tiles from the entity) */
bool cs2d_gen_no_weather(const cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                         float half_w, float half_h);

#endif /* CS2D_GEN_H */
