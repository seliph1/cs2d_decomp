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
#include "cs2d_network.h"
#include "cs2d_sv_lua.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/*
 * The Construction helpers live in the section at the end of this file; the movement validation (which
 * teleports a player who steps onto a charged entrance) needs one of them, so it is forward declared.
 */
static void sv_broadcast_obj_data(cs2d_server_t *sv, const cs2d_dynobj_t *o, int sub, int value, bool has_value);
/* The building on the tile a player just entered (barbed wire, gate field, mine) */
static bool sv_tile_object_effect(cs2d_server_t *sv, int id, int tx, int ty);

/* FUN_140026150 (T) / FUN_140026210 (CT): players in a team (bots included) */
int cs2d_sv_count_team(const cs2d_server_t *sv, int team)
{
    int n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && sv->players[i].team == team) n++;
    }
    return n;
}

/* FUN_140026150 (side 1) / FUN_140026210 (side 2): the original counts the T side as team == 1 and the
 * CT side as team > 1, so the V.I.P. (team 3) belongs to the counter-terrorists. */
static int sv_count_side(const cs2d_server_t *sv, int side)
{
    int n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) continue;
        if (side == 1 ? sv->players[i].team == 1 : sv->players[i].team >= 2) n++;
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
    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)type);
    if (def) {
        pl->clip[type] = (int16_t)def->clip_size;
        pl->reserve[type] = (int16_t)def->max_reserve;
    }
}

static void sv_remove_item(cs2d_sv_player_t *pl, int type)
{
    for (int i = 0; i < pl->item_count; i++) {
        if (pl->items[i] == type) {
            for (int j = i; j < pl->item_count - 1; j++) pl->items[j] = pl->items[j + 1];
            pl->item_count--;
            i--;
        }
    }
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

void cs2d_sv_spawn_at(cs2d_server_t *sv, int id, float custom_x, float custom_y)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || pl->team < 1) return;

    /* Spawn-time auto-teambalance (FUN_140160740 lines 162-191):
     * when mp_autoteambalance == 1, sv_gamemode != 4 (Zombies) and mode->balance == 1 */
    const sv_mode_t *m = sv_mode(sv);
    if (sv->set.mp_autoteambalance == 1 && sv->set.sv_gamemode != 4 && m->balance == 1) {
        int t = cs2d_sv_count_team(sv, 1);
        int ct = cs2d_sv_count_team(sv, 2);
        int diff = t - ct;
        if (diff < 0) diff = -diff;
        if (diff > 1) {
            if (ct < t && pl->team == 1) {
                pl->team = 2;
                pl->look = cs2d_sv_rand(0, 3);
                pl->pending_team = 0;
                pl->team_changes++;
                uint8_t msg[4] = { 20, (uint8_t)id, 2, (uint8_t)pl->look };
                sv_broadcast_joined(sv, msg, 4);
            } else if (t < ct && pl->team > 1) {
                pl->team = 1;
                pl->look = cs2d_sv_rand(0, 3);
                pl->pending_team = 0;
                pl->team_changes++;
                uint8_t msg[4] = { 20, (uint8_t)id, 1, (uint8_t)pl->look };
                sv_broadcast_joined(sv, msg, 4);
            }
        }
    }

    float x = custom_x, y = custom_y;
    if (x <= 0.0f && y <= 0.0f) {
        if (!sv_pick_spawn(sv, id, &x, &y)) return;
    }
    pl->x = x;
    pl->y = y;
    pl->last_valid_x = x;
    pl->last_valid_y = y;
    pl->last_move_ms = cs2d_sv_now();
    pl->maxhealth = 100;
    pl->health = 100;
    pl->armor = 0;
    pl->speedmod = 0;
    pl->alive = true;
    pl->move_flag = pl->rot_flag = 0;
    pl->is_reloading = false;
    pl->reload_end_ms = 0;
    pl->team_changes = 0;
    pl->name_changes = 0;

    /* Apply deferred name change if queued while dead (FUN_140160740 lines 200-203) */
    if (pl->pending_name[0] != '\0') {
        char pending[64];
        snprintf(pending, sizeof(pending), "%s", pl->pending_name);
        pl->pending_name[0] = '\0';
        cs2d_sv_change_name(sv, id, pending, false, false);
    }

    /* Items: the mode's list or the Lua spawn hook override, then the melee weapon */
    char custom_items[128] = {0};
    int sret = cs2d_sv_lua_hook_spawn(sv, id, custom_items, sizeof(custom_items));
    if (sret == 1) return;

    pl->item_count = 0;
    const char *list = (pl->team == 1) ? m->items_t : m->items_ct;
    if (custom_items[0] != '\0') {
        list = custom_items;
    }
    if (strcmp(list, "x") != 0) {
        for (const char *q = list; *q;) {
            int t = atoi(q);
            if (t > 0 && !sv_has_item(pl, t)) sv_give(pl, t);
            while (*q && *q != ',') q++;
            if (*q == ',') q++;
        }
        int melee = (pl->team == 1) ? m->melee_t : m->melee_ct;
        if (!sv_has_item(pl, melee)) sv_give(pl, melee);
    }
    sv_best_weapon(sv, pl);

    /*
     * Money (FUN_140160740 0x1401618xx): the first spawn (no spawns, no deaths) sets mp_startmoney and
     * clears the seconds counter at player +0x3fc; every later spawn of a respawn mode (DM / TeamDM /
     * Construction / Zombies) adds mp_dmspawnmoney (DAT_1405a6554, default 2000 - the Changelog: "money
     * each spawn, DM/TeamDM/Construction, 2000$ by default") when the counter passed 14, i.e. at most
     * every 15 seconds ("Player spawnmoney time limit (max every 15 sec)"). Clamped to 0..16000.
     */
    if (pl->score == 0 && pl->deaths == 0) {
        pl->money = sv->set.mp_startmoney;
        pl->money_secs = 0;
    } else if (sv_mode(sv)->respawn == 1 && sv->set.mp_dmspawnmoney > 0 && pl->money_secs > 14) {
        pl->money_secs = 0;
        pl->money += sv->set.mp_dmspawnmoney;
    }
    if (pl->money > 16000) pl->money = 16000;
    if (pl->money < 0) pl->money = 0;

    /* [21] to every joined player */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_sv_write_spawn(cs2d_sv_packet(sv, c, true, false), id, pl);
    }
}

void cs2d_sv_spawn(cs2d_server_t *sv, int id)
{
    cs2d_sv_spawn_at(sv, id, 0.0f, 0.0f);
}

void cs2d_sv_team_ex(cs2d_server_t *sv, int id, int team, int look, bool force)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    /* Validation: spectator needs look 5, T / CT a look 0..5, 4 = automatic team */
    bool ok = (team == 0 && look == 5) || ((team == 1 || team == 2) && look < 6) || (team == 4 && look == 5);
    if (!ok) {
        cs2d_server_print("ERROR: team - %s tried to switch to invalid team/look (%d/%d)", pl->name, team, look);
        return;
    }
    if (look != 5 && pl->pending_team > 0) {
        team = pl->pending_team;
        pl->pending_team = 0;
    }
    if (team == 4) {
        /* FUN_1401624c0 LAB_140162b4c: the smaller team, a random one when even */
        int t = cs2d_sv_count_team(sv, 1), ct = cs2d_sv_count_team(sv, 2);
        team = (t == ct) ? cs2d_sv_rand(1, 2) : (ct <= t) + 1;
    }

    if (cs2d_sv_lua_hook_team(sv, id, team, look) == 1 && !force) return;

    /* Per-spawn team change limit (FUN_1401624c0 lines 144-147, 193, 245-254):
     * client cannot switch teams more than once per spawn while on T/CT; answers [20, id, 12, look] */
    bool already_changed = (!force && pl->team_changes > 0 && pl->team >= 1);

    /* Team balance (mp_autoteambalance with a balanced mode): T only while T <= CT + 1, CT while CT <= T + 1;
     * a refusal answers [20, id, 10 / 11, look] to the player alone */
    if (!force && team >= 1 && sv->set.mp_autoteambalance == 1 && sv_mode(sv)->balance && pl->team != team) {
        int t = cs2d_sv_count_team(sv, 1), ct = cs2d_sv_count_team(sv, 2);
        bool refuse = (team == 1) ? (t > ct + 1) : (ct > t + 1);
        if (refuse) {
            if (already_changed) {
                if (look == 5 && (team == 1 || team == 2)) pl->pending_team = team;
                if (c) {
                    cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                    cs2d_pkt_w_byte(p, 20); cs2d_pkt_w_byte(p, (uint8_t)id);
                    cs2d_pkt_w_byte(p, 12); cs2d_pkt_w_byte(p, (uint8_t)look);
                }
                return;
            }
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 20);
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_byte(p, (uint8_t)(team == 1 ? 10 : 11));
                cs2d_pkt_w_byte(p, (uint8_t)look);
            }
            return;
        }
    }

    if (already_changed) {
        if (look == 5 && (team == 1 || team == 2)) pl->pending_team = team;
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 20); cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_byte(p, 12); cs2d_pkt_w_byte(p, (uint8_t)look);
        }
        return;
    }

    /* Zombies mode (sv_gamemode == 4, FUN_1401624c0 lines 266-313):
     * switching between T and CT is forbidden (refusal code 13); joining CT > 30s into round forces T */
    if (!force && sv->set.sv_gamemode == 4) {
        if (pl->team >= 1 && team >= 1) {
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 20); cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_byte(p, 13); cs2d_pkt_w_byte(p, (uint8_t)look);
            }
            return;
        }
        if (team == 2) {
            int t = cs2d_sv_count_team(sv, 1), ct = cs2d_sv_count_team(sv, 2);
            if (t > 0 && ct > 0) {
                float secs = (float)(int)(cs2d_sv_now() - sv->round_start_ms) * 0.001f;
                if ((int)secs > 30) team = 1;
            }
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

    /* Leaving a team alive kills the player (FUN_1401624c0) */
    int old_team = pl->team;
    if (pl->alive && old_team >= 1 && old_team <= 2 && old_team != team) {
        cs2d_sv_kill(sv, id, 0, 0, 0);
    }
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

    /* "Game commencing" (FUN_1401624c0 LAB_14015cff0 = 0x14015cff0): in the standard mode a team change
     * restarts the round with reason 4 and a 3000 ms delay while the player counts are no real match -
     * nobody at all, a single player who is NOT alive, 1 vs 1, or 2 vs 0 / 0 vs 2. A lone player who is
     * alive keeps the running round (t + ct == 1 and alive -> return, the "alive" test of the original's
     * player vtable +0x90; the Changelog entry fixes the old "Game Commencing"-restart of 1 alive player). */
    if (sv->round_end == 0 && sv_mode(sv)->balance == 1 && sv_mode(sv)->respawn == 0) {
        int t = cs2d_sv_count_team(sv, 1);
        int ct = cs2d_sv_count_team(sv, 2);
        int total = t + ct;
        bool restart = false;
        if (total == 0) {
            restart = true;
        } else if (total == 1) {
            restart = true;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                cs2d_sv_player_t *o = &sv->players[i];
                if (o->used && o->team >= 1 && o->alive) { restart = false; break; }
            }
        } else if ((t == 1 && ct == 1) || (t == 2 && ct == 0) || (ct == 2 && t == 0)) {
            restart = true;
        }
        if (restart) cs2d_sv_round_end(sv, 4, 3000);
    }
}

void cs2d_sv_team(cs2d_server_t *sv, int id, int team, int look)
{
    cs2d_sv_team_ex(sv, id, team, look, false);
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
            /* Spectators following this player get the aim too ([70]/[71]/[72], DAT_1405a64ac) */
            bool spec_follow = (!o->alive && o->spectating_player == id);
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
            if (spec_follow) {
                uint8_t aim_scaled = (uint8_t)(int)(pl->aim * 0.512f);
                if (pl->move_flag < 1) {
                    cs2d_pkt_w_byte(p, 70);
                    cs2d_pkt_w_byte(p, (uint8_t)id);
                    cs2d_pkt_w_short(p, rot);
                    cs2d_pkt_w_byte(p, aim_scaled);
                } else if (pl->rot_flag == 0) {
                    cs2d_pkt_w_byte(p, (uint8_t)(pl->move_flag == 1 ? 71 : 72));
                    cs2d_pkt_w_byte(p, (uint8_t)id);
                    cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
                    cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
                    cs2d_pkt_w_byte(p, aim_scaled);
                } else {
                    cs2d_pkt_w_byte(p, 72);
                    cs2d_pkt_w_byte(p, (uint8_t)id);
                    cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
                    cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
                    cs2d_pkt_w_short(p, rot);
                    cs2d_pkt_w_byte(p, aim_scaled);
                }
            } else {
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
        }
        pl->move_flag = 0;
        pl->rot_flag = 0;
    }
}

/* FUN_14015c080: the winner team of a round end type (0 = draw). The type decides the winner and the
 * MVP banner (FUN_14015c840's switch); the last byte of [226] carries the round end *reason*.
 *   1 T win / 2 CT win                                  elimination
 *  10 VIP assassinated (T)  11 VIP escaped (CT)         12 VIP did not escape (T, AS time out)
 *  20 bomb detonated (T)    21 bomb defused (CT)        22 target protected (CT, DE time out)
 *  30 hostages not rescued (T, CS time out)             31 hostages rescued (CT)
 *  40/50 T objective wins   41/51 CT objective wins     60/61 zombies
 */
static int sv_round_winner_of(int type)
{
    switch (type) {
    case 1: case 10: case 12: case 20: case 30: case 40: case 50: case 60: return 1;
    case 2: case 11: case 21: case 22: case 31: case 41: case 51: case 61: return 2;
    default: return 0;      /* 3 draw, 4 game commencing, 5 round restart */
    }
}

/* FUN_14015c840's MVP banner text (the literals of the original's switch) */
static const char *sv_round_mvp_text(int type)
{
    switch (type) {
    case 1: case 2: case 22: return "for most eliminations";
    case 11:                 return "for escaping from the terrorists";
    case 12:                 return "for killing the VIP";
    case 20:                 return "for detonating the bomb";
    case 21:                 return "for defusing the bomb";
    case 31:                 return "for rescuing most hostages";
    case 60:                 return "for most infections";
    case 61:                 return "for killing most zombies";
    default:                 return " - ";
    }
}

/* FUN_14015c080: the best player of one team (kills * 2 + assists, the highest value wins). The
 * original reads its Kills / Assists stats; our port has no separate kill counter yet, so score
 * (1 per enemy kill) stands in. Team 2 only: the VIP (team 3) never becomes the MVP. */
static int sv_best_of_team(cs2d_server_t *sv, int team)
{
    int best = 0, best_val = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || pl->team != team) continue;
        int pts = pl->score * 2 + pl->assists;
        if (pts > best_val) { best_val = pts; best = i; }
    }
    return best;
}

/* FUN_14015c840(type, mode, delay, player): end the round. `type` picks winner and MVP
 * (FUN_14015c080), `mode` is the reason byte of [226] and the reason handed to the next round,
 * `player_id` is the planter / defuser (r9 of the bomb calls) or the best rescuer (type 31).
 * type 0 is the original's silent reset (FUN_14015c840(0, -1, 3000, null)). */
void cs2d_sv_round_end_ex(cs2d_server_t *sv, int type, int mode, int delay_ms, int player_id)
{
    if (type < 0 || sv->round_end != 0) return;

    if (type == 0) {                    /* flush only, no banner (FUN_14015c840 param_1 == 0) */
        sv->round_end = 0;
        sv->round_winner = 0;
        sv->round_mvp = 0;
        return;
    }

    sv->round_end       = (mode != 0) ? mode : type;
    sv->round_end_type  = type;
    sv->round_end_delay = delay_ms;
    sv->round_end_ms    = cs2d_sv_now();
    sv->round_winner    = sv_round_winner_of(type);
    sv->round_mvp       = 0;

    /* FUN_14015c080: the MVP plus its +1 mvp count ([42, id, 10, n]) */
    int mvp = 0;
    switch (type) {
    case 1:  mvp = sv_best_of_team(sv, 1); break;
    case 2:  mvp = sv_best_of_team(sv, 2); break;
    case 20: case 21: mvp = player_id; break;    /* the planter / the defuser */
    case 31: mvp = player_id; break;             /* the best rescuer */
    case 60: mvp = sv_best_of_team(sv, 1); break;
    case 61: mvp = sv_best_of_team(sv, 2); break;
    default: break;    /* 3, 10, 11, 12, 22, 30, 40, 41, 50, 51: no MVP (FUN_14015c080) */
    }
    if (mvp >= 1 && mvp <= CS2D_SV_MAX_PLAYERS && sv->players[mvp].used) {
        sv->round_mvp = mvp;
        cs2d_sv_set_playerdata(sv, mvp, 10, sv->players[mvp].mvp + 1);
    }

    /* [226, 1, winner, mvp, mvp_text (lstr), reason] to every joined player */
    const char *mvp_text = sv_round_mvp_text(type);
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 226);
        cs2d_pkt_w_byte(p, 1);
        cs2d_pkt_w_byte(p, (uint8_t)sv->round_winner);
        cs2d_pkt_w_byte(p, (uint8_t)sv->round_mvp);
        cs2d_pkt_w_lstr(p, mvp_text);
        cs2d_pkt_w_byte(p, (uint8_t)sv->round_end);
    }
    cs2d_sv_lua_hook_endround(sv, sv->round_end);
}

/* FUN_14015c840 with type == mode (every standard mode call site of the original) */
void cs2d_sv_round_end(cs2d_server_t *sv, int mode, int delay_ms)
{
    cs2d_sv_round_end_ex(sv, mode, mode, delay_ms, 0);
}

/* DAT_1407dfac0 / 1407dfac4 / 1407dfac8: the mission entity counts of the map (the numbers the Lua
 * map("mission_bombspots" / "mission_hostages" / "mission_vips") queries return). They decide the
 * standard mode time out (0x1401a31e0: vips > 0 -> 12, hostages > 0 -> 30, bombspots > 0 -> 22). */
