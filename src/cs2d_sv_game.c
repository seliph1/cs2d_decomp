/*
 * Dedicated server game logic (phase 2): team choice, spawn and the movement relay.
 *
 * Sources (cs2d_dedicated.exe, pseudo-C in docs/dump/dedicated/):
 *   FUN_1401624c0  team change (Tpl team / look, the first-choice spawn, "game commencing")
 *   FUN_140160740  spawn (spawn point, items, money, [21])
 *   FUN_1401a68e0  per-frame movement relay ([10]/[11]/[12]/[13]/[14], [70]/[71]/[72] for spectators)
 *   0x14021f4xx    the game mode tables (start items, melee weapon, team flags)
 */
#include "cs2d_server.h"
#include "cs2d_weapons.h"

#include <stdlib.h>
#include <string.h>

/* Game mode tables, index = sv_gamemode (init at 0x14021f4f4..0x14021fbef; proxy: CT gets USP + knife in
 * mode 0). "items" are the comma lists of PTR_DAT_14059c080 (T) / 0x14059c078 (CT), "melee" the weapon
 * appended when missing (0x14059c090 / 0x14059c088), then the flags of 0x14059c0a8 (team balance applies),
 * 0x14059c0a0 and 0x14059c098 (respawn mode: start money on every spawn / no rounds). */
typedef struct {
    const char *items_t, *items_ct;
    int         melee_t, melee_ct;
    int         balance, flag_a0, respawn;
} sv_mode_t;

static const sv_mode_t s_modes[5] = {
    { "2",    "1",    50, 50, 1, 1, 0 }, /* 0 Standard */
    { "2",    "1",    50, 50, 0, 0, 1 }, /* 1 Deathmatch */
    { "2",    "1",    50, 50, 1, 0, 1 }, /* 2 Team Deathmatch */
    { "2,50", "1,50", 74, 74, 1, 0, 1 }, /* 3 Construction (74 = Wrench) */
    { "",     "1",    78, 50, 1, 1, 1 }, /* 4 Zombies! (78 = Claw) */
};

static const sv_mode_t *sv_mode(const cs2d_server_t *sv)
{
    int m = sv->set.sv_gamemode;
    if (m < 0 || m > 4) m = 0;
    return &s_modes[m];
}

/* FUN_1403977c0(lo, hi): BlitzMax Rand. The generator state cannot match the original's (seeded from the
 * clock), so any uniform integer generator is equivalent */
int cs2d_sv_rand(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

/* FUN_140026150 (T) / FUN_140026210 (CT): players in a team (bots included) */
int cs2d_sv_count_team(const cs2d_server_t *sv, int team)
{
    int n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && sv->players[i].team == team) n++;
    }
    return n;
}

static void sv_broadcast_joined(cs2d_server_t *sv, const uint8_t *msg, int len)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        for (int k = 0; k < len; k++) cs2d_pkt_w_byte(p, msg[k]);
    }
}

/* Tpl.bestweapon (CS2D.exe 0x140032450, the same code in the dedicated): the last owned item of HUD slot 1,
 * else the last of slot 2, else the team's melee weapon */
static void sv_best_weapon(const cs2d_server_t *sv, cs2d_sv_player_t *pl)
{
    int primary = 0, secondary = 0;
    for (int i = 0; i < pl->item_count; i++) {
        int s = cs2d_weapon_hud_slot(pl->items[i]);
        if (s == 1) primary = pl->items[i];
        if (s == 2) secondary = pl->items[i];
    }
    if (primary > 0) pl->weapon = primary;
    else if (secondary > 0) pl->weapon = secondary;
    else pl->weapon = (pl->team == 1) ? sv_mode(sv)->melee_t : sv_mode(sv)->melee_ct;
}

static bool sv_has_item(const cs2d_sv_player_t *pl, int type)
{
    for (int i = 0; i < pl->item_count; i++) if (pl->items[i] == type) return true;
    return false;
}

static void sv_give(cs2d_sv_player_t *pl, int type)
{
    if (type <= 0 || pl->item_count >= (int)sizeof(pl->items)) return;
    pl->items[pl->item_count++] = (uint8_t)type;
}

