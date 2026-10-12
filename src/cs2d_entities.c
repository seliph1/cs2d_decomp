#include "cs2d_entities.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ENTS_MAX_DEPTH 32

/* ---------------------------------------------------------------------------
 * Collision
 * --------------------------------------------------------------------------- */

/* Collision value of a tile mode (same mapping as the map loader) */
static uint8_t mode_collision(uint8_t mode)
{
    if (mode == CS2D_TILE_WALL || mode == CS2D_TILE_WALL_NOSHADOW || mode == CS2D_TILE_WALL_FLOORLEVEL) return 1;
    if (mode == CS2D_TILE_OBSTACLE || mode == CS2D_TILE_OBSTACLE_NOSHADOW) return 2;
    if (mode >= CS2D_TILE_DEADLY_NORMAL && mode <= CS2D_TILE_DEADLY_ABYSS) return 3;
    return 0;
}

/* Collision an active (state 0) DynWall / Breakable sets on its tile */
static uint8_t entity_collision(const cs2d_map_t *map, const cs2d_entity_t *e)
{
    if (e->type == CS2D_ENT_FUNC_DYNWALL) {
        switch (e->int_settings[1]) {
            case 0: case 2: return 1; /* wall (with / without shadow) */
            case 1: case 3: return 2; /* obstacle (with / without shadow) */
            default: {               /* 4 = floor + tile behaviour */
                int tile = e->int_settings[0];
                return (tile >= 0 && tile < 256) ? mode_collision(map->tile_modes[tile]) : 0;
            }
        }
    }
    if (e->type == CS2D_ENT_ENV_BREAKABLE) {
        return (e->int_settings[6] == 3) ? 2 : 1;
    }
    return 0;
}

/* Recomputes the collision of one tile: base tile collision overridden by active DynWall/Breakable, then by
 * a building (cs2d_update_mapdata writes the Tdo cell props last) */
static void refresh_tile_collision(cs2d_map_t *map, int tx, int ty)
{
    if (!map->collision || !map->base_collision) return;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return;
    size_t idx = (size_t)ty * map->width + (size_t)tx;
    uint8_t col = map->base_collision[idx];
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x != tx || e->y != ty || map->entity_state[i] != 0) continue;
        if (e->type == CS2D_ENT_FUNC_DYNWALL || e->type == CS2D_ENT_ENV_BREAKABLE) {
            col = entity_collision(map, e);
        }
    }
    if (map->object_collision && map->object_collision[idx]) col = map->object_collision[idx];
    map->collision[idx] = col;
}

void cs2d_ents_refresh_tile(cs2d_map_t *map, int tx, int ty)
{
    if (map) refresh_tile_collision(map, tx, ty);
}

/* ---------------------------------------------------------------------------
 * Name index
 * --------------------------------------------------------------------------- */

static const cs2d_map_t *s_sort_map;

static int cmp_entity_name(const void *a, const void *b)
{
    const cs2d_entity_t *ea = &s_sort_map->entities[*(const int *)a];
    const cs2d_entity_t *eb = &s_sort_map->entities[*(const int *)b];
    int c = strcmp(ea->name, eb->name);
    return c ? c : (*(const int *)a - *(const int *)b); /* Stable: map order within a name */
}

static void build_name_index(cs2d_entity_world_t *w)
{
    w->name_count = 0;
    for (uint32_t i = 0; i < w->map->entity_count; i++) {
        if (w->map->entities[i].name[0] != '\0') w->name_index[w->name_count++] = (int)i;
    }
    s_sort_map = w->map;
    qsort(w->name_index, (size_t)w->name_count, sizeof(int), cmp_entity_name);
}

int cs2d_ents_find_by_name(const cs2d_entity_world_t *w, const char *name, int *out, int max_out)
{
    if (!w || !name || !name[0]) return 0;
    /* Lower bound of name in the sorted index */
    int lo = 0, hi = w->name_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(w->map->entities[w->name_index[mid]].name, name) < 0) lo = mid + 1;
        else hi = mid;
    }
    int n = 0;
    for (int i = lo; i < w->name_count && strcmp(w->map->entities[w->name_index[i]].name, name) == 0; i++) {
        if (out && n < max_out) out[n] = w->name_index[i];
        n++;
    }
    return n;
}

