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
}

/* FUN_1402520b0 / FUN_140123530: start of a new round, respawning players and sending [22] */
void cs2d_sv_round_start(cs2d_server_t *sv, int reason)
{
    sv->round_end = 0;
    sv->round_start_ms = cs2d_sv_now();

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

    /* Standard win condition check: if all players of one team are dead (FUN_14015ce30) */
    const sv_mode_t *m = sv_mode(sv);
    if (m->respawn == 0) {
        int t_total = cs2d_sv_count_team(sv, 1);
        int ct_total = cs2d_sv_count_team(sv, 2);
        if (t_total > 0 && ct_total > 0) {
            int t_alive = 0, ct_alive = 0;
            for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                cs2d_sv_player_t *pl = &sv->players[i];
                if (!pl->used || !pl->alive) continue;
                if (pl->team == 1) t_alive++;
                else if (pl->team == 2) ct_alive++;
            }
            if (t_alive == 0 && ct_alive == 0) {
                cs2d_sv_round_end(sv, 3, 3000); /* Round draw */
            } else if (t_alive == 0) {
                cs2d_sv_round_end(sv, 2, 5000); /* CTs win */
            } else if (ct_alive == 0) {
                cs2d_sv_round_end(sv, 1, 5000); /* Terrorists win */
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

    pl->weapon = weapon;

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
    if (weapon != 0xfc) v->deaths++;

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