void cs2d_sv_count_missions(cs2d_server_t *sv)
{
    sv->mission_bombspots = 0;
    sv->mission_hostages = 0;
    sv->mission_vips = 0;
    for (uint32_t e = 0; e < sv->map.entity_count; e++) {
        switch (sv->map.entities[e].type) {
        case CS2D_ENT_INFO_BOMBSPOT: sv->mission_bombspots++; break;
        case CS2D_ENT_INFO_HOSTAGE:  sv->mission_hostages++;  break;
        case CS2D_ENT_INFO_VIP:      sv->mission_vips++;      break;
        default: break;
        }
    }
}

/* 0x1401717a1 / 0x140174254: one counter-terrorist becomes the V.I.P. each round. He is team 3 (a CT
 * for the client), gets 200 armor (0xc8 through the armor object, 0x1401717d1) and - per
 * help/howtoplay.txt - "a knife, a pistol and extra armor": the CT spawn list (pistol + knife) is his
 * inventory and the buy menu refuses team 3. The team is set before the spawn so the VIP uses an
 * Info_VIP spawn point; the armor and the [20] team update follow the spawn. */
static int sv_pick_vip(cs2d_server_t *sv)
{
    sv->vip_id = 0;

    /* the previous round's VIP is a normal counter-terrorist again until a new one is picked */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && sv->players[i].team == 3) sv->players[i].team = 2;
    }

    if (sv->set.sv_gamemode != 0 || sv->mission_vips <= 0) return 0;

    int cts[CS2D_SV_MAX_PLAYERS], n = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (sv->players[i].used && sv->players[i].team == 2) cts[n++] = i;
    }
    if (n == 0) return 0;

    int id = cts[cs2d_sv_rand(0, n - 1)];
    sv->players[id].team = 3;
    sv->vip_id = id;
    return id;
}

static void sv_announce_vip(cs2d_server_t *sv)
{
    if (sv->vip_id < 1 || sv->vip_id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[sv->vip_id];
    if (!pl->used) return;
    cs2d_sv_set_playerdata(sv, sv->vip_id, 3, 200);          /* [42, id, 3, 200]: the 200 armor */
    uint8_t pkt[4] = { 20, (uint8_t)sv->vip_id, 3, (uint8_t)pl->look };
    sv_broadcast_joined(sv, pkt, 4);                         /* the VIP is team 3 */
    cs2d_server_print("%s is the V.I.P.\n", pl->name);
}

void cs2d_sv_assign_vip(cs2d_server_t *sv)
{
    sv_pick_vip(sv);
    sv_announce_vip(sv);
}

/* 0x14015d230 / 0x140160500 (VIP death) and 0x140188167 (VIP escape): the two VIP objectives. The
 * death is reason 10 (T win, 5000 ms), the escape reason 11 (CT win, 3000 ms). */
void cs2d_sv_vip_check(cs2d_server_t *sv)
{
    if (sv->set.sv_gamemode != 0) return;
    if (sv->round_end != 0 || sv->round_phase == 0) return;
    if (sv->vip_id < 1 || sv->vip_id > CS2D_SV_MAX_PLAYERS) return;

    cs2d_sv_player_t *vip = &sv->players[sv->vip_id];
    if (!vip->used) return;

    if (!vip->alive) {                     /* 0x14015d230: the VIP died -> terrorists win */
        if (sv->round_end == 0) cs2d_sv_round_end(sv, 10, 5000);
        return;
    }

    /* the VIP standing on an Info_EscapePoint escapes (the same 2 tile tolerance the hostages use;
     * the original reads the escape entity at the VIP's tile) */
    int tx = (int)(vip->x * 0.03125f), ty = (int)(vip->y * 0.03125f);
    for (uint32_t e = 0; e < sv->map.entity_count; e++) {
        if (sv->map.entities[e].type != CS2D_ENT_INFO_ESCAPE) continue;
        if (abs((int)sv->map.entities[e].x - tx) <= 2 && abs((int)sv->map.entities[e].y - ty) <= 2) {
            cs2d_sv_round_end(sv, 11, 3000);
            return;
        }
    }
}

/* 0x14015ff80 (FUN_14015ffb0's tail): a hostage that reached an Info_RescuePoint. The round is won
 * by the counter-terrorists as soon as MORE THAN HALF of the map's hostages are rescued
 * (rescued / mission_hostages > 0.5, the float 0x1406da614 = 0.5f): reason 31, 3000 ms. */
void cs2d_sv_hostage_escaped(cs2d_server_t *sv, int h_id, int ct_id)
{
    (void)h_id;
    int total = sv->mission_hostages > 0 ? sv->mission_hostages : sv->hostage_count;
    if (total <= 0) return;
    if ((float)sv->hostages_rescued / (float)total > 0.5f) {
        cs2d_sv_round_end_ex(sv, 31, 31, 3000, ct_id);
    }
}

/* DAT_1407dfea0: 0 = freezetime (the original rejects movement, 0x1401c9a02), 1 = playing,
 * 2 = the round is decided (the round clock runs on, the time out no longer ends the round). */
static bool sv_round_frozen(const cs2d_server_t *sv)
{
    /* round_start_ms is only set by cs2d_sv_round_start; a server that never started a round (the
     * unit tests build the state directly) is never frozen */
    return sv->round_phase == 0 && sv->round_start_ms != 0;
}

static void sv_step_phase(cs2d_server_t *sv)
{
    if (sv->round_phase != 0 || sv->round_end != 0) return;
    if (sv->round_start_ms == 0) { sv->round_phase = 1; return; }
    uint32_t freeze_ms = (uint32_t)sv->set.mp_freezetime * 1000u;
    if (cs2d_sv_now() - sv->round_phase_ms < freeze_ms) return;

    sv->round_phase = 1;
    sv->round_phase_ms = cs2d_sv_now();
    sv->round_clock_ms = sv->round_phase_ms;   /* mp_roundtime counts from the end of the freeze */
}

/* FUN_1402520b0 / FUN_140123530: start of a new round, respawning players and sending [22] */
void cs2d_sv_round_start(cs2d_server_t *sv, int reason)
{
    sv->round_end = 0;
    sv->round_winner = 0;
    sv->round_mvp = 0;
    sv->round_start_ms = cs2d_sv_now();
    sv->round_number++;
    cs2d_sv_lua_hook_startround(sv, reason);

    /* Standard mode: the map's mission counters and this round's VIP (0x1401717a1). The new round
     * starts in the freezetime phase (mp_freezetime); the round clock begins when it ends
     * (0x1401a326e sets DAT_1407dfea0 = 1 and the players then may move, 0x1401c9a02). */
    cs2d_sv_count_missions(sv);
    sv_pick_vip(sv);
    sv->round_phase = 0;
    sv->round_phase_ms = sv->round_start_ms;
    sv->round_clock_ms = sv->round_start_ms;
    sv->hostages_rescued = 0;

    /* Reset bomb state (Phase 5) */
    sv->bomb_planted = false;
    sv->bomb_item_id = 0;
    sv->bomb_explode_time = 0;
    sv->bomb_planter = 0;
    sv->defuser_id = 0;
    sv->defuse_start_ms = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        sv->players[i].is_planting = false;
        sv->players[i].is_defusing = false;
    }
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (sv->items[i].active && sv->items[i].type == CS2D_WEP_PLANTEDBOMB) {
            sv->items[i].active = false;
        }
    }
    cs2d_sv_init_hostages(sv);

    /* Update team scores according to round end reason (FUN_1402520b0 / cs2d_game_apply_round_reason) */
    uint64_t bit = (reason >= 0 && reason <= 61) ? (1ULL << reason) : 0;
    if (bit & 0x1004010040101000ULL) {
        sv->score_t++;
    } else if ((bit & 0x2008020080000804ULL) || reason == 21 || reason == 22) {
        sv->score_ct++;
    } else if (bit & 0x30ULL) {
        sv->score_t = sv->score_ct = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            pl->score = pl->assists = pl->deaths = pl->mvp = 0;
        }
    }

    /* Hook startround_prespawn */
    cs2d_sv_lua_hook_startround_prespawn(sv, reason);

    /* Respawn all team players */
    int count = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (pl->used && pl->team >= 1) {
            count++;
            float x, y;
            if (sv_pick_spawn(sv, i, &x, &y)) {
                pl->x = x;
                pl->y = y;
            }
            pl->health = 100;
            pl->armor = 0;
            pl->alive = true;
            pl->move_flag = pl->rot_flag = 0;
            pl->is_reloading = false;
            pl->reload_end_ms = 0;

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
        }
    }

    /* The C4 goes to a random terrorist in the standard mode (the map has Info_BombSpot entities) */
    if (sv->mission_bombspots > 0 && sv->set.sv_gamemode == 0) {
        int t_ids[CS2D_SV_MAX_PLAYERS];
        int t_cnt = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (sv->players[i].used && sv->players[i].team == 1 && sv->players[i].alive) {
                t_ids[t_cnt++] = i;
            }
        }
        if (t_cnt > 0) {
            int pick = t_ids[cs2d_sv_rand(0, t_cnt - 1)];
            cs2d_sv_player_t *tpl = &sv->players[pick];
            if (!sv_has_item(tpl, CS2D_WEP_BOMB)) sv_give(tpl, CS2D_WEP_BOMB);
            sv_best_weapon(sv, tpl);
        }
    }

    sv_announce_vip(sv);   /* [42, id, 3, 200] + [20, id, 3, look] after the spawn */

    /* Send [22, round_reason u8, count u8] + count * 12-byte player records + recipient inventory */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 22);
        cs2d_pkt_w_byte(p, (uint8_t)reason);
        cs2d_pkt_w_byte(p, (uint8_t)count);

        for (int q = 1; q <= CS2D_SV_MAX_PLAYERS; q++) {
            cs2d_sv_player_t *pl = &sv->players[q];
            if (!pl->used || pl->team < 1) continue;
            cs2d_pkt_w_byte(p, (uint8_t)q);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->x);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->y);
            float angle = pl->rot;
            uint32_t angle_bits;
            memcpy(&angle_bits, &angle, 4);
            cs2d_pkt_w_int(p, (int32_t)angle_bits);
            cs2d_pkt_w_byte(p, (uint8_t)pl->weapon);
            cs2d_pkt_w_byte(p, (uint8_t)pl->armor);
            cs2d_pkt_w_byte(p, (uint8_t)pl->special);
        }

        /* Inventory of the recipient */
        cs2d_sv_player_t *recip = &sv->players[i];
        cs2d_pkt_w_short(p, (uint16_t)recip->money);
        cs2d_pkt_w_byte(p, (uint8_t)recip->item_count);
        for (int k = 0; k < recip->item_count; k++) {
            cs2d_pkt_w_byte(p, recip->items[k]);
        }
    }
}

/* FUN_14015ce30(0): the standard mode's win conditions. A team without players cannot play the round
 * (draw, LAB_14015d0c7), nobody alive is a draw or - with the C4 already ticking - a terrorist win
 * (LAB_14015d0a5), an empty terrorist force is a CT win (2), an empty counter-terrorist force a T win (1).
 *
 * The original calls this from **player events only**: the check at 0x140150859 sits in the
 * player-state function FUN_1401502a0 (called on spawn / death / join / leave) and the team change
 * calls it at 0x140164da3 - its per-frame tick (0x1401a2xxx) never does. Measured on
 * cs2d_dedicated.exe 1.0.1.8 p1 (2026-10-16, tools/agent/round_timing.py +
 * tools/agent/lua/round_probe.lua): a server with 0 or 1 players ends no round by itself; the round of
 * a lone player ends only when he dies (`killplayer 1` -> draw 3, 3000 ms) and then keeps running.
 * Re-checking every frame, as we did before, restarted the round of an empty or one-player server
 * every 3 s (the round itself lasted ~20 ms, so the player never got to play). */
void cs2d_sv_check_win(cs2d_server_t *sv)
{
    if (sv->round_end != 0) return;

    const sv_mode_t *m = sv_mode(sv);
    if (m->respawn != 0 || m->balance != 1) return;

    int t_total = sv_count_side(sv, 1);
    int ct_total = sv_count_side(sv, 2);

    int t_alive = 0, ct_alive = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->alive) continue;
        if (pl->team == 1) t_alive++;
        else if (pl->team >= 2) ct_alive++;         /* the VIP counts for the CT side */
    }

    /* A team without players cannot play the round: draw (FUN_14015ce30 LAB_14015d0c7) */
    if (t_total == 0 || ct_total == 0) {
        cs2d_sv_round_end(sv, 3, 3000);
        return;
    }

    /* Elimination: the bomb already planted keeps the round alive for the terrorists */
    if (t_alive + ct_alive == 0) {                  /* nobody alive */
        if (sv->bomb_planted) cs2d_sv_round_end(sv, 1, 5000);
        else                  cs2d_sv_round_end(sv, 3, 3000);
        return;
    }
    if (t_alive == 0) {                             /* all terrorists dead */
        if (!sv->bomb_planted) cs2d_sv_round_end(sv, 2, 5000);
        return;
    }
    if (ct_alive == 0) {                            /* all counter-terrorists dead */
        cs2d_sv_round_end(sv, 1, 5000);
        return;
    }
}

/* Called every server tick: the freezetime phase, the pending round end delay and - in the standard
 * mode - the per-frame objectives with the mission time out (0x1401a3128). The elimination rules are
 * not checked here; they belong to the player events (cs2d_sv_check_win). */
void cs2d_sv_step_round(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();
    if (sv->round_end != 0) {
        if (now - sv->round_end_ms >= (uint32_t)sv->round_end_delay) {
            cs2d_sv_round_start(sv, sv->round_end);
        }
        return;
    }

    sv_step_phase(sv);                      /* freezetime -> playing (DAT_1407dfea0, 0x1401a326e) */
    if (sv_round_frozen(sv)) return;        /* frozen: no movement (0x1401c9a02), no objectives */

    const sv_mode_t *m = sv_mode(sv);
    if (m->respawn == 0 && m->balance == 1) {
        /* The mission objectives (VIP escape / death, hostage rescue) are checked every tick */
        cs2d_sv_vip_check(sv);

        /* Time out (0x1401a3128): skipped while the C4 ticks; the map's mission type decides the winner
         * (vips -> 12 "did not escape", hostages -> 30 "not rescued", bombspots -> 22 "protected",
         * no mission -> draw). mp_roundtime 100 = endless (0x1401a3137). */
        if (sv->round_end == 0 && !sv->bomb_planted &&
            sv->set.mp_roundtime > 0 && sv->set.mp_roundtime < 100) {
            uint32_t round_duration_ms = (uint32_t)sv->set.mp_roundtime * 60000u;
            uint32_t clock = sv->round_clock_ms ? sv->round_clock_ms : sv->round_start_ms;
            if (now - clock >= round_duration_ms) {
                if (sv->mission_vips > 0)           cs2d_sv_round_end(sv, 12, 5000);
                else if (sv->mission_hostages > 0)  cs2d_sv_round_end(sv, 30, 5000);
                else if (sv->mission_bombspots > 0) cs2d_sv_round_end(sv, 22, 5000);
                else                                cs2d_sv_round_end(sv, 3, 3000);
            }
        }
    }
}


/* FUN_140158060: player selects/switches weapon -> [9, player, weapon, mode] reliable to others */
void cs2d_sv_select_weapon(cs2d_server_t *sv, int id, int weapon, int mode)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    /* Verify player owns the weapon or default melee */
    if (!sv_has_item(pl, weapon)) {
        int melee = (pl->team == 1) ? sv_mode(sv)->melee_t : sv_mode(sv)->melee_ct;
        if (weapon != melee) return;
    }

    if (pl->weapon != weapon) {
        pl->weapon = weapon;
        if (pl->is_reloading) {
            pl->is_reloading = false;
            pl->reload_end_ms = 0;
            uint8_t abort_msg[3] = { 16, (uint8_t)id, 3 };
            sv_broadcast_joined(sv, abort_msg, sizeof(abort_msg));
        }
    }
    cs2d_sv_lua_hook_select(sv, id, weapon, mode);

    /* Broadcast [9, id, weapon, mode] reliable to every other joined player */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (i == id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 9);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)weapon);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
    }
}

/* FUN_140191a70: player fires primary attack -> [7, player, seed] unreliable to others */
void cs2d_sv_fire(cs2d_server_t *sv, int id, int seed)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;
    if (sv_round_frozen(sv)) return;   /* DAT_1407dfea0 == 0: no attacks during the freezetime */

    /* FUN_140191a70 / Tpl.decammo (0x140003260): deduct bullet on firearm shot */
    if (pl->weapon > 0) {
        const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
        if (def && def->slot != CS2D_SLOT_MELEE && pl->weapon != CS2D_WEP_WRENCH &&
            pl->weapon != CS2D_WEP_PORTALGUN && def->clip_size > 0) {
            if (pl->clip[pl->weapon] > 0) {
                pl->clip[pl->weapon]--;
            }
        }
    }

    cs2d_sv_lua_hook_attack(sv, id);

    /* Construction: the Wrench's primary attack repairs (or upgrades) an own building in reach instead
     * of shooting (Tshoot.Shoot weapon 0x4a). Every other weapon - bullets and melee alike - damages the
     * building the swing reaches. */
    if (pl->weapon == CS2D_WEP_WRENCH) {
        cs2d_sv_wrench_hit(sv, id);
    } else {
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get((uint8_t)pl->weapon);
        if (wdef) cs2d_sv_shot_at_building(sv, id, wdef);
    }

    /* Broadcast [7, id, seed] unreliable to other joined players */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (i == id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
        cs2d_pkt_w_byte(p, 7);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)seed);
    }
}

/* FUN_1401fd590: player performs secondary attack -> [8, player, mode] reliable to others */
void cs2d_sv_attack2(cs2d_server_t *sv, int id, int mode)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    cs2d_sv_lua_hook_attack2(sv, id, mode);

    /* Broadcast [8, id, mode] reliable to other joined players */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (i == id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 8);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
    }
}