void cs2d_sv_write_spawn(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl)
{
    cs2d_pkt_w_byte(p, 21);
    cs2d_pkt_w_byte(p, (uint8_t)id);
    cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
    cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
    cs2d_pkt_w_byte(p, (uint8_t)pl->weapon);   /* held weapon (+0x2e0 +0xc) */
    cs2d_pkt_w_byte(p, (uint8_t)pl->special);  /* vtable +0x200 */
    cs2d_pkt_w_short(p, (uint16_t)pl->money);
    cs2d_pkt_w_byte(p, (uint8_t)pl->item_count);
    for (int i = 0; i < pl->item_count; i++) cs2d_pkt_w_byte(p, pl->items[i]);
}

/* The spawn point: a random active Info_T / Info_CT / Info_VIP of the player's team; when a living player
 * stands on its tile it is dropped from the candidates and another is drawn (FUN_140160740, list
 * DAT_1407dfa18). False when the map has none ("FATAL ERROR: no active spawnpoints found!") */
static bool sv_pick_spawn(cs2d_server_t *sv, int id, float *ox, float *oy)
{
    const cs2d_sv_player_t *pl = &sv->players[id];
    int want = (pl->team == 1) ? CS2D_ENT_INFO_T : (pl->team == 2) ? CS2D_ENT_INFO_CT : CS2D_ENT_INFO_VIP;
    int cand[1024], n = 0;
    for (uint32_t e = 0; e < sv->map.entity_count && n < 1024; e++) {
        if ((int)sv->map.entities[e].type == want) cand[n++] = (int)e;
    }
    if (n == 0) {
        cs2d_server_print("\xa9" "255000000FATAL ERROR: no active spawnpoints found!");
        return false;
    }
    for (;;) {
        int k = cs2d_sv_rand(0, n - 1);
        const cs2d_entity_t *sp = &sv->map.entities[cand[k]];
        bool taken = false;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            const cs2d_sv_player_t *o = &sv->players[i];
            if (!o->used || o->team < 1 || !o->alive) continue;
            if ((int)(o->x * 0.03125f) == sp->x && (int)(o->y * 0.03125f) == sp->y) { taken = true; break; }
        }
        if (!taken || n < 2) {
            *ox = (float)(sp->x * 32 + 16);
            *oy = (float)(sp->y * 32 + 16);
            return true;
        }
        cand[k] = cand[--n];
    }
}

void cs2d_sv_spawn(cs2d_server_t *sv, int id)
{
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || pl->team < 1) return;
    float x, y;
    if (!sv_pick_spawn(sv, id, &x, &y)) return;
    pl->x = x;
    pl->y = y;
    pl->health = 100;
    pl->armor = 0;
    pl->alive = true;
    pl->move_flag = pl->rot_flag = 0;

    /* Items: the mode's list (the Lua spawn hook can replace it: not ported), then the melee weapon */
    const sv_mode_t *m = sv_mode(sv);
    pl->item_count = 0;
    const char *list = (pl->team == 1) ? m->items_t : m->items_ct;
    for (const char *q = list; *q;) {
        int t = atoi(q);
        if (t > 0 && !sv_has_item(pl, t)) sv_give(pl, t);
        while (*q && *q != ',') q++;
        if (*q == ',') q++;
    }
    int melee = (pl->team == 1) ? m->melee_t : m->melee_ct;
    if (!sv_has_item(pl, melee)) sv_give(pl, melee);
    sv_best_weapon(sv, pl);

    /* Money: the start money on the first spawn (score and deaths 0); clamped to 0..16000 */
    if (pl->score == 0 && pl->deaths == 0) pl->money = sv->set.mp_startmoney;
    if (pl->money > 16000) pl->money = 16000;
    if (pl->money < 0) pl->money = 0;

    /* [21] to every joined player */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_sv_write_spawn(cs2d_sv_packet(sv, c, true, false), id, pl);
    }
}