int cs2d_ents_find_at(const cs2d_map_t *map, int tx, int ty, cs2d_entity_type_t type)
{
    if (!map) return -1;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x == tx && e->y == ty && e->type == type) return (int)i;
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * State & events
 * --------------------------------------------------------------------------- */

static void push_event(cs2d_entity_world_t *w, cs2d_ent_event_type_t type, int index)
{
    if (w->event_count >= CS2D_ENTS_MAX_EVENTS) {
        memmove(&w->events[0], &w->events[1], sizeof(w->events[0]) * (CS2D_ENTS_MAX_EVENTS - 1));
        w->event_count--;
    }
    w->events[w->event_count].type   = type;
    w->events[w->event_count].entity = index;
    w->event_count++;
}

bool cs2d_ents_pop_event(cs2d_entity_world_t *w, cs2d_ent_event_t *out)
{
    if (!w || w->event_count == 0) return false;
    if (out) *out = w->events[0];
    memmove(&w->events[0], &w->events[1], sizeof(w->events[0]) * (size_t)(w->event_count - 1));
    w->event_count--;
    return true;
}

void cs2d_ents_set_state(cs2d_entity_world_t *w, int index, uint8_t state)
{
    if (!w || !w->map || index < 0 || (uint32_t)index >= w->map->entity_count) return;
    cs2d_map_t *map = w->map;
    state = state ? 1 : 0;
    if (map->entity_state[index] == state) return;
    map->entity_state[index] = state;
    map->entity_state_version++;

    const cs2d_entity_t *e = &map->entities[index];
    if (e->type == CS2D_ENT_FUNC_DYNWALL || e->type == CS2D_ENT_ENV_BREAKABLE) {
        refresh_tile_collision(map, e->x, e->y);
        map->entity_solid_version++;
    }
    push_event(w, CS2D_ENT_EVENT_STATE, index);
}

/* ---------------------------------------------------------------------------
 * Triggering
 * --------------------------------------------------------------------------- */

void cs2d_ents_trigger(cs2d_entity_world_t *w, const char *name_list)
{
    if (!w || !w->map || !name_list || !name_list[0]) return;
    if (w->depth >= ENTS_MAX_DEPTH) return; /* Trigger loops (A -> B -> A) stop here */
    w->depth++;

    const char *p = name_list;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        const char *end = p;
        while (*end && *end != ',') end++;
        const char *trim = end;
        while (trim > p && trim[-1] == ' ') trim--;

        char name[64];
        size_t len = (size_t)(trim - p);
        if (len > 0 && len < sizeof(name)) {
            memcpy(name, p, len);
            name[len] = '\0';
            int found[256]; /* Per-name fan-out limit (keeps deep chains off a huge stack) */
            int n = cs2d_ents_find_by_name(w, name, found, 256);
            for (int i = 0; i < n && i < 256; i++) {
                cs2d_ents_activate(w, found[i]);
            }
        }
        p = end;
    }
    w->depth--;
}

/* Delay of a Trigger_Delay in seconds (ints[1] ms takes precedence over ints[0] s) */
static float delay_seconds(const cs2d_entity_t *e)
{
    if (e->int_settings[1] > 0) return (float)e->int_settings[1] / 1000.0f;
    return (float)e->int_settings[0];
}

/* Trigger_If conditions that only depend on map state */
static bool evaluate_if(const cs2d_entity_world_t *w, const cs2d_entity_t *e)
{
    int cond = e->int_settings[0];
    int a = e->int_settings[1], b = e->int_settings[2];
    if (cond == 3 || cond == 4) {
        for (uint32_t i = 0; i < w->map->entity_count; i++) {
            const cs2d_entity_t *t = &w->map->entities[i];
            if (t->x == a && t->y == b && t != e) {
                return w->map->entity_state[i] == (cond == 3 ? 0 : 1);
            }
        }
        return false;
    }
    return cond == 9; /* No round time limit modelled: "time >= A" always holds, "time <= A" never */
}