/* FUN_1401647d0: player killed -> update scores, alive=false, broadcast reliable [19, victim, killer, assist, weapon, x, y] */
void cs2d_sv_kill(cs2d_server_t *sv, int victim_id, int killer_id, int assist_id, int weapon)
{
    if (victim_id < 1 || victim_id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *v = &sv->players[victim_id];
    if (!v->used || !v->alive) return;

    v->health = 0;
    v->alive = false;
    v->is_reloading = false;
    v->reload_end_ms = 0;
    v->death_ms = cs2d_sv_now();
    if (weapon != 0xfc) v->deaths++;

    cs2d_sv_lua_hook_kill(sv, killer_id, victim_id, weapon, (int)v->x, (int)v->y, 0);
    cs2d_sv_lua_hook_die(sv, victim_id, killer_id, weapon, (int)v->x, (int)v->y, 0);

    cs2d_sv_player_t *k = (killer_id >= 1 && killer_id <= CS2D_SV_MAX_PLAYERS && sv->players[killer_id].used)
                        ? &sv->players[killer_id] : NULL;

    if (k) {
        bool enemies = (v->team == 1 && k->team >= 2) || (v->team >= 2 && k->team == 1);
        if (enemies) {
            k->score++;
            k->money += 300;
            if (k->money > 16000) k->money = 16000;
            if (assist_id >= 1 && assist_id <= CS2D_SV_MAX_PLAYERS && sv->players[assist_id].used) {
                sv->players[assist_id].assists++;
            }
        } else {
            /* Friendly fire or suicide / world */
            unsigned w = (unsigned)weapon - 72u;
            if (k != v && weapon != 0xff && !(w < 16 && ((0x8033u >> w) & 1))) {
                if (sv->set.sv_gamemode != 1) {
                    k->score--;
                    k->money -= 3300;
                    if (k->money < 0) k->money = 0;
                } else {
                    k->score++;
                }
            }
        }
    }

    uint8_t msg[9];
    msg[0] = 19;
    msg[1] = (uint8_t)victim_id;
    msg[2] = (uint8_t)killer_id;
    msg[3] = (uint8_t)assist_id;
    msg[4] = (uint8_t)weapon;
    uint16_t x = (uint16_t)(int)v->x;
    uint16_t y = (uint16_t)(int)v->y;
    msg[5] = (uint8_t)(x & 0xff);
    msg[6] = (uint8_t)((x >> 8) & 0xff);
    msg[7] = (uint8_t)(y & 0xff);
    msg[8] = (uint8_t)((y >> 8) & 0xff);

    sv_broadcast_joined(sv, msg, sizeof(msg));

    /* The death is what calls the win check in the original: FUN_140160500 (inside the death path,
     * `cmp [pid+0x230], 3`) and the tails at 0x1401647c4 / 0x1401840d1 both end with
     * `if (no round end pending) return FUN_14015ce30(0)`. A V.I.P. death has its own reason (10,
     * "assassinated") and therefore comes first (0x140160530 -> round_end(10,10,5000)).
     * Measured: `killplayer 1` on a one-player standard server ends the round immediately. */
    cs2d_sv_vip_check(sv);
    cs2d_sv_check_win(sv);
}

/* FUN_1401658c0 / [17]: apply damage to victim and broadcast [17, victim, attacker, new_hp, new_arm] */
void cs2d_sv_damage(cs2d_server_t *sv, int victim_id, int attacker_id, int dmg, int weapon)
{
    if (victim_id < 1 || victim_id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *v = &sv->players[victim_id];
    if (!v->used || !v->alive || dmg <= 0) return;

    if (cs2d_sv_lua_hook_hit(sv, victim_id, attacker_id, weapon, dmg, dmg, dmg) == 1) return;

    int remaining_dmg = dmg;
    if (v->armor > 0) {
        /* Default kevlar absorbs ~35% of damage (armor_penetration 0.65) */
        int absorbed = (int)((float)dmg * 0.35f);
        if (absorbed > v->armor) absorbed = v->armor;
        v->armor -= absorbed;
        remaining_dmg -= absorbed;
    }

    v->health -= remaining_dmg;
    if (v->health <= 0) {
        v->health = 0;
        /* Broadcast final damage state before or as part of kill */
        uint8_t dmg_msg[5] = { 17, (uint8_t)victim_id, (uint8_t)attacker_id, (uint8_t)v->health, (uint8_t)v->armor };
        sv_broadcast_joined(sv, dmg_msg, sizeof(dmg_msg));
        cs2d_sv_kill(sv, victim_id, attacker_id, 0, weapon);
        return;
    }

    uint8_t dmg_msg[5] = { 17, (uint8_t)victim_id, (uint8_t)attacker_id, (uint8_t)v->health, (uint8_t)v->armor };
    sv_broadcast_joined(sv, dmg_msg, sizeof(dmg_msg));
}

/* FUN_1401cd710: player buy request -> validate and respond [23, id, type, money u16, 0] */
void cs2d_sv_buy(cs2d_server_t *sv, int id, int weapon_id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;
    if (pl->team == 3) return;   /* the V.I.P. "can't buy any equipment" (help/howtoplay.txt) */

    if (cs2d_sv_lua_hook_buy(sv, id, weapon_id) == 1) return;

    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)weapon_id);
    if (!def || pl->money < def->price) {
        /* Refusal: type = 250 */
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 23);
            cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_byte(p, 250);
            cs2d_pkt_w_short(p, (uint16_t)pl->money);
            cs2d_pkt_w_byte(p, 0);
        }
        return;
    }

    /* Deduct price */
    pl->money -= def->price;

    /* Add item / special / armor to player */
    if (def->slot != CS2D_SLOT_ITEM) {
        sv_give(pl, weapon_id);
        pl->weapon = weapon_id;
    } else if (weapon_id == CS2D_WEP_DEFUSEKIT && pl->team == 2) {
        pl->special |= 1;
    } else if (weapon_id == CS2D_WEP_KEVLAR) {
        pl->armor = 100;
    } else if (weapon_id == CS2D_WEP_KEVLARHELM) {
        pl->armor = 100;
    } else if (weapon_id == CS2D_WEP_NIGHTVISION) {
        pl->special |= 2;
    } else if (weapon_id == CS2D_WEP_GASMASK) {
        pl->special |= 16;
    }

    /* Send success [23, id, weapon_id, money, 0] to the buyer if connected */
    if (c) {
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 23);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)weapon_id);
        cs2d_pkt_w_short(p, (uint16_t)pl->money);
        cs2d_pkt_w_byte(p, 0);
    }

    /* If held weapon changed, broadcast [9, id, weapon, 0] to others */
    if (def->slot != CS2D_SLOT_ITEM) {
        cs2d_sv_select_weapon(sv, id, weapon_id, 0);
    }
}

/* FUN_14016bbd0: player drops a carried item -> [24, pid, type, clip, res, sub, item_id, tx, ty, held_weapon] */
void cs2d_sv_drop(cs2d_server_t *sv, int id, int type, int clip, int reserve, int sub)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;
    if (!sv_has_item(pl, type)) return;

    /* Melee weapons cannot be dropped: Knife (50), Wrench (74), Claw (78) */
    if (type == CS2D_WEP_KNIFE || type == 74 || type == 78) return;

    if (cs2d_sv_lua_hook_drop(sv, id, 0, type, reserve, clip) == 1) return;

    /* Remove from player's inventory */
    int idx = -1;
    for (int i = 0; i < pl->item_count; i++) {
        if (pl->items[i] == type) { idx = i; break; }
    }
    if (idx >= 0) {
        for (int i = idx; i < pl->item_count - 1; i++) pl->items[i] = pl->items[i + 1];
        pl->item_count--;
    }
    pl->clip[type] = 0;
    pl->reserve[type] = 0;

    /* Clear special byte bit 1 if bomb or defuse kit */
    if (type == CS2D_WEP_BOMB || type == CS2D_WEP_DEFUSEKIT) {
        pl->special &= ~1;
    }

    /* If held weapon was dropped, switch to next best weapon */
    if (pl->weapon == type) {
        sv_best_weapon(sv, pl);
    }

    /* Allocate ground item */
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    if (sv->next_item_id == 0) sv->next_item_id = 1;
    uint16_t item_id = (uint16_t)sv->next_item_id++;

    int tx = (int)(pl->x * 0.03125f);
    int ty = (int)(pl->y * 0.03125f);
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;

    cs2d_sv_item_t *it = &sv->items[slot];
    it->active = true;
    it->id = item_id;
    it->type = (uint8_t)type;
    it->clip = (uint16_t)clip;
    it->reserve = (uint16_t)reserve;
    it->sub = (uint8_t)sub;
    it->tile_x = (uint16_t)tx;
    it->tile_y = (uint16_t)ty;
    it->dropped = true;
    it->drop_time_ms = cs2d_sv_now();
    it->dropped_by = id;
    it->drop_tx = (uint16_t)tx;
    it->drop_ty = (uint16_t)ty;

    /* Broadcast [24, id, type, clip u16, reserve u16, sub u8, item_id u16, tx u16, ty u16, held_weapon u8] */
    uint8_t msg[15];
    msg[0] = 24;
    msg[1] = (uint8_t)id;
    msg[2] = (uint8_t)type;
    msg[3] = (uint8_t)(clip & 0xff);
    msg[4] = (uint8_t)((clip >> 8) & 0xff);
    msg[5] = (uint8_t)(reserve & 0xff);
    msg[6] = (uint8_t)((reserve >> 8) & 0xff);
    msg[7] = (uint8_t)sub;
    msg[8] = (uint8_t)(item_id & 0xff);
    msg[9] = (uint8_t)((item_id >> 8) & 0xff);
    msg[10] = (uint8_t)(tx & 0xff);
    msg[11] = (uint8_t)((tx >> 8) & 0xff);
    msg[12] = (uint8_t)(ty & 0xff);
    msg[13] = (uint8_t)((ty >> 8) & 0xff);
    msg[14] = (uint8_t)pl->weapon;

    sv_broadcast_joined(sv, msg, sizeof(msg));
}

/* Ground item pickups [25] and 15s drop lifetime fadeout [40] (FUN_14004cd20) */
void cs2d_sv_step_items(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();

    /* 1. Item fadeout [40] for dropped items older than 15s (0x140a14d94) */
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        cs2d_sv_item_t *it = &sv->items[i];
        if (!it->active || !it->dropped) continue;
        if (now - it->drop_time_ms >= 15000) {
            it->active = false;
            uint8_t msg[3] = { 40, (uint8_t)(it->id & 0xff), (uint8_t)((it->id >> 8) & 0xff) };
            sv_broadcast_joined(sv, msg, sizeof(msg));
        }
    }

    /* 2. Item collection [25] */
    for (int pid = 1; pid <= CS2D_SV_MAX_PLAYERS; pid++) {
        cs2d_sv_player_t *pl = &sv->players[pid];
        if (!pl->used || !pl->alive || pl->team < 1) continue;

        int ptx = (int)(pl->x * 0.03125f);
        int pty = (int)(pl->y * 0.03125f);

        for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
            cs2d_sv_item_t *it = &sv->items[i];
            if (!it->active) continue;
            if (it->tile_x != ptx || it->tile_y != pty) continue;
            /* Player who dropped the item cannot pick it back up on the drop tile until moving away */
            if (it->dropped && it->dropped_by == pid && it->drop_tx == ptx && it->drop_ty == pty) continue;

            const cs2d_weapon_def_t *def = cs2d_weapon_get(it->type);
            if (!def) continue;
            /* The planted C4 (63) is the round objective, not a pickup: no branch below matches its
             * CS2D_SLOT_ITEM, so it used to fall through to the generic "does the player own it"
             * check and the planter - who keeps standing on the tile he planted - collected it on
             * the next tick. The server then sent [25] and every client removed the ground item, so
             * neither team could see or defuse the bomb (the round only ended with the explosion).
             * The original only ever touches it with the defuse use ([26, 3]). */
            if (it->type == CS2D_WEP_PLANTEDBOMB) continue;

            bool can_pickup = false;
            if (def->slot == CS2D_SLOT_PRIMARY) {
                bool has_primary = false;
                for (int k = 0; k < pl->item_count; k++) {
                    if (cs2d_weapon_hud_slot(pl->items[k]) == 1) { has_primary = true; break; }
                }
                if (!has_primary) can_pickup = true;
            } else if (def->slot == CS2D_SLOT_SECONDARY) {
                bool has_secondary = false;
                for (int k = 0; k < pl->item_count; k++) {
                    if (cs2d_weapon_hud_slot(pl->items[k]) == 2) { has_secondary = true; break; }
                }
                if (!has_secondary) can_pickup = true;
            } else if (it->type == CS2D_WEP_BOMB) {
                if (pl->team == 1 && !sv_has_item(pl, CS2D_WEP_BOMB)) {
                    can_pickup = true;
                    pl->special |= 1;
                }
            } else if (it->type == CS2D_WEP_DEFUSEKIT) {
                if (pl->team == 2 && !(pl->special & 1)) {
                    can_pickup = true;
                    pl->special |= 1;
                }
            } else if (it->type == CS2D_WEP_KEVLAR || it->type == CS2D_WEP_KEVLARHELM) {
                if (pl->armor < 100) {
                    can_pickup = true;
                    pl->armor = 100;
                }
            } else if (def->slot == CS2D_SLOT_GRENADE) {
                can_pickup = true;
            } else {
                if (!sv_has_item(pl, it->type)) can_pickup = true;
            }

            if (can_pickup) {
                if (cs2d_sv_lua_hook_collect(sv, pid, it->id, it->type, it->reserve, it->clip) == 1) continue;
                sv_give(pl, it->type);
                pl->clip[it->type] = (int16_t)it->clip;
                pl->reserve[it->type] = (int16_t)it->reserve;
                if (def->slot == CS2D_SLOT_PRIMARY || def->slot == CS2D_SLOT_SECONDARY) {
                    pl->weapon = it->type;
                }
                uint16_t taken_id = it->id;
                uint8_t type = it->type;
                uint16_t clip = it->clip;
                uint16_t reserve = it->reserve;
                it->active = false;

                /* Broadcast [25, player, item_id u16, type u8, clip u16, reserve u16, 0 u8] */
                uint8_t msg[10];
                msg[0] = 25;
                msg[1] = (uint8_t)pid;
                msg[2] = (uint8_t)(taken_id & 0xff);
                msg[3] = (uint8_t)((taken_id >> 8) & 0xff);
                msg[4] = type;
                msg[5] = (uint8_t)(clip & 0xff);
                msg[6] = (uint8_t)((clip >> 8) & 0xff);
                msg[7] = (uint8_t)(reserve & 0xff);
                msg[8] = (uint8_t)((reserve >> 8) & 0xff);
                msg[9] = 0;
                sv_broadcast_joined(sv, msg, sizeof(msg));
                break;
            }
        }
    }
}

/* FUN_14018b1c0: reload message [16, id, mode] */
void cs2d_sv_reload(cs2d_server_t *sv, int id, int mode)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    if (pl->weapon <= 0) return;
    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
    if (!def) return;

    uint32_t now = cs2d_sv_now();

    if (mode == 1 || mode == 100) {
        /* Start reload: mode 1 or 100 */
        if (def->slot == CS2D_SLOT_MELEE || def->clip_size <= 0) return;
        if (pl->clip[pl->weapon] >= def->clip_size) return;
        if (pl->reserve[pl->weapon] <= 0 && sv->set.mp_infammo == 0) return;
        if (pl->is_reloading) return;
        if (pl->is_planting || pl->is_defusing) return;

        pl->is_reloading = true;
        uint32_t duration_ms = (uint32_t)(def->reload_ticks * 5);
        pl->reload_end_ms = now + duration_ms;
    } else if (mode == 2) {
        /* Clip weapon finished reloading */
        if (!pl->is_reloading) {
            mode = 3; /* Abort */
        } else {
            pl->is_reloading = false;
            pl->reload_end_ms = 0;
            if (def->clip_size > 0) {
                if (sv->set.mp_infammo != 0) {
                    pl->clip[pl->weapon] = (int16_t)def->clip_size;
                } else {
                    int needed = def->clip_size - pl->clip[pl->weapon];
                    if (needed > 0) {
                        int taken = pl->reserve[pl->weapon];
                        if (taken > needed) taken = needed;
                        pl->reserve[pl->weapon] -= (int16_t)taken;
                        pl->clip[pl->weapon] += (int16_t)taken;
                    }
                }
            }
        }
    } else if (mode == 4) {
        /* Shell-by-shell reload stage */
        if (!pl->is_reloading || !def->shell_reload) {
            mode = 3; /* Abort */
        } else {
            uint32_t duration_ms = (uint32_t)(def->reload_ticks * 5);
            pl->reload_end_ms = now + duration_ms;
            if (sv->set.mp_infammo != 0) {
                if (pl->clip[pl->weapon] < def->clip_size) pl->clip[pl->weapon]++;
            } else {
                if (pl->reserve[pl->weapon] > 0 && pl->clip[pl->weapon] < def->clip_size) {
                    pl->reserve[pl->weapon]--;
                    pl->clip[pl->weapon]++;
                }
            }
            if (pl->clip[pl->weapon] >= def->clip_size ||
                (sv->set.mp_infammo == 0 && pl->reserve[pl->weapon] <= 0)) {
                pl->is_reloading = false;
                pl->reload_end_ms = 0;
            }
        }
    } else if (mode == 101) {
        /* Mode 101 (0x65): client shell compensation */
        if (pl->is_reloading && def->shell_reload) {
            if (sv->set.mp_infammo != 0) {
                if (pl->clip[pl->weapon] < def->clip_size) pl->clip[pl->weapon]++;
            } else {
                if (pl->reserve[pl->weapon] > 0 && pl->clip[pl->weapon] < def->clip_size) {
                    pl->reserve[pl->weapon]--;
                    pl->clip[pl->weapon]++;
                }
            }
            if (pl->clip[pl->weapon] >= def->clip_size ||
                (sv->set.mp_infammo == 0 && pl->reserve[pl->weapon] <= 0)) {
                pl->is_reloading = false;
                pl->reload_end_ms = 0;
            }
        }
        /* Mode 101 returns without broadcast (LAB_140214c10) */
        cs2d_sv_lua_hook_reload(sv, id, mode);
        return;
    } else if (mode == 3) {
        /* Abort reload */
        pl->is_reloading = false;
        pl->reload_end_ms = 0;
    }

    cs2d_sv_lua_hook_reload(sv, id, mode);

    uint8_t msg[3] = { 16, (uint8_t)id, (uint8_t)mode };
    sv_broadcast_joined(sv, msg, sizeof(msg));
}

