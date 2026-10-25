/*
 * Dedicated-server AI: the engine side of the bot API and the server-side NPC behaviour.
 *
 * Evidence:
 *  - bots/Standard AI.lua (shipped with CS2D 1.0.1.8): the AI itself. It is loaded into the server
 *    Lua state and called by the engine (ai_onspawn, ai_update_living, ai_update_dead,
 *    ai_hear_radio, ai_hear_chat).
 *  - docs/cs2d_help_db_complete.json, lua_commands ai_*: the documented return values / semantics.
 *  - Client dump docs/dump/cs2d: cs2dlua_ai_* (140070000.c .. 140210000.c), the ai_ update tick
 *    (1402a0000.c:3490..3560), Tpl.ai_* fields (Tpl.c:346..355), the spawn reset (140240000.c:8135),
 *    the path finder (FUN_1401893c0 / FUN_14018a000 / FUN_14018aa00, 140180000.c), the line of
 *    sight (FUN_14018b520) and the flashbang (Tpro.c:3227..3237).
 *  - Movement: docs/agents/knowledge/movement.md: (2 + weapon bonus) * 1.2 per 60 Hz frame (~144 px/s),
 *    walking halves it; the weapon bonus is our cs2d_weapon_def_t.speed_modifier.
 *  - NPC AI: docs/dump/cs2d/_global/140280000.c (FUN_140283f20 update, FUN_140283120 motion,
 *    FUN_140283760 target picker, FUN_140283a10 tile waypoint + [36,id,7]) and 1401e0000.c
 *    (FUN_1401ed310 attack / FUN_1401ec180 damage: zombie 10, headcrab 2, snark 1, Vortigaunt
 *    15 melee / 40 ranged, soldier 5). dynobj.md "NPC motion" has the speed / turn tables.
 */

#include "cs2d_sv_ai.h"
#include "cs2d_sv_lua.h"
#include "cs2d_weapons.h"
#include "cs2d_map.h"
#include "cs2d_dynobj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AI_PI              3.14159265358979323846f
#define AI_DT              (1.0f / 60.0f)   /* the dedicated runs at 60 ticks */
#define AI_TURN_MIN        8.0f             /* bot_skill 0: degrees per frame (calibrated) */
#define AI_TURN_STEP       2.0f             /* + 2 deg/frame per skill point */
#define AI_PATH_MAX        256
#define AI_PATH_THROTTLE   250u             /* ms between failed path searches */

/* --------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* --------------------------------------------------------------------------- */

static cs2d_sv_player_t *ai_bot(cs2d_server_t *sv, int id)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return NULL;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->bot) return NULL;
    return pl;
}

static int ai_skill(const cs2d_server_t *sv)
{
    const char *s = cs2d_sv_settings_get(&sv->set, "bot_skill");
    int v = s ? atoi(s) : 4;
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    return v;
}

bool cs2d_sv_ai_bot_active(const cs2d_server_t *sv)
{
    if (!sv) return false;
    const char *s = cs2d_sv_settings_get(&sv->set, "bot_freeze");
    return !(s && atoi(s) != 0);
}

/* Same enemy rule as bots/includes/general.lua fai_enemies: different team, except CT (2) and the
 * VIP (3) are always friends; deathmatch (sv_gamemode 1) makes every player an enemy. */
static bool ai_enemy(const cs2d_server_t *sv, const cs2d_sv_player_t *a, const cs2d_sv_player_t *b)
{
    if (!a || !b || a == b) return false;
    if (b->team < 1 || !b->alive || b->health < 1) return false;
    if (a->team == b->team) return sv->set.sv_gamemode == 1;
    if (a->team >= 2 && b->team >= 2) return false;
    return true;
}

/* g_prop_class[mode] == 1 and no active DynWall / Breakable / building on the tile.
 * map->collision already combines the base tile, the entities and the object overlay
 * (cs2d_entities.c refresh_tile_collision), so it is the server's g_map_cellprop. */
static bool ai_tile_free(const cs2d_server_t *sv, int tx, int ty)
{
    if (!sv || !sv->map.width || !sv->map.height) return false;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= sv->map.width || (uint32_t)ty >= sv->map.height) return false;
    uint8_t mode = cs2d_map_get_tile_mode(&sv->map, (uint32_t)tx, (uint32_t)ty);
    if (cs2d_map_prop_class(mode) != 1) return false;
    if (sv->map.collision &&
        sv->map.collision[(size_t)ty * sv->map.width + (size_t)tx] != 0) return false;
    return true;
}

/* FUN_14018b520: 40 px steps, blocked by a tile that is not g_prop_class 1. Buildings are part of
 * the original cellprop; here they are the map collision overlay, so both are tested. */
static bool ai_los_clear(const cs2d_server_t *sv, float x1, float y1, float x2, float y2)
{
    if (!sv || !sv->map.width) return false;
    float dx = x2 - x1, dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.0f) return true;
    float ux = dx / dist, uy = dy / dist;
    int steps = (int)(dist / 40.0f) + 1;
    for (int i = 1; i <= steps; i++) {
        if (i == steps) break;                  /* the target itself is not an obstacle */
        float px = x1 + ux * (float)i * 40.0f;
        float py = y1 + uy * (float)i * 40.0f;
        int tx = (int)(px * 0.03125f);
        int ty = (int)(py * 0.03125f);
        if (!ai_tile_free(sv, tx, ty)) return false;
    }
    return true;
}

/* FUN_14011c3a0(x1, y1, x2, y2, 5.0, 0), called by the NPC target picker (CS2D.exe 0x14028394b
 * pushes 0x40a00000 = 5.0f as the step). It marches in 5 px steps and stops once both axes are
 * within 2 * step of the target (the target tile itself is never tested). Only a tile of
 * g_prop_class 0 (wall: collision 1, which includes a closed Func_DynWall wall and building walls)
 * blocks the sight; an obstacle (collision 2) does not. Out-of-map tiles are skipped. */