void cs2d_ents_activate(cs2d_entity_world_t *w, int index)
{
    if (!w || !w->map || index < 0 || (uint32_t)index >= w->map->entity_count) return;
    cs2d_map_t *map = w->map;
    cs2d_entity_t *e = &map->entities[index];
    w->activations++;

    switch (e->type) {
        case CS2D_ENT_TRIGGER_DELAY:
            /* State 1 while counting down (a running delay ignores further triggers), back to 0 when it fires */
            if (map->entity_state[index] == 0 && w->delay_count < CS2D_ENTS_MAX_DELAYS) {
                cs2d_ents_set_state(w, index, 1);
                w->delays[w->delay_count].entity    = index;
                w->delays[w->delay_count].remaining = delay_seconds(e);
                w->delay_count++;
            }
            break;

        case CS2D_ENT_TRIGGER_ONCE:
            /* Fires while active, then switches itself off for the rest of the round */
            if (map->entity_state[index] == 0) {
                w->once_fired[index] = 1;
                cs2d_ents_set_state(w, index, 1);
                cs2d_ents_trigger(w, e->trigger);
            }
            break;

        case CS2D_ENT_TRIGGER_IF:
            if (evaluate_if(w, e)) cs2d_ents_trigger(w, e->trigger);
            break;

        case CS2D_ENT_FUNC_MESSAGE:
            push_event(w, CS2D_ENT_EVENT_MESSAGE, index);
            break;

        case CS2D_ENT_FUNC_GAMEACTION:
            push_event(w, CS2D_ENT_EVENT_ACTION, index);
            break;

        case CS2D_ENT_ENV_EXPLODE:
            push_event(w, CS2D_ENT_EVENT_EXPLODE, index);
            break;

        case CS2D_ENT_ENV_ITEM:
            cs2d_ents_set_state(w, index, map->entity_state[index] ^ 1);
            push_event(w, CS2D_ENT_EVENT_ITEM, index);
            break;

        case CS2D_ENT_ENV_SOUND:
            /* Looping sounds (ints[1] = 0) switch on / off; one-shot sounds just play and keep their state */
            if (e->int_settings[1] == 0) cs2d_ents_set_state(w, index, map->entity_state[index] ^ 1);
            push_event(w, CS2D_ENT_EVENT_SOUND, index);
            break;

        case CS2D_ENT_ENV_BREAKABLE:
            /* Triggering a breakable destroys it (once) */
            if (map->entity_state[index] == 0) cs2d_ents_break(w, index);
            break;

        default:
            /* DynWall, Sprite, Image, Object, Gen_*, Hurt, Teleport, TeamGate, Use, Move, ... */
            cs2d_ents_set_state(w, index, map->entity_state[index] ^ 1);
            break;
    }
}

void cs2d_ents_break(cs2d_entity_world_t *w, int index)
{
    if (!w || !w->map || index < 0 || (uint32_t)index >= w->map->entity_count) return;
    if (w->map->entities[index].type != CS2D_ENT_ENV_BREAKABLE || w->map->entity_state[index] != 0) return;
    w->health[index] = 0;
    cs2d_ents_set_state(w, index, 1);
    push_event(w, CS2D_ENT_EVENT_BREAK, index);
    cs2d_ents_trigger(w, w->map->entities[index].trigger);
}

bool cs2d_ents_damage_breakable(cs2d_entity_world_t *w, int index, int damage)
{
    if (!w || !w->map || index < 0 || (uint32_t)index >= w->map->entity_count) return false;
    if (w->map->entities[index].type != CS2D_ENT_ENV_BREAKABLE || w->map->entity_state[index] != 0) return false;
    if (damage <= 0) return false;
    w->health[index] -= damage;
    if (w->health[index] > 0) return false;
    cs2d_ents_break(w, index);
    return true;
}