/* FUN_140194650: Dedicated server steps reload timers for all reloading players */
void cs2d_sv_step_reloads(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->alive || pl->team < 1 || !pl->is_reloading) continue;
        if (now < pl->reload_end_ms) continue;

        if (pl->weapon <= 0) {
            pl->is_reloading = false;
            continue;
        }
        const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)pl->weapon);
        if (!def) {
            pl->is_reloading = false;
            continue;
        }

        if (def->shell_reload) {
            /* M3 / XM1014: load one shell */
            cs2d_sv_reload(sv, i, 4);
        } else {
            /* Clip weapon: finish reload */
            cs2d_sv_reload(sv, i, 2);
        }
    }
}

/* FUN_140153900: chat message [240, id, mode, char_len, UTF-16LE] */
void cs2d_sv_chat(cs2d_server_t *sv, int id, int mode, int char_len, const uint16_t *chars)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->joined) return;
    if (char_len < 0 || char_len > 255) return;

    /* Decode UTF-16LE to UTF-8 for server console output */
    char utf8_text[1024];
    int out = 0;
    for (int i = 0; i < char_len && out < (int)sizeof(utf8_text) - 4; i++) {
        uint16_t ch = chars[i];
        if (ch < 0x80) {
            utf8_text[out++] = (char)ch;
        } else if (ch < 0x800) {
            utf8_text[out++] = (char)(0xC0 | (ch >> 6));
            utf8_text[out++] = (char)(0x80 | (ch & 0x3F));
        } else {
            utf8_text[out++] = (char)(0xE0 | (ch >> 12));
            utf8_text[out++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            utf8_text[out++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    utf8_text[out] = '\0';

    if (mode == 1) {
        if (cs2d_sv_lua_hook_say(sv, id, utf8_text) == 1) return;
    } else if (mode == 2) {
        if (cs2d_sv_lua_hook_sayteam(sv, id, utf8_text) == 1) return;
    }

    cs2d_server_print("%s%s: %s", (mode == 2 ? "(TEAM) " : ""), pl->name, utf8_text);

    /* Broadcast to eligible recipients */
    for (int q = 1; q <= CS2D_SV_MAX_PLAYERS; q++) {
        cs2d_sv_player_t *recip = &sv->players[q];
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, q);
        if (!recip->used || !recip->joined || !c) continue;

        if (mode == 2) {
            if (recip->team != pl->team) continue;
            if (!pl->alive && sv->set.mp_deadchat == 0 && recip->alive) continue;
        } else {
            if (!pl->alive && sv->set.mp_deadchat == 0 && recip->alive) continue;
        }

        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 240);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
        cs2d_pkt_w_short(p, (uint16_t)char_len);
        for (int i = 0; i < char_len; i++) cs2d_pkt_w_short(p, chars[i]);
    }
}

/* 0x140108260: spectator target player [67, target_pid] */
void cs2d_sv_spec_target(cs2d_server_t *sv, int id, int target_pid)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    pl->spectating_player = target_pid;
}

/* FUN_1401ced00: spectator camera position [32, sx u16, sy u16] */
void cs2d_sv_spec_pos(cs2d_server_t *sv, int id, int sx, int sy)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || pl->alive) return;
    pl->x = (float)(sx - 32768);
    pl->y = (float)(sy - 32768);
    pl->move_flag = 0;
    pl->rot_flag = 0;
}

/* FUN_1401c99a0 (position validation) & FUN_1401ca500 (anti-speeder):
 * Validates player movement; resets position if colliding with solid walls or moving too fast. */
bool cs2d_sv_validate_move(cs2d_server_t *sv, int id, float new_x, float new_y, int op)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return false;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return false;

    /* DAT_1407dfea0 == 0 (freezetime / round not running): the original bails out before any
     * collision work (0x1401c9a02), so the player keeps his spawn position. */
    if (sv_round_frozen(sv)) return false;

    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    /* 1. Map boundary check (if map is loaded) */
    if (sv->map_loaded && sv->map.width > 0 && sv->map.height > 0) {
        float max_x = (float)(sv->map.width * 32 - 16);
        float max_y = (float)(sv->map.height * 32 - 16);
        if (new_x < 16.0f || new_x > max_x || new_y < 16.0f || new_y > max_y) {
            /* Out of bounds: send [11] to reset client position (FUN_1401c99a0) */
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 11);
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_x);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_y);
            }
            return false;
        }

        /* 2. Solid wall check */
        if (cs2d_map_is_solid(&sv->map, new_x, new_y)) {
            /* Inside a solid tile: reset position to last valid via [11] */
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 11);
                cs2d_pkt_w_byte(p, (uint8_t)id);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_x);
                cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_y);
            }
            return false;
        }
    }

    /* 3. Anti-speeder check (FUN_1401ca500):
     * Calculate delta distance and time elapsed since last move */
    uint32_t now = cs2d_sv_now();
    float dx = new_x - pl->last_valid_x;
    float dy = new_y - pl->last_valid_y;
    float dist = sqrtf(dx * dx + dy * dy);
    float dt = (float)(now - pl->last_move_ms) * 0.001f;
    if (dt < 0.010f) dt = 0.010f; /* clamp to avoid divide-by-zero on burst */

    float speed = dist / dt;
    /* Normal CS2D knife run speed is ~260 px/s; allow burst up to ~650 px/s or 160 px single jump */
    if (dist > 160.0f && speed > 650.0f) {
        cs2d_server_print("MP_ANTISPEEDER: %s is too fast - reset position!", pl->name);
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 10);
            cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_x);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_y);
        }
        return false;
    }

    /* Move is valid: commit */
    int old_tx = (int)(pl->x * 0.03125f);
    int old_ty = (int)(pl->y * 0.03125f);
    int new_tx = (int)(new_x * 0.03125f);
    int new_ty = (int)(new_y * 0.03125f);
    if (new_tx != old_tx || new_ty != old_ty) {
        for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
            if (sv->items[i].active && sv->items[i].dropped && sv->items[i].dropped_by == id) {
                if (sv->items[i].drop_tx != new_tx || sv->items[i].drop_ty != new_ty) {
                    sv->items[i].dropped_by = 0;
                }
            }
        }
    }

    pl->x = new_x;
    pl->y = new_y;
    pl->last_valid_x = new_x;
    pl->last_valid_y = new_y;
    pl->last_move_ms = now;
    pl->move_flag = (op == 10) ? 1 : 2;

    /*
     * Teleport (Tpl.tilepos 0x14020f7d0, server side; the client only applies [15]): stepping onto one of
     * the owner's portal tiles (22 / 23) moves him to the other colour's centre, a charged (mode 1)
     * Teleporter Entrance of his side to the owner's exit of that team (a random exit otherwise). The
     * entrance recharges when mp_teleportreload > 0: [36,id,6] (mode 0) now and [36,id,5] (mode 1) after
     * reload x 1000 ms; with 0 it stays charged, like our client's assumption.
     * "Teleport failed! No exit found!" / "Teleporter is not charged! Please wait!" are g_lang 0806 / 0807.
     */
    if (new_tx != old_tx || new_ty != old_ty) {
        /* The building on the tile first (barbed wire, gate field, mine); one object per tile, so when
         * one of those handled the step the teleporter of the same tile cannot apply. */
        if (sv_tile_object_effect(sv, id, new_tx, new_ty)) return true;

        float dx = 0.0f, dy = 0.0f;
        int used_id = 0;
        int r = cs2d_dynobj_teleport(&sv->dynobj, new_tx, new_ty, pl->team, sv->set.sv_gamemode != 1,
                                     &dx, &dy, &used_id);
        if (r == 1) {
            cs2d_sv_set_pos(sv, id, dx, dy);
            cs2d_dynobj_t *e = used_id ? cs2d_dynobj_get(&sv->dynobj, used_id) : NULL;
            if (e && e->typ == CS2D_DO_TELE_ENTRANCE && sv->set.mp_teleportreload > 0) {
                e->mode  = 0;
                e->t2_ms = now + (uint32_t)sv->set.mp_teleportreload * 1000u;
                sv_broadcast_obj_data(sv, e, 6, 0, false); /* [36,id,6] used: recharging */
            }
        } else if (r == -1) {
            cs2d_sv_msg(sv, "Teleporter is not charged! Please wait!", id);
        } else if (r == -2) {
            cs2d_sv_msg(sv, "Teleport failed! No exit found!", id);
        }
    }
    return true;
}

/* FUN_14017cbb0 / FUN_14017ca60: step respawns for modes where respawn == 1 */
void cs2d_sv_step_respawns(cs2d_server_t *sv)
{
    if (sv_mode(sv)->respawn != 1) return;
    uint32_t now = cs2d_sv_now();
    uint32_t delay_ms = (uint32_t)(sv->set.mp_respawndelay > 0 ? sv->set.mp_respawndelay * 1000 : 0);

    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->joined || pl->alive || pl->team < 1 || pl->team > 2) continue;
        if (now - pl->death_ms >= delay_ms) {
            cs2d_sv_spawn(sv, i);
        }
    }
}

/* FUN_1401cf8d0 / [39]: client clicks to respawn */
void cs2d_sv_request_respawn(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->joined || pl->alive || pl->team < 1 || pl->team > 2) return;
    if (sv_mode(sv)->respawn != 1 && sv->set.sv_gamemode == 0) return;

    uint32_t now = cs2d_sv_now();
    uint32_t delay_ms = (uint32_t)(sv->set.mp_respawndelay > 0 ? sv->set.mp_respawndelay * 1000 : 0);
    if (now - pl->death_ms >= delay_ms) {
        cs2d_sv_spawn(sv, id);
    }
}

/* FUN_14010ddb0: in-game name change ([238] and setname command) */
void cs2d_sv_change_name(cs2d_server_t *sv, int id, const char *new_name, bool force, bool hide_msg)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    /* Sanitize while temporarily hiding pl->name so keeping one's own name doesn't get " 2" */
    char old_name[64];
    snprintf(old_name, sizeof(old_name), "%s", pl->name);
    pl->name[0] = '\0';
    char clean[64];
    cs2d_sv_sanitize_name(sv, new_name, clean, sizeof(clean));
    snprintf(pl->name, sizeof(pl->name), "%s", old_name);

    /* Same name: nothing to do (FUN_14010ddb0 0x14010df6f) */
    if (strcmp(clean, pl->name) == 0) return;

    if (!force && cs2d_sv_lua_hook_name(sv, id, pl->name, clean, force) == 1) return;

    if (!force) {
        /* Only one name change per spawn (0x14010df8b -> 0x14010e610: [238, 255, 0, ""]) */
        if (pl->name_changes > 0) {
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 238);
                cs2d_pkt_w_byte(p, 255);
                cs2d_pkt_w_byte(p, 0);
                cs2d_pkt_w_str(p, "");
            }
            return;
        }
        /* Dead player: defer until next spawn (0x14010dfd9 -> 0x14010dfef: [238, 254, 0, ""]) */
        if (!pl->alive) {
            snprintf(pl->pending_name, sizeof(pl->pending_name), "%s", clean);
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 238);
                cs2d_pkt_w_byte(p, 254);
                cs2d_pkt_w_byte(p, 0);
                cs2d_pkt_w_str(p, "");
            }
            return;
        }
    }

    cs2d_server_print("%s changed name to %s", pl->name, clean);
    snprintf(pl->name, sizeof(pl->name), "%s", clean);
    pl->name_changes++;
    pl->pending_name[0] = '\0';

    /* Broadcast [238, id, hide_msg, name] to all joined players (0x14010e108) */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = cs2d_sv_conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p, 238);
        cs2d_pkt_w_byte(p, (uint8_t)id);
        cs2d_pkt_w_byte(p, (uint8_t)(hide_msg ? 1 : 0));
        cs2d_pkt_w_str(p, pl->name);
    }
}

/* FUN_140159840: playerdata setter & broadcaster [42, id u8, mode u8, val u16] */
void cs2d_sv_set_playerdata(cs2d_server_t *sv, int id, int mode, int val)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;

    /* Armor item IDs in equip (5) / strip (6) redirect to mode 3 (0x14015bc60 / 0x14015be50) */
    if (mode == 5) {
        if (val == 57) { mode = 3; val = 100; }
        else if (val == 58) { mode = 3; val = 200; }
        else if (val >= 79 && val <= 84) { mode = 3; val = val + 122; }
    } else if (mode == 6) {
        if (val == 57 || val == 58 || (val >= 79 && val <= 84)) { mode = 3; val = 0; }
    }

    switch (mode) {
    case 0: { /* health */
        if (val < 0) val = 0;
        int maxhp = pl->maxhealth > 0 ? pl->maxhealth : 100;
        if (val > maxhp) val = maxhp;
        pl->health = val;
        if (val == 0 && pl->alive) {
            cs2d_sv_kill(sv, id, 0, 0, 0);
            return;
        }
        break;
    }
    case 1: /* score */
        pl->score = val;
        break;
    case 2: /* money (0..16000, 0x140159b60) */
        if (val < 0) val = 0;
        if (val > 16000) val = 16000;
        pl->money = val;
        break;
    case 3: /* armor (0..255) */
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        pl->armor = val;
        break;
    case 4: /* deaths */
        pl->deaths = val;
        break;
    case 5: /* equip weapon (0x140159d80 -> 0x140170970) */
        if (val > 0 && val < CS2D_MAX_WEAPONS) {
            if (!sv_has_item(pl, val)) sv_give(pl, val);
            if (pl->weapon == 0) sv_best_weapon(sv, pl);
        }
        break;
    case 6: /* strip weapon (0x140159da0; val == 0 strips all) */
        if (val == 0) {
            pl->item_count = 0;
            pl->weapon = 0;
        } else {
            sv_remove_item(pl, val);
            if (pl->weapon == val) sv_best_weapon(sv, pl);
        }
        break;
    case 7: /* speedmod: wire value is speedmod + 100, clamped 0..200 (0x140159e80) */
        if (val < 0) val = 0;
        if (val > 200) val = 200;
        pl->speedmod = val - 100;
        break;
    case 8: /* maxhealth: clamped 1..250, also sets health = maxhealth (0x140159ab0) */
        if (val < 1) val = 1;
        if (val > 250) val = 250;
        pl->maxhealth = val;
        pl->health = val;
        break;
    case 9: /* assists */
        pl->assists = val;
        break;
    case 10: /* mvp */
        pl->mvp = val;
        break;
    case 11: /* rank (0..17) */
        if (val < 0) val = 0;
        if (val > 17) val = 17;
        pl->rank = val;
        break;
    default:
        return;
    }

    uint8_t msg[5];
    msg[0] = 42;
    msg[1] = (uint8_t)id;
    msg[2] = (uint8_t)mode;
    uint16_t uval = (uint16_t)val;
    memcpy(&msg[3], &uval, 2);
    sv_broadcast_joined(sv, msg, 5);
}

/* FUN_140102950: teleport player [15, id u8, x u16, y u16] */
void cs2d_sv_set_pos(cs2d_server_t *sv, int id, float x, float y)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;

    pl->x = x;
    pl->y = y;
    pl->last_valid_x = x;
    pl->last_valid_y = y;
    pl->last_move_ms = cs2d_sv_now();
    pl->move_flag = 0;

    uint8_t msg[6];
    msg[0] = 15;
    msg[1] = (uint8_t)id;
    uint16_t ux = (uint16_t)(int)x;
    uint16_t uy = (uint16_t)(int)y;
    memcpy(&msg[2], &ux, 2);
    memcpy(&msg[4], &uy, 2);
    sv_broadcast_joined(sv, msg, 6);
}

/* FUN_140051ac0: set team scores [64, score_t u16, score_ct u16] */
void cs2d_sv_set_teamscores(cs2d_server_t *sv, int score_t, int score_ct)
{
    if (score_t < 0) score_t = 0;
    if (score_t > 65535) score_t = 65535;
    if (score_ct < 0) score_ct = 0;
    if (score_ct > 65535) score_ct = 65535;
    sv->score_t = score_t;
    sv->score_ct = score_ct;

    uint8_t msg[5];
    msg[0] = 64;
    uint16_t ut = (uint16_t)score_t;
    uint16_t uct = (uint16_t)score_ct;
    memcpy(&msg[1], &ut, 2);
    memcpy(&msg[3], &uct, 2);
    sv_broadcast_joined(sv, msg, 5);
}

/* FUN_140153900(0, 1, text, 1, target_id): server message [240, 0, 1, len u16, UTF-16LE] */
void cs2d_sv_msg(cs2d_server_t *sv, const char *text, int target_id)
{
    if (!text) return;
    int len = (int)strlen(text);
    if (len > 255) len = 255;

    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (target_id > 0 && i != target_id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 240);
        cs2d_pkt_w_byte(p, 0);
        cs2d_pkt_w_byte(p, 1);
        cs2d_pkt_w_short(p, (uint16_t)len);
        for (int j = 0; j < len; j++) {
            cs2d_pkt_w_short(p, (uint8_t)text[j]);
        }
    }
}

/* FUN_140167e40: slap (10 HP) / deathslap (1000 HP) */
void cs2d_sv_slap(cs2d_server_t *sv, int id, bool death)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    char msg_txt[128];
    snprintf(msg_txt, sizeof(msg_txt), "%s was slapped", pl->name);
    cs2d_server_print("%s", msg_txt);
    cs2d_sv_msg(sv, msg_txt, 0);

    int dmg = death ? 1000 : 10;
    /* FUN_1401658c0(id, 0, dmg, 255, 1): bypasses armor, weapon = 255 */
    if (pl->health > dmg) {
        pl->health -= dmg;
        uint8_t pkt[5] = { 17, (uint8_t)id, 0, (uint8_t)pl->health, (uint8_t)pl->armor };
        sv_broadcast_joined(sv, pkt, 5);
    } else {
        cs2d_sv_kill(sv, id, 0, 0, 255);
    }
}

/* ========================================================================= */
/* Phase 5: Bomb System                                                      */
/* ========================================================================= */

/* FUN_1401588b0: [30, 1] plant start */
void cs2d_sv_plant_start(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team != 1 || pl->weapon != CS2D_WEP_BOMB) return;
    if (sv->bomb_planted || pl->is_planting) return;

    /* Check if in bomb spot (CS2D_ENT_INFO_BOMBSPOT) */
    int tx = (int)pl->x / 32, ty = (int)pl->y / 32;
    bool in_spot = false;
    for (uint32_t e = 0; e < sv->map.entity_count; e++) {
        const cs2d_entity_t *ent = &sv->map.entities[e];
        if (ent->type == CS2D_ENT_INFO_BOMBSPOT) {
            if (abs((int)ent->x - tx) <= 3 && abs((int)ent->y - ty) <= 3) {
                in_spot = true;
                break;
            }
        }
    }
    if (!in_spot) return;

    pl->is_planting = true;
    pl->plant_start_ms = cs2d_sv_now();

    uint8_t pkt[3] = { 30, (uint8_t)id, 1 };
    sv_broadcast_joined(sv, pkt, 3);
}