void cs2d_sv_team(cs2d_server_t *sv, int id, int team, int look)
{
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    /* Validation: spectator needs look 5, T / CT a look 0..5, 4 = automatic team */
    bool ok = (team == 0 && look == 5) || ((team == 1 || team == 2) && look < 6) || (team == 4 && look == 5);
    if (!ok) {
        cs2d_server_print("ERROR: team - %s tried to switch to invalid team/look (%d/%d)", pl->name, team, look);
        return;
    }
    if (team == 4) {
        /* FUN_1401624c0 LAB_140162b4c: the smaller team, a random one when even */
        int t = cs2d_sv_count_team(sv, 1), ct = cs2d_sv_count_team(sv, 2);
        team = (t == ct) ? cs2d_sv_rand(1, 2) : (ct <= t) + 1;
    }

    /* Team balance (mp_autoteambalance with a balanced mode): T only while T <= CT + 1, CT while CT <= T + 1;
     * a refusal answers [20, id, 10 / 11, look] to the player alone */
    if (team >= 1 && sv->set.mp_autoteambalance == 1 && sv_mode(sv)->balance && pl->team != team) {
        int t = cs2d_sv_count_team(sv, 1), ct = cs2d_sv_count_team(sv, 2);
        bool refuse = (team == 1) ? (t > ct + 1) : (ct > t + 1);
        if (refuse && c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 20);
            cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_byte(p, (uint8_t)(team == 1 ? 10 : 11));
            cs2d_pkt_w_byte(p, (uint8_t)look);
            return;
        }
    }

    /* Look 5 on a team: the team is picked, the skin menu follows; only the player gets [20, id, team, 5]
     * (proxy_ph1.log "14 01 02 05") */
    if (team >= 1 && look == 5) {
        pl->pending_team = team;
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 20); cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_byte(p, (uint8_t)team); cs2d_pkt_w_byte(p, 5);
        }
        return;
    }
    if (team >= 1 && look == 4) look = cs2d_sv_rand(0, 3); /* LAB_14016371c: random skin */

    /* Leaving a team alive kills the player (FUN_1401647d0): not ported yet, the player just changes team */
    pl->team_changes++;
    pl->team = team;
    pl->look = look;
    if (team == 0) pl->alive = false;
    uint8_t msg[4] = { 20, (uint8_t)id, (uint8_t)team, (uint8_t)look };
    sv_broadcast_joined(sv, msg, 4);

    /* The first team choice spawns at once while the round is younger than mp_postspawn seconds, in the
     * standard mode and with no round end pending */
    if (!pl->team_chosen) {
        pl->team_chosen = true;
        if (sv->set.sv_gamemode == 0 && sv->round_end == 0 && team >= 1) {
            float secs = (float)(int)(cs2d_sv_now() - sv->round_start_ms) * 0.001f;
            if ((int)secs <= sv->set.mp_postspawn) cs2d_sv_spawn(sv, id);
        }
    }
    /* "Game commencing" (both teams filled for the first time -> round end 4 after 3000 ms) is the next step */
}

/* FUN_1401a68e0, once per server frame */
void cs2d_sv_relay_movement(cs2d_server_t *sv)
{
    for (int id = 1; id <= CS2D_SV_MAX_PLAYERS; id++) {
        cs2d_sv_player_t *pl = &sv->players[id];
        if (!pl->used || (pl->move_flag < 1 && pl->rot_flag < 1)) continue;
        pl->relay_ctr++;
        if (pl->relay_ctr == 3) pl->relay_ctr = 0;
        float r = pl->rot;
        while (r < 0.0f) r += 360.0f;
        while (r > 360.0f) r -= 360.0f;
        uint16_t rot = (uint16_t)(int)(r * 182.04445f);
        for (int q = 1; q <= CS2D_SV_MAX_PLAYERS; q++) {
            cs2d_sv_player_t *o = &sv->players[q];
            cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, q);
            if (q == id || !c || !o->joined) continue;
            /* Far players (>= 525 x 340 px away) only get every 3rd update */
            if (pl->relay_ctr != 0) {
                float dx = o->x - pl->x, dy = o->y - pl->y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (!(dx < 525.0f && dy < 340.0f)) continue;
            }
            /* Spectators following this player get the aim too ([70]/[71]/[72], DAT_1405a64ac): not ported */
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
            if (pl->move_flag < 1) {
                cs2d_pkt_w_byte(p, 12);
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_short(p, rot);
            } else if (pl->rot_flag == 0) {
                cs2d_pkt_w_byte(p, (uint8_t)(pl->move_flag == 1 ? 10 : 11));
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
            } else {
                cs2d_pkt_w_byte(p, (uint8_t)(pl->move_flag == 1 ? 13 : 14));
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
                cs2d_pkt_w_short(p, rot);
            }
        }
        pl->move_flag = 0;
        pl->rot_flag = 0;
    }
}