static bool ai_npc_los_clear(const cs2d_server_t *sv, float x1, float y1, float x2, float y2)
{
    if (!sv || !sv->map.width || !sv->map.collision) return false;
    float dx = x2 - x1, dy = y2 - y1;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.0f) return true;
    float ux = dx / dist, uy = dy / dist;
    float px = x1, py = y1;
    int last_x = -1000, last_y = -1000;
    for (int guard = 0; guard < 100000; guard++) {
        px += ux * 5.0f;
        py += uy * 5.0f;
        if (fabsf(px - x2) < 10.0f && fabsf(py - y2) < 10.0f) return true;
        int tx = (int)(px * 0.03125f);
        int ty = (int)(py * 0.03125f);
        if (tx == last_x && ty == last_y) continue;  /* same tile: the original skips the test */
        last_x = tx;
        last_y = ty;
        if (px < 0.0f || py < 0.0f || (uint32_t)tx >= sv->map.width || (uint32_t)ty >= sv->map.height) continue;
        if (sv->map.collision[(size_t)ty * sv->map.width + (size_t)tx] == 1) return false;
    }
    return false;
}

static float ai_dist2(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    return dx * dx + dy * dy;
}

/* Movement speed of a bot: the player movement formula of docs/agents/knowledge/movement.md. */
static float ai_speed(const cs2d_sv_player_t *pl, int walk)
{
    float speed = 2.4f * 60.0f;                 /* 2 * 1.2 per 60 Hz frame */
    const cs2d_weapon_def_t *def = (pl->weapon > 0) ? cs2d_weapon_get((uint8_t)pl->weapon) : NULL;
    if (def && def->speed_modifier > 0.0f) speed *= def->speed_modifier;
    if (pl->speedmod != 0) speed *= (float)(100 + pl->speedmod) / 100.0f;
    if (walk) speed *= 0.5f;
    return speed;
}


static int ai_facing_tile_x(const cs2d_sv_player_t *pl)
{
    return (int)((pl->x + sinf(pl->rot * AI_PI / 180.0f) * 24.0f) * 0.03125f);
}

static int ai_facing_tile_y(const cs2d_sv_player_t *pl)
{
    return (int)((pl->y - cosf(pl->rot * AI_PI / 180.0f) * 24.0f) * 0.03125f);
}

/* Broadcast a raw reliable message to every joined player (same filter as sv_broadcast_joined). */
static void ai_broadcast(cs2d_server_t *sv, const uint8_t *msg, int len)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        for (int k = 0; k < len; k++) cs2d_pkt_w_byte(p, msg[k]);
    }
}

/* [36, id u16, 7, m_x u16, m_y u16, m_rot u16]: NPC movement target (Tdo.getdata case 7).
 * The client reads m_rot as a plain ushort and compares it with its own degrees, so the value is
 * sent as degrees (the original FUN_140283a10 writes (int)m_rot, not the 182.04445 fixed point of
 * the player rotation packets). */
static void ai_npc_send_move(cs2d_server_t *sv, const cs2d_dynobj_t *o)
{
    int mx = (int)o->m_x, my = (int)o->m_y;
    int mr = (int)o->m_rot & 0xffff;
    uint8_t msg[10];
    msg[0] = 36;
    msg[1] = (uint8_t)(o->id & 0xff);
    msg[2] = (uint8_t)(o->id >> 8);
    msg[3] = 7;
    msg[4] = (uint8_t)(mx & 0xff);
    msg[5] = (uint8_t)((mx >> 8) & 0xff);
    msg[6] = (uint8_t)(my & 0xff);
    msg[7] = (uint8_t)((my >> 8) & 0xff);
    msg[8] = (uint8_t)(mr & 0xff);
    msg[9] = (uint8_t)((mr >> 8) & 0xff);
    ai_broadcast(sv, msg, 10);
}

/* [36, id u16, 9, player u8, kind u8]: NPC attack (FUN_1401ed310). */
static void ai_npc_send_attack(cs2d_server_t *sv, const cs2d_dynobj_t *o, int pid, int kind)
{
    uint8_t msg[6] = { 36, (uint8_t)(o->id & 0xff), (uint8_t)(o->id >> 8), 9,
                       (uint8_t)pid, (uint8_t)kind };
    ai_broadcast(sv, msg, 6);
}

/* --------------------------------------------------------------------------- */
/* Bot rotation (FUN_1402a6450): turn by skill degrees/frame towards ai_aim      */
/* --------------------------------------------------------------------------- */

static void ai_rotate_towards(cs2d_server_t *sv, cs2d_sv_player_t *pl)
{
    if (pl->ai_aim_x == 0.0f && pl->ai_aim_y == 0.0f) return;
    float target = (float)(atan2((double)(pl->ai_aim_x - pl->x),
                                 (double)(pl->y - pl->ai_aim_y)) * 57.29577951308232);
    float diff = fmodf(target - pl->rot, 360.0f);
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    float turn = AI_TURN_MIN + (float)ai_skill(sv) * AI_TURN_STEP;
    if (fabsf(diff) <= turn) {
        pl->rot = target;
    } else {
        pl->rot += (diff > 0.0f) ? turn : -turn;
    }
    while (pl->rot < 0.0f) pl->rot += 360.0f;
    while (pl->rot >= 360.0f) pl->rot -= 360.0f;
    pl->rot_flag = 1;
}

/* --------------------------------------------------------------------------- */
/* ai_* engine API                                                            */
/* --------------------------------------------------------------------------- */