/* FUN_1401588b0: [30, 2] plant abort */
void cs2d_sv_plant_abort(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->is_planting) return;

    pl->is_planting = false;
    uint8_t pkt[3] = { 30, (uint8_t)id, 2 };
    sv_broadcast_joined(sv, pkt, 3);
}

/* FUN_1401588b0: [30, 3] plant complete */
void cs2d_sv_plant_complete(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || !pl->is_planting) return;

    pl->is_planting = false;

    /* Strip bomb weapon */
    for (int i = 0; i < pl->item_count; i++) {
        if (pl->items[i] == CS2D_WEP_BOMB) {
            memmove(&pl->items[i], &pl->items[i + 1], (size_t)(pl->item_count - 1 - i));
            pl->item_count--;
            break;
        }
    }
    sv_best_weapon(sv, pl);
    uint8_t sel[4] = { 9, (uint8_t)id, (uint8_t)pl->weapon, 0 };
    sv_broadcast_joined(sv, sel, 4);

    /* Allocate ground item for planted bomb */
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) { slot = i; break; }
    }
    uint16_t item_id = (uint16_t)++sv->next_item_id;
    uint16_t tx = (uint16_t)((int)pl->x / 32);
    uint16_t ty = (uint16_t)((int)pl->y / 32);
    if (slot >= 0) {
        cs2d_sv_item_t *it = &sv->items[slot];
        it->active = true;
        it->id = item_id;
        it->type = CS2D_WEP_PLANTEDBOMB;
        it->tile_x = tx;
        it->tile_y = ty;
        it->clip = 0;
        it->reserve = 0;
        it->dropped = false;
        it->drop_time_ms = 0;
    }

    uint8_t c4timer = (sv->set.mp_c4timer > 0) ? (uint8_t)sv->set.mp_c4timer : 35;
    uint32_t now = cs2d_sv_now();
    sv->bomb_planted = true;
    sv->bomb_item_id = item_id;
    sv->bomb_tx = tx;
    sv->bomb_ty = ty;
    sv->bomb_plant_time = now;
    sv->bomb_explode_time = now + (uint32_t)c4timer * 1000u;
    sv->bomb_planter = id;

    pl->score += 3;
    pl->money += 300;
    if (pl->money > 16000) pl->money = 16000;

    /* Broadcast [30, id, 3, tx u16, ty u16, item_id u16, c4timer u8] (10 bytes) */
    uint8_t pkt[10];
    pkt[0] = 30;
    pkt[1] = (uint8_t)id;
    pkt[2] = 3;
    memcpy(&pkt[3], &tx, 2);
    memcpy(&pkt[5], &ty, 2);
    memcpy(&pkt[7], &item_id, 2);
    pkt[9] = c4timer;
    sv_broadcast_joined(sv, pkt, 10);
    cs2d_sv_lua_hook_bombplant(sv, id, (int)tx, (int)ty);
    cs2d_server_print("Bomb has been planted by %s at %d,%d", pl->name, tx, ty);
}

/* FUN_140138a40: [26, 3] defuse start */
void cs2d_sv_defuse_start(cs2d_server_t *sv, int id, uint16_t tx, uint16_t ty)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team != 2 || !sv->bomb_planted) return;
    if (pl->is_defusing) return;

    /* Verify proximity to planted bomb */
    float bx = sv->bomb_tx * 32.0f + 16.0f;
    float by = sv->bomb_ty * 32.0f + 16.0f;
    float dx = pl->x - bx, dy = pl->y - by;
    if (dx * dx + dy * dy > 64.0f * 64.0f) return;

    bool has_kit = sv_has_item(pl, CS2D_WEP_DEFUSEKIT) || ((pl->special & 1) != 0);
    pl->is_defusing = true;
    sv->defuser_id = id;
    sv->defuse_start_ms = cs2d_sv_now();
    sv->defuse_duration_ms = has_kit ? 5000u : 10000u;

    /* Broadcast [26, id, 3, data i32, x u16, y u16] (11 bytes) */
    uint8_t pkt[11];
    pkt[0] = 26;
    pkt[1] = (uint8_t)id;
    pkt[2] = 3;
    int32_t data = has_kit ? 1 : 0;
    memcpy(&pkt[3], &data, 4);
    memcpy(&pkt[7], &tx, 2);
    memcpy(&pkt[9], &ty, 2);
    sv_broadcast_joined(sv, pkt, 11);
}

/* FUN_140169830: [30, 4] defuse abort */
void cs2d_sv_defuse_abort(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->is_defusing) return;

    pl->is_defusing = false;
    if (sv->defuser_id == id) sv->defuser_id = 0;

    uint8_t pkt[3] = { 30, (uint8_t)id, 4 };
    sv_broadcast_joined(sv, pkt, 3);
}

/* FUN_140169830: [30, 5] defuse complete */
void cs2d_sv_defuse_complete(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || !pl->is_defusing) return;

    pl->is_defusing = false;
    sv->defuser_id = 0;
    sv->bomb_planted = false;

    /* Remove ground item */
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (sv->items[i].active && sv->items[i].id == sv->bomb_item_id) {
            sv->items[i].active = false;
            break;
        }
    }

    pl->score += 3;
    pl->money += 300;
    if (pl->money > 16000) pl->money = 16000;

    uint8_t pkt[3] = { 30, (uint8_t)id, 5 };
    sv_broadcast_joined(sv, pkt, 3);
    cs2d_sv_lua_hook_bombdefuse(sv, id);
    cs2d_server_print("Bomb has been defused by %s", pl->name);

    /* CT victory: type 21 = Bomb defused, CT win; the defuser is the MVP (r9 of 0x14016aa78) */
    cs2d_sv_round_end_ex(sv, 21, 21, 5000, id);
}

/* FUN_140169830: [30, 6] bomb explode */
void cs2d_sv_bomb_explode(cs2d_server_t *sv)
{
    sv->bomb_planted = false;
    sv->defuser_id = 0;

    /* Remove ground item */
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (sv->items[i].active && sv->items[i].id == sv->bomb_item_id) {
            sv->items[i].active = false;
            break;
        }
    }

    uint8_t pkt[3] = { 30, 0, 6 };
    sv_broadcast_joined(sv, pkt, 3);
    cs2d_sv_lua_hook_bombexplode(sv, sv->bomb_planter, (int)sv->bomb_tx, (int)sv->bomb_ty);
    cs2d_server_print("Bomb exploded!");

    /* T victory: type 20 = Bomb detonated, T win; the planter is the MVP (r9 of 0x14016aca2).
     * Ended BEFORE the blast damage: since the death path now runs the win check (0x1401647c4 /
     * 0x1401840d1), the kills of the blast would otherwise decide the round first (a draw with an empty
     * team). The original's check is gated on DAT_1407dfe6c == 0, so the pending "Bomb detonated" always
     * wins there - this order keeps the same observable reason. */
    cs2d_sv_round_end_ex(sv, 20, 20, 5000, sv->bomb_planter);

    /* Damage players in explosion radius (350 px) */
    float bx = sv->bomb_tx * 32.0f + 16.0f;
    float by = sv->bomb_ty * 32.0f + 16.0f;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->alive) continue;
        float dx = pl->x - bx, dy = pl->y - by;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 350.0f) {
            int dmg = (int)(500.0f * (1.0f - dist / 350.0f));
            if (dmg > 0) cs2d_sv_damage(sv, i, sv->bomb_planter, dmg, CS2D_WEP_PLANTEDBOMB);
        }
    }

    /* ... and the buildings: the same 350 px / 500 falloff through Tdo.damage (FUN_1401f1130), so the C4
     * levels the walls, turrets and dispensers it reaches. */
    cs2d_sv_explosion_objects(sv, bx, by, 350.0f, 500, sv->bomb_planter);
}

void cs2d_sv_step_bomb(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();
    /* 1. Planting check */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (pl->used && pl->is_planting) {
            if (!pl->alive || pl->weapon != CS2D_WEP_BOMB) {
                cs2d_sv_plant_abort(sv, i);
            } else if (now - pl->plant_start_ms >= 3000u) {
                cs2d_sv_plant_complete(sv, i);
            }
        }
    }

    /* 2. Defusing check */
    if (sv->bomb_planted && sv->defuser_id > 0) {
        int did = sv->defuser_id;
        cs2d_sv_player_t *dpl = &sv->players[did];
        if (!dpl->used || !dpl->alive || !dpl->is_defusing) {
            sv->defuser_id = 0;
        } else {
            float bx = sv->bomb_tx * 32.0f + 16.0f;
            float by = sv->bomb_ty * 32.0f + 16.0f;
            float dx = dpl->x - bx, dy = dpl->y - by;
            if (dx * dx + dy * dy > 70.0f * 70.0f) {
                cs2d_sv_defuse_abort(sv, did);
            } else if (now - sv->defuse_start_ms >= sv->defuse_duration_ms) {
                cs2d_sv_defuse_complete(sv, did);
            }
        }
    }

    /* 3. Bomb explode countdown */
    if (sv->bomb_planted && sv->round_end == 0 && now >= sv->bomb_explode_time) {
        cs2d_sv_bomb_explode(sv);
    }
}

/* ========================================================================= */
/* Phase 5: Opcode 26 (Use)                                                  */
/* ========================================================================= */

/* FUN_140138a40: [26, mode, data i32, x u16, y u16] */
void cs2d_sv_use(cs2d_server_t *sv, int id, int mode, int32_t data, uint16_t x, uint16_t y)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    cs2d_sv_lua_hook_use(sv, id, mode, (int)data, (int)x, (int)y);

    if (mode == 1 || mode == 2) {
        cs2d_sv_hostage_use(sv, id, (int)data, mode);
    } else if (mode == 3) {
        cs2d_sv_defuse_start(sv, id, x, y);
    } else if (mode == 100) {
        float tx_px = (float)x * 32.0f + 16.0f;
        float ty_px = (float)y * 32.0f + 16.0f;
        float dx = pl->x - tx_px, dy = pl->y - ty_px;
        if (dx * dx + dy * dy <= 45.0f * 45.0f) {
            uint8_t pkt[11];
            pkt[0] = 26;
            pkt[1] = (uint8_t)id;
            pkt[2] = 100;
            memcpy(&pkt[3], &data, 4);
            memcpy(&pkt[7], &x, 2);
            memcpy(&pkt[9], &y, 2);
            sv_broadcast_joined(sv, pkt, 11);
        }
    }
}

/* ========================================================================= */
/* Phase 5: Hostages                                                         */
/* ========================================================================= */

void cs2d_sv_init_hostages(cs2d_server_t *sv)
{
    sv->hostage_count = 0;
    memset(sv->hostages, 0, sizeof(sv->hostages));
    if (!sv->map_loaded) return;

    int id = 1;
    for (uint32_t i = 0; i < sv->map.entity_count && sv->hostage_count < CS2D_SV_MAX_HOSTAGES; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (e->type != CS2D_ENT_INFO_HOSTAGE) continue;
        cs2d_sv_hostage_t *h = &sv->hostages[sv->hostage_count++];
        h->id = id++;
        h->x = (float)(e->x * 32 + 16);
        h->y = (float)(e->y * 32 + 16);
        h->rot = (float)e->int_settings[0];
        h->health = 100;
        h->look = e->int_settings[1] > 7 ? 7 : (e->int_settings[1] < 0 ? 0 : e->int_settings[1]);
        h->follow = 0;
        h->target_tx = (uint16_t)e->x;
        h->target_ty = (uint16_t)e->y;
    }
}

void cs2d_sv_hostage_use(cs2d_server_t *sv, int id, int hostage_id, int mode)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team != 2) return;

    cs2d_sv_hostage_t *h = NULL;
    for (int i = 0; i < sv->hostage_count; i++) {
        if (sv->hostages[i].id == hostage_id) { h = &sv->hostages[i]; break; }
    }
    if (!h || h->health <= 0) return;

    float dx = pl->x - h->x, dy = pl->y - h->y;
    if (dx * dx + dy * dy > 64.0f * 64.0f) return;

    if (mode == 1) {
        bool first = !h->used;
        h->used = true;
        h->follow = (uint8_t)id;
        if (first) {
            pl->money += 150;
            if (pl->money > 16000) pl->money = 16000;
        }
        uint8_t pkt[11];
        pkt[0] = 26; pkt[1] = (uint8_t)id; pkt[2] = 1;
        int32_t hid = hostage_id;
        memcpy(&pkt[3], &hid, 4);
        uint16_t ux = first ? 0 : 1;
        uint16_t uy = 0;
        memcpy(&pkt[7], &ux, 2); memcpy(&pkt[9], &uy, 2);
        sv_broadcast_joined(sv, pkt, 11);
        cs2d_sv_lua_hook_hostageuse(sv, id, hostage_id, 1);
    } else if (mode == 2) {
        h->follow = 0;
        uint8_t pkt[11];
        pkt[0] = 26; pkt[1] = (uint8_t)id; pkt[2] = 2;
        int32_t hid = hostage_id;
        memcpy(&pkt[3], &hid, 4);
        uint16_t ux = 1, uy = 0;
        memcpy(&pkt[7], &ux, 2); memcpy(&pkt[9], &uy, 2);
        sv_broadcast_joined(sv, pkt, 11);
        cs2d_sv_lua_hook_hostageuse(sv, id, hostage_id, 0);
    }
}

void cs2d_sv_damage_hostage(cs2d_server_t *sv, int hostage_id, int attacker_id, int dmg)
{
    cs2d_sv_hostage_t *h = NULL;
    for (int i = 0; i < sv->hostage_count; i++) {
        if (sv->hostages[i].id == hostage_id) { h = &sv->hostages[i]; break; }
    }
    if (!h || h->health <= 0) return;

    cs2d_sv_player_t *atk = (attacker_id >= 1 && attacker_id <= CS2D_SV_MAX_PLAYERS)
                            ? &sv->players[attacker_id] : NULL;

    if (h->health > dmg) {
        h->health -= dmg;
        if (atk) {
            atk->money -= 150;
            if (atk->money < 0) atk->money = 0;
        }
        /* [29, hostage_id, 3, health u16, attacker u16] */
        uint8_t pkt[7];
        pkt[0] = 29; pkt[1] = (uint8_t)hostage_id; pkt[2] = 3;
        uint16_t hp = (uint16_t)h->health;
        uint16_t att = (uint16_t)attacker_id;
        memcpy(&pkt[3], &hp, 2); memcpy(&pkt[5], &att, 2);
        sv_broadcast_joined(sv, pkt, 7);
        cs2d_sv_lua_hook_hostagedamage(sv, attacker_id, hostage_id, dmg);
    } else {
        h->health = 0;
        h->follow = 0;
        if (atk) {
            atk->money -= 1500;
            if (atk->money < 0) atk->money = 0;
            atk->score -= 1;
        }
        /* [29, hostage_id, 4, 0, killer u16] */
        uint8_t pkt[7];
        pkt[0] = 29; pkt[1] = (uint8_t)hostage_id; pkt[2] = 4;
        uint16_t zero = 0;
        uint16_t att = (uint16_t)attacker_id;
        memcpy(&pkt[3], &zero, 2); memcpy(&pkt[5], &att, 2);
        sv_broadcast_joined(sv, pkt, 7);
        cs2d_sv_lua_hook_hostagekill(sv, attacker_id, hostage_id, dmg);
    }
}

void cs2d_sv_step_hostages(cs2d_server_t *sv)
{
    for (int i = 0; i < sv->hostage_count; i++) {
        cs2d_sv_hostage_t *h = &sv->hostages[i];
        if (h->health <= 0) continue;
        if (h->follow > 0) {
            cs2d_sv_player_t *pl = &sv->players[h->follow];
            if (!pl->used || !pl->alive || pl->team != 2) {
                h->follow = 0;
                continue;
            }
            float dx = pl->x - h->x, dy = pl->y - h->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 48.0f) {
                int target_tx = (int)pl->x / 32;
                int target_ty = (int)pl->y / 32;
                if (target_tx != (int)h->target_tx || target_ty != (int)h->target_ty) {
                    h->target_tx = (uint16_t)target_tx;
                    h->target_ty = (uint16_t)target_ty;
                    uint8_t pkt[7];
                    pkt[0] = 29; pkt[1] = (uint8_t)h->id; pkt[2] = 0;
                    uint16_t utx = (uint16_t)target_tx, uty = (uint16_t)target_ty;
                    memcpy(&pkt[3], &utx, 2); memcpy(&pkt[5], &uty, 2);
                    sv_broadcast_joined(sv, pkt, 7);
                }
                float spd = 2.5f;
                h->x += (dx / dist) * spd;
                h->y += (dy / dist) * spd;
            }

            /* Rescue point check */
            int htx = (int)h->x / 32, hty = (int)h->y / 32;
            bool rescued = false;
            for (uint32_t e = 0; e < sv->map.entity_count; e++) {
                if (sv->map.entities[e].type == CS2D_ENT_INFO_RESCUEPOINT) {
                    if (abs((int)sv->map.entities[e].x - htx) <= 2 &&
                        abs((int)sv->map.entities[e].y - hty) <= 2) {
                        rescued = true;
                        break;
                    }
                }
            }
            if (rescued) {
                uint16_t rescuer = (uint16_t)h->follow;   /* capture before the follow flag is cleared */
                h->health = 0;
                h->follow = 0;
                uint8_t pkt[7];
                pkt[0] = 29; pkt[1] = (uint8_t)h->id; pkt[2] = 2;
                uint16_t zero = 0;
                memcpy(&pkt[3], &zero, 2); memcpy(&pkt[5], &rescuer, 2);
                sv_broadcast_joined(sv, pkt, 7);
                cs2d_sv_lua_hook_hostagerescue(sv, (int)rescuer, htx, hty);

                pl->score += 2;
                for (int q = 1; q <= CS2D_SV_MAX_PLAYERS; q++) {
                    if (sv->players[q].used && sv->players[q].team == 2) {
                        sv->players[q].money += 1000;
                        if (sv->players[q].money > 16000) sv->players[q].money = 16000;
                    }
                }

                sv->hostages_rescued++;
                cs2d_sv_hostage_escaped(sv, (int)h->id, (int)rescuer);
            }
        }
    }
}

