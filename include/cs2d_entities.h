#ifndef CS2D_ENTITIES_H
#define CS2D_ENTITIES_H

#include "cs2d_types.h"

/*
 * Map entity logic: Name / Trigger wiring and per-type reactions.
 *
 * Every entity has a Name and a Trigger field. Triggering a name activates every entity
 * with that name; the Trigger field may list several names separated by commas.
 * Activation either toggles the entity state (map->entity_state: 0 = active/visible/closed,
 * 1 = inactive/hidden/open) or, for logic entities, forwards the signal:
 *   Trigger_Delay  forwards its trigger after ints[1] ms (or ints[0] s)
 *   Trigger_Once   forwards its trigger only once per round
 *   Trigger_If     forwards its trigger when its condition holds
 * Effects that need the game (messages, sounds, items, explosions, game actions) are queued
 * as events for the caller to perform.
 *
 * Players fire Trigger_Use with the use key, Trigger_Move / Func_Teleport by walking onto
 * them; Trigger_Start entities fire at round start. Only state-0 (active) sources fire.
 */

#define CS2D_ENTS_MAX_DELAYS 256
#define CS2D_ENTS_MAX_EVENTS 64

typedef enum {
    CS2D_ENT_EVENT_STATE   = 0, /* Entity state toggled (render / collision already updated) */
    CS2D_ENT_EVENT_MESSAGE = 1, /* Func_Message triggered: show str_settings[0] */
    CS2D_ENT_EVENT_SOUND   = 2, /* Env_Sound triggered: play str_settings[0] */
    CS2D_ENT_EVENT_ITEM    = 3, /* Env_Item triggered: spawn item ints[0] */
    CS2D_ENT_EVENT_EXPLODE = 4, /* Env_Explode triggered: explosion range ints[0], power ints[1] */
    CS2D_ENT_EVENT_ACTION  = 5, /* Func_GameAction triggered: action ints[0] */
    CS2D_ENT_EVENT_BREAK   = 6  /* Env_Breakable destroyed: explodes when ints[3] (range ints[4] px, power ints[5]) */
} cs2d_ent_event_type_t;

typedef struct {
    cs2d_ent_event_type_t type;
    int                   entity;
} cs2d_ent_event_t;

typedef struct {
    int   entity;     /* Trigger_Delay index */
    float remaining;  /* Seconds until it forwards its trigger */
} cs2d_ent_delay_t;

typedef struct {
    cs2d_map_t       *map;

    /* Entity indices sorted by name (entities with an empty name are left out) */
    int               name_index[CS2D_MAX_ENTITIES];
    int               name_count;

    uint8_t           once_fired[CS2D_MAX_ENTITIES];
    int               health[CS2D_MAX_ENTITIES];       /* Env_Breakable health left (ints[1] at round start) */
    int               breakables[CS2D_MAX_ENTITIES];   /* Env_Breakable indices (bullet traces) */
    int               breakable_count;
    float             use_cooldown[CS2D_MAX_ENTITIES]; /* Trigger_Use re-use delay remaining (s) */

    cs2d_ent_delay_t  delays[CS2D_ENTS_MAX_DELAYS];
    int               delay_count;

    cs2d_ent_event_t  events[CS2D_ENTS_MAX_EVENTS];
    int               event_count;

    int               depth;        /* Recursion guard for trigger chains */
    uint32_t          activations;  /* Total activations (tests / diagnostics) */
} cs2d_entity_world_t;

/* Binds the world to a loaded map: builds the name index, base collision and resets all state */
void cs2d_ents_init(cs2d_entity_world_t *w, cs2d_map_t *map);

/* Round start: every entity back to state 0, Trigger_Once re-armed, then all Trigger_Start fire */
void cs2d_ents_round_start(cs2d_entity_world_t *w);

/* Activates every entity named in list ("a,b,c") */
void cs2d_ents_trigger(cs2d_entity_world_t *w, const char *name_list);

/* Activates one entity (toggle or forward, depending on its type) */
void cs2d_ents_activate(cs2d_entity_world_t *w, int index);

/* Recomputes one tile's collision (tiles, then DynWall / Breakable, then the building overlay) */
void cs2d_ents_refresh_tile(cs2d_map_t *map, int tx, int ty);

/* Sets an entity state directly (server snapshot / events), updating collision */
void cs2d_ents_set_state(cs2d_entity_world_t *w, int index, uint8_t state);

/* Advances Trigger_Delay timers and use cooldowns */
void cs2d_ents_update(cs2d_entity_world_t *w, float dt);

/*
 * Use key target (FUN_14014f820): the tiles 0, 16 and 32 px ahead of the player along its aim
 * (x + sin(angle) * d, y - cos(angle) * d) are probed for an active Trigger_Use the team may use.
 * Returns the entity index or -1.
 */
int  cs2d_ents_find_use_target(const cs2d_entity_world_t *w, float px, float py, float angle_deg, cs2d_team_t team);

/* Use key (offline): fires the use target's trigger, honouring its re-use delay. Returns its index or -1. */
int  cs2d_ents_try_use(cs2d_entity_world_t *w, float px, float py, float angle_deg, cs2d_team_t team);

/* Player entered tile (tx, ty): fires Trigger_Move there. Returns a Func_Teleport index or -1. */
int  cs2d_ents_enter_tile(cs2d_entity_world_t *w, int tx, int ty, cs2d_team_t team);

/* Env_Breakable destroyed: switches it off, queues CS2D_ENT_EVENT_BREAK and fires its trigger */
void cs2d_ents_break(cs2d_entity_world_t *w, int index);

/* Damages an active Env_Breakable; it breaks when its health drops to 0. Returns true when it broke. */
bool cs2d_ents_damage_breakable(cs2d_entity_world_t *w, int index, int damage);

/* Active Env_Breakable on tile (tx, ty) or -1 */
int  cs2d_ents_breakable_at(const cs2d_entity_world_t *w, int tx, int ty);

/*
 * Bullet path: walks from (x, y) along angle (0 = north, clockwise) for up to reach px and stops
 * at the first active Env_Breakable (its tile may contain the start point) or wall tile (collision 1;
 * obstacles let bullets through). Returns the breakable index or -1; end_x / end_y get the stop point.
 */
int  cs2d_ents_trace_shot(const cs2d_entity_world_t *w, float x, float y, float angle_deg, float reach,
                          float *end_x, float *end_y);

/*
 * Explosion of power at (x, y): every active Env_Breakable whose tile center lies within range px
 * takes power * (1 - distance / range) damage (chains through CS2D_ENT_EVENT_BREAK).
 */
void cs2d_ents_explode(cs2d_entity_world_t *w, float x, float y, float range, int power);

/* Lookup helpers */
int  cs2d_ents_find_by_name(const cs2d_entity_world_t *w, const char *name, int *out, int max_out);
int  cs2d_ents_find_at(const cs2d_map_t *map, int tx, int ty, cs2d_entity_type_t type);

/* Pops the oldest pending effect event; returns false when none are left */
bool cs2d_ents_pop_event(cs2d_entity_world_t *w, cs2d_ent_event_t *out);


#endif /* CS2D_ENTITIES_H */