int cs2d_sv_ai_move(cs2d_server_t *sv, int id, float angle, int walk)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || pl->team < 1 || !cs2d_sv_ai_bot_active(sv)) return 0;
    if (walk != 0) walk = 1;
    float dist = ai_speed(pl, walk) * AI_DT;
    float rad = angle * AI_PI / 180.0f;
    float nx = pl->x + sinf(rad) * dist;
    float ny = pl->y - cosf(rad) * dist;
    return cs2d_sv_validate_move(sv, id, nx, ny, walk ? 11 : 10) ? 1 : 0;
}

/* BFS path scratch buffers (single-threaded server). */
static int32_t *s_path_parent;
static int32_t *s_path_queue;
static int      s_path_cap;

static bool ai_path_build(cs2d_server_t *sv, int sx, int sy, int gx, int gy,
                          int16_t *out_x, int16_t *out_y, int *out_len)
{
    *out_len = 0;
    int w = (int)sv->map.width, h = (int)sv->map.height;
    if (w <= 0 || h <= 0 || gx < 0 || gy < 0 || gx >= w || gy >= h) return false;
    if (!ai_tile_free(sv, gx, gy)) return false;
    size_t total = (size_t)w * (size_t)h;
    if (total > ((size_t)1 << 22)) return false;   /* sanity: 4M tiles */
    if ((int)total > s_path_cap) {
        int32_t *p = (int32_t *)realloc(s_path_parent, total * sizeof(int32_t));
        if (!p) return false;
        s_path_parent = p;
        int32_t *q = (int32_t *)realloc(s_path_queue, total * sizeof(int32_t));
        if (!q) return false;
        s_path_queue = q;
        s_path_cap = (int)total;
    }
    for (size_t i = 0; i < total; i++) s_path_parent[i] = -1;
    int start = sy * w + sx, goal = gy * w + gx;
    if (start == goal) return true;
    int head = 0, tail = 0;
    s_path_parent[start] = start;
    s_path_queue[tail++] = start;
    static const int dx8[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dy8[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    bool found = false;
    while (head < tail) {
        int cur = s_path_queue[head++];
        if (cur == goal) { found = true; break; }
        int cx = cur % w, cy = cur / w;
        for (int k = 0; k < 8; k++) {
            int nx = cx + dx8[k], ny = cy + dy8[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            int ni = ny * w + nx;
            if (s_path_parent[ni] != -1) continue;
            if (!ai_tile_free(sv, nx, ny)) continue;
            if (k >= 4) {   /* no diagonal corner cutting */
                if (!ai_tile_free(sv, cx + dx8[k], cy) || !ai_tile_free(sv, cx, cy + dy8[k])) continue;
            }
            s_path_parent[ni] = cur;
            s_path_queue[tail++] = ni;
        }
    }
    if (!found) return false;
    /* Walk the parent chain back to the start (the queue is free again after the BFS). */
    int n = 0;
    for (int cur = goal; cur != start && n < s_path_cap; cur = s_path_parent[cur]) {
        s_path_queue[n++] = cur;
        if (cur == s_path_parent[cur]) break;
    }
    if (n > AI_PATH_MAX) return false;   /* keep the waypoint array bounded; caller retries later */
    int count = 0;
    for (int i = n - 1; i >= 0; i--) {
        out_x[count] = (int16_t)(s_path_queue[i] % w);
        out_y[count] = (int16_t)(s_path_queue[i] / w);
        count++;
    }
    *out_len = count;
    return true;
}

int cs2d_sv_ai_goto(cs2d_server_t *sv, int id, int tx, int ty, int walk)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || pl->team < 1 || !cs2d_sv_ai_bot_active(sv)) return 0;
    if (walk != 0) walk = 1;
    int px = (int)(pl->x * 0.03125f), py = (int)(pl->y * 0.03125f);
    if (px == tx && py == ty) {
        pl->ai_path_len = pl->ai_path_idx = 0;
        return 1;
    }
    if (pl->ai_path_len <= 0 || pl->ai_path_idx >= pl->ai_path_len ||
        pl->ai_path_goal_x != tx || pl->ai_path_goal_y != ty) {
        uint32_t now = cs2d_sv_now();
        if (pl->ai_path_ms && pl->ai_path_goal_x == tx && pl->ai_path_goal_y == ty &&
            now - pl->ai_path_ms < AI_PATH_THROTTLE) {
            return 0;
        }
        pl->ai_path_goal_x = tx;
        pl->ai_path_goal_y = ty;
        pl->ai_path_ms = now;
        pl->ai_path_len = pl->ai_path_idx = 0;
        if (!ai_path_build(sv, px, py, tx, ty, pl->ai_path_x, pl->ai_path_y, &pl->ai_path_len)) {
            return 0;
        }
    }
    if (pl->ai_path_idx >= pl->ai_path_len) return 1;

    int wx = pl->ai_path_x[pl->ai_path_idx], wy = pl->ai_path_y[pl->ai_path_idx];
    float cx = (float)wx * 32.0f + 16.0f, cy = (float)wy * 32.0f + 16.0f;
    float dx = cx - pl->x, dy = cy - pl->y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = ai_speed(pl, walk) * AI_DT;
    if (dist <= step) {
        if (!cs2d_sv_validate_move(sv, id, cx, cy, walk ? 11 : 10)) {
            pl->ai_path_len = pl->ai_path_idx = 0;
            return 0;
        }
        pl->ai_path_idx++;
        if ((int)(pl->x * 0.03125f) == tx && (int)(pl->y * 0.03125f) == ty) {
            pl->ai_path_len = pl->ai_path_idx = 0;
            return 1;
        }
        return 2;
    }
    float nx = pl->x + dx / dist * step;
    float ny = pl->y + dy / dist * step;
    if (!cs2d_sv_validate_move(sv, id, nx, ny, walk ? 11 : 10)) {
        pl->ai_path_len = pl->ai_path_idx = 0;
        return 0;
    }
    return 2;
}

void cs2d_sv_ai_aim(cs2d_server_t *sv, int id, float x, float y)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl) return;
    pl->ai_aim_x = x;
    pl->ai_aim_y = y;
}