/* ========================================================================= */
/* Phase 5: Projectiles & Flashlight                                         */
/* ========================================================================= */

/* FUN_1401d4910: [27, player u8, type u8, x u16, y u16, dir u16, flydist u16] (11 bytes) */
void cs2d_sv_projectile(cs2d_server_t *sv, int id, int type, uint16_t x, uint16_t y, uint16_t dir, uint16_t flydist)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    /* Grenade types: 51..54, 72..73, 75..77 */
    if (type == 51 || type == 52 || type == 53 || type == 54 ||
        type == 72 || type == 73 || type == 75 || type == 76 || type == 77) {
        for (int i = 0; i < pl->item_count; i++) {
            if (pl->items[i] == type) {
                memmove(&pl->items[i], &pl->items[i + 1], (size_t)(pl->item_count - 1 - i));
                pl->item_count--;
                break;
            }
        }
        sv_best_weapon(sv, pl);
    }

    uint8_t pkt[11];
    pkt[0] = 27;
    pkt[1] = (uint8_t)id;
    pkt[2] = (uint8_t)type;
    memcpy(&pkt[3], &x, 2);
    memcpy(&pkt[5], &y, 2);
    memcpy(&pkt[7], &dir, 2);
    memcpy(&pkt[9], &flydist, 2);
    sv_broadcast_joined(sv, pkt, 11);
    cs2d_sv_lua_hook_projectile(sv, id, type, (int)x, (int)y, 0);
}

/* FUN_1401cfd00: [41, proj_id u16, id u8] (4 bytes) */
void cs2d_sv_kill_projectile(cs2d_server_t *sv, int id, int proj_id)
{
    uint8_t pkt[4];
    pkt[0] = 41;
    uint16_t pid = (uint16_t)proj_id;
    memcpy(&pkt[1], &pid, 2);
    pkt[3] = (uint8_t)id;
    sv_broadcast_joined(sv, pkt, 4);
}

/* FUN_1401d28b0: [57, player u8, state u8] (3 bytes) */
void cs2d_sv_flashlight(cs2d_server_t *sv, int id, int state)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;

    uint8_t pkt[3] = { 57, (uint8_t)id, (uint8_t)state };
    sv_broadcast_joined(sv, pkt, 3);
    cs2d_sv_lua_hook_flashlight(sv, id, state);
}

/* ========================================================================= */
/* Phase 5: Bot Management ("Present only, no AI")                            */
/* ========================================================================= */

int cs2d_sv_bot_add(cs2d_server_t *sv, int team, int look, const char *name)
{
    int slot = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (!sv->players[i].used) { slot = i; break; }
    }
    if (slot == 0) return 0;

    cs2d_sv_player_t *pl = &sv->players[slot];
    memset(pl, 0, sizeof(*pl));
    pl->used = true;
    pl->bot = true;
    pl->conn = -1;
    pl->ping = 0xfffe;
    pl->joined = true;
    pl->maxhealth = 100;
    pl->os = 1;

    if (name && name[0]) {
        cs2d_sv_sanitize_name(sv, name, pl->name, sizeof(pl->name));
    } else {
        snprintf(pl->name, sizeof(pl->name), "Bot %d", slot);
    }

    int t = (team >= 1 && team <= 2) ? team
            : (cs2d_sv_count_team(sv, 1) <= cs2d_sv_count_team(sv, 2) ? 1 : 2);
    pl->team = t;
    pl->look = (look >= 0 && look <= 3) ? look : cs2d_sv_rand(0, 3);

    /* Broadcast [248] join notice */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = cs2d_sv_conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *p248 = cs2d_sv_packet(sv, oc, true, false);
        cs2d_pkt_w_byte(p248, 248);
        cs2d_pkt_w_byte(p248, (uint8_t)slot);
        cs2d_pkt_w_str(p248, pl->name);
        cs2d_pkt_w_str(p248, "");
        cs2d_pkt_w_int(p248, 0);
        cs2d_pkt_w_lstr(p248, "");
        cs2d_pkt_w_long(p248, 0);
        cs2d_pkt_w_lstr(p248, "");
        cs2d_pkt_w_byte(p248, 1);
    }

    /* Spawn if round running */
    if (sv->round_end == 0 && pl->team >= 1) {
        cs2d_sv_spawn(sv, slot);
    }
    cs2d_server_print("%s joined the game (bot)", pl->name);
    return slot;
}

void cs2d_sv_bot_kick(cs2d_server_t *sv, const char *name_or_id)
{
    int target = 0;
    if (name_or_id && name_or_id[0]) {
        int id = atoi(name_or_id);
        if (id >= 1 && id <= CS2D_SV_MAX_PLAYERS && sv->players[id].used && sv->players[id].bot) {
            target = id;
        } else {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].bot &&
                    _stricmp(sv->players[i].name, name_or_id) == 0) {
                    target = i;
                    break;
                }
            }
        }
    } else {
        /* Kick last added bot */
        for (int i = CS2D_SV_MAX_PLAYERS; i >= 1; i--) {
            if (sv->players[i].used && sv->players[i].bot) { target = i; break; }
        }
    }
    if (target > 0) {
        cs2d_sv_kick(sv, target, "Bot removed");
    }
}

void cs2d_sv_bot_kill(cs2d_server_t *sv, const char *name_or_id)
{
    int target = 0;
    if (name_or_id && name_or_id[0]) {
        int id = atoi(name_or_id);
        if (id >= 1 && id <= CS2D_SV_MAX_PLAYERS && sv->players[id].used && sv->players[id].bot) {
            target = id;
        } else {
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                if (sv->players[i].used && sv->players[i].bot &&
                    _stricmp(sv->players[i].name, name_or_id) == 0) {
                    target = i;
                    break;
                }
            }
        }
    } else {
        for (int i = CS2D_SV_MAX_PLAYERS; i >= 1; i--) {
            if (sv->players[i].used && sv->players[i].bot && sv->players[i].alive) {
                target = i; break;
            }
        }
    }
    if (target > 0 && sv->players[target].alive) {
        cs2d_sv_kill(sv, target, 0, 0, 255);
    }
}

/* ========================================================================= */
/* Phase 5: Bans & Ban List                                                  */
/* ========================================================================= */

bool cs2d_sv_is_banned(const cs2d_server_t *sv, const char *ip, int32_t usgn, int64_t steam_id,
                       char *out_reason, size_t reason_size)
{
    if (!sv) return false;
    for (int i = 0; i < sv->ban_count; i++) {
        const cs2d_sv_ban_t *b = &sv->bans[i];
        if (!b->active) continue;
        bool match = false;
        if (ip && b->ip[0] && strcmp(b->ip, ip) == 0) match = true;
        if (usgn != 0 && b->usgn == usgn) match = true;
        if (steam_id != 0 && b->steam_id == steam_id) match = true;
        if (match) {
            if (out_reason && reason_size > 0) {
                snprintf(out_reason, reason_size, "%s", b->reason[0] ? b->reason : "Banned");
            }
            return true;
        }
    }
    return false;
}

void cs2d_sv_ban_ip(cs2d_server_t *sv, const char *ip, const char *reason)
{
    if (!sv || !ip || !ip[0] || sv->ban_count >= CS2D_SV_MAX_BANS) return;
    cs2d_sv_ban_t *b = &sv->bans[sv->ban_count++];
    memset(b, 0, sizeof(*b));
    b->active = true;
    snprintf(b->ip, sizeof(b->ip), "%s", ip);
    snprintf(b->reason, sizeof(b->reason), "%s", (reason && reason[0]) ? reason : "Banned by administrator");
    cs2d_server_print("Banned IP: %s (%s)", b->ip, b->reason);
}

void cs2d_sv_ban_usgn(cs2d_server_t *sv, int32_t usgn, const char *reason)
{
    if (!sv || usgn <= 0 || sv->ban_count >= CS2D_SV_MAX_BANS) return;
    cs2d_sv_ban_t *b = &sv->bans[sv->ban_count++];
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->usgn = usgn;
    snprintf(b->reason, sizeof(b->reason), "%s", (reason && reason[0]) ? reason : "Banned by administrator");
    cs2d_server_print("Banned U.S.G.N.: #%d (%s)", b->usgn, b->reason);
}

void cs2d_sv_ban_steam(cs2d_server_t *sv, int64_t steam_id, const char *reason)
{
    if (!sv || steam_id <= 0 || sv->ban_count >= CS2D_SV_MAX_BANS) return;
    cs2d_sv_ban_t *b = &sv->bans[sv->ban_count++];
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->steam_id = steam_id;
    snprintf(b->reason, sizeof(b->reason), "%s", (reason && reason[0]) ? reason : "Banned by administrator");
    cs2d_server_print("Banned Steam ID: %lld (%s)", (long long)b->steam_id, b->reason);
}

void cs2d_sv_unban(cs2d_server_t *sv, const char *target)
{
    if (!sv || !target || !target[0]) return;
    int32_t usgn = atoi(target);
    for (int i = 0; i < sv->ban_count; i++) {
        cs2d_sv_ban_t *b = &sv->bans[i];
        if (!b->active) continue;
        bool match = false;
        if (strcmp(b->ip, target) == 0) match = true;
        if (usgn > 0 && b->usgn == usgn) match = true;
        if (match) {
            cs2d_server_print("Unbanned: %s", target);
            memmove(&sv->bans[i], &sv->bans[i + 1], (size_t)(sv->ban_count - 1 - i) * sizeof(cs2d_sv_ban_t));
            sv->ban_count--;
            return;
        }
    }
    cs2d_server_print("Ban target '%s' not found", target);
}

/* ========================================================================= */
/* Phase 5: Map Change                                                       */
/* ========================================================================= */

/* FUN_1401104f0: [239, 1] broadcast and map change */
bool cs2d_sv_change_map(cs2d_server_t *sv, const char *mapname)
{
    if (!sv || !mapname || !mapname[0]) return false;

    char path[256];
    snprintf(path, sizeof(path), "maps/%s.map", mapname);
    cs2d_map_t *new_map = (cs2d_map_t *)calloc(1, sizeof(cs2d_map_t));
    if (!new_map) return false;
    if (!cs2d_map_load_file(new_map, path)) {
        cs2d_server_print("Failed to load map '%s'", path);
        free(new_map);
        return false;
    }

    /* Broadcast [239, 1] to all joined players */
    uint8_t pkt[2] = { 239, 1 };
    sv_broadcast_joined(sv, pkt, 2);

    cs2d_map_free(&sv->map);
    sv->map = *new_map;
    free(new_map);
    sv->map_loaded = true;
    snprintf(sv->set.sv_map, sizeof(sv->set.sv_map), "%s", mapname);

    /* Recalculate SHA */
    cs2d_net_sha256_file(path, sv->map_sha);

    cs2d_sv_init_hostages(sv);
    cs2d_dynobj_init(&sv->dynobj, &sv->map);
    cs2d_dynobj_spawn_map_buildings(&sv->dynobj);
    cs2d_sv_fix_building_health(sv);
    if (sv->lua && sv->set.mp_luamap) {
        cs2d_sv_lua_load_map(sv, mapname);
    }
    sv->score_t = sv->score_ct = 0;

    cs2d_sv_round_start(sv, 0);

    cs2d_sv_lua_hook_mapchange(sv, mapname);
    cs2d_server_print("Map changed to '%s'", mapname);
    return true;
}


/* ------------------------------------------------------------------------------------------------ */
/* Construction (sv_gamemode 3): buildings and the Wrench                                            */
/*                                                                                                   */
/* Sources (cs2d_dedicated.exe unless marked CS2D.exe):                                              */
/*   FUN_140175550  "place object", called by the [34] receive handler 0x1401cf0c0                   */
/*   CS2D.exe FUN_1401fab30 (docs/dump/cs2d/_global/1401f0000.c:1202): the same code, decompiled      */
/*   Tdo.inibuilding 0x140142e00, Tdo.damage 0x140035110, Tdo.doupgrade 0x140143d60,                  */
/*   Tdo.counttype 0x140035680, Tdo.sametypetoclose 0x14004a2b0, Tdo.hpupnetvalue 0x140005350         */
/*   Tdo.draw_wall turret branch (CS2D.exe 0x14028c630, docs/dump/cs2d/_global/140280000.c:1971)      */
/*   Tshoot.Shoot wrench branch (CS2D.exe, docs/dump/cs2d/Tshoot.c:540..650)                          */
/* ------------------------------------------------------------------------------------------------ */

/*
 * Refusal codes: byte 3 of the 15 byte [34] answer when it is >= 0xf8 (the client then shows the
 * localized line, for the local player only). The switch of FUN_1401fab30 at 0x1401fbece reads
 * g_lang[0x9d..0x9f] / g_lang[0x7c] (element size 0x24); the ids are the sys/language text numbers.
 */
enum {
    SV_BUILD_SITE_UNFINISHED = 0xf8, /* 0716 Your last building has not been finished yet */
    SV_BUILD_NOT_ON_SERVER   = 0xf9, /* 0715 It's not allowed to build that on this server */
    SV_BUILD_TOO_CLOSE       = 0xfa, /* 0714 You can't build here - buildings of same type are too close */
    SV_BUILD_LIMIT           = 0xfb, /* 0713 You can't build more buildings of this type */
    SV_BUILD_PLAYERS_CLOSE   = 0xfc, /* 0712 You can't build here - other players are too close */
    SV_BUILD_NOT_ALLOWED     = 0xfd, /* 0711 You are not allowed to build this */
    SV_BUILD_INSUFFICIENT    = 0xfe, /* 0562 You have insufficient funds */
    SV_BUILD_CANT_HERE       = 0xff  /* 0710 You can't build here */
};

/*
 * How long a Construction Site needs (FUN_1401fab30: the Ttimer of the site, ms). Barbed Wire (2) is
 * built at once; everything else first becomes a Construction Site (typ 10) whose mode is the wanted
 * type. On the dedicated the switch is compiled as a table load; the values come from the CS2D.exe copy
 * of the same function (docs/dump/cs2d/_global/1401f0000.c:1572).
 */
static int sv_build_time(int typ)
{
    switch (typ) {
        case 1:                           return 1000; /* Barricade */
        case 3:                           return 2000; /* Wall I */
        case 4: case 6: case 13: case 14: return 3000; /* Wall II, Gate Field, teleporters */
        case 5:                           return 4000; /* Wall III */
        case 7: case 8: case 9:           return 5000; /* Dispenser, Turret, Supply */
        default:                          return 0;
    }
}

/* [37, id u16, value u8]: unreliable health / upgrade percent (Tdo.damage 0x140035245) */
static void sv_broadcast_obj_health(cs2d_server_t *sv, const cs2d_dynobj_t *o)
{
    uint8_t msg[4];
    msg[0] = 37;
    msg[1] = (uint8_t)(o->id & 0xff);
    msg[2] = (uint8_t)(o->id >> 8);
    msg[3] = (uint8_t)cs2d_dynobj_hpup_net_value(o);
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
        for (int k = 0; k < 4; k++) cs2d_pkt_w_byte(p, msg[k]);
    }
}

/* [36, id u16, sub, ...]: reliable dynamic object mode / data (Tdo.getdata) */
static void sv_broadcast_obj_data(cs2d_server_t *sv, const cs2d_dynobj_t *o, int sub, int value, bool has_value)
{
    uint8_t msg[5];
    int n = 4;
    msg[0] = 36;
    msg[1] = (uint8_t)(o->id & 0xff);
    msg[2] = (uint8_t)(o->id >> 8);
    msg[3] = (uint8_t)sub;
    if (has_value) { msg[4] = (uint8_t)value; n = 5; }
    sv_broadcast_joined(sv, msg, n);
}

/* [35, id u16] (Tdo.kill) */
static void sv_broadcast_obj_remove(cs2d_server_t *sv, int obj_id)
{
    uint8_t msg[3] = { 35, (uint8_t)(obj_id & 0xff), (uint8_t)((obj_id >> 8) & 0xff) };
    sv_broadcast_joined(sv, msg, 3);
}

/*
 * [34, pid, team, typ, tx u16, ty u16, mode, rot + 1000 u16, money u16, id u16] (15 bytes): the build
 * result. The client (cs2d_client.c net_build_result) reads b[0] pid, b[1] team, b[2] typ, b[3..6] tx/ty,
 * b[7] mode, b[8..9] rot + 1000, b[10..11] money, b[12..13] the object id; typ >= 0xf8 is a refusal.
 */
static void sv_build_result(cs2d_server_t *sv, int pid, int team, int typ, int tx, int ty, int mode,
                            float rot, int money, int obj_id)
{
    uint8_t msg[15];
    int r = (int)(rot + 1000.0f) & 0xffff;
    msg[0]  = 34;
    msg[1]  = (uint8_t)pid;
    msg[2]  = (uint8_t)team;
    msg[3]  = (uint8_t)typ;
    msg[4]  = (uint8_t)(tx & 0xff);    msg[5]  = (uint8_t)((tx >> 8) & 0xff);
    msg[6]  = (uint8_t)(ty & 0xff);    msg[7]  = (uint8_t)((ty >> 8) & 0xff);
    msg[8]  = (uint8_t)mode;
    msg[9]  = (uint8_t)(r & 0xff);     msg[10] = (uint8_t)(r >> 8);
    msg[11] = (uint8_t)(money & 0xff); msg[12] = (uint8_t)((money >> 8) & 0xff);
    msg[13] = (uint8_t)(obj_id & 0xff); msg[14] = (uint8_t)((obj_id >> 8) & 0xff);
    sv_broadcast_joined(sv, msg, 15);
}

/* A refused build: only the client of `id` shows the message (pid field) */
static void sv_build_refuse(cs2d_server_t *sv, int id, int team, int tx, int ty, int code)
{
    sv_build_result(sv, id, team, code, tx, ty, 0, 0.0f, 0, 0);
}

