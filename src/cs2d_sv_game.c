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

    /* "Game commencing": when both teams have at least 1 player (or DM has >= 2) and no round end is pending (FUN_1401624c0 LAB_14015cff0) */
    if (sv->round_end == 0) {
        int t = cs2d_sv_count_team(sv, 1);
        int ct = cs2d_sv_count_team(sv, 2);
        if (sv_mode(sv)->respawn == 0) {
            /* Standard mode: if t == 1 && ct == 1 (or 2 vs 0 / 0 vs 2), trigger round end 4 with 3000 ms */
            if ((t == 1 && ct == 1) || (t == 2 && ct == 0) || (ct == 2 && t == 0)) {
                cs2d_sv_round_end(sv, 4, 3000);
            }
        }
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

/* FUN_14015c840: triggers a round end and broadcasts opcode 226 mode 1 (the end message banner) */
void cs2d_sv_round_end(cs2d_server_t *sv, int mode, int delay_ms)
{
    if (mode < 0 || sv->round_end != 0) return;
    sv->round_end = mode;
    sv->round_end_delay = delay_ms;
    sv->round_end_ms = cs2d_sv_now();

    /* Find MVP player (kills * 2 + assists) or specific player based on mode (FUN_14015c080) */
    int mvp_id = 0;
    int best_score = -1;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || pl->team < 1) continue;
        int pts = pl->score * 2 + pl->assists;
        if (pts > best_score) {
            best_score = pts;
            mvp_id = i;
        }
    }

    const char *reason_str = " - ";
    switch (mode) {
    case 1: case 2: case 0x16: reason_str = "for most eliminations"; break;
    case 0xb:                  reason_str = "for escaping from the terrorists"; break;
    case 0xc:                  reason_str = "for killing the VIP"; break;
    case 0x14:                 reason_str = "for detonating the bomb"; break;
    case 0x15:                 reason_str = "for defusing the bomb"; break;
    case 0x1f:                 reason_str = "for rescuing most hostages"; break;
    case 0x3c:                 reason_str = "for most infections"; break;
    case 0x3d:                 reason_str = "for killing most zombies"; break;
    default: break;
    }

    /* Broadcast opcode 226 mode 1: [226, 1, winner_team, mvp_id, text_lstr, reason_byte] */
    int winner_team = (mode == 1) ? 1 : (mode == 2 || mode == 0xb || mode == 0x15) ? 2 : 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 226);
        cs2d_pkt_w_byte(p, 1);
        cs2d_pkt_w_byte(p, (uint8_t)winner_team);
        cs2d_pkt_w_byte(p, (uint8_t)mvp_id);
        cs2d_pkt_w_lstr(p, reason_str);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
    }
    cs2d_sv_lua_hook_endround(sv, mode);
}

/* FUN_1402520b0 / FUN_140123530: start of a new round, respawning players and sending [22] */
void cs2d_sv_round_start(cs2d_server_t *sv, int reason)
{
    sv->round_end = 0;
    sv->round_start_ms = cs2d_sv_now();
    cs2d_sv_lua_hook_startround(sv, reason);

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

    /* If map has Info_BombSpot, give bomb to a random alive T in standard mode */
    bool has_bombspot = false;
    for (uint32_t e = 0; e < sv->map.entity_count; e++) {
        if (sv->map.entities[e].type == CS2D_ENT_INFO_BOMBSPOT) {
            has_bombspot = true;
            break;
        }
    }
    if (has_bombspot && sv->set.sv_gamemode == 0) {
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

/* Called every server tick to handle pending round end delay and standard win conditions (FUN_14015ce30) */
void cs2d_sv_step_round(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();
    if (sv->round_end != 0) {
        if (now - sv->round_end_ms >= (uint32_t)sv->round_end_delay) {
            int reason = sv->round_end;
            cs2d_sv_round_start(sv, reason);
        }
        return;
    }

    /* Standard win condition check: if all players of one team are dead or single player dies (FUN_14015ce30) */
    const sv_mode_t *m = sv_mode(sv);
    if (m->respawn == 0) {
        int t_total = cs2d_sv_count_team(sv, 1);
        int ct_total = cs2d_sv_count_team(sv, 2);

        int t_alive = 0, ct_alive = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || !pl->alive) continue;
            if (pl->team == 1) t_alive++;
            else if (pl->team == 2) ct_alive++;
        }

        /* If at least one team has players registered */
        if (t_total > 0 || ct_total > 0) {
            /* If only one team exists on the server (e.g. single player or same team) and no one is alive */
            if (t_total == 0 || ct_total == 0) {
                if (t_alive == 0 && ct_alive == 0) {
                    /* Original dedicated: if (t_total == 0 || ct_total == 0) -> round draw (reason 3, 3000 ms) */
                    cs2d_sv_round_end(sv, 3, 3000);
                    return;
                }
            } else {
                /* Both teams have players */
                if (t_alive == 0 && ct_alive == 0) {
                    if (sv->bomb_planted) {
                        cs2d_sv_round_end(sv, 1, 5000); /* Terrorists win (bomb planted) */
                    } else {
                        cs2d_sv_round_end(sv, 3, 3000); /* Round draw */
                    }
                    return;
                } else if (t_alive == 0) {
                    if (!sv->bomb_planted) {
                        cs2d_sv_round_end(sv, 2, 5000); /* CTs win */
                        return;
                    }
                } else if (ct_alive == 0) {
                    cs2d_sv_round_end(sv, 1, 5000); /* Terrorists win */
                    return;
                }
            }
        }

        /* Round timer expiration check (0x1401a3128) */
        if (sv->set.mp_roundtime > 0 && sv->set.mp_roundtime < 100) {
            uint32_t round_duration_ms = (uint32_t)sv->set.mp_roundtime * 60000;
            if (now - sv->round_start_ms >= round_duration_ms) {
                if (!sv->bomb_planted) {
                    /* Time ran out without bomb: CTs win (reason 22 = target saved / time out) */
                    cs2d_sv_round_end(sv, 22, 5000);
                    return;
                }
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

    /* CT victory: mode 21 = Bomb defused, CT win */
    cs2d_sv_round_end(sv, 21, 5000);
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

    /* T victory: mode 20 = Target bombed, T win */
    cs2d_sv_round_end(sv, 20, 5000);
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
                h->health = 0;
                h->follow = 0;
                uint8_t pkt[7];
                pkt[0] = 29; pkt[1] = (uint8_t)h->id; pkt[2] = 2;
                uint16_t zero = 0, rescuer = (uint16_t)h->follow;
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

                int rem = 0;
                for (int k = 0; k < sv->hostage_count; k++) {
                    if (sv->hostages[k].health > 0) rem++;
                }
                if (rem == 0) {
                    cs2d_sv_round_end(sv, 21, 5000);
                }
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
    if (sv->lua && sv->set.mp_luamap) {
        cs2d_sv_lua_load_map(sv, mapname);
    }
    sv->score_t = sv->score_ct = 0;

    cs2d_sv_round_start(sv, 0);

    cs2d_sv_lua_hook_mapchange(sv, mapname);
    cs2d_server_print("Map changed to '%s'", mapname);
    return true;
}