void cs2d_sv_ai_rotate(cs2d_server_t *sv, int id, float rot)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl) return;
    pl->rot = rot;
    while (pl->rot < 0.0f) pl->rot += 360.0f;
    while (pl->rot >= 360.0f) pl->rot -= 360.0f;
    pl->ai_aim_x = pl->x + sinf(pl->rot * AI_PI / 180.0f) * 100.0f;
    pl->ai_aim_y = pl->y - cosf(pl->rot * AI_PI / 180.0f) * 100.0f;
    pl->rot_flag = 1;
}

/* FUN_14018bc30: closest enemy inside the bot's virtual screen. The original uses per-skill
 * range arrays (PTR_bbEmptyArray_1409eda28 / 1409eda20) and only checks the line of sight when
 * fog of war is on. 420 x 235 is the range the shipped Standard AI also uses (engage.lua:39). */
int cs2d_sv_ai_findtarget(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return 0;
    int skill = ai_skill(sv);
    float range_x = 420.0f + (float)skill * 4.0f;
    float range_y = 235.0f + (float)skill * 2.5f;
    int best = 0;
    float best_d = 1e30f;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *o = &sv->players[i];
        if (!o->used || !o->alive || o->team < 1 || i == id) continue;
        if (fabsf(o->x - pl->x) >= range_x || fabsf(o->y - pl->y) >= range_y) continue;
        if (!ai_enemy(sv, pl, o)) continue;
        if (sv->set.sv_fow == 1 && !ai_los_clear(sv, pl->x, pl->y, o->x, o->y)) continue;
        float d = ai_dist2(pl->x, pl->y, o->x, o->y);
        if (d < best_d) { best_d = d; best = i; }
    }
    pl->ai_target = best;
    return best;
}

bool cs2d_sv_ai_freeline(cs2d_server_t *sv, int id, float x, float y)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return false;
    return ai_los_clear(sv, pl->x, pl->y, x, y);
}
/* Damage of one bot shot. Bots have no client, so the dedicated resolves the hit itself (the
 * shooter's client does this for human players: [7] is only the effect relay, [17] the hit). */
static void ai_shot_damage(cs2d_server_t *sv, int shooter_id, cs2d_sv_player_t *pl,
                           const cs2d_weapon_def_t *def, float angle, float reach)
{
    if (!def) return;
    float ux = sinf(angle * AI_PI / 180.0f);
    float uy = -cosf(angle * AI_PI / 180.0f);
    int best = 0;
    float best_t = reach + 14.0f;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *o = &sv->players[i];
        if (!o->used || !o->alive || i == shooter_id) continue;
        if (!ai_enemy(sv, pl, o)) continue;
        float ox = o->x - pl->x, oy = o->y - pl->y;
        float t = ox * ux + oy * uy;
        if (t < 0.0f || t > reach + 14.0f) continue;
        float px = ox - ux * t, py = oy - uy * t;
        if (px * px + py * py > 14.0f * 14.0f) continue;   /* 24x24 player box */
        if (t < best_t) { best_t = t; best = i; }
    }
    if (best <= 0) return;
    cs2d_sv_player_t *v = &sv->players[best];
    if (!ai_los_clear(sv, pl->x, pl->y, v->x, v->y)) return;
    int dmg = def->damage;
    if (sv->set.sv_gamemode == 4 && v->team == 1 && def->damage_z1 > 0) dmg = def->damage_z1;
    if (dmg > 0) cs2d_sv_damage(sv, best, shooter_id, dmg, def->id);
}

static void ai_fire(cs2d_server_t *sv, int id, cs2d_sv_player_t *pl, int secondary)
{
    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
    if (!def) return;
    if (secondary) {
        cs2d_sv_attack2(sv, id, 0);
        return;
    }
    /* ai_attack: an empty limited-ammo weapon does not fire (help.php). */
    bool limited = def->slot != CS2D_SLOT_MELEE && def->clip_size > 0 &&
                   pl->weapon != CS2D_WEP_WRENCH && pl->weapon != CS2D_WEP_PORTALGUN;
    if (limited && pl->clip[pl->weapon] <= 0) return;
    uint32_t now = cs2d_sv_now();
    if (now < pl->ai_next_fire_ms) return;
    float delay = def->fire_delay > 0.0f ? def->fire_delay : 0.15f;
    pl->ai_next_fire_ms = now + (uint32_t)(delay * 1000.0f);
    int skill = ai_skill(sv);
    float spread = def->spread * (1.6f - (float)skill * 0.06f);
    if (spread < 0.0f) spread = 0.0f;
    float jitter = ((float)cs2d_sv_rand(-1000, 1000) / 1000.0f) * spread * 57.29578f;
    float angle = pl->rot + jitter;
    float reach = (float)(def->range > 0 ? def->range : 300);
    cs2d_sv_fire(sv, id, cs2d_sv_rand(0, 255));
    ai_shot_damage(sv, id, pl, def, angle, reach);
}
void cs2d_sv_ai_attack(cs2d_server_t *sv, int id, int secondary)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || !cs2d_sv_ai_bot_active(sv)) return;
    ai_fire(sv, id, pl, secondary != 0);
}