/*
 * The dedicated keeps Tdo.health in points while the client keeps the percent of Tdo.healthpercent
 * (Tdo.inibuilding sets max_health[typ] on the server, 100 in a client). The map's Env_Building entities
 * go through cs2d_dynobj_spawn, which sets the client's 100, so the server raises them to their maximum
 * after a map load. Called from cs2d_sv_change_map and the map load of cs2d_server.c.
 */
void cs2d_sv_fix_building_health(cs2d_server_t *sv)
{
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active) continue;
        const cs2d_dynobj_def_t *d = cs2d_dynobj_def(o->typ);
        if (!d || d->category != 1) continue;
        if (o->health < d->max_health) o->health = d->max_health;
    }
}


/* [34, typ, tx u16, ty u16, mode, rot + 1000 u16]: the client asked to build */
void cs2d_sv_build(cs2d_server_t *sv, int id, int typ, int tx, int ty, int mode, float rot)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    /* The "X" buttons of the Construct menu send tile 0xffff / 0xffff; the dedicated returns right there
     * (FUN_140175550 0x1401758f0..0x1401758ff -> 0x140175787, the epilogue), so nothing happens. */
    if (tx == 0xffff && ty == 0xffff) return;
    if (tx < 0 || ty < 0) return;

    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(typ);
    if (!def || def->category != 1 || typ == CS2D_DO_SITE) return;

    /* 1. funds: money < price refuses with 0562 (category 2 - mines, portals - is exempt) */
    if (def->category != 2 && pl->money < def->price) {
        sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_INSUFFICIENT);
        return;
    }

    /* 2. the builder needs the Wrench (item 74): the original only logs the attempt */
    if (!sv_has_item(pl, CS2D_WEP_WRENCH)) {
        cs2d_server_print("ERROR: %s tries to build without having a wrench!", pl->name);
        return;
    }

    /* 3. one own Construction Site that is not finished yet (0716) */
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (o->active && o->typ == CS2D_DO_SITE && o->player == id) {
            sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_SITE_UNFINISHED);
            return;
        }
    }

    /* 4. FUN_14014ef60: the tile must be free. -1 is "another living player is within 32 px" (0712). */
    int ptx = (int)(pl->x * 0.03125f), pty = (int)(pl->y * 0.03125f);
    int can = cs2d_dynobj_can_build_at(&sv->dynobj, ptx, pty, tx, ty, false);
    if (can == 1) {
        float cx = (float)(tx * 32 + 16), cy = (float)(ty * 32 + 16);
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            const cs2d_sv_player_t *p = &sv->players[i];
            if (i == id || !p->used || !p->alive || p->team < 1) continue;
            if (fabsf(p->x - cx) < 32.0f && fabsf(p->y - cy) < 32.0f) { can = -1; break; }
        }
    }
    if (can == 0) { sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_CANT_HERE); return; }
    if (can < 0) { sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_PLAYERS_CLOSE); return; }

    /* 5. price sanity / the Site itself: 0711 ("you are not allowed to build this") */
    if (def->price > 16000 || def->price < 0 || typ == CS2D_DO_SITE) {
        sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_NOT_ALLOWED);
        return;
    }

    /* 6. per player limit (0713). A Turret counts its Dual / Triple upgrades too (limit = limit[8]). */
    if (typ == CS2D_DO_TURRET) {
        int n = cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_TURRET, false) +
                cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_DUAL_TURRET, false) +
                cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_TRIPLE_TURRET, false);
        if (def->limit <= n) { sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_LIMIT); return; }
    } else if (cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, typ, false) >= def->limit) {
        sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_LIMIT);
        return;
    }

    /* 7. only a Dispenser refuses a same-type building within 192 px (0714): FUN_1401fab30 0x1401fc290
     * calls Tdo.sametypetoclose(7, x, y, 0xc0, team) and nothing else uses the 0714 line. */
    if (typ == CS2D_DO_DISPENSER &&
        cs2d_dynobj_same_type_close(&sv->dynobj, typ, (float)(tx * 32), (float)(ty * 32), 192.0f, pl->team)) {
        sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_TOO_CLOSE);
        return;
    }

    /* 8. the Lua "buildattempt" hook may cancel the build (the original reads its int result) */
    if (cs2d_sv_lua_hook_buildattempt(sv, id, typ, tx, ty, mode) == 1) return;

    /* 9. pay (FUN_1401fab30 0x1401fb494: Tpl.money.Inc(-price), clamped at 0) */
    pl->money -= def->price;
    if (pl->money < 0) pl->money = 0;

    /* 10. place: Barbed Wire directly (mode 60 = the hit wobble), the rest as a Construction Site whose
     * mode is the wanted type (Tdo.inibuilding(-1, player, 10, typ, tx, ty) + a Ttimer). */
    int site_typ  = (typ == CS2D_DO_BARBED_WIRE) ? CS2D_DO_BARBED_WIRE : CS2D_DO_SITE;
    int site_mode = (typ == CS2D_DO_BARBED_WIRE) ? 60 : typ;
    cs2d_dynobj_t *o = cs2d_dynobj_spawn(&sv->dynobj, -1, site_typ, tx * 32, ty * 32, rot, site_mode,
                                         pl->team, id);
    if (!o) return; /* the 1024 object pool is full */
    const cs2d_dynobj_def_t *sitedef = cs2d_dynobj_def(site_typ);
    o->health = sitedef ? sitedef->max_health : 100; /* Tdo.inibuilding: max_health of the placed type */
    if (site_typ == CS2D_DO_SITE) o->t1_ms = cs2d_sv_now() + (uint32_t)sv_build_time(typ);

    /* 11. [34] to everyone: the Site (or the Barbed Wire) with the requested type in the mode byte */
    sv_build_result(sv, id, pl->team, site_typ, tx, ty, site_mode, rot, pl->money, o->id);

    /* 12. the "build" Lua hook sees the placed object */
    cs2d_sv_lua_hook_build(sv, id, typ, tx, ty, site_mode, o->id);
}


/* Tdo.sameteam (0x140005460): in a team game both T, or both "not T"; in deathmatch (no teams) only the
 * object's owner. The gate is 0x140a015d0[sv_gamemode], which is 0 for Deathmatch (sv_gamemode 1). */
static bool sv_obj_same_team(const cs2d_server_t *sv, const cs2d_dynobj_t *o, const cs2d_sv_player_t *pl,
                             int player_id)
{
    if (sv->set.sv_gamemode == 1) return o->player == player_id;
    if (pl->team == 1) return o->team == 1;
    if (pl->team > 1) return o->team > 1;
    return false;
}

/* The kill reward and [35] of a destroyed object (Tdo.kill / Tdo.killfromplayer) */
static void sv_object_kill(cs2d_server_t *sv, cs2d_dynobj_t *o, int player)
{
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(o->typ);
    int obj_id = o->id;
    int typ = o->typ;
    (void)typ;
    cs2d_sv_lua_hook_objectkill(sv, obj_id, player);
    if (player >= 1 && player <= CS2D_SV_MAX_PLAYERS && sv->players[player].used && def && def->kill_reward > 0) {
        cs2d_sv_set_playerdata(sv, player, 2, sv->players[player].money + def->kill_reward);
    }
    if (o->typ == CS2D_DO_SITE && o->t1_ms) o->t1_ms = 0;
    cs2d_dynobj_kill(&sv->dynobj, obj_id, NULL); /* Tdo.kill: the explosion of the client is a Tp */
    sv_broadcast_obj_remove(sv, obj_id);
}

/*
 * Tdo.damage (0x140035110): damage > 0 hurts, damage < 0 repairs. `player` is the attacker (0 = none).
 * The portals (22 / 23) and the Lua images (40) never take damage. A building of the attacker's own team
 * that he does not own is left alone while friendly fire is off (DAT_140a14d98 == 0; == 2 would allow it
 * after five team building kills). Losing health drops the Wrench upgrade progress.
 */
void cs2d_sv_object_damage(cs2d_server_t *sv, int obj_id, int damage, int player)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(&sv->dynobj, obj_id);
    if (!o) return;
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(o->typ);
    if (!def) return;
    if (o->typ == CS2D_DO_ORANGE_PORTAL || o->typ == CS2D_DO_BLUE_PORTAL || o->typ == CS2D_DO_IMAGE) return;
    if (cs2d_sv_lua_hook_objectdamage(sv, obj_id, damage, player) == 1) return;

    if (player >= 1 && player <= CS2D_SV_MAX_PLAYERS && def->category == 1 && damage > 0) {
        const cs2d_sv_player_t *a = &sv->players[player];
        if (a->used && sv->set.sv_friendlyfire == 0 && o->player != player &&
            sv_obj_same_team(sv, o, a, player)) {
            return; /* friendly building: no damage */
        }
    }

    o->health -= damage;
    if (o->health != def->max_health) o->upgrade = 0;
    if (o->health <= 0) {
        o->health = 0;
        sv_object_kill(sv, o, player);
        return;
    }
    if (o->health > def->max_health) o->health = def->max_health;
    cs2d_dynobj_refresh(&sv->dynobj, o);
    sv_broadcast_obj_health(sv, o); /* [37] */
}

/*
 * The object half of an explosion (FUN_1401f1130, the last loop of
 * docs/dump/cs2d/_global/1401f0000.c:144): every dynamic object whose centre (x + 16, y + 16) is within
 * `radius` of (x, y) takes `(radius - distance) * damage / radius` through Tdo.damage (the vtable slot
 * +0xb8) - the full damage at the centre, 0 at the rim. The Te breakables (type 0x19) and the players of
 * the same function are handled elsewhere; the line of sight test (its param_8) is skipped.
 */
void cs2d_sv_explosion_objects(cs2d_server_t *sv, float x, float y, float radius, int damage, int attacker)
{
    if (radius <= 0.0f || damage <= 0) return;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active) continue;
        float dx = x - (o->x + 16.0f), dy = y - (o->y + 16.0f);
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > radius) continue;
        int dmg = (int)((radius - dist) * (float)damage / radius);
        if (dmg > 0) cs2d_sv_object_damage(sv, o->id, dmg, attacker);
    }
}

/*
 * Tdo.doupgrade (0x140143d60): `points` Wrench hits. Below upgrade_points the [37] percent moves into
 * the 100 + n range; at upgrade_points the type becomes upgrade_to with the new max health ([36,id,4]).
 * Returns 1 like the original (the caller charges the money).
 */
int cs2d_sv_object_upgrade(cs2d_server_t *sv, int obj_id, int player, int points)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(&sv->dynobj, obj_id);
    if (!o) return 0;
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(o->typ);
    if (!def) return 0;
    (void)player;
    if (def->upgrade_points <= 0 || def->upgrade_to <= 0) return 1;

    if (o->upgrade < def->upgrade_points) {
        o->upgrade += points;
        sv_broadcast_obj_health(sv, o);
    }
    if (o->upgrade >= def->upgrade_points) {
        const cs2d_dynobj_def_t *nd = cs2d_dynobj_def(def->upgrade_to);
        o->typ = (uint8_t)def->upgrade_to;
        o->upgrade = 0;
        o->health = nd ? nd->max_health : 100;
        cs2d_dynobj_refresh(&sv->dynobj, o); /* cs2d_update_mapdata */
        sv_broadcast_obj_data(sv, o, 4, def->upgrade_to, true);
    }
    return 1;
}


/* The turret's bullet damage: Tdo.shoot sets c_dmg from its 5th argument; the turret passes -1, which
 * falls back to the global at CS2D.exe 0x140a14da8 = 5 (tools/agent/extracted_api.json lists the
 * mp_turretdamage cvar that defaults to it). */
#define SV_TURRET_DAMAGE 5

/*
 * What a building on the tile the player just entered does to him (Tpl.tilepos, CS2D.exe 0x14020f7d0,
 * docs/dump/cs2d/Tpl.c:3443..3604). The function walks the Tdo list, handles every Barbed Wire and then
 * switches on the first other object of the tile; one object per tile, so the cases are exclusive:
 *   2  Barbed Wire: 20 damage (weapon 0xff) credited to its owner, the wire takes 60 and wobbles
 *      ([36,id,5]) - no team test, a wire hurts its own side too;
 *   6  Gate Field: the other side (in deathmatch anyone but the builder; a team 0 field hurts everyone,
 *      attacker 0) takes 20 damage (weapon 0xfe), the field takes 100;
 *   20 Mine / 21 Laser Mine: the mine explodes (Tdo.kill), see OPEN_ISSUES for its blast;
 *   13 / 14 / 22 / 23 are the teleporters, handled by cs2d_dynobj_teleport.
 * Returns true when the tile object was one of the damaging kinds (the caller then skips the teleport).
 */
static bool sv_tile_object_effect(cs2d_server_t *sv, int id, int tx, int ty)
{
    cs2d_sv_player_t *pl = &sv->players[id];
    bool handled = false;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active) continue;
        if ((int)(o->x * 0.03125f) != tx || (int)(o->y * 0.03125f) != ty) continue;

        if (o->typ == CS2D_DO_BARBED_WIRE) {
            cs2d_sv_damage(sv, id, o->player, 20, 0xff);
            cs2d_sv_object_damage(sv, o->id, 60, 0);
            sv_broadcast_obj_data(sv, o, 5, 0, false); /* [36,id,5]: the wobble */
            handled = true;
            continue;
        }
        if (o->typ == CS2D_DO_GATE_FIELD) {
            bool other_side;
            if (sv->set.sv_gamemode == 1) other_side = (o->player != id);
            else other_side = (pl->team == 1 && o->team == 2) || (pl->team > 1 && o->team == 1);
            if (!other_side && o->team != 0) return true;
            cs2d_sv_damage(sv, id, other_side ? o->player : 0, 20, 0xfe);
            cs2d_sv_object_damage(sv, o->id, 100, 0);
            return true;
        }
        if (o->typ == CS2D_DO_MINE) {
            /* Tpl.tilepos case 0x14: everyone but the builder (a team 0 mine: everyone) triggers it, and
             * Tdo.kill (case 0x14) explodes it with the Mine's own weapon entry (77: range 96, damage 150)
             * through FUN_1401f1130 with attacker 0. */
            bool other_side;
            if (sv->set.sv_gamemode == 1) other_side = (o->player != id);
            else other_side = (pl->team == 1 && o->team == 2) || (pl->team > 1 && o->team == 1);
            if (!other_side && o->team != 0) return true;
            const cs2d_weapon_def_t *w = cs2d_weapon_get(77);
            float mx = o->x + 16.0f, my = o->y + 16.0f;
            float radius = (w && w->range > 0) ? (float)w->range : 96.0f;
            int dmg = (w && w->damage > 0) ? w->damage : 150;
            sv_object_kill(sv, o, 0);   /* Tdo.kill(obj, 0): the client shows the Tp.explosion */
            for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                cs2d_sv_player_t *pp = &sv->players[p];
                if (!pp->used || !pp->alive) continue;
                float ddx = pp->x - mx, ddy = pp->y - my;
                float dist = sqrtf(ddx * ddx + ddy * ddy);
                if (dist > radius) continue;
                int d = (int)((radius - dist) * (float)dmg / radius);
                if (d > 0) cs2d_sv_damage(sv, p, 0, d, CS2D_WEP_MINE);
            }
            cs2d_sv_explosion_objects(sv, mx, my, radius, dmg, 0);
            return true;
        }
        return handled;
    }
    return handled;
}

static bool sv_tile_blocks(const cs2d_server_t *sv, int tx, int ty)
{
    const cs2d_map_t *map = &sv->map;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return true;
    if (cs2d_map_prop_class(cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty)) != 1) return true;
    if (map->object_collision && map->object_collision[(size_t)ty * map->width + tx]) return true;
    return false;
}

/* A bullet from (x, y) along `rot` up to `dist` px (Tshoot.Shoot): the first player hit, else 0. A player
 * is a 12 px box (the Lua "freeline" help: "mode 2+ checks player hitboxes (12px radius)"). obj_out (may
 * be NULL) gets the object that stopped the bullet, 0 for a bare wall. */
static int sv_trace_shot(cs2d_server_t *sv, float x, float y, float rot, float dist, int skip, int *obj_out)
{
    double a = (double)rot * 0.017453292519943295;
    float dx = (float)sin(a), dy = -(float)cos(a);
    int sx = (int)(x * 0.03125f), sy = (int)(y * 0.03125f);
    if (obj_out) *obj_out = 0;
    for (float d = 4.0f; d <= dist; d += 4.0f) {
        float px = x + dx * d, py = y + dy * d;
        int tx = (int)(px * 0.03125f), ty = (int)(py * 0.03125f);
        /* the muzzle sits inside the shooting building's own tile: that tile never blocks the bullet */
        if (!(tx == sx && ty == sy) && sv_tile_blocks(sv, tx, ty)) {
            if (obj_out) {
                const cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&sv->dynobj, tx, ty);
                if (o) *obj_out = o->id;
            }
            break;
        }
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            const cs2d_sv_player_t *p = &sv->players[i];
            if (i == skip || !p->used || !p->alive || p->team < 1) continue;
            if (fabsf(p->x - px) < 12.0f && fabsf(p->y - py) < 12.0f) return i;
        }
    }
    return 0;
}

/* FUN_14011c3a0: a clear straight line between two points (the turret only shoots what it can see) */
static bool sv_line_clear(const cs2d_server_t *sv, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return true;
    int sx = (int)(x1 * 0.03125f), sy = (int)(y1 * 0.03125f);
    int steps = (int)(len / 8.0f) + 1;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / (float)steps;
        int tx = (int)((x1 + dx * t) * 0.03125f), ty = (int)((y1 + dy * t) * 0.03125f);
        if (tx == sx && ty == sy) continue; /* the turret's own tile */
        if (sv_tile_blocks(sv, tx, ty)) return false;
    }
    return true;
}

/* One turret burst frame: Tdo.shoot once per barrel (the method at Tdo vtable +0x120). Type 8 fires from
 * the centre, 11 from +-6 px beside it, 12 from the centre and +-8 px (FUN_14028c630). */
