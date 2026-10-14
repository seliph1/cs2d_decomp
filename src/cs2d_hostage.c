#include "cs2d_hostage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void cs2d_hostages_clear(cs2d_hostage_world_t *w)
{
    if (w) memset(w, 0, sizeof(*w));
}

static cs2d_hostage_t *add_hostage(cs2d_hostage_world_t *w, int id)
{
    for (int i = 0; i < CS2D_MAX_HOSTAGES; i++) {
        cs2d_hostage_t *h = &w->h[i];
        if (h->active) continue;
        /* Thostage.add (0x140034a60): health 100, no target tile */
        memset(h, 0, sizeof(*h));
        h->active = true;
        h->id     = id;
        h->health = 100;
        h->tx = h->ty = -1;
        return h;
    }
    return NULL;
}

void cs2d_hostages_spawn_from_map(cs2d_hostage_world_t *w, const cs2d_map_t *map)
{
    if (!w) return;
    cs2d_hostages_clear(w);
    if (!map) return;
    int id = 1;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type != CS2D_ENT_INFO_HOSTAGE) continue;
        cs2d_hostage_t *h = add_hostage(w, id++);
        if (!h) break;
        h->x    = (float)(e->x * 32 + 16);
        h->y    = (float)(e->y * 32 + 16);
        h->rot  = (float)e->int_settings[0];
        h->look = e->int_settings[1] > 7 ? 7 : (e->int_settings[1] < 0 ? 0 : e->int_settings[1]);
    }
}

bool cs2d_hostages_read_snapshot(cs2d_hostage_world_t *w, const uint8_t *buf, int len, int *pos)
{
    if (!w || !buf || !pos || *pos >= len) return false;
    cs2d_hostages_clear(w);
    int count = buf[(*pos)++];
    for (int k = 0; k < count; k++) {
        if (*pos + 8 > len) return false; /* "ERROR: failed to load hostage data" */
        const uint8_t *r = &buf[*pos];
        *pos += 8;
        cs2d_hostage_t *h = add_hostage(w, r[0]);
        if (!h) continue;
        h->health = r[1];
        h->look   = r[2] & 7;
        h->follow = r[3];
        h->x      = (float)(r[4] | (r[5] << 8));
        h->y      = (float)(r[6] | (r[7] << 8));
        /* FUN_1402ce7c0: target = current tile, rot = Rand(1, 360) */
        h->tx     = (int)(h->x * 0.03125f);
        h->ty     = (int)(h->y * 0.03125f);
        h->rot    = (float)(1 + rand() % 360);
    }
    return true;
}

cs2d_hostage_t *cs2d_hostages_get(cs2d_hostage_world_t *w, int id)
{
    if (!w) return NULL;
    for (int i = 0; i < CS2D_MAX_HOSTAGES; i++) {
        if (w->h[i].active && w->h[i].id == id) return &w->h[i];
    }
    return NULL;
}

cs2d_hostage_t *cs2d_hostages_at_tile(cs2d_hostage_world_t *w, int tx, int ty)
{
    if (!w) return NULL;
    for (int i = 0; i < CS2D_MAX_HOSTAGES; i++) {
        cs2d_hostage_t *h = &w->h[i];
        /* FUN_14014f820: (int)x with BlitzMax Shr 5 (rounds towards -inf, same for x >= 0) */
        if (h->active && h->health > 0 && ((int)h->x >> 5) == tx && ((int)h->y >> 5) == ty) return h;
    }
    return NULL;
}

bool cs2d_hostages_use(cs2d_hostage_world_t *w, int id, cs2d_player_t *user, int pid, bool release)
{
    cs2d_hostage_t *h = cs2d_hostages_get(w, id);
    if (!h || h->health <= 0 || !user || user->team != CS2D_TEAM_CT) return false;
    if (release) {
        h->follow = 0;
        return false;
    }
    bool first = !h->used;
    if (first) {
        h->used = true;
        user->money += 150;
        if (user->money > 16000) user->money = 16000;
    }
    h->follow = pid; /* + a random sfx/hostage/hos1..4.wav at the hostage (no audio in this client yet) */
    return first;
}

int cs2d_hostages_net_message(cs2d_hostage_world_t *w, int id, int kind, int a, int b)
{
    (void)b;
    cs2d_hostage_t *h = cs2d_hostages_get(w, id);
    if (!h) return -1;
    switch (kind) {
        case 0:
            /* New target tile; a hostage without a follower is marked as following (follow = 1) */
            if (h->follow == 0) h->follow = 1;
            h->tx = a;
            h->ty = b;
            return 0;
        case 2:
        case 4:
            h->health = 0;
            return kind;
        case 3:
            h->health = a;
            return 3;
        default:
            return -1;
    }
}

void cs2d_hostages_update(cs2d_hostage_world_t *w, const cs2d_player_t *players, float frames)
{
    if (!w) return;
    float step = 4.0f * frames; /* 4 px per drawn frame (Thostage.draw), taken as one 60 Hz frame */
    for (int i = 0; i < CS2D_MAX_HOSTAGES; i++) {
        cs2d_hostage_t *h = &w->h[i];
        if (!h->active || h->health <= 0 || h->follow <= 0) continue;
        if (players && h->follow <= CS2D_MAX_PLAYERS) {
            const cs2d_player_t *p = &players[h->follow];
            if (!p->alive || p->health <= 0 || p->team == CS2D_TEAM_SPEC) {
                h->follow = 0;
            } else {
                h->rot = (float)(atan2((double)(p->pos.x - h->x), (double)(h->y - p->pos.y)) * 57.29577951308232);
            }
        }
        if (h->tx < 0 || h->ty < 0) continue;
        float gx = (float)(h->tx * 32 + 16), gy = (float)(h->ty * 32 + 16);
        if (fabsf(h->x - gx) > 4.0f) {
            float d = fminf(step, fabsf(h->x - gx));
            h->x += (gx > h->x) ? d : -d;
        }
        if (fabsf(h->y - gy) > 4.0f) {
            float d = fminf(step, fabsf(h->y - gy));
            h->y += (gy > h->y) ? d : -d;
        }
    }
}
