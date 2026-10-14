#ifndef CS2D_PROJECTILE_H
#define CS2D_PROJECTILE_H

#include "cs2d_types.h"

#define CS2D_MAX_PROJECTILES 128
#define CS2D_MAX_FIELDS      64

struct cs2d_client;

/* State of a projectile: in flight or settled on the ground */
typedef enum {
    CS2D_PROJ_STATE_INACTIVE = 0,
    CS2D_PROJ_STATE_FLYING   = 1,
    CS2D_PROJ_STATE_GROUND   = 2
} cs2d_proj_state_t;

/*
 * Projectile object: represents all throwable items (grenades, flare, airstrike, gutbomb, etc.)
 * as well as fired missiles (rocket, RPG) in the engine and network.
 */
typedef struct cs2d_projectile_s {
    bool              active;
    uint8_t           id;            /* Unique network id (0..255) */
    uint8_t           player;        /* ID of player who instantiated projectile (0 if not player) */
    uint8_t           type;          /* Projectile type = Item ID (47, 48, 49, 51, 52, 53, 54, 72, 73, 75, 76, 86, etc.) */
    float             x, y;          /* Current position in pixels */
    float             flydist;       /* Remaining distance in pixels that the projectile will travel */
    float             start_flydist; /* Initial fly distance */
    float             dir;           /* Direction angle in degrees (0 = north, clockwise) */
    float             speed;         /* Travel speed in px/s */
    float             lifetime;      /* Lifetime in seconds the projectile lives on the map */
    float             max_lifetime;  /* Initial lifetime */
    float             rot;           /* Current visual rotation angle in degrees */
    float             rot_speed;     /* Spin speed in deg/s */
    cs2d_proj_state_t state;         /* Flying vs ground */
    float             effect_timer;  /* Interval timer for ground particle emission / periodic effects */
} cs2d_projectile_t;

/* Field types for persistent area effects (smoke cloud, poison gas, molotov fire) */
typedef enum {
    CS2D_FIELD_NONE  = 0,
    CS2D_FIELD_FIRE  = 1,   /* Molotov fire area (damages players over time) */
    CS2D_FIELD_GAS   = 2,   /* Toxic gas cloud (damages players unless wearing gas mask) */
    CS2D_FIELD_SMOKE = 3    /* Smoke grenade cloud (blocks line of sight / provides cover) */
} cs2d_field_type_t;

/* Smoke cloud fade-out after the 30 s: Tp 0x1b a -= 0.01 per 60 Hz frame (Tp.draw 0x140263cc0) */
#define CS2D_SMOKE_FADE_SECONDS (100.0f / 60.0f)

/* Persistent area effect object */
typedef struct cs2d_field_s {
    bool              active;
    uint8_t           id;
    cs2d_field_type_t type;
    uint8_t           player;        /* Owner player id */
    float             x, y;          /* Center in pixels */
    float             radius;        /* Radius in pixels */
    float             lifetime;      /* Remaining lifetime in seconds */
    float             max_lifetime;  /* Initial lifetime */
    float             tick_timer;    /* Damage / particle tick timer */
    float             rot, rots;     /* Smoke cloud sprite angle (deg) and spin (deg per 60 Hz frame) */
    float             shade;         /* Smoke cloud grey level 0..1 */
} cs2d_field_t;

/* World container holding all active projectiles and fields */
typedef struct cs2d_projectile_world_s {
    cs2d_projectile_t projectiles[CS2D_MAX_PROJECTILES];
    cs2d_field_t      fields[CS2D_MAX_FIELDS];
    uint8_t           next_id;
    uint8_t           next_field_id;
} cs2d_projectile_world_t;

/* Lifecycle */
void cs2d_projectile_init(cs2d_projectile_world_t *pw);
void cs2d_projectile_clear(cs2d_projectile_world_t *pw);

/* Query weapon projectile classification */
bool cs2d_projectile_is_throwable(uint8_t item_id);
bool cs2d_projectile_is_rocket(uint8_t item_id);
bool cs2d_projectile_is_impact(uint8_t item_id);
void cs2d_projectile_get_default_properties(uint8_t type, float *out_speed, float *out_lifetime, float *out_flydist);

/* Spawn projectile */
cs2d_projectile_t *cs2d_projectile_spawn(
    cs2d_projectile_world_t *pw,
    uint8_t player,
    uint8_t type,
    float x, float y,
    float dir,
    float flydist,
    float speed,
    float lifetime
);

cs2d_projectile_t *cs2d_projectile_spawn_with_id(
    cs2d_projectile_world_t *pw,
    uint8_t id,
    uint8_t player,
    uint8_t type,
    float x, float y,
    float dir,
    float flydist,
    float speed,
    float lifetime
);

/* Find projectile by ID and player */
cs2d_projectile_t *cs2d_projectile_get(cs2d_projectile_world_t *pw, uint8_t id, uint8_t player);

/* Remove / detonate */
void cs2d_projectile_kill(cs2d_projectile_world_t *pw, uint8_t id, uint8_t player);
void cs2d_projectile_remove(struct cs2d_client *client, uint8_t id, uint8_t player);

/* Spawn persistent area field */
cs2d_field_t *cs2d_field_spawn(
    cs2d_projectile_world_t *pw,
    cs2d_field_type_t type,
    uint8_t player,
    float x, float y,
    float radius,
    float lifetime
);

/* Simulation update for all projectiles and fields */
void cs2d_projectile_update(struct cs2d_client *client, float dt);

/* Check if a world position is covered by smoke */
bool cs2d_projectile_smoke_cover(const cs2d_projectile_world_t *pw, float x, float y);

#endif /* CS2D_PROJECTILE_H */

