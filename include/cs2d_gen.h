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
    CS2D_GEN_LAYER_WEATHER = 3, /* Tp list 6 (screen space) */
    CS2D_GEN_LAYER_SMOKE   = 4  /* Smoke grenade puffs: over the light buffer, like rain (screenshots of CS2D.exe) */
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
    int                 noweather[CS2D_GEN_MAX_ENTS]; /* Info_NoWeather and Env_Room indices */
    int                 noweather_count;

    int                 quake[CS2D_GEN_MAX_ENTS]; /* Info_Quake indices */
    int                 quake_count;
    float               shake;     /* g_screenshake: camera shake in px, -1 per frame (cs2d_draw_world 0x140290310) */
    float               wind_x;    /* Info_Storm wind, px per frame (_DAT_140ddf028), added to Tp 3 / 14 */
    float               wind_y;    /* (_DAT_140ddf024) */
    float               flash;     /* White screen flash (0x140a17ff0, drawn by FUN_140131710) */
    int                 thunder;   /* Thunder claps so far (the sound is not played yet) */
    float               step_acc;  /* Seconds not yet simulated */
    int                 second_ms; /* time_ms of the last 1 s tick */
    bool                second_tick; /* This frame is a 1 s tick (DAT_140ddf540, main loop 1000 ms timer) */
    int                 time_ms;   /* Simulated g_time_ms */
    uint32_t            seed;
} cs2d_gen_t;

/* Binds to a map: collects its Gen_* and Info_NoWeather entities and drops every particle */
void cs2d_gen_init(cs2d_gen_t *g, const cs2d_map_t *map);

/*
 * Advances dt seconds in 60 Hz steps: each step spawns from the active (state 0) generators near the
 * camera (cam = top-left of the screen, half_w / half_h = half the screen) and moves every particle.
 */
/*
 * cs2d_draw_world (0x140290310): while the world is drawn the camera moves by Rand(-s, s) on each axis,
 * s = min(shake, 30) truncated; restored before the HUD. Returns false (0, 0) when nothing shakes.
 */
bool cs2d_gen_shake_offset(cs2d_gen_t *g, int *dx, int *dy);

void cs2d_gen_update(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                     float half_w, float half_h, float dt);

/* One 60 Hz step (cs2d_gen_update calls it; exposed for the tests) */
/* Smoke grenade puff (Tpro.draw_ground case 0x35, particles setting >= 3): Tp 0x20, which shares Tp 3's
 * creation and update; one per 100 ms tick while the grenade lies on the ground */
void cs2d_gen_smoke_grenade_puff(cs2d_gen_t *g, float x, float y);

/* Gas grenade puff (Tpro.draw_ground case 0x48, particles >= 3): a green Tp 3 per 100 ms tick */
void cs2d_gen_gas_grenade_puff(cs2d_gen_t *g, float x, float y);

/* Molotov fire (Tpro.draw_ground case 0x49, particles >= 3): one 60 Hz frame of flames, a Tp 14 half of
 * the time, up to 40 px from the centre */
void cs2d_gen_molotov_flame(cs2d_gen_t *g, float x, float y);

void cs2d_gen_step(cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y, float half_w, float half_h);

/* Whether the screen centre lies in an Info_NoWeather zone (ints[0] x ints[1] tiles from the entity) */
bool cs2d_gen_no_weather(const cs2d_gen_t *g, const cs2d_map_t *map, float cam_x, float cam_y,
                         float half_w, float half_h);

#endif /* CS2D_GEN_H */