static void sv_turret_fire(cs2d_server_t *sv, cs2d_dynobj_t *o)
{
    int barrels = (o->typ == CS2D_DO_TRIPLE_TURRET) ? 3 : (o->typ == CS2D_DO_DUAL_TURRET ? 2 : 1);
    float cx = o->x + 16.0f, cy = o->y + 16.0f;
    double a = ((double)o->rot + 90.0) * 0.017453292519943295;
    for (int b = 0; b < barrels; b++) {
        float side = (barrels == 2) ? (b ? -6.0f : 6.0f)
                                   : (barrels == 3 ? (b == 0 ? 0.0f : (b == 1 ? 8.0f : -8.0f)) : 0.0f);
        float bx = cx + (float)sin(a) * side, by = cy - (float)cos(a) * side;
        /* Tdo.shoot: the angle is rot + Rand(-3, 3), c_dist 400 */
        float rot = o->rot + (float)cs2d_sv_rand(-3, 3);
        int hit = sv_trace_shot(sv, bx, by, rot, 400.0f, o->player, NULL);
        if (hit == 0) continue;
        /* Do not shoot your own team (the scan only picks enemies, a bystander can still be in the way) */
        const cs2d_sv_player_t *t = &sv->players[hit];
        bool enemy = (sv->set.sv_gamemode == 1) || (o->team == 1 && t->team > 1) || (o->team > 1 && t->team == 1);
        if (!enemy) continue;
        cs2d_sv_damage(sv, hit, o->player, SV_TURRET_DAMAGE, 0xfd);
    }
}

/*
 * A player's bullet that reaches a building (Tshoot.Shoot 0x1402073ed and the server branch at
 * docs/dump/cs2d/Tshoot.c:581..592): the ray runs from the shooter along its aim up to the weapon's reach
 * (cs2d_weapon_shot_reach), stops on the tile of a solid object and calls Tdo.damage(object, weapon
 * damage, shooter, 0). Only the types whose bullets throw debris are hit (cs2d_dynobj_hit_debris) - a
 * Barricade is an obstacle and takes "close combat / explosions only" (Changelog), so it is skipped.
 * Damage to *players* stays the client's [17] report, otherwise the server would apply it twice.
 */
void cs2d_sv_shot_at_building(cs2d_server_t *sv, int id, const cs2d_weapon_def_t *wep)
{
    if (!wep || wep->damage <= 0) return;
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    const cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    int obj_id = 0;
    (void)sv_trace_shot(sv, pl->x, pl->y, pl->rot, cs2d_weapon_shot_reach(wep), id, &obj_id);
    if (obj_id == 0) return;               /* the bullet flew into a bare wall (or a player stopped it) */
    const cs2d_dynobj_t *o = cs2d_dynobj_get(&sv->dynobj, obj_id);
    if (!o || !cs2d_dynobj_hit_debris(o->typ) || o->typ == CS2D_DO_BARRICADE) return;
    cs2d_sv_object_damage(sv, obj_id, wep->damage, id);
}

/*
 * FUN_14011ba70(type, tx, ty, clip, sub, reserve, ...): the server puts a ground item on a tile and sends
 * [33, type u8, item id u16, tile x u16, tile y u16, clip + 1 u16, reserve + 1 u16] (12 bytes). The binder
 * passes clip / reserve -1, which the wire shows as 0 and the client turns into the type's defaults.
 * Returns the new item id (0 when the table is full).
 */
static int sv_spawn_item(cs2d_server_t *sv, int type, int tx, int ty)
{
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) { slot = i; break; }
    }
    if (slot < 0) return 0;
    if (sv->next_item_id == 0) sv->next_item_id = 1;
    uint16_t item_id = (uint16_t)sv->next_item_id++;
    cs2d_sv_item_t *it = &sv->items[slot];
    it->active = true;
    it->id = item_id;
    it->type = (uint8_t)type;
    it->clip = it->reserve = 0;   /* -1: the type's defaults */
    it->sub = 0;
    it->tile_x = (uint16_t)tx;
    it->tile_y = (uint16_t)ty;
    it->dropped = false;
    it->dropped_by = 0;
    it->drop_time_ms = cs2d_sv_now();

    uint8_t msg[12];
    msg[0] = 33;
    msg[1] = (uint8_t)type;
    msg[2] = (uint8_t)(item_id & 0xff); msg[3] = (uint8_t)(item_id >> 8);
    msg[4] = (uint8_t)(tx & 0xff);      msg[5] = (uint8_t)((tx >> 8) & 0xff);
    msg[6] = (uint8_t)(ty & 0xff);      msg[7] = (uint8_t)((ty >> 8) & 0xff);
    msg[8] = 0; msg[9] = 0;             /* clip -1 */
    msg[10] = 0; msg[11] = 0;           /* reserve -1 */
    sv_broadcast_joined(sv, msg, 12);
    return item_id;
}

/* A free floor tile within one tile of the object, at most 8 tries (FUN_14028b980 / FUN_14028dba0): the
 * candidate list is the 8 neighbours, (-1,-1) first and the rest from the two tables 0x140bc8a00 (y) and
 * 0x140bc8a20 (x): (0,-1) (1,-1) (-1,0) (1,0) (-1,1) (0,1) (1,1). */
static void sv_spawn_item_near(cs2d_server_t *sv, const cs2d_dynobj_t *o, int type)
{
    static const int s_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int s_dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    bool used[8] = { false };
    int cx = (int)(o->x * 0.03125f), cy = (int)(o->y * 0.03125f);
    for (int n = 0; n < 8; n++) {
        int pick;
        do { pick = cs2d_sv_rand(0, 7); } while (used[pick]);
        used[pick] = true;
        int tx = cx + s_dx[pick], ty = cy + s_dy[pick];
        if (tx < 0 || ty < 0 || (uint32_t)tx >= sv->map.width || (uint32_t)ty >= sv->map.height) continue;
        if (cs2d_map_prop_class(cs2d_map_get_tile_mode(&sv->map, (uint32_t)tx, (uint32_t)ty)) != 1) continue;
        sv_spawn_item(sv, type, tx, ty);
        return;
    }
}

/* Ground items of `type` within one tile of the object (the "already there" test of both effects) */
static int sv_items_near(const cs2d_server_t *sv, const cs2d_dynobj_t *o, int type)
{
    int n = 0;
    float cx = o->x * 0.03125f, cy = o->y * 0.03125f;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        const cs2d_sv_item_t *it = &sv->items[i];
        if (!it->active || it->type != type) continue;
        if (fabsf(cx - (float)it->tile_x) <= 1.0f && fabsf(cy - (float)it->tile_y) <= 1.0f) n++;
    }
    return n;
}

/* mp_supply_items is the comma list of the server settings; the supply mode (1..) indexes it (0 = none) */
static int sv_supply_item_at(const cs2d_server_t *sv, int mode)
{
    if (mode < 1) return 0;
    const char *s = sv->set.mp_supply_items;
    for (int i = 1; i < mode; i++) {
        s = strchr(s, ',');
        if (!s) return 0;
        s++;
    }
    return atoi(s);
}

/* A building serves a player: everyone in deathmatch, its own side otherwise (FUN_14028b980 0x14028ba0x) */
static bool sv_building_serves(const cs2d_server_t *sv, const cs2d_dynobj_t *o, int team)
{
    if (sv->set.sv_gamemode == 1) return true;
    if (o->team == 0) return true;
    if (o->team == 1) return team == 1;
    return team > 1;
}



/*
 * Dispenser (type 7, CS2D.exe FUN_14028b980, dedicated FUN_1401b5890) and Supply (type 9,
 * FUN_14028dba0, dedicated FUN_1401b7380).
 * Both operate on a 1-second cadence (1 Hz, regulated by scan_ms):
 *   Dispenser: every living player of the served side within 48 px of its centre is healed by
 *   mp_dispenser_health (DAT_140a14dac = 10) up to maxhealth and gets mp_dispenser_money
 *   (DAT_140a14db0 = 500, the Changelog default) money up to 16000 once per second.
 *   The seconds counter at Tdo +0x58 (our obj->countdown) increments every second; it spawns a secondary
 *   ammo item (62) at second 5 and a primary ammo one (61) at second 10 (then wraps), only while
 *   mp_infammo is off and no such item lies within one tile.
 *   Supply: the same counter spawns the item mp_supply_items[mode - 1] every 7 seconds when fewer than
 *   three of them lie within one tile.
 */
static void sv_step_dispenser_supply(cs2d_server_t *sv, cs2d_dynobj_t *o)
{
    if (o->typ == CS2D_DO_SUPPLY || o->typ == CS2D_DO_SUPER_SUPPLY) {
        if (++o->countdown <= 6) return;
        o->countdown = 0;
        int type = sv_supply_item_at(sv, o->mode);
        if (type <= 0) return;
        if (sv_items_near(sv, o, type) < 3) sv_spawn_item_near(sv, o, type);
        return;
    }

    /* Dispenser */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *p = &sv->players[i];
        if (!p->used || !p->alive || p->team < 1) continue;
        if (fabsf((o->x + 16.0f) - p->x) >= 48.0f || fabsf((o->y + 16.0f) - p->y) >= 48.0f) continue;
        if (!sv_building_serves(sv, o, p->team)) continue;
        if (sv->set.mp_dispenser_health > 0 && p->health < p->maxhealth) {
            cs2d_sv_set_playerdata(sv, i, 0, p->health + sv->set.mp_dispenser_health); /* [42,id,0,health] */
        }
        if (sv->set.mp_dispenser_money > 0 && p->money < 16000) {
            int v = p->money + sv->set.mp_dispenser_money;
            if (v > 16000) v = 16000;
            cs2d_sv_set_playerdata(sv, i, 2, v);                /* [42,id,2,money] */
        }
    }
    o->countdown++;
    if (o->countdown != 5 && o->countdown <= 9) return;
    if (o->countdown > 9) o->countdown = 0;
    int type = (o->countdown == 5) ? CS2D_WEP_SECONDARYAMMO : CS2D_WEP_PRIMARYAMMO;
    if (sv->set.mp_infammo != 0) return;   /* "no ammo from dispensers with mp_infammo" (Changelog) */
    if (sv_items_near(sv, o, type) == 0) sv_spawn_item_near(sv, o, type);
}




/*
 * Per frame: Construction Sites finish, Teleporter Entrances charge, turrets scan and fire.
 *
 * Turret (CS2D.exe FUN_14028c630, docs/dump/cs2d/_global/140280000.c:1971..2150): at most one scan per
 * 1000 ms (the Ttimer at +0x48 -> scan_ms), enemies only (no teams -> anyone, else the other side), the
 * builder is skipped, |dx| < 440 and |dy| < 255 of the turret centre, the nearest with a clear line wins;
 * then target = id, countdown = 15 and a reliable [36,id,0,target]. While countdown runs the turret aims
 * within 445 x 260 and fires at most every 100 ms (the Ttimer at +0x50 -> t2_ms) while countdown > 10.
 */
void cs2d_sv_step_buildings(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();
    bool team_game = sv->set.sv_gamemode != 1;

    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active) continue;

        /* Construction Site finished: the object becomes the type in its mode byte (Tdo.getdata case 1) */
        if (o->typ == CS2D_DO_SITE && o->t1_ms != 0 && (int32_t)(now - o->t1_ms) >= 0) {
            int typ = o->mode;
            const cs2d_dynobj_def_t *def = cs2d_dynobj_def(typ);
            o->t1_ms = 0;
            if (def && def->category == 1 && typ != CS2D_DO_SITE) {
                o->typ    = (uint8_t)typ;
                o->mode   = (typ == CS2D_DO_SUPPLY) ? 1 : 0; /* Tdo.inibuilding case 9 */
                o->health = def->max_health;
                if (typ == CS2D_DO_TURRET) { /* ini_building: a random rest angle */
                    o->rot = (float)cs2d_sv_rand(1, 360);
                    o->rrot = o->rot;
                    o->rotvar = 0.0f;
                    o->target = 0;
                    o->countdown = 0;
                    o->scan_ms = now;
                }
                if (typ == CS2D_DO_DISPENSER || typ == CS2D_DO_SUPPLY || typ == CS2D_DO_SUPER_SUPPLY) {
                    o->countdown = 0;
                    o->scan_ms = now + 1000;
                }
                if (typ == CS2D_DO_TELE_ENTRANCE) {
                    /* Tdo.draw_ground 0xd: charged after mp_teleportreload (DAT_140a14d48) seconds */
                    o->t2_ms = now + (uint32_t)(sv->set.mp_teleportreload > 0 ? sv->set.mp_teleportreload : 5) * 1000u;
                }
                cs2d_dynobj_refresh(&sv->dynobj, o);
                sv_broadcast_obj_data(sv, o, 1, 0, false); /* [36,id,1] */
            }
            continue;
        }

        /* A Teleporter Entrance is charged five seconds after it stands ([36,id,5], mode 1) */
        if (o->typ == CS2D_DO_TELE_ENTRANCE && o->t2_ms != 0 && (int32_t)(now - o->t2_ms) >= 0) {
            o->t2_ms = 0;
            o->mode  = 1;
            sv_broadcast_obj_data(sv, o, 5, 0, false);
            continue;
        }

        /* A Dispenser or Supply runs at 1 Hz (1 second interval) */
        if (o->typ == CS2D_DO_DISPENSER || o->typ == CS2D_DO_SUPPLY || o->typ == CS2D_DO_SUPER_SUPPLY) {
            if (o->scan_ms != 0 && (int32_t)(now - o->scan_ms) < 0) continue;
            o->scan_ms = now + 1000;
            sv_step_dispenser_supply(sv, o);
            continue;
        }

        if (o->typ != CS2D_DO_TURRET && o->typ != CS2D_DO_DUAL_TURRET && o->typ != CS2D_DO_TRIPLE_TURRET) continue;

        if (o->countdown > 0) {
            /* Aim at the target while it is alive and within 445 x 260 px */
            const cs2d_sv_player_t *t = (o->target >= 1 && o->target <= CS2D_SV_MAX_PLAYERS)
                                        ? &sv->players[o->target] : NULL;
            if (t && t->used && t->alive && t->team >= 1 &&
                fabsf((o->x + 16.0f) - t->x) < 445.0f && fabsf((o->y + 16.0f) - t->y) < 260.0f) {
                o->rot = (float)(atan2((double)((t->x - 16.0f) - o->x),
                                       (double)((o->y + 16.0f) - t->y)) * 57.29577951308232);
            }
            if (o->countdown > 10 && (o->t2_ms == 0 || (int32_t)(now - o->t2_ms) >= 100)) {
                o->t2_ms = now; /* the 100 ms shot throttle */
                sv_turret_fire(sv, o);
            }
            o->countdown--;
            if (o->countdown <= 0) { /* the burst ended: the idle sweep restarts from the aim angle */
                o->countdown = 0;
                o->rrot = o->rot;
                o->rotvar = 0.0f;
                o->scan_ms = now + 1000;
            }
            continue;
        }

        if (o->scan_ms != 0 && (int32_t)(now - o->scan_ms) < 0) continue;
        o->scan_ms = now + 1000;
        int best = 0;
        float best_d = 0.0f;
        /* A Lua "turretscan" hook may name the target itself (FUN_14028c630: the returned id wins). */
        int hooked = cs2d_sv_lua_hook_turretscan(sv, o->id, o->team, (int)(o->x + 16.0f), (int)(o->y + 16.0f));
        if (hooked > 0) {
            best = hooked;
        } else {
            for (int k = 1; k <= CS2D_SV_MAX_PLAYERS; k++) {
                const cs2d_sv_player_t *p = &sv->players[k];
                if (!p->used || !p->alive || p->team < 1) continue;
                if (o->player != 0 && k == o->player) continue;
                bool enemy = !team_game || (o->team == 1 && p->team > 1) || (o->team > 1 && p->team == 1);
                if (!enemy) continue;
                float dx = (o->x + 16.0f) - p->x, dy = (o->y + 16.0f) - p->y;
                if (fabsf(dx) >= 440.0f || fabsf(dy) >= 255.0f) continue;
                float d = dx * dx + dy * dy;
                if (best != 0 && d >= best_d) continue;
                if (!sv_line_clear(sv, o->x + 16.0f, o->y + 16.0f, p->x, p->y)) continue;
                best = k;
                best_d = d;
            }
        }
        if (best != 0) {
            o->target = best;
            o->countdown = 15;
            o->t2_ms = now;
            sv_broadcast_obj_data(sv, o, 0, best, true); /* [36,id,0,target] */
        }
    }
}


/*
 * The Wrench primary attack on an own building (Tshoot.Shoot, docs/dump/cs2d/Tshoot.c:594..650):
 *   health < max health -> repair: damage -15 (a negative amount heals), needs $50, and the health
 *                         message drops the upgrade progress with it;
 *   else, upgrade_points > 0 -> one upgrade point (Tdo.doupgrade), needs $100.
 * The target is the object on the tile where the swing ends (Tdo.getatpos at the end of the shot ray;
 * the Wrench's range is 15 px in the weapon table set_wep(74, ...)).
 */
static cs2d_dynobj_t *sv_wrench_target(const cs2d_server_t *sv, const cs2d_sv_player_t *pl)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get(CS2D_WEP_WRENCH);
    float reach = (w && w->range > 0) ? (float)w->range : 15.0f;
    double a = (double)pl->rot * 0.017453292519943295;
    float ex = pl->x + (float)sin(a) * reach, ey = pl->y - (float)cos(a) * reach;
    return cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)&sv->dynobj, (int)(ex * 0.03125f), (int)(ey * 0.03125f));
}

void cs2d_sv_wrench_hit(cs2d_server_t *sv, int id)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;

    cs2d_dynobj_t *o = sv_wrench_target(sv, pl);
    if (!o) return;
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(o->typ);
    if (!def || def->category != 1) return;
    /* Tshoot.Shoot: only a same team building is repaired / upgraded */
    if (!sv_obj_same_team(sv, o, pl, id)) return;

    if (o->health < def->max_health) {
        if (pl->money <= 49) return;                 /* needs $50 */
        o->health += 15;                             /* Tdo.damage(-15, player, 0) */
        if (o->health != def->max_health) o->upgrade = 0;
        if (o->health > def->max_health) o->health = def->max_health;
        cs2d_dynobj_refresh(&sv->dynobj, o);
        sv_broadcast_obj_health(sv, o);
        cs2d_sv_set_playerdata(sv, id, 2, pl->money - 50);   /* [42,id,2,money] */
    } else if (def->upgrade_points > 0) {
        if (pl->money <= 99) return;                 /* needs $100 */
        if (cs2d_sv_object_upgrade(sv, o->id, id, 1) == 1) {
            cs2d_sv_set_playerdata(sv, id, 2, pl->money - 100);
        }
    }
}