/* Best usable weapon: primary, then secondary, then melee, with ammo left. */
static int ai_best_weapon(cs2d_sv_player_t *pl)
{
    int best_primary = 0, best_secondary = 0, best_melee = 0;
    for (int i = 0; i < pl->item_count; i++) {
        int type = pl->items[i];
        const cs2d_weapon_def_t *d = cs2d_weapon_get((uint8_t)type);
        if (!d) continue;
        if (d->slot == CS2D_SLOT_MELEE) { best_melee = type; continue; }
        bool ammo = (d->clip_size == 0 || pl->clip[type] > 0 || pl->reserve[type] > 0);
        if (!ammo) continue;
        if (d->slot == CS2D_SLOT_PRIMARY && best_primary == 0) best_primary = type;
        else if (d->slot == CS2D_SLOT_SECONDARY && best_secondary == 0) best_secondary = type;
    }
    if (best_primary) return best_primary;
    if (best_secondary) return best_secondary;
    return best_melee;
}

void cs2d_sv_ai_iattack(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || !cs2d_sv_ai_bot_active(sv)) return;
    if (pl->is_reloading || pl->is_planting || pl->is_defusing) return;
    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
    if (!def) return;
    bool limited = def->slot != CS2D_SLOT_MELEE && def->clip_size > 0 &&
                   pl->weapon != CS2D_WEP_WRENCH && pl->weapon != CS2D_WEP_PORTALGUN;
    if (limited && pl->clip[pl->weapon] <= 0) {
        if (def->max_reserve > 0 && pl->reserve[pl->weapon] > 0) {
            cs2d_sv_reload(sv, id, 1);
            return;
        }
        int best = ai_best_weapon(pl);
        if (best > 0 && best != pl->weapon) cs2d_sv_select_weapon(sv, id, best, 0);
    }
    ai_fire(sv, id, pl, 0);
}
void cs2d_sv_ai_reload(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || !cs2d_sv_ai_bot_active(sv)) return;
    cs2d_sv_reload(sv, id, 1);
}

void cs2d_sv_ai_selectweapon(cs2d_server_t *sv, int id, int type)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    cs2d_sv_select_weapon(sv, id, type, 0);
}

void cs2d_sv_ai_buy(cs2d_server_t *sv, int id, int type)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    cs2d_sv_buy(sv, id, type);
}

void cs2d_sv_ai_use(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    /* ai_use passes (0,0,0,0,force=1): the engine uses the tile in front of the bot. Ours triggers
     * a Trigger_Use there (mode 100); the Lua "use" hook also sees mode 0 like the original call. */
    int tx = ai_facing_tile_x(pl), ty = ai_facing_tile_y(pl);
    bool found = false;
    if (sv->map_loaded) {
        for (uint32_t i = 0; i < sv->map.entity_count; i++) {
            const cs2d_entity_t *e = &sv->map.entities[i];
            if (e->type != CS2D_ENT_TRIGGER_USE || e->x != tx || e->y != ty) continue;
            int team_filter = e->int_settings[2];
            if (team_filter != 0 && team_filter != pl->team) continue;
            found = true;
            break;
        }
    }
    if (found) cs2d_sv_use(sv, id, 100, 0, (uint16_t)tx, (uint16_t)ty);
    else cs2d_sv_use(sv, id, 0, 0, (uint16_t)tx, (uint16_t)ty);
}
void cs2d_sv_ai_respawn(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || pl->alive || pl->team < 1 || pl->team > 3) return;
    if (sv->set.sv_gamemode < 1 || sv->set.sv_gamemode > 4) return;  /* respawn modes */
    uint32_t delay_ms = (uint32_t)(sv->set.mp_respawndelay > 0 ? sv->set.mp_respawndelay : 0) * 1000u;
    if (cs2d_sv_now() - pl->death_ms < delay_ms) return;
    cs2d_sv_spawn(sv, id);
}

void cs2d_sv_ai_drop(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive || pl->weapon <= 0) return;
    cs2d_sv_drop(sv, id, pl->weapon, pl->clip[pl->weapon], pl->reserve[pl->weapon], 0);
}

void cs2d_sv_ai_radio(cs2d_server_t *sv, int id, int msg)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    char text[16];
    snprintf(text, sizeof(text), "%d", msg);
    uint16_t units[64];
    int len = cs2d_net_utf8_to_utf16(text, units, 64);
    cs2d_sv_chat(sv, id, 3, len, units);
}

void cs2d_sv_ai_say(cs2d_server_t *sv, int id, const char *text, int team)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !text) return;
    uint16_t units[512];
    int len = cs2d_net_utf8_to_utf16(text, units, 255);
    cs2d_sv_chat(sv, id, team ? 2 : 1, len, units);
}
void cs2d_sv_ai_spray(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    /* [28, player, 0, x u16, y u16, mode] relay (identical to the [28] receive handler). */
    int x = (int)pl->x, y = (int)pl->y;
    uint8_t msg[8] = { 28, (uint8_t)id, 0,
                       (uint8_t)(x & 0xff), (uint8_t)((x >> 8) & 0xff),
                       (uint8_t)(y & 0xff), (uint8_t)((y >> 8) & 0xff), 0 };
    ai_broadcast(sv, msg, 8);
}

void cs2d_sv_ai_build(cs2d_server_t *sv, int id, int typ, int tx, int ty)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl || !pl->alive) return;
    cs2d_sv_build(sv, id, typ, tx, ty, 0, pl->rot);
}

void cs2d_sv_ai_debug(cs2d_server_t *sv, int id, const char *text)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl) return;
    snprintf(pl->ai_debug, sizeof(pl->ai_debug), "%s", text ? text : "");
    const char *dbg = cs2d_sv_settings_get(&sv->set, "debugai");
    if (dbg && atoi(dbg) != 0 && pl->ai_debug[0]) {
        cs2d_server_print("[AI] %s: %s", pl->name, pl->ai_debug);
    }
}

/* --------------------------------------------------------------------------- */
/* Bot lifecycle and the Lua callbacks                                        */
/* --------------------------------------------------------------------------- */