int cs2d_ents_breakable_at(const cs2d_entity_world_t *w, int tx, int ty)
{
    if (!w || !w->map) return -1;
    for (int k = 0; k < w->breakable_count; k++) {
        int i = w->breakables[k];
        const cs2d_entity_t *e = &w->map->entities[i];
        if (e->x == tx && e->y == ty && w->map->entity_state[i] == 0) return i;
    }
    return -1;
}

int cs2d_ents_trace_shot(const cs2d_entity_world_t *w, float x, float y, float angle_deg, float reach,
                         float *end_x, float *end_y)
{
    if (end_x) *end_x = x;
    if (end_y) *end_y = y;
    if (!w || !w->map || !w->map->collision) return -1;
    const cs2d_map_t *map = w->map;
    float rad = angle_deg * 0.017453292519943295f;
    float dx = sinf(rad), dy = -cosf(rad);
    int last_tx = -1, last_ty = -1;

    /* 1 px steps; tiles are only examined when the ray enters a new one */
    for (float d = 0.0f; d <= reach; d += 1.0f) {
        float px = x + dx * d, py = y + dy * d;
        if (end_x) *end_x = px;
        if (end_y) *end_y = py;
        if (px < 0.0f || py < 0.0f) return -1;
        int tx = (int)(px * 0.03125f), ty = (int)(py * 0.03125f);
        if ((uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return -1;
        if (tx == last_tx && ty == last_ty) continue;
        last_tx = tx;
        last_ty = ty;
        int hit = cs2d_ents_breakable_at(w, tx, ty);
        if (hit >= 0) return hit;
        if (map->collision[(size_t)ty * map->width + (size_t)tx] == 1) return -1;
    }
    return -1;
}

void cs2d_ents_explode(cs2d_entity_world_t *w, float x, float y, float range, int power)
{
    if (!w || !w->map || range <= 0.0f || power <= 0) return;
    for (int k = 0; k < w->breakable_count; k++) {
        int i = w->breakables[k];
        const cs2d_entity_t *e = &w->map->entities[i];
        if (w->map->entity_state[i] != 0) continue;
        float ex = (float)e->x * CS2D_TILE_SIZE + 16.0f - x;
        float ey = (float)e->y * CS2D_TILE_SIZE + 16.0f - y;
        float dist = sqrtf(ex * ex + ey * ey);
        if (dist > range) continue;
        cs2d_ents_damage_breakable(w, i, (int)((float)power * (1.0f - dist / range)));
    }
}

/* Every breakable back to its configured health (ints[1]) */
static void reset_breakable_health(cs2d_entity_world_t *w)
{
    for (int k = 0; k < w->breakable_count; k++) {
        int i = w->breakables[k];
        int hp = w->map->entities[i].int_settings[1];
        w->health[i] = hp > 0 ? hp : 1;
    }
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------------- */

void cs2d_ents_init(cs2d_entity_world_t *w, cs2d_map_t *map)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->map = map;
    if (!map || !map->collision) return;

    size_t total = (size_t)map->width * (size_t)map->height;
    if (!map->base_collision) {
        map->base_collision = (uint8_t *)malloc(total);
        if (map->base_collision) memcpy(map->base_collision, map->collision, total);
    }
    memset(map->entity_state, 0, sizeof(map->entity_state));
    map->entity_state_version++;
    build_name_index(w);

    /* Active DynWalls / Breakables start solid */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type == CS2D_ENT_FUNC_DYNWALL || e->type == CS2D_ENT_ENV_BREAKABLE) {
            refresh_tile_collision(map, e->x, e->y);
        }
        if (e->type == CS2D_ENT_ENV_BREAKABLE) w->breakables[w->breakable_count++] = (int)i;
    }
    reset_breakable_health(w);
}