bool cs2d_sv_ai_load(cs2d_server_t *sv)
{
    if (!sv || !sv->lua) return false;
    return cs2d_sv_lua_dofile(sv, "bots/Standard AI.lua");
}

void cs2d_sv_ai_on_spawn(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = ai_bot(sv, id);
    if (!pl) return;
    /* Spawn reset (CS2D.exe 0x140240000.c:8140..8180): random rotation, aim 250 px ahead,
     * no target, no flash, empty path. */
    pl->rot = (float)cs2d_sv_rand(0, 360);
    pl->ai_aim_x = pl->x + sinf(pl->rot * AI_PI / 180.0f) * 250.0f;
    pl->ai_aim_y = pl->y - cosf(pl->rot * AI_PI / 180.0f) * 250.0f;
    pl->ai_target = 0;
    pl->ai_flash = 0.0f;
    pl->ai_path_len = pl->ai_path_idx = 0;
    pl->ai_path_goal_x = pl->ai_path_goal_y = -100;
    pl->ai_path_ms = 0;
    pl->ai_next_fire_ms = 0;
    pl->ai_debug[0] = '\0';
    pl->rot_flag = 1;
    if (sv->lua) cs2d_sv_lua_call_ai(sv, "ai_onspawn", id);
}
void cs2d_sv_ai_hear_radio(cs2d_server_t *sv, int source, int radio)
{
    if (!sv || !sv->lua) return;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used || !sv->players[i].bot) continue;
        cs2d_sv_lua_call_ai_radio(sv, "ai_hear_radio", source, radio);
    }
}

void cs2d_sv_ai_hear_chat(cs2d_server_t *sv, int source, const char *msg, int teamonly)
{
    if (!sv || !sv->lua) return;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used || !sv->players[i].bot) continue;
        cs2d_sv_lua_call_ai_chat(sv, "ai_hear_chat", source, msg, teamonly);
    }
}

void cs2d_sv_ai_flashbang(cs2d_server_t *sv, float x, float y)
{
    if (!sv) return;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->bot || !pl->alive || pl->team < 1) continue;
        if (fabsf(pl->x - x) >= 640.0f || fabsf(pl->y - y) >= 480.0f) continue;
        if (!ai_los_clear(sv, x, y, pl->x, pl->y)) continue;
        pl->ai_flash = (float)cs2d_sv_rand(4, 6);   /* Tpro.c:3236 Rand(4,6) */
    }
}
/* bot_count / bot_autofill / bot_keepfreeslots / bot_jointeam (sys/server.cfg). */
static void ai_autofill(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    const char *s = cs2d_sv_settings_get(&sv->set, "bot_count");
    int want = s ? atoi(s) : 0;
    s = cs2d_sv_settings_get(&sv->set, "bot_autofill");
    bool autofill = s && atoi(s) != 0;
    int keep = 0;
    s = cs2d_sv_settings_get(&sv->set, "bot_keepfreeslots");
    if (s) { keep = atoi(s); if (keep < 0) keep = 0; }
    int bots = 0, humans = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) continue;
        if (sv->players[i].bot) bots++; else humans++;
    }
    int target = want;
    if (autofill) {
        int room = sv->set.sv_maxplayers - humans - keep;
        if (room < 0) room = 0;
        target = room;
    }
    if (bots >= target || humans + bots >= sv->set.sv_maxplayers) return;
    int team = 0;
    s = cs2d_sv_settings_get(&sv->set, "bot_jointeam");
    if (s) team = atoi(s);
    if (team < 1 || team > 2) team = 0;
    while (bots < target && humans + bots < sv->set.sv_maxplayers) {
        if (cs2d_sv_bot_add(sv, team, -1, NULL) <= 0) break;
        bots++;
    }
}

void cs2d_sv_ai_frame(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    if (sv->lua) {
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || !pl->bot || pl->team < 1 || !pl->alive) continue;
            ai_rotate_towards(sv, pl);
            cs2d_sv_lua_call_ai(sv, "ai_update_living", i);
        }
    }
    cs2d_sv_ai_npc_frame(sv);
}

void cs2d_sv_ai_second(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->bot) continue;
        if (pl->ai_flash > 0.0f) {
            pl->ai_flash -= 1.0f;
            if (pl->ai_flash < 0.0f) pl->ai_flash = 0.0f;
        }
        if (!pl->alive && pl->team >= 1 && sv->lua) {
            cs2d_sv_lua_call_ai(sv, "ai_update_dead", i);
        }
    }
    ai_autofill(sv);
}
/* --------------------------------------------------------------------------- */
/* NPC AI (Tdo 30)                                                            */
/* --------------------------------------------------------------------------- */

/* dynobj.md "NPC motion": px per 60 Hz frame and degrees per frame. */
static const float s_npc_speed[6] = { 0.0f, 1.0f, 2.5f, 6.0f, 7.0f, 3.0f };
static const float s_npc_turn[6]  = { 0.0f, 5.0f, 5.0f, 4.0f, 20.0f, 7.0f };

/* Ranged NPCs (Vortigaunt / soldier) fire from this far: Tdo.shoot's 400 px minus a margin for
 * the 12 px hit box and the Rnd(-3, 3) spread (estimated). */
#define AI_NPC_RANGE 380.0f

/* FUN_140283760: the closest living player inside the NPC's 405 x 220 view box with a clear
 * 32 px line of sight (FUN_14011c3a0(..., 32, 0)). Spectators (team 0) and dead players are
 * never picked, which is why an NPC must not chase a spectator or a player behind a wall. */
static int ai_npc_target(cs2d_server_t *sv, const cs2d_dynobj_t *o)
{
    int best = 0;
    float best_d = 1e30f;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        const cs2d_sv_player_t *p = &sv->players[i];
        if (!p->used || p->team < 1 || p->health < 1) continue;
        if (fabsf(p->x - o->x) >= 405.0f || fabsf(p->y - o->y) >= 220.0f) continue;
        float d = ai_dist2(o->x, o->y, p->x, p->y);
        if (d >= best_d) continue;
        if (!ai_npc_los_clear(sv, o->x, o->y, p->x, p->y)) continue;
        best_d = d;
        best = i;
    }
    return best;
}

/* FUN_140283a10: probe 45 px ahead, require that tile to be walkable and choose an adjacent
 * walkable tile (a free side tile first, else the diagonal), then send the waypoint. m_x / m_y
 * become the tile centre and m_rot the (raw degree) angle to the requested position. */
static bool ai_npc_set_waypoint(cs2d_server_t *sv, cs2d_dynobj_t *o, float tx, float ty)
{
    if (!sv->map_loaded || !sv->map.width || !sv->map.height) return false;
    double a = atan2((double)(tx - o->x), (double)(o->y - ty));
    float px = o->x + (float)(sin(a) * 45.0);
    float py = o->y - (float)(cos(a) * 45.0);
    if (px < -31.0f || py < -31.0f) return false;
    int ptx = (int)(px * 0.03125f), pty = (int)(py * 0.03125f);
    if (!ai_tile_free(sv, ptx, pty)) return false;   /* the probe tile itself must be a floor */

    int cx = (int)(o->x * 0.03125f), cy = (int)(o->y * 0.03125f);
    int nx = ptx, ny = pty;
    if (ptx != cx && pty != cy) {
        int sy = (pty < cy) ? cy - 1 : cy + 1;
        int sx = (ptx < cx) ? cx - 1 : cx + 1;
        bool free_y = ai_tile_free(sv, cx, sy);
        bool free_x = ai_tile_free(sv, sx, cy);
        if (free_x) {
            if (free_y) { nx = ptx; ny = pty; }   /* both sides free: the diagonal */
            else        { nx = sx;  ny = cy;  }
        } else if (free_y) {
            nx = cx;
            ny = sy;
        } else {
            return false;
        }
    }
    /* The walk (FUN_140283120) has no map test, and from the far edge of its tile the 45 px probe can
     * land two tiles away, hopping an obstacle / DynWall tile in between. Reject a waypoint whose
     * straight segment crosses a blocked tile (our addition: the server and the clients run the same
     * deterministic motion, so only the waypoint choice can keep an NPC out of obstacles). */
    float wx = (float)(nx * 32 + 16), wy = (float)(ny * 32 + 16);
    float sdx = wx - o->x, sdy = wy - o->y;
    int seg_steps = (int)(sqrtf(sdx * sdx + sdy * sdy) / 8.0f) + 1;
    for (int s = 1; s <= seg_steps; s++) {
        float t = (float)s / (float)seg_steps;
        int sx = (int)((o->x + sdx * t) * 0.03125f), sy = (int)((o->y + sdy * t) * 0.03125f);
        if ((sx == cx && sy == cy) || (sx == nx && sy == ny)) continue;
        if (!ai_tile_free(sv, sx, sy)) return false;
    }
    o->m_x = wx;
    o->m_y = wy;
    o->m_rot = (float)(a * 57.29577951308232);
    ai_npc_send_move(sv, o);
    return true;
}

/* FUN_140283120: turn towards m_rot while more than 3 degrees off, else walk `speed` px towards
 * (m_x, m_y) while more than 3 px away. There is no map test here in the original either: the
 * waypoints are walkable tile centres, so client and server run the same motion. */
static bool ai_npc_move(cs2d_dynobj_t *o)
{
    int type = o->player;
    if (type < 1 || type > 5) return false;
    float d = fmodf(o->m_rot - o->rot, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    if (fabsf(d) > 3.0f) {
        float turn = s_npc_turn[type];
        o->rot = (fabsf(d) <= turn) ? o->m_rot : o->rot + (d > 0.0f ? turn : -turn);
        return true;
    }
    if (fabsf(o->m_x - o->x) <= 3.0f && fabsf(o->m_y - o->y) <= 3.0f) return false;
    double a = atan2((double)(o->m_x - o->x), (double)(o->y - o->m_y));
    float speed = s_npc_speed[type];
    o->x += (float)(sin(a) * speed);
    o->y -= (float)(cos(a) * speed);
    if (fabsf(o->x - o->m_x) < speed && fabsf(o->y - o->m_y) < speed) {
        o->x = o->m_x;
        o->y = o->m_y;
    }
    return true;
}

/* Tdo.shoot (0x140209b50) for the soldier and the Vortigaunt ranged attack: a 400 px hitscan
 * along rot + Rnd(-3, 3), stopped by a wall (Tshoot.Shoot). */
static void ai_npc_shoot(cs2d_server_t *sv, const cs2d_dynobj_t *o, int dmg, int weapon)
{
    float rot = o->rot + (float)cs2d_sv_rand(-3, 3);
    double a = (double)rot * 0.017453292519943295;
    float ux = (float)sin(a), uy = -(float)cos(a);
    int own_x = (int)(o->x * 0.03125f), own_y = (int)(o->y * 0.03125f);
    int shot_dmg = dmg;
    float prev_x = o->x, prev_y = o->y;
    for (float d = 4.0f; d <= 400.0f; d += 4.0f) {
        float px = o->x + ux * d, py = o->y + uy * d;
        int tx = (int)(px * 0.03125f), ty = (int)(py * 0.03125f);
        /* image hit zones: an NPC shot has player id 0 and its own object id (help.php "hitzone"). */
        if (cs2d_sv_hitzone_at(sv, px, py, prev_x, prev_y, 0, o->id, weapon, &shot_dmg) == 2) return;
        prev_x = px;
        prev_y = py;
        if (!(tx == own_x && ty == own_y) && !ai_tile_free(sv, tx, ty)) return;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            const cs2d_sv_player_t *p = &sv->players[i];
            if (!p->used || !p->alive || p->team < 1 || p->health < 1) continue;
            if (fabsf(p->x - px) < 12.0f && fabsf(p->y - py) < 12.0f) {
                cs2d_sv_damage_ex(sv, i, 0, shot_dmg, weapon, o->id);
                return;
            }
        }
    }
}