void cs2d_ents_round_start(cs2d_entity_world_t *w)
{
    if (!w || !w->map) return;
    cs2d_map_t *map = w->map;
    for (uint32_t i = 0; i < map->entity_count; i++) cs2d_ents_set_state(w, (int)i, 0);
    memset(w->once_fired, 0, sizeof(w->once_fired));
    memset(w->use_cooldown, 0, sizeof(w->use_cooldown));
    reset_breakable_health(w);
    w->delay_count = 0;
    w->event_count = 0;

    for (uint32_t i = 0; i < map->entity_count; i++) {
        if (map->entities[i].type == CS2D_ENT_TRIGGER_START) {
            cs2d_ents_trigger(w, map->entities[i].trigger);
        }
    }
}

void cs2d_ents_update(cs2d_entity_world_t *w, float dt)
{
    if (!w || !w->map) return;
    for (uint32_t i = 0; i < w->map->entity_count; i++) {
        if (w->use_cooldown[i] > 0.0f) w->use_cooldown[i] -= dt;
    }

    /* Fire expired delays; a fired delay may schedule new ones, so iterate over a snapshot */
    int count = w->delay_count;
    int fired[CS2D_ENTS_MAX_DELAYS];
    int fired_count = 0;
    int keep = 0;
    for (int i = 0; i < count; i++) {
        w->delays[i].remaining -= dt;
        if (w->delays[i].remaining <= 0.0f) {
            fired[fired_count++] = w->delays[i].entity;
        } else {
            w->delays[keep++] = w->delays[i];
        }
    }
    /* Keep delays scheduled after the snapshot */
    for (int i = count; i < w->delay_count; i++) w->delays[keep++] = w->delays[i];
    w->delay_count = keep;

    for (int i = 0; i < fired_count; i++) {
        const cs2d_entity_t *e = &w->map->entities[fired[i]];
        cs2d_ents_set_state(w, fired[i], 0);
        cs2d_ents_trigger(w, e->trigger);
    }
}

/* ---------------------------------------------------------------------------
 * Player interaction
 * --------------------------------------------------------------------------- */

static bool team_allowed(int filter, cs2d_team_t team)
{
    return filter == 0 || (filter == 1 && team == CS2D_TEAM_T) || (filter == 2 && team == CS2D_TEAM_CT);
}

int cs2d_ents_find_use_target(const cs2d_entity_world_t *w, float px, float py, float angle_deg, cs2d_team_t team)
{
    if (!w || !w->map) return -1;
    float rad = angle_deg * 0.017453292519943295f;
    for (int d = 0; d < 48; d += 16) {
        int tx = (int)((px + sinf(rad) * (float)d) * 0.03125f);
        int ty = (int)((py - cosf(rad) * (float)d) * 0.03125f);
        int i  = cs2d_ents_find_at(w->map, tx, ty, CS2D_ENT_TRIGGER_USE);
        if (i >= 0 && w->map->entity_state[i] == 0 && team_allowed(w->map->entities[i].int_settings[2], team)) {
            return i;
        }
    }
    return -1;
}

int cs2d_ents_try_use(cs2d_entity_world_t *w, float px, float py, float angle_deg, cs2d_team_t team)
{
    int best = cs2d_ents_find_use_target(w, px, py, angle_deg, team);
    if (best >= 0 && w->use_cooldown[best] > 0.0f) return -1;
    if (best >= 0) {
        const cs2d_entity_t *e = &w->map->entities[best];
        w->use_cooldown[best] = (float)e->int_settings[3] / 1000.0f;
        cs2d_ents_trigger(w, e->trigger);
    }
    return best;
}

int cs2d_ents_enter_tile(cs2d_entity_world_t *w, int tx, int ty, cs2d_team_t team)
{
    if (!w || !w->map) return -1;
    int teleport = -1;
    for (uint32_t i = 0; i < w->map->entity_count; i++) {
        const cs2d_entity_t *e = &w->map->entities[i];
        if (e->x != tx || e->y != ty || w->map->entity_state[i] != 0) continue;
        if (e->type == CS2D_ENT_TRIGGER_MOVE && team_allowed(e->int_settings[0], team)) {
            cs2d_ents_trigger(w, e->trigger);
        } else if (e->type == CS2D_ENT_FUNC_TELEPORT && teleport < 0) {
            teleport = (int)i;
        }
    }
    return teleport;
}