/* FUN_1401ed310: the server half of an NPC attack. Damage values come from its FUN_1401ec180 /
 * Tdo.shoot calls: zombie 10, headcrab 2, snark 1, Vortigaunt melee 15 / ranged 40, soldier 5.
 * Kind 1 starts the Vortigaunt charge; kind 2 fires it. */
static void ai_npc_attack(cs2d_server_t *sv, cs2d_dynobj_t *o, int target, int kind)
{
    if (target < 1 || target > CS2D_SV_MAX_PLAYERS) return;
    ai_npc_send_attack(sv, o, target, kind);
    const cs2d_sv_player_t *p = &sv->players[target];
    if (!p->used) return;
    /* FUN_1401ed310 tail: the NPC turns to face the attacked player (m_rot = angle to target). */
    o->m_rot = (float)(atan2((double)(p->x - o->x), (double)(o->y - p->y)) * 57.29577951308232);
    if (p->health < 1) return;
    switch (o->player) {
        case 1: cs2d_sv_damage_ex(sv, target, 0, 10, 0xf9, o->id); break;
        case 2: cs2d_sv_damage_ex(sv, target, 0, 2, 0xf9, o->id); break;
        case 3: cs2d_sv_damage_ex(sv, target, 0, 1, 0xf9, o->id); break;
        case 4:
            if (kind == 1) o->upgrade = 1;
            else if (kind == 2) { o->upgrade = 0; ai_npc_shoot(sv, o, 40, 0xf9); }
            else cs2d_sv_damage_ex(sv, target, 0, 15, 0xf9, o->id);
            break;
        case 5: ai_npc_shoot(sv, o, 5, 0xf8); break;
        default: break;
    }
}

/*
 * FUN_140283f20: movement first; when the NPC has arrived (movement returns 0) the server runs
 * its attack timer. Types 1..3 melee a target within a 32 x 32 box; the Vortigaunt charges when
 * farther away and fires after 40 frames; the soldier shoots every 8 frames. Types 4 / 5 walk on
 * towards the last seen target position.
 */
void cs2d_sv_ai_npc_frame(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active || o->typ != CS2D_DO_NPC || o->health <= 0) continue;
        int type = o->player;
        if (type < 1 || type > 5) continue;

        if (ai_npc_move(o)) {
            o->countdown = (type == 5) ? 0 : 10;
            continue;
        }

        /* Vortigaunt ranged charge (Tdo +0x6c): 40 idle frames, then kind 2 (Tdo.shoot, 40 dmg). */
        if (type == 4 && o->upgrade > 0) {
            o->countdown = 0;
            o->upgrade++;
            if (o->upgrade >= 40) {
                o->upgrade = 0;
                if (o->target > 0) ai_npc_attack(sv, o, o->target, 2);
            }
            continue;
        }

        int threshold = (type == 5) ? 8 : 11;
        o->countdown++;
        if (o->countdown < threshold) continue;
        o->countdown = 0;

        int target = ai_npc_target(sv, o);
        if (target > 0) {
            cs2d_sv_player_t *p = &sv->players[target];
            bool in_melee = fabsf(p->x - o->x) < 32.0f && fabsf(p->y - o->y) < 32.0f;
            if (type <= 3) {
                o->target = target;
                if (in_melee) {
                    static const int cool[4] = { 0, -50, -20, -15 };
                    o->countdown = cool[type];
                    ai_npc_attack(sv, o, target, 0);
                    continue;
                }
                ai_npc_set_waypoint(sv, o, p->x, p->y);
                continue;
            }
            /* Types 4 / 5 (FUN_140283f20 lines 1243..1283): attack, remember the position and
             * return. They stay put while the player is in sight; the walk below only runs when
             * no target was found. */
            o->target = target;
            o->npc_last_x = (int)p->x;
            o->npc_last_y = (int)p->y;
            /* Out of range (Tdo.shoot reaches 400 px, the view box is 405 x 220 so its corners are
             * farther): approach instead of firing into the air. The original has no range test;
             * this follows the observed behaviour "walk closer only when spotted but out of range". */
            if (!in_melee && ai_dist2(o->x, o->y, p->x, p->y) > AI_NPC_RANGE * AI_NPC_RANGE) {
                o->countdown = 0;
                ai_npc_set_waypoint(sv, o, p->x, p->y);
                continue;
            }
            if (type == 4) {
                o->countdown = -50;
                if (in_melee) ai_npc_attack(sv, o, target, 0);
                else ai_npc_attack(sv, o, target, 1);   /* start the charge: stands still ~40 frames */
            } else {                               /* type 5: soldier */
                o->countdown = 0;
                ai_npc_attack(sv, o, target, 0);   /* Tdo.shoot: 400 px, 5 damage */
            }
            continue;
        }

        /* Types 4 and 5 keep walking towards the last seen position (+0x110 / +0x114). */
        if (type < 4) continue;
        if (o->npc_last_x == 0 && o->npc_last_y == 0) continue;
        if (fabsf(o->x - (float)o->npc_last_x) <= 16.0f &&
            fabsf(o->y - (float)o->npc_last_y) <= 16.0f) {
            o->npc_last_x = 0;
            o->npc_last_y = 0;
            continue;
        }
        ai_npc_set_waypoint(sv, o, (float)o->npc_last_x, (float)o->npc_last_y);
    }
}


