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
#include "cs2d_random.h"
#include "cs2d_sv_lua.h"
#include "cs2d_sv_ai.h"

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

static void sv_projectile_props(int type, float *out_speed, float *out_lifetime, float *out_flydist);
static void sv_place_dropped_item(cs2d_server_t *sv, cs2d_sv_player_t *pl, uint16_t item_id, int type,
                                  int clip, int reserve, int sub);

static bool sv_is_grenade_type(int t)
{
    return t == 51 || t == 52 || t == 53 || t == 54 ||
           t == 72 || t == 73 || t == 75 || t == 76 || t == 77;
}

/* Comma/pipe separated id list ("1,2|3", mp_unbuyable / mp_unbuildable). */
static bool sv_list_has_id(const char *list, int id)
{
    if (!list || !list[0] || id <= 0) return false;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '|') p++;
        if (!*p) break;
        if (atoi(p) == id) return true;
        while (*p && *p != ',' && *p != '|' && *p != ' ' && *p != '\t') p++;
    }
    return false;
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

static void sv_broadcast_joined_except(cs2d_server_t *sv, int except_id, const uint8_t *msg, int len)
{
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (i == except_id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        for (int k = 0; k < len; k++) cs2d_pkt_w_byte(p, msg[k]);
    }
}

static void sv_broadcast_joined(cs2d_server_t *sv, const uint8_t *msg, int len)
{
    sv_broadcast_joined_except(sv, 0, msg, len);
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
 * DAT_1407dfa18). False when the map has none ("FATAL ERROR: no active spawnpoints found!")
 *
 * In Deathmatch (mode 1) and Team Deathmatch (mode 2) with mp_randomspawn == 1
 * (0x140160da0 - 0x140160dae, 0x140162313 -> 0x1400d5650), players spawn on random walkable tiles. */
/* Damage-assist tracker [victim][attacker]: HP the victim lost to each attacker since spawn (saturates at 255;
 * the assist threshold is 40). Process-wide, not in cs2d_sv_player_t: the test binary holds ~50 static servers
 * and is already at the image size ceiling. */
static uint8_t sv_dmg_from[CS2D_SV_MAX_PLAYERS + 1][CS2D_SV_MAX_PLAYERS + 1];

static bool sv_pick_spawn(cs2d_server_t *sv, int id, float *ox, float *oy)
{
    const cs2d_sv_player_t *pl = &sv->players[id];

    /* mp_randomspawn (0x140160da0 - 0x140160dae, 0x140162313 -> 0x1400d5650):
     * in Deathmatch (mode 1) and Team Deathmatch (mode 2) with mp_randomspawn == 1,
     * pick a random non-solid walkable tile anywhere on the map */
    if ((sv->set.sv_gamemode == 1 || sv->set.sv_gamemode == 2) && sv->set.mp_randomspawn == 1) {
        if (sv->map.width > 2 && sv->map.height > 2) {
            for (int attempt = 0; attempt < 1000; attempt++) {
                int tx = cs2d_sv_rand(1, (int)sv->map.width - 2);
                int ty = cs2d_sv_rand(1, (int)sv->map.height - 2);
                if (cs2d_map_is_solid(&sv->map, tx, ty)) continue;

                bool taken = false;
                for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
                    const cs2d_sv_player_t *o = &sv->players[i];
                    if (!o->used || o->team < 1 || !o->alive) continue;
                    if ((int)(o->x * 0.03125f) == tx && (int)(o->y * 0.03125f) == ty) {
                        taken = true;
                        break;
                    }
                }
                if (!taken || attempt > 900) {
                    *ox = (float)(tx * 32 + 16);
                    *oy = (float)(ty * 32 + 16);
                    return true;
                }
            }
        }
    }

    int want = (pl->team == 1) ? CS2D_ENT_INFO_T : (pl->team == 2) ? CS2D_ENT_INFO_CT : CS2D_ENT_INFO_VIP;
    int cand[1024], n = 0;
    for (uint32_t e = 0; e < sv->map.entity_count && n < 1024; e++) {
        if ((int)sv->map.entities[e].type == want) cand[n++] = (int)e;
    }

    /* Fallback in Deathmatch (sv_gamemode 1): players are not bound to team sides, so if the map only
     * contains spawns for one team (e.g. only Info_T), allow any spawn point */
    if (n == 0 && sv->set.sv_gamemode == 1) {
        for (uint32_t e = 0; e < sv->map.entity_count && n < 1024; e++) {
            int t = (int)sv->map.entities[e].type;
            if (t == CS2D_ENT_INFO_T || t == CS2D_ENT_INFO_CT) cand[n++] = (int)e;
        }
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
    memset(sv_dmg_from[id], 0, sizeof(sv_dmg_from[id]));
    pl->alive = true;
    pl->aim = 0.0f;
    pl->aim_valid = false;
    pl->move_flag = pl->rot_flag = 0;
    pl->is_reloading = false;
    pl->reload_end_ms = 0;
    pl->team_changes = 0;
    memset(pl->grenade_rebuy, 0, sizeof(pl->grenade_rebuy));
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

    /* The engine calls ai_onspawn(id) after the spawn (bots/Standard AI.lua:58). */
    if (pl->bot) cs2d_sv_ai_on_spawn(sv, id);
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

/* FUN_14015c080: the best player of one team (kills * 2 + assists, the highest value wins).
 * Team 2 only: the VIP (team 3) never becomes the MVP. */
static int sv_best_of_team(cs2d_server_t *sv, int team)
{
    int best = 0, best_val = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || pl->team != team) continue;
        int pts = pl->kills * 2 + pl->assists;
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
    case 31: {                                   /* 0x14015c080 case 0x1f: most hostages rescued */
        int best_rescuer = 0, best_cnt = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || pl->team != 2) continue;
            if (pl->hostages_rescued > best_cnt) {
                best_cnt = pl->hostages_rescued;
                best_rescuer = i;
            }
        }
        mvp = best_rescuer ? best_rescuer : player_id;
        break;
    }
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

/* Ground item ids are one counter shared by every spawn path (Env_Item, spawnitem, drop, planted
 * bomb). The id must be reserved BEFORE the Lua drop hook runs: the hook receives the id of the
 * item that will be created (help.php drop: "iid : item id for new item on the map"). */
static uint16_t sv_alloc_item_id(cs2d_server_t *sv)
{
    if (sv->next_item_id == 0) sv->next_item_id = 1;
    return (uint16_t)sv->next_item_id++;
}

/*
 * FUN_14011ba70(type, tx, ty, clip, sub, reserve, ...): the server puts a ground item on a tile and sends
 * [33, type u8, item id u16, tile x u16, tile y u16, clip + 1 u16, reserve + 1 u16] (12 bytes). The binder
 * passes clip / reserve -1, which the wire shows as 0 and the client turns into the type's defaults.
 * Returns the new item id (0 when the table is full).
 */
static int sv_spawn_item_ex(cs2d_server_t *sv, int type, int tx, int ty, int clip, int reserve)
{
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) { slot = i; break; }
    }
    if (slot < 0) return 0;
    uint16_t item_id = sv_alloc_item_id(sv);
    cs2d_sv_item_t *it = &sv->items[slot];
    it->active = true;
    it->id = item_id;
    it->type = (uint8_t)type;
    int default_clip = -1, default_res = -1;
    cs2d_weapon_item_ammo((uint8_t)type, &default_clip, &default_res);
    it->clip = (uint16_t)((clip >= 0) ? clip : (default_clip > 0 ? default_clip : 1));
    it->reserve = (uint16_t)((reserve >= 0) ? reserve : (default_res > 0 ? default_res : 0));
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
    /* clip / reserve are sent + 1 (client FUN_14011c0b0 reads ushort - 1); 0 means "defaults" */
    uint16_t wclip = (uint16_t)((clip >= 0) ? (clip + 1) : 0);
    uint16_t wres = (uint16_t)((reserve >= 0) ? (reserve + 1) : 0);
    msg[8] = (uint8_t)(wclip & 0xff);   msg[9] = (uint8_t)(wclip >> 8);
    msg[10] = (uint8_t)(wres & 0xff);   msg[11] = (uint8_t)(wres >> 8);
    sv_broadcast_joined(sv, msg, 12);
    return item_id;
}

static int sv_spawn_item(cs2d_server_t *sv, int type, int tx, int ty)
{
    return sv_spawn_item_ex(sv, type, tx, ty, -1, -1);
}

/* spawnitem (help.php): server-spawned ground item with optional ammo values */
int cs2d_sv_spawn_item(cs2d_server_t *sv, int type, int tx, int ty, int clip, int reserve)
{
    if (!sv || type <= 0 || type >= CS2D_MAX_WEAPONS) return 0;
    return sv_spawn_item_ex(sv, type, tx, ty, clip, reserve);
}

/* spawnobject / spawnnpc (help.php): [49, id u16, typ u8, x u16, y u16, rot u16, mode u8, team u8, player u8]
 * Position (cs2d_dedicated.exe): spawnobject FUN_14011d6c0 passes tile * 32 (the tile corner), spawnnpc
 * FUN_14011d910 passes tile * 32 + 16 (the tile centre); `center` selects the NPC case. */
void cs2d_sv_spawn_object(cs2d_server_t *sv, int typ, int tx, int ty, int mode, int team, int player, float rot,
                          bool center)
{
    if (!sv || typ <= 0 || typ >= CS2D_DYNOBJ_TYPES) return;
    float px = (float)(tx * CS2D_TILE_SIZE + (center ? CS2D_TILE_SIZE / 2 : 0));
    float py = (float)(ty * CS2D_TILE_SIZE + (center ? CS2D_TILE_SIZE / 2 : 0));
    cs2d_dynobj_t *o = cs2d_dynobj_spawn(&sv->dynobj, -1, typ, px, py, rot, mode, team, player);
    if (!o) return;
    /* Tdo.inibuilding sets max_health[typ] on the server; spawnnpc uses the NPC type's default health */
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(typ);
    if (def && def->category == 1) {
        o->health = cs2d_sv_building_health(sv, typ);
    } else if (typ == CS2D_DO_NPC) {
        int hp = cs2d_dynobj_npc_health(player);
        if (hp > 0) o->health = hp;
    }
    uint8_t msg[13];
    msg[0] = 49;
    msg[1] = (uint8_t)(o->id & 0xff); msg[2] = (uint8_t)(o->id >> 8);
    msg[3] = (uint8_t)typ;
    uint16_t x = (uint16_t)(int)px, y = (uint16_t)(int)py;
    msg[4] = (uint8_t)(x & 0xff); msg[5] = (uint8_t)(x >> 8);
    msg[6] = (uint8_t)(y & 0xff); msg[7] = (uint8_t)(y >> 8);
    uint16_t r = (uint16_t)(int)(rot * 182.04445f);
    msg[8] = (uint8_t)(r & 0xff); msg[9] = (uint8_t)(r >> 8);
    msg[10] = (uint8_t)mode;
    msg[11] = (uint8_t)team;
    msg[12] = (uint8_t)player;
    sv_broadcast_joined(sv, msg, sizeof(msg));
}

/* Tpro.add(-1, type, player) (CS2D.exe 0x1400a8b30): projectile ids are bytes that are unique
 * per owner (1..255). The original scans that range and fails when every id is in use. */
static int sv_next_projectile_id(const cs2d_server_t *sv, int player)
{
    for (int id = 1; id < 256; id++) {
        bool used = false;
        for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
            const cs2d_sv_projectile_t *p = &sv->projectiles[i];
            if (p->active && (int)p->player == player && (int)p->id == id) {
                used = true;
                break;
            }
        }
        if (!used) return id;
    }
    return 0;
}

/* spawnprojectile (help.php) / Tpro.spawnprojectile (CS2D.exe 0x1400a8d50):
 * [58, player u8, type u8, id u8, x+0x8000 u16, y+0x8000 u16, flydist u16, dir u16] (12 bytes).
 * x / y and flydist are in PIXELS; flydist <= 0 spawns the projectile lying on the ground. */
int cs2d_sv_spawn_projectile(cs2d_server_t *sv, int player, int type, int x, int y, int flydist, int dir)
{
    if (!sv || type <= 0 || type >= CS2D_MAX_WEAPONS) return 0;
    if (player < 0 || player > CS2D_SV_MAX_PLAYERS) return 0;
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
        if (!sv->projectiles[i].active) { slot = i; break; }
    }
    if (slot < 0) return 0;
    int proj_id = sv_next_projectile_id(sv, player);
    if (proj_id == 0) return 0;

    cs2d_sv_projectile_t *p = &sv->projectiles[slot];
    float speed, lifetime, def_fly;
    sv_projectile_props(type, &speed, &lifetime, &def_fly);
    (void)def_fly;
    if (flydist > 50000) flydist = 50000;   /* Tpro.spawnprojectile clamps to 0xc351 */
    if (flydist < 0) flydist = 0;
    float fd = (float)flydist;
    uint32_t now = cs2d_sv_now();
    p->active = true;
    p->id = (uint16_t)proj_id;
    p->player = (uint8_t)player;
    p->type = (uint8_t)type;
    p->x = (float)x;
    p->y = (float)y;
    p->dir = (float)((dir % 360 + 360) % 360);
    p->flydist = fd;
    p->speed = speed;
    p->spawn_ms = now;
    p->detonate_ms = (type == CS2D_WEP_HE || type == CS2D_WEP_FLASHBANG) ? (now + 1800) : 0;
    p->is_flying = (fd > 0.0f);
    p->expire_ms = now + (uint32_t)(lifetime * 1000.0f);

    uint8_t msg[12];
    msg[0] = 58;
    msg[1] = (uint8_t)player;
    msg[2] = (uint8_t)type;
    msg[3] = (uint8_t)proj_id;
    uint16_t bx = (uint16_t)(x + 0x8000), by = (uint16_t)(y + 0x8000);
    msg[4] = (uint8_t)(bx & 0xff); msg[5] = (uint8_t)(bx >> 8);
    msg[6] = (uint8_t)(by & 0xff); msg[7] = (uint8_t)(by >> 8);
    uint16_t fw = (uint16_t)flydist;
    msg[8] = (uint8_t)(fw & 0xff); msg[9] = (uint8_t)(fw >> 8);
    uint16_t dw = (uint16_t)(p->dir * 182.04445f);
    msg[10] = (uint8_t)(dw & 0xff); msg[11] = (uint8_t)(dw >> 8);
    sv_broadcast_joined(sv, msg, sizeof(msg));
    return (int)p->id;
}

static bool sv_has_item_at_tile(const cs2d_server_t *sv, int tx, int ty, int type)
{
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        const cs2d_sv_item_t *it = &sv->items[i];
        if (it->active && it->tile_x == tx && it->tile_y == ty) {
            if (type <= 0 || it->type == type) return true;
        }
    }
    return false;
}

/*
 * Env_Item (ID 21) map entity initialization:
 * ints[0] = item type (1..89)
 * ints[1] = spawn mode: 0 (one time), 1 (instant/infinite), 2 (every X sec), 3 (on trigger only)
 * ints[2] = spawn delay in seconds (for mode 2)
 */
void cs2d_sv_init_env_items(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    memset(sv->items, 0, sizeof(sv->items));
    memset(sv->env_item_timer, 0, sizeof(sv->env_item_timer));
    sv->next_item_id = 1;
    uint32_t now = cs2d_sv_now();
    for (uint32_t i = 0; i < sv->map.entity_count; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (e->type != CS2D_ENT_ENV_ITEM) continue;
        int wep_type = e->int_settings[0];
        int spawn_mode = e->int_settings[1];
        if (wep_type <= 0 || wep_type >= CS2D_MAX_WEAPONS) continue;
        /* A disabled Env_Item (state 1) does not spawn; Trigger_Start decides which ones are off. */
        if (sv->map.entity_state[i] != 0) continue;
        if (spawn_mode == 0 || spawn_mode == 1 || spawn_mode == 2) {
            if (!sv_has_item_at_tile(sv, e->x, e->y, wep_type)) {
                sv_spawn_item(sv, wep_type, e->x, e->y);
            }
            if (spawn_mode == 2) sv->env_item_timer[i] = now;
        }
    }
}

void cs2d_sv_trigger_env_items(cs2d_server_t *sv, const char *trigger_name)
{
    if (!sv || !sv->map_loaded || !trigger_name || !trigger_name[0]) return;
    for (uint32_t i = 0; i < sv->map.entity_count; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (e->type != CS2D_ENT_ENV_ITEM) continue;
        if (e->name[0] && _stricmp(e->name, trigger_name) == 0) {
            int wep_type = e->int_settings[0];
            if (wep_type > 0 && wep_type < CS2D_MAX_WEAPONS) {
                if (!sv_has_item_at_tile(sv, e->x, e->y, wep_type)) {
                    sv_spawn_item(sv, wep_type, e->x, e->y);
                }
            }
        }
    }
}

/* ==========================================================================
 * Entity trigger graph
 *
 * The dedicated runs the map's trigger entities itself: round start fires every Trigger_Start
 * (90), a `trigger <name>` command / Trigger_Use / Trigger_Move fires the names in the entity's
 * "trigger" field, Trigger_If (96) evaluates its condition and Trigger_Delay (95) / Trigger_Once
 * run their state machines. Verified against the original with a probe script:
 *   - `parse("trigger guarita2")` toggles the Func_DynWall state and the Lua `trigger` hook runs
 *     once per comma-separated name (source false for map entities, true for the script command);
 *   - Trigger_If runs "_cs2d_trigger_if=" + strs[0] and only fires its list when true.
 * ========================================================================== */

#define SV_TRIGGER_MAX_DEPTH 16

static int sv_trigger_find_at(cs2d_server_t *sv, int x, int y)
{
    if (!sv || !sv->map_loaded) return -1;
    for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
        if (sv->map.entities[i].x == x && sv->map.entities[i].y == y) return (int)i;
    }
    return -1;
}

static void sv_entity_state_set(cs2d_server_t *sv, int idx, uint8_t state, bool broadcast)
{
    if (!sv || idx < 0 || idx >= (int)sv->map.entity_count || idx >= CS2D_MAX_ENTITIES) return;
    if (sv->map.entity_state[idx] == state) return;
    sv->map.entity_state[idx] = state;
    sv->map.entity_state_version++;
    if (broadcast && state <= 1) {
        const cs2d_entity_t *e = &sv->map.entities[idx];
        uint8_t pkt[6] = { 31, (uint8_t)(e->x & 0xff), (uint8_t)((e->x >> 8) & 0xff),
                           (uint8_t)(e->y & 0xff), (uint8_t)((e->y >> 8) & 0xff), state };
        sv_broadcast_joined(sv, pkt, 6);
    }
}

/* Env_Building / Env_NPC: keep the shared Tdo list in step with the entity state. The map spawn
 * pass skips entities in state != 0; a trigger that hides one removes its object, showing it
 * creates the object again (same parameters as cs2d_dynobj_spawn_map_objects). */
static void sv_map_object_entity_apply(cs2d_server_t *sv, const cs2d_entity_t *e, int state)
{
    if (!sv || !e) return;
    int found = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active || !o->entity[0]) continue;
        if (o->entity[1] != e->x || o->entity[2] != e->y) continue;
        found = o->id;
        if (state) {
            cs2d_dynobj_kill(&sv->dynobj, o->id, NULL);
            return;
        }
    }
    if (found || state) return;

    if (e->type == CS2D_ENT_ENV_BUILDING) {
        cs2d_dynobj_t *o = cs2d_dynobj_spawn(&sv->dynobj, -1, e->int_settings[0], e->x << 5, e->y << 5,
                                             (float)e->int_settings[2], 0, e->int_settings[1], 0);
        if (o) {
            o->entity[0] = 1;
            o->entity[1] = e->x;
            o->entity[2] = e->y;
        }
    } else if (e->type == CS2D_ENT_ENV_NPC && e->int_settings[3] == 0) {
        int npc_type = e->int_settings[1];
        if (npc_type <= 0) return;
        cs2d_dynobj_t *o = cs2d_dynobj_spawn(&sv->dynobj, -1, CS2D_DO_NPC, (e->x << 5) + 16, (e->y << 5) + 16,
                                             (float)e->int_settings[0], 0, 0, npc_type);
        if (o) {
            int hp = cs2d_dynobj_npc_health(npc_type);
            o->health = (e->int_settings[2] > 0) ? e->int_settings[2] : hp;
            o->entity[0] = 1;
            o->entity[1] = e->x;
            o->entity[2] = e->y;
        }
    }
}

/* Trigger_If condition (help.php entities.php?entity=Trigger_If). */
static bool sv_trigger_if_condition(cs2d_server_t *sv, const cs2d_entity_t *e)
{
    if (!sv || !e) return false;
    int type = e->int_settings[0];
    int a = e->int_settings[1], b = e->int_settings[2], c = e->int_settings[3];
    int d = e->int_settings[4], f = e->int_settings[5];

    if (type == 0) {
        if (!sv->set.mp_luamap) return false;
        bool result = false;
        cs2d_sv_lua_trigger_if(sv, e->str0[0] ? e->str0 : e->str_settings[0], &result);
        return result;
    }
    if (type == 1 || type == 2) {
        int cnt = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || !pl->alive) continue;
            int tx = (int)(pl->x * 0.03125f), ty = (int)(pl->y * 0.03125f);
            if (tx >= b && tx < b + d && ty >= c && ty < c + f) cnt++;
        }
        return (type == 1) ? (cnt >= a) : (cnt <= a);
    }
    if (type == 3 || type == 4) {
        int idx = sv_trigger_find_at(sv, a, b);
        if (idx < 0) return false;
        bool hidden = sv->map.entity_state[idx] != 0;
        return (type == 3) ? !hidden : hidden;
    }
    if (type >= 5 && type <= 8) {
        int t = 0, ct = 0;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || !pl->alive) continue;
            if (pl->team == 1) t++;
            else if (pl->team == 2) ct++;
        }
        int n = (type == 5 || type == 7) ? t : ct;
        return (type == 5 || type == 6) ? (n >= a) : (n <= a);
    }
    if (type == 9 || type == 10) {
        if (sv->set.mp_roundtime <= 0) return type == 9;
        uint32_t limit = (uint32_t)sv->set.mp_roundtime * 60000u;
        uint32_t elapsed = cs2d_sv_now() - sv->round_start_ms;
        int left = (elapsed >= limit) ? 0 : (int)((limit - elapsed) / 1000u);
        return (type == 9) ? (left >= a) : (left <= a);
    }
    if (type == 11 || type == 12) {
        int cnt = 0;
        for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
            if (sv->items[i].active && sv->items[i].tile_x == b && sv->items[i].tile_y == c) cnt++;
        }
        return (type == 11) ? (cnt >= a) : (cnt <= a);
    }
    if (type == 13 || type == 14) {
        bool found = false;
        for (int i = 0; i < CS2D_DYNOBJ_MAX && !found; i++) {
            cs2d_dynobj_t *o = &sv->dynobj.objs[i];
            if (!o->active || o->typ < CS2D_DO_BARRICADE || o->typ > CS2D_DO_SUPER_SUPPLY) continue;
            if ((int)(o->x / 32.0f) == a && (int)(o->y / 32.0f) == b) found = true;
        }
        return (type == 13) ? found : !found;
    }
    return false;
}

static void sv_trigger_entity(cs2d_server_t *sv, int idx, int source, int depth);

static void sv_trigger_names_depth_ex(cs2d_server_t *sv, const char *names, int source, int depth,
                                      bool lua_trigger_hook)
{
    if (!sv || !names || !names[0] || depth > SV_TRIGGER_MAX_DEPTH) return;
    char list[256];
    snprintf(list, sizeof(list), "%s", names);

    char *p = list;
    while (p && *p) {
        char *comma = strchr(p, ',');
        if (comma) *comma = '\0';
        while (*p == ' ' || *p == '\t') p++;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';

        if (*p) {
            /* help.php: the trigger hook runs once per list entry, even when no entity matches,
             * and source is false for map actions, true for the script `trigger` command.
             * Trigger_Delay forwarding also runs it (see cs2d_sv_step_trigger_delays). */
            if (!lua_trigger_hook || cs2d_sv_lua_hook_trigger(sv, p, source) != 1) {
                /* Every entity with that name is affected (duplicate names are used to toggle
                 * groups of sprites). Trigger_Delay forwarding goes through cs2d_sv_step_trigger_delays. */
                for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
                    const cs2d_entity_t *e = &sv->map.entities[i];
                    if (e->name[0] == '\0' || _stricmp(e->name, p) != 0) continue;
                    if (cs2d_sv_lua_hook_triggerentity(sv, e->x, e->y) == 1) continue;
                    sv_trigger_entity(sv, (int)i, source, depth + 1);
                }
            }
        }
        p = comma ? comma + 1 : NULL;
    }
}

static void sv_trigger_names_depth(cs2d_server_t *sv, const char *names, int source, int depth)
{
    sv_trigger_names_depth_ex(sv, names, source, depth, true);
}

void cs2d_sv_trigger_names(cs2d_server_t *sv, const char *names, int source)
{
    sv_trigger_names_depth(sv, names, source, 0);
}

static void sv_trigger_entity(cs2d_server_t *sv, int idx, int source, int depth)
{
    if (!sv || idx < 0 || idx >= (int)sv->map.entity_count || idx >= CS2D_MAX_ENTITIES) return;
    if (depth > SV_TRIGGER_MAX_DEPTH) return;
    cs2d_entity_t *e = &sv->map.entities[idx];
    uint8_t state = sv->map.entity_state[idx];

    switch (e->type) {
    case CS2D_ENT_TRIGGER_IF:
        if (sv_trigger_if_condition(sv, e)) {
            sv_trigger_names_depth(sv, e->trigger, source, depth + 1);
        }
        break;

    case CS2D_ENT_TRIGGER_START:
        /* Only Trigger_Start propagates its list when it is triggered by name. Trigger_Move and
         * Trigger_Hit do that only on the event itself (walkover / hit), never when their name is
         * in another trigger list (verified: Trigger_Start -> d31m2 stops at d31m2). */
        sv_trigger_names_depth(sv, e->trigger, source, depth + 1);
        break;

    case CS2D_ENT_TRIGGER_MOVE:
    case CS2D_ENT_TRIGGER_HIT:
        break;

    case CS2D_ENT_TRIGGER_ONCE:
        if (state == 0) {
            sv_entity_state_set(sv, idx, 1, true);
            sv_trigger_names_depth(sv, e->trigger, source, depth + 1);
        }
        break;

    case CS2D_ENT_TRIGGER_DELAY: {
        /* ints[1] ms takes precedence over ints[0] seconds (same rule as the client). */
        if (state == 0) {
            uint32_t delay = (e->int_settings[1] > 0) ? (uint32_t)e->int_settings[1]
                                                      : (uint32_t)e->int_settings[0] * 1000u;
            sv_entity_state_set(sv, idx, 1, true);
            sv->delay_fire_ms[idx] = cs2d_sv_now() + delay;
        }
        break;
    }

    case CS2D_ENT_ENV_ITEM:
        /* Env_Item toggles and spawns its item (knowledge/entities.md). Its "trigger" field is not
         * propagated: only trigger entities do that (verified with `parse("trigger bank_btn")`). */
        sv_entity_state_set(sv, idx, state ^ 1, true);
        {
            int wep_type = e->int_settings[0];
            if (wep_type > 0 && wep_type < CS2D_MAX_WEAPONS &&
                !sv_has_item_at_tile(sv, e->x, e->y, wep_type)) {
                sv_spawn_item(sv, wep_type, e->x, e->y);
            }
        }
        break;

    case CS2D_ENT_ENV_BUILDING:
    case CS2D_ENT_ENV_NPC:
        sv_entity_state_set(sv, idx, state ^ 1, true);
        sv_map_object_entity_apply(sv, e, sv->map.entity_state[idx]);
        break;

    default:
        /* Every other entity (Func_DynWall, Gen_Weather, Env_Sprite, Env_Light, Trigger_Use, ...)
         * is a plain toggle. It does not fire its own trigger field: the original only does that
         * for Trigger_Start/Move/Hit/If/Once/Delay (and for a Trigger_Use pressed by a player). */
        sv_entity_state_set(sv, idx, state ^ 1, true);
        break;
    }
}

void cs2d_sv_trigger_at_position(cs2d_server_t *sv, int x, int y, int source)
{
    int idx = sv_trigger_find_at(sv, x, y);
    if (idx < 0) return;
    if (cs2d_sv_lua_hook_triggerentity(sv, x, y) == 1) return;
    sv_trigger_entity(sv, idx, source, 0);
}

void cs2d_sv_trigger_start_entities(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
        if (sv->map.entities[i].type != CS2D_ENT_TRIGGER_START) continue;
        if (sv->map.entities[i].trigger[0] == '\0') continue;
        sv_trigger_entity(sv, (int)i, 0, 0);
    }
}

void cs2d_sv_step_trigger_delays(cs2d_server_t *sv)
{
    if (!sv || !sv->map_loaded) return;
    uint32_t now = cs2d_sv_now();
    /* Entity order, not scheduling order (probe on the original: the delays that elapse together
     * forward their lists in entity order). A Delay goes back to state 0 when it fires
     * (knowledge/entities.md) and its names run through the Lua trigger hook with source false. */
    for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
        const cs2d_entity_t *e = &sv->map.entities[i];
        if (e->type != CS2D_ENT_TRIGGER_DELAY || sv->map.entity_state[i] != 1) continue;
        if ((int32_t)(now - sv->delay_fire_ms[i]) < 0) continue;
        sv_entity_state_set(sv, (int)i, 0, true);
        sv_trigger_names_depth_ex(sv, e->trigger, 0, 0, true);
    }
}

/* FUN_1402520b0 / FUN_140123530: start of a new round, respawning players and sending [22] */
void cs2d_sv_round_start(cs2d_server_t *sv, int reason)
{
    sv->round_end = 0;
    sv->round_winner = 0;
    sv->round_mvp = 0;
    sv->round_start_ms = cs2d_sv_now();
    sv->round_number++;
    /* Every entity state goes back to 0 at round start (knowledge/entities.md); the client resets
     * with the [22] round packet and later joiners get the cleared states from snapshot section 4. */
    if (sv->map_loaded) {
        memset(sv->map.entity_state, 0, sizeof(sv->map.entity_state));
        memset(sv->entity_reuse_ms, 0, sizeof(sv->entity_reuse_ms));
        sv->map.entity_state_version++;
        /* setentityaistate is reset to 0 on every round start (help.php). */
        for (uint32_t i = 0; i < sv->map.entity_count; i++) sv->map.entities[i].ai_state = 0;
    }
    /* FUN_1402520b0: Tdo.freeall() and then FUN_140190e10 rebuilds the map's Env_Building and Env_NPC
     * entities. Built buildings, NPCs and Lua images share the one Tdo list, so all of them go; the
     * map entities come back in entity order, which keeps the object ids in step with the client's
     * own teardown/rebuild of the [22] packet. The server stores building health in points, so the
     * rebuilt buildings get their maximum again. */
    cs2d_dynobj_clear(&sv->dynobj);
    if (sv->map_loaded) {
        cs2d_dynobj_spawn_map_objects(&sv->dynobj);
        cs2d_sv_fix_building_health(sv);
    }

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
    memset(sv->projectiles, 0, sizeof(sv->projectiles));
    cs2d_sv_init_hostages(sv);
    cs2d_sv_init_env_items(sv);

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
            pl->score = pl->kills = pl->assists = pl->deaths = pl->mvp = pl->hostages_rescued = 0;
        }
    }

    /* mp_roundlimit / mp_winlimit (help.php): change the map after N rounds or N team wins.
     * The change runs on the next server tick, not inside this round start. */
    if ((sv->set.mp_roundlimit > 0 && sv->round_number > sv->set.mp_roundlimit) ||
        (sv->set.mp_winlimit > 0 && (sv->score_t >= sv->set.mp_winlimit || sv->score_ct >= sv->set.mp_winlimit))) {
        sv->mapchange_pending = true;
    }

    /* Hook startround_prespawn: before actually spawning all players */
    cs2d_sv_lua_hook_startround_prespawn(sv, reason);

    /* Respawn all team players */
    int count = 0;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        pl->hostages_rescued = 0;
        if (pl->used && pl->team >= 1) {
            count++;
            float x, y;
            if (sv_pick_spawn(sv, i, &x, &y)) {
                pl->x = x;
                pl->y = y;
            }
            pl->health = 100;
            pl->armor = 0;
            memset(sv_dmg_from[i], 0, sizeof(sv_dmg_from[i]));
            pl->alive = true;
            pl->move_flag = pl->rot_flag = 0;
            pl->is_reloading = false;
            pl->reload_end_ms = 0;

            char custom_items[128] = {0};
            int sret = cs2d_sv_lua_hook_spawn(sv, i, custom_items, sizeof(custom_items));
            if (sret != 1) {
                const sv_mode_t *m = sv_mode(sv);
                pl->item_count = 0;
                const char *list = (custom_items[0] != '\0') ? custom_items : ((pl->team == 1) ? m->items_t : m->items_ct);
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
            }
        }
    }

    /* mp_tkpunish: teamkillers are killed at the next round start (help.php) */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->tk_punish) continue;
        pl->tk_punish = false;
        if (pl->alive) cs2d_sv_kill(sv, i, 0, 0, 0);
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

    /* Flush [22] roundstart packets before triggering startround hook so clients reset world before receiving new objects */
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (c && sv->players[i].joined) cs2d_sv_flush(sv, c);
    }

    /* Round start fires every Trigger_Start (help.php; the original's round-start trigger run was
     * captured with a probe). This is what initialises the RP map: its Trigger_If entities run
     * Lua (rp.spawnitem, doors, dungeons, ...), so it must happen after the Lua scripts loaded. It runs BEFORE the startround hook:
     * probe on the original (fun_roleplay): timer `light true`, the Trigger_Start cascade, then rp.startround's `light true`. */
    cs2d_sv_trigger_start_entities(sv);

    /* Hook startround: executed AFTER spawning all players and sending [22] */
    cs2d_sv_lua_hook_startround(sv, reason);
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
    if (sv->mapchange_pending) {
        sv->mapchange_pending = false;
        cs2d_server_print("round/win limit reached - changing map...");
        const char *next = cs2d_sv_next_map(sv);
        cs2d_sv_change_map(sv, (next && next[0]) ? next : sv->set.sv_map);
        return;
    }
    if (sv->round_end != 0) {
        if (now - sv->round_end_ms >= (uint32_t)sv->round_end_delay) {
            cs2d_sv_round_start(sv, sv->round_end);
        }
        return;
    }

    /* Map timelimit (0x1401a3058 - 0x1401a30c5): automatic map change when timelimit reached */
    if (sv->set.mp_timelimit > 0) {
        uint32_t limit_ms = (uint32_t)sv->set.mp_timelimit * 60000u;
        uint32_t elapsed = now - sv->map_start_ms;
        if (elapsed >= limit_ms) {
            const sv_mode_t *mode = sv_mode(sv);
            bool respawn_mode = (mode->respawn == 1);
            if (respawn_mode || sv->round_end != 0 || cs2d_sv_count_team(sv, 1) + cs2d_sv_count_team(sv, 2) <= 1) {
                cs2d_server_print("sv_timelimit reached - changing map...");
                const char *next = cs2d_sv_next_map(sv);
                if (next && next[0]) {
                    cs2d_sv_change_map(sv, next);
                } else {
                    cs2d_sv_change_map(sv, sv->set.sv_map);
                }
                return;
            }
        }
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

    /* [7, seed]: the shooter's Tpl.random, 0 read as 1, stored before the shot runs (proxy, see
     * cs2d_scene_game.c fire_shot). Bots keep their own counter (cs2d_sv_shot_at_players). */
    if (seed > 0 || pl->shot_random < 1) pl->shot_random = seed > 0 ? seed : 1;

    cs2d_sv_lua_hook_attack(sv, id);

    /* Construction: the Wrench's primary attack repairs (or upgrades) an own building in reach instead
     * of shooting (Tshoot.Shoot weapon 0x4a). Every other weapon - bullets and melee alike - damages the
     * building the swing reaches. */
    if (pl->weapon == CS2D_WEP_WRENCH) {
        cs2d_sv_wrench_hit(sv, id);
    } else {
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get((uint8_t)pl->weapon);
        if (wdef) {
            cs2d_sv_shot_at_building(sv, id, wdef);
            /* Players: Tshoot.Shoot's server branch (DAT_140ddf400 != 0, docs/dump/cs2d/Tshoot.c:779..863)
             * damages them through FUN_1401ec180 when the [7] fire record is processed. Our client sends no
             * [17] report for its bullets, so without this a human shot never hurt anybody. */
            cs2d_sv_shot_at_players(sv, id, wdef);
        }
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
void cs2d_sv_kill_ex(cs2d_server_t *sv, int victim_id, int killer_id, int assist_id, int weapon, int killer_object)
{
    if (victim_id < 1 || victim_id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *v = &sv->players[victim_id];
    if (!v->used || !v->alive) return;

    /* mp_deathdrop: the victim drops his items before the death packet (help.php) */
    cs2d_sv_death_drop(sv, victim_id);

    v->health = 0;
    v->alive = false;
    v->is_reloading = false;
    v->reload_end_ms = 0;
    v->death_ms = cs2d_sv_now();
    if (weapon != 0xfc) v->deaths++;

    /* help.php: die(victim, killer, weapon, x, y, killerobject) and
     * kill(killer, victim, weapon, x, y, killerobject, assistant). "kill" only runs when a player
     * caused the death (directly or through a building); an NPC / neutral object only gets "die"
     * with its dynamic object id in killerobject. */
    if (killer_id >= 1 && killer_id <= CS2D_SV_MAX_PLAYERS && sv->players[killer_id].used)
        cs2d_sv_lua_hook_kill(sv, killer_id, victim_id, weapon, (int)v->x, (int)v->y, killer_object, assist_id);
    cs2d_sv_lua_hook_die(sv, victim_id, killer_id, weapon, (int)v->x, (int)v->y, killer_object);

    cs2d_sv_player_t *k = (killer_id >= 1 && killer_id <= CS2D_SV_MAX_PLAYERS && sv->players[killer_id].used)
                        ? &sv->players[killer_id] : NULL;

    if (k) {
        bool enemies = (v->team == 1 && k->team >= 2) || (v->team >= 2 && k->team == 1);
        if (sv->set.sv_gamemode == 1 && k != v) enemies = true;
        if (enemies) {
            k->kills++;
            k->score++;
            k->money += 300;
            if (k->money > 16000) k->money = 16000;
            /* Zombies! mode (help.php): zombies get mp_zombiekillscore score/frags and an item */
            if (sv->set.sv_gamemode == 4 && k->team == 1 && v->team >= 2) {
                int zscore = sv->set.mp_zombiekillscore;
                if (zscore > 0) {
                    k->score += zscore - 1;
                    k->kills += zscore - 1;
                }
                if (sv->set.mp_zombiekillequip > 0 && !sv_has_item(k, sv->set.mp_zombiekillequip)) {
                    sv_give(k, sv->set.mp_zombiekillequip);
                }
            }
            if (assist_id >= 1 && assist_id <= CS2D_SV_MAX_PLAYERS && sv->players[assist_id].used) {
                sv->players[assist_id].assists++;
            }
        } else {
            /* Friendly fire or suicide / world */
            unsigned w = (unsigned)weapon - 72u;
            if (k != v && weapon != 0xff && !(w < 16 && ((0x8033u >> w) & 1))) {
                k->score--;
                k->money -= 3300;
                if (k->money < 0) k->money = 0;
                /* mp_teamkillpenalty / mp_tkpunish (help.php) */
                if (v->team == k->team && k->team >= 1) {
                    k->teamkills++;
                    if (sv->set.mp_tkpunish) k->tk_punish = true;
                    if (sv->set.mp_teamkillpenalty > 0 &&
                        k->teamkills >= sv->set.mp_teamkillpenalty) {
                        cs2d_sv_kick(sv, killer_id, "Too many teamkills");
                    }
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

void cs2d_sv_kill(cs2d_server_t *sv, int victim_id, int killer_id, int assist_id, int weapon)
{
    cs2d_sv_kill_ex(sv, victim_id, killer_id, assist_id, weapon, 0);
}

/* FUN_1401658c0 / [17]: apply damage to victim and broadcast [17, victim, attacker, new_hp, new_arm] */
void cs2d_sv_damage(cs2d_server_t *sv, int victim_id, int attacker_id, int dmg, int weapon)
{
    cs2d_sv_damage_ex(sv, victim_id, attacker_id, dmg, weapon, 0);
}

/* Same with the dynamic object that caused the damage (NPC, turret, mine, ...). */
void cs2d_sv_damage_ex(cs2d_server_t *sv, int victim_id, int attacker_id, int dmg, int weapon, int object_id)
{
    if (victim_id < 1 || victim_id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *v = &sv->players[victim_id];
    if (!v->used || !v->alive || dmg <= 0) return;

    if (cs2d_sv_lua_hook_hit(sv, victim_id, attacker_id, weapon, dmg, dmg, dmg, object_id) == 1) return;

    /* mp_wpndmg <name> <damage> overrides the weapon's damage (help.php); mp_damagefactor then
     * multiplies the result (1 = normal, 0 = no damage). */
    int applied = dmg;
    if (weapon >= 0 && weapon < CS2D_MAX_WEAPONS && sv->wpndmg_set[weapon]) applied = sv->wpndmg[weapon];
    if (sv->set.mp_damagefactor != 1.0) {
        double scaled = (double)applied * sv->set.mp_damagefactor;
        applied = (int)(scaled + (scaled >= 0.0 ? 0.5 : -0.5));
    }
    if (applied < 0) applied = 0;
    if (applied == 0) return;

    /* mp_zombiedmg: percentage of damage zombies take (help.php: 0.0 still takes 1 HP per hit) */
    if (sv->set.sv_gamemode == 4 && v->team == 1 && sv->set.mp_zombiedmg != 1.0) {
        double z = sv->set.mp_zombiedmg;
        if (z < 0.0) z = 0.0;
        if (z > 1.0) z = 1.0;
        applied = (int)((double)applied * z);
        if (applied < 1) applied = 1;
    }

    /* Armor: FUN_1401658c0 (cs2d_weapons.c:610-633). Normal armor (< 201) absorbs
     * mp_kevlar * damage (DAT_1405a6508 default 0.35); 201..205 are special armors that do not
     * lose armor points and scale the HP damage instead. */
    int remaining_dmg = applied;
    if (v->armor >= 201) {
        int mode = v->armor - 201;
        double f = 1.0;
        switch (mode) {
        case 0: f = 0.75; break;  /* 201 Light Armor */
        case 1: f = 0.50; break;  /* 202 Armor */
        case 2: f = 0.25; break;  /* 203 Heavy Armor */
        case 3: f = 0.50; break;  /* 204 Medic Armor */
        case 4: f = 0.05; break;  /* 205 Super Armor */
        default: break;
        }
        remaining_dmg = (int)((double)applied * f);
    } else if (v->armor > 0) {
        double kevlar = sv->set.mp_kevlar;
        if (kevlar < 0.0) kevlar = 0.0;
        if (kevlar > 1.0) kevlar = 1.0;
        int absorbed = (int)((double)applied * kevlar);
        int new_armor = v->armor - applied;
        if (new_armor < 0) {
            absorbed = (int)((double)v->armor * kevlar);
            new_armor = 0;
        }
        v->armor = new_armor;
        remaining_dmg = applied - absorbed;
    }

    /* Assist tracker (help.php, hook "assist": a player counts as assistant if he dealt at least
     * 40 damage to the victim; only one player can). The HP actually lost is what we sum. */
    if (attacker_id >= 1 && attacker_id <= CS2D_SV_MAX_PLAYERS && attacker_id != victim_id)
    {
        int sum = sv_dmg_from[victim_id][attacker_id] + (remaining_dmg < v->health ? remaining_dmg : v->health);
        sv_dmg_from[victim_id][attacker_id] = (uint8_t)(sum > 255 ? 255 : sum);
    }

    v->health -= remaining_dmg;
    if (v->health <= 0) {
        v->health = 0;
        /* Broadcast final damage state before or as part of kill */
        uint8_t dmg_msg[5] = { 17, (uint8_t)victim_id, (uint8_t)attacker_id, (uint8_t)v->health, (uint8_t)v->armor };
        sv_broadcast_joined(sv, dmg_msg, sizeof(dmg_msg));
        /* The assistant is the non-killer with the most damage (>= 40); the hook can veto him. */
        int assist_id = 0, best = 39;
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (i == attacker_id || i == victim_id || !sv->players[i].used) continue;
            if (sv_dmg_from[victim_id][i] > best) { best = sv_dmg_from[victim_id][i]; assist_id = i; }
        }
        if (assist_id && attacker_id >= 1 && cs2d_sv_lua_hook_assist(sv, assist_id, victim_id, attacker_id) == 1)
            assist_id = 0;
        memset(sv_dmg_from[victim_id], 0, sizeof(sv_dmg_from[victim_id]));
        cs2d_sv_kill_ex(sv, victim_id, attacker_id, assist_id, weapon, object_id);
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

    int buy_hook = cs2d_sv_lua_hook_buy(sv, id, weapon_id);
    if (buy_hook == 1) return;

    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);

    /* mp_buytime (minutes, floats allowed) and mp_unbuyable (help.php). A refused buy uses the
     * same [23, id, 250, money, 0] reply as an unaffordable item. */
    bool buy_denied = false;
    if (sv->set.mp_buytime > 0.0) {
        uint32_t start = sv->round_clock_ms ? sv->round_clock_ms : sv->round_start_ms;
        uint32_t window = (uint32_t)(sv->set.mp_buytime * 60000.0);
        /* No round clock yet (server just started / unit tests): there is no buy window to miss. */
        if (start != 0 && window > 0 && cs2d_sv_now() - start > window) buy_denied = true;
    }
    if (sv_list_has_id(sv->set.mp_unbuyable, weapon_id)) buy_denied = true;
    /* mp_grenaderebuy 0: a grenade that was thrown/dropped cannot be bought again this life */
    if (!sv->set.mp_grenaderebuy && sv_is_grenade_type(weapon_id) &&
        weapon_id < CS2D_MAX_WEAPONS && pl->grenade_rebuy[weapon_id]) buy_denied = true;

    const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)weapon_id);
    if (buy_denied || !def || pl->money < def->price) {
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
    if (sv_is_grenade_type(weapon_id) && weapon_id < CS2D_MAX_WEAPONS) {
        pl->grenade_rebuy[weapon_id] = 0;
    }

    /* Add item / special / armor to player */
    if (weapon_id == CS2D_WEP_PRIMARYAMMO) {
        for (int k = 0; k < pl->item_count; k++) {
            uint8_t t = pl->items[k];
            if (cs2d_weapon_hud_slot(t) == 1) {
                const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                if (wd) pl->reserve[t] = (int16_t)wd->max_reserve;
            }
        }
    } else if (weapon_id == CS2D_WEP_SECONDARYAMMO) {
        for (int k = 0; k < pl->item_count; k++) {
            uint8_t t = pl->items[k];
            if (cs2d_weapon_hud_slot(t) == 2) {
                const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                if (wd) pl->reserve[t] = (int16_t)wd->max_reserve;
            }
        }
    } else if (def->slot != CS2D_SLOT_ITEM) {
        int hslot = cs2d_weapon_hud_slot((uint8_t)weapon_id);
        bool is_stack = (hslot == 4 || weapon_id == CS2D_WEP_MINE || weapon_id == CS2D_WEP_LASERMINE || weapon_id == CS2D_WEP_SATCHEL);
        if (is_stack) {
            int max_cap = def->clip_size > 0 ? def->clip_size : 1;
            if (sv_has_item(pl, weapon_id)) {
                if (pl->clip[weapon_id] < max_cap) pl->clip[weapon_id]++;
            } else {
                sv_give(pl, weapon_id);
                pl->clip[weapon_id] = 1;
                pl->reserve[weapon_id] = 0;
            }
        } else {
            sv_give(pl, weapon_id);
            pl->weapon = weapon_id;
        }
    } else if (weapon_id == CS2D_WEP_DEFUSEKIT && pl->team == 2) {
        pl->special |= 1;
    } else if (weapon_id == CS2D_WEP_KEVLAR) {
        pl->armor = 65;
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

    /* mp_dropgrenades 0 forbids dropping grenades (help.php) */
    if (!sv->set.mp_dropgrenades && sv_is_grenade_type(type)) return;

    /* help.php drop(id, iid, type, ammoin, ammo, mode, xpos, ypos), measured on the original
     * (tools/agent/lua/drop_probe.lua): a Laser drop gave iid=1 type=45 ammoin=15 ammo=45
     * mode=0 x=74 y=40. The id is reserved before the hook (the original creates the item object
     * first); xpos/ypos are the player's tile. Passing only five arguments made rp.lua call
     * tile(nil,nil) at line 12624. */
    int tx = (int)(pl->x * 0.03125f);
    int ty = (int)(pl->y * 0.03125f);
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    uint16_t item_id = sv_alloc_item_id(sv);
    if (cs2d_sv_lua_hook_drop(sv, id, item_id, type, clip, reserve, sub, tx, ty) == 1) return;

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
    if (sv_is_grenade_type(type) && type < CS2D_MAX_WEAPONS) pl->grenade_rebuy[type] = 1;

    /* Clear special byte bit 1 if bomb or defuse kit */
    if (type == CS2D_WEP_BOMB || type == CS2D_WEP_DEFUSEKIT) {
        pl->special &= ~1;
    }

    /* If held weapon was dropped, switch to next best weapon */
    if (pl->weapon == type) {
        sv_best_weapon(sv, pl);
    }

    sv_place_dropped_item(sv, pl, item_id, type, clip, reserve, sub);
}

/* [24] broadcast shared by the drop key and mp_deathdrop; the caller reserves `item_id` first
 * (cs2d_sv_drop needs it for the Lua drop hook, mp_deathdrop for the wire record). */
static void sv_place_dropped_item(cs2d_server_t *sv, cs2d_sv_player_t *pl, uint16_t item_id, int type,
                                  int clip, int reserve, int sub)
{
    int id = (int)(pl - sv->players);
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        if (!sv->items[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

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

/* mp_deathdrop (help.php): what a player drops when he dies or leaves the game. */
void cs2d_sv_death_drop(cs2d_server_t *sv, int id)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    int mode = sv->set.mp_deathdrop;
    if (mode >= 4) return;

    for (int i = pl->item_count - 1; i >= 0; i--) {
        int type = pl->items[i];
        int slot = cs2d_weapon_hud_slot((uint8_t)type);
        bool take = (mode == 0) || (slot == 1 || slot == 2) ||
                    (mode <= 2 && type == CS2D_WEP_DEFUSEKIT) ||
                    (mode == 1 && type == CS2D_WEP_NIGHTVISION);
        if (!take) continue;
        if (type == CS2D_WEP_BOMB) continue;   /* the C4 is the round objective, not loot */
        int clip = pl->clip[type], reserve = pl->reserve[type];
        for (int k = i; k < pl->item_count - 1; k++) pl->items[k] = pl->items[k + 1];
        pl->item_count--;
        pl->clip[type] = 0;
        pl->reserve[type] = 0;
        if (type == CS2D_WEP_DEFUSEKIT) pl->special &= ~1;
        sv_place_dropped_item(sv, pl, sv_alloc_item_id(sv), type, clip, reserve, 0);
    }
}

/* Ground item pickups [25] and 15s drop lifetime fadeout [40] (FUN_14004cd20) */
void cs2d_sv_step_items(cs2d_server_t *sv)
{
    uint32_t now = cs2d_sv_now();

    /* 1. Item fadeout [40]: mp_weaponfadeout seconds (0 = never; default 15, 0x140a14d94) */
    int fade_secs = sv->set.mp_weaponfadeout;
    for (int i = 0; i < CS2D_SV_MAX_GROUND_ITEMS; i++) {
        cs2d_sv_item_t *it = &sv->items[i];
        if (!it->active || !it->dropped || fade_secs <= 0) continue;
        if (now - it->drop_time_ms >= (uint32_t)fade_secs * 1000u) {
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
            bool is_consumable = false;
            int hud_slot = cs2d_weapon_hud_slot(it->type);
            bool is_stackable = (hud_slot == 4 || it->type == CS2D_WEP_MINE || it->type == CS2D_WEP_LASERMINE || it->type == CS2D_WEP_SATCHEL);

            if (it->type == CS2D_WEP_PRIMARYAMMO) {
                is_consumable = true;
                for (int k = 0; k < pl->item_count; k++) {
                    uint8_t t = pl->items[k];
                    if (cs2d_weapon_hud_slot(t) == 1) {
                        const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                        if (wd && pl->reserve[t] < wd->max_reserve) { can_pickup = true; break; }
                    }
                }
            } else if (it->type == CS2D_WEP_SECONDARYAMMO) {
                is_consumable = true;
                for (int k = 0; k < pl->item_count; k++) {
                    uint8_t t = pl->items[k];
                    if (cs2d_weapon_hud_slot(t) == 2) {
                        const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                        if (wd && pl->reserve[t] < wd->max_reserve) { can_pickup = true; break; }
                    }
                }
            } else if (it->type == CS2D_WEP_MEDIKIT || it->type == CS2D_WEP_BANDAGE) {
                is_consumable = true;
                if (pl->health < pl->maxhealth) can_pickup = true;
            } else if (it->type == CS2D_WEP_COINS || it->type == CS2D_WEP_MONEY || it->type == CS2D_WEP_GOLD) {
                is_consumable = true;
                if (pl->money < 16000) can_pickup = true;
            } else if (it->type == CS2D_WEP_KEVLAR) {
                if (pl->armor <= 64) can_pickup = true;
            } else if (it->type == CS2D_WEP_KEVLARHELM) {
                if (pl->armor < 100) can_pickup = true;
            } else if (it->type >= 79 && it->type <= 84) {
                can_pickup = true;
            } else if (it->type == CS2D_WEP_DEFUSEKIT) {
                if (pl->team == 2 && !(pl->special & 1)) can_pickup = true;
            } else if (it->type == CS2D_WEP_NIGHTVISION) {
                if (!(pl->special & 2)) can_pickup = true;
            } else if (it->type == CS2D_WEP_GASMASK) {
                if (!(pl->special & 16)) can_pickup = true;
            } else if (it->type == CS2D_WEP_BOMB) {
                if (pl->team == 1 && !(pl->special & 1) && !sv_has_item(pl, CS2D_WEP_BOMB)) {
                    can_pickup = true;
                }
            } else if (hud_slot == 1) {
                bool has_primary = false;
                for (int k = 0; k < pl->item_count; k++) {
                    if (cs2d_weapon_hud_slot(pl->items[k]) == 1) { has_primary = true; break; }
                }
                if (!has_primary) can_pickup = true;
            } else if (hud_slot == 2) {
                bool has_secondary = false;
                for (int k = 0; k < pl->item_count; k++) {
                    if (cs2d_weapon_hud_slot(pl->items[k]) == 2) { has_secondary = true; break; }
                }
                if (!has_secondary) can_pickup = true;
            } else if (is_stackable) {
                int max_cap = def->clip_size > 0 ? def->clip_size : 1;
                if (!sv_has_item(pl, it->type)) can_pickup = true;
                else if (pl->clip[it->type] < max_cap) can_pickup = true;
            } else {
                if (!sv_has_item(pl, it->type)) can_pickup = true;
            }

            if (can_pickup) {
                /* help.php walkover(id, iid, type, ain, a, mode): returning 1 prevents the pickup.
                 * collect(id, iid, type, ain, a, mode) has the same arguments but cannot cancel it
                 * (its return value is ignored; the old call swapped ammoin/ammo and dropped mode). */
                if (cs2d_sv_lua_hook_walkover(sv, pid, it->id, it->type, it->clip, it->reserve, it->sub) == 1) continue;
                cs2d_sv_lua_hook_collect(sv, pid, it->id, it->type, it->clip, it->reserve, it->sub);

                if (is_consumable) {
                    if (it->type == CS2D_WEP_PRIMARYAMMO) {
                        for (int k = 0; k < pl->item_count; k++) {
                            uint8_t t = pl->items[k];
                            if (cs2d_weapon_hud_slot(t) == 1) {
                                const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                                if (wd) pl->reserve[t] = (int16_t)wd->max_reserve;
                            }
                        }
                    } else if (it->type == CS2D_WEP_SECONDARYAMMO) {
                        for (int k = 0; k < pl->item_count; k++) {
                            uint8_t t = pl->items[k];
                            if (cs2d_weapon_hud_slot(t) == 2) {
                                const cs2d_weapon_def_t *wd = cs2d_weapon_get(t);
                                if (wd) pl->reserve[t] = (int16_t)wd->max_reserve;
                            }
                        }
                    } else if (it->type == CS2D_WEP_MEDIKIT) {
                        pl->health = pl->maxhealth;
                        /* [42] field 0 = health (field 1 is score, net-messages.md) */
                        uint8_t hmsg[5] = { 42, (uint8_t)pid, 0, (uint8_t)(pl->health & 0xff), (uint8_t)((pl->health >> 8) & 0xff) };
                        sv_broadcast_joined(sv, hmsg, sizeof(hmsg));
                    } else if (it->type == CS2D_WEP_BANDAGE) {
                        pl->health += 20;
                        if (pl->health > pl->maxhealth) pl->health = pl->maxhealth;
                        uint8_t hmsg[5] = { 42, (uint8_t)pid, 0, (uint8_t)(pl->health & 0xff), (uint8_t)((pl->health >> 8) & 0xff) };
                        sv_broadcast_joined(sv, hmsg, sizeof(hmsg));
                    } else if (it->type == CS2D_WEP_COINS || it->type == CS2D_WEP_MONEY || it->type == CS2D_WEP_GOLD) {
                        int add_m = (it->type == CS2D_WEP_COINS) ? 100 : (it->type == CS2D_WEP_MONEY) ? 500 : 1000;
                        pl->money += add_m;
                        if (pl->money > 16000) pl->money = 16000;
                        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, pid);
                        if (c) {
                            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                            cs2d_pkt_w_byte(p, 42);
                            cs2d_pkt_w_byte(p, (uint8_t)pid);
                            cs2d_pkt_w_byte(p, 2);
                            cs2d_pkt_w_short(p, (uint16_t)pl->money);
                        }
                    }

                    uint16_t taken_id = it->id;
                    uint8_t type = it->type;
                    it->active = false;

                    /* Broadcast [25, player, item_id u16, type u8, 0, 0, 0] */
                    uint8_t msg[10] = { 25, (uint8_t)pid, (uint8_t)(taken_id & 0xff), (uint8_t)((taken_id >> 8) & 0xff), type, 0, 0, 0, 0, 0 };
                    sv_broadcast_joined(sv, msg, sizeof(msg));
                    break;
                } else if (it->type == CS2D_WEP_KEVLAR || it->type == CS2D_WEP_KEVLARHELM || (it->type >= 79 && it->type <= 84)) {
                    if (it->type == CS2D_WEP_KEVLAR) pl->armor = 65;
                    else if (it->type == CS2D_WEP_KEVLARHELM) pl->armor = 100;
                    else pl->armor = 201 + (it->type - 79);

                    uint16_t taken_id = it->id;
                    uint8_t type = it->type;
                    it->active = false;

                    uint8_t amsg[5] = { 42, (uint8_t)pid, 3, (uint8_t)(pl->armor & 0xff), (uint8_t)((pl->armor >> 8) & 0xff) };
                    sv_broadcast_joined(sv, amsg, sizeof(amsg));

                    uint8_t msg[10] = { 25, (uint8_t)pid, (uint8_t)(taken_id & 0xff), (uint8_t)((taken_id >> 8) & 0xff), type, 0, 0, 0, 0, 0 };
                    sv_broadcast_joined(sv, msg, sizeof(msg));
                    break;
                } else if (it->type == CS2D_WEP_DEFUSEKIT || it->type == CS2D_WEP_NIGHTVISION || it->type == CS2D_WEP_GASMASK) {
                    if (it->type == CS2D_WEP_DEFUSEKIT) pl->special |= 1;
                    else if (it->type == CS2D_WEP_NIGHTVISION) pl->special |= 2;
                    else if (it->type == CS2D_WEP_GASMASK) pl->special |= 16;

                    uint16_t taken_id = it->id;
                    uint8_t type = it->type;
                    it->active = false;

                    uint8_t msg[10] = { 25, (uint8_t)pid, (uint8_t)(taken_id & 0xff), (uint8_t)((taken_id >> 8) & 0xff), type, 0, 0, 0, 0, 0 };
                    sv_broadcast_joined(sv, msg, sizeof(msg));
                    break;
                } else if (is_stackable) {
                    int max_cap = def->clip_size > 0 ? def->clip_size : 1;
                    int ground_count = it->clip > 0 ? (int)it->clip : 1;
                    int have = sv_has_item(pl, it->type) ? (int)pl->clip[it->type] : 0;
                    int space = max_cap - have;
                    if (space > 0) {
                        int take = ground_count <= space ? ground_count : space;
                        if (!sv_has_item(pl, it->type)) {
                            sv_give(pl, it->type);
                        }
                        pl->clip[it->type] = (int16_t)(have + take);
                        pl->reserve[it->type] = 0;

                        uint16_t taken_id = it->id;
                        uint8_t type = it->type;
                        uint16_t orig_ground_clip = (uint16_t)ground_count;

                        if (it->clip <= (uint16_t)take) {
                            it->clip = 0;
                            it->active = false;
                        } else {
                            it->clip -= (uint16_t)take;
                        }

                        uint8_t msg[10];
                        msg[0] = 25;
                        msg[1] = (uint8_t)pid;
                        msg[2] = (uint8_t)(taken_id & 0xff);
                        msg[3] = (uint8_t)((taken_id >> 8) & 0xff);
                        msg[4] = type;
                        msg[5] = (uint8_t)(orig_ground_clip & 0xff);
                        msg[6] = (uint8_t)((orig_ground_clip >> 8) & 0xff);
                        msg[7] = 0;
                        msg[8] = 0;
                        msg[9] = 0;
                        sv_broadcast_joined(sv, msg, sizeof(msg));
                        break;
                    }
                } else {
                    sv_give(pl, it->type);
                    pl->clip[it->type] = (int16_t)it->clip;
                    pl->reserve[it->type] = (int16_t)it->reserve;
                    if (def->slot == CS2D_SLOT_PRIMARY || def->slot == CS2D_SLOT_SECONDARY) {
                        pl->weapon = it->type;
                    }
                    if (it->type == CS2D_WEP_BOMB) {
                        pl->special |= 1;
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

    /* 3. Env_Item respawn maintenance (modes 1: instant, 2: every X sec) */
    if (sv->map_loaded) {
        for (uint32_t i = 0; i < sv->map.entity_count; i++) {
            const cs2d_entity_t *e = &sv->map.entities[i];
            if (e->type != CS2D_ENT_ENV_ITEM) continue;
            int wep_type = e->int_settings[0];
            int spawn_mode = e->int_settings[1];
            if (wep_type <= 0 || wep_type >= CS2D_MAX_WEAPONS) continue;

            if (spawn_mode == 1) {
                /* Instant / Infinity: respawn immediately if tile is clear */
                if (!sv_has_item_at_tile(sv, e->x, e->y, wep_type)) {
                    sv_spawn_item(sv, wep_type, e->x, e->y);
                }
            } else if (spawn_mode == 2) {
                /* Every X seconds: respawn after delay_sec if tile is clear */
                int delay_sec = e->int_settings[2];
                if (delay_sec <= 0) delay_sec = 10;
                if (sv_has_item_at_tile(sv, e->x, e->y, wep_type)) {
                    sv->env_item_timer[i] = now;
                } else {
                    if (sv->env_item_timer[i] == 0) {
                        sv->env_item_timer[i] = now;
                    } else if (now - sv->env_item_timer[i] >= (uint32_t)(delay_sec * 1000)) {
                        sv_spawn_item(sv, wep_type, e->x, e->y);
                        sv->env_item_timer[i] = now;
                    }
                }
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
            } else {
                /* Re-arm the next reload cycle timer so step_reloads does NOT immediately
                 * fire mode 4 in the next frame, which would double-load shells! */
                uint32_t duration_ms = (uint32_t)(def->reload_ticks * 5);
                pl->reload_end_ms = now + duration_ms;
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

/* The sayutf8 / sayteamutf8 Lua hooks receive the chat text with every UTF-8 byte hex encoded
 * as a lowercase 'x' plus two uppercase hex digits: "hello" -> "x68x65x6Cx6Cx6F" (Changelog.txt
 * 1.0.1.8; probe on cs2d_dedicated.exe 2026-10-21: say and sayutf8 both run, say first, and a
 * return 1 from either suppresses the message; "ol\u00e1" arrives as x6Fx6CxC3xA1...). */
static void sv_chat_hex_utf8(const char *utf8, char *out, size_t max_out)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
        if (o + 3 >= max_out) break;
        out[o++] = 'x';
        out[o++] = hex[*p >> 4];
        out[o++] = hex[*p & 0x0f];
    }
    out[o] = '\0';
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
        /* Surrogate pair (a 4-byte UTF-8 character on the client) */
        if (ch >= 0xd800 && ch <= 0xdbff && i + 1 < char_len) {
            uint16_t lo = chars[i + 1];
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                uint32_t cp = 0x10000u + ((uint32_t)(ch - 0xd800) << 10) + (uint32_t)(lo - 0xdc00);
                utf8_text[out++] = (char)(0xf0 | (cp >> 18));
                utf8_text[out++] = (char)(0x80 | ((cp >> 12) & 0x3f));
                utf8_text[out++] = (char)(0x80 | ((cp >> 6) & 0x3f));
                utf8_text[out++] = (char)(0x80 | (cp & 0x3f));
                i++;
                continue;
            }
        }
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

    if (mode == 1 || mode == 2) {
        /* say/sayteam run first; when they return 0 the UTF-8 variant runs with the hex text. */
        char hex_text[4112];
        sv_chat_hex_utf8(utf8_text, hex_text, sizeof(hex_text));
        if (mode == 1) {
            if (cs2d_sv_lua_hook_say(sv, id, utf8_text) == 1) return;
            if (cs2d_sv_lua_hook_sayutf8(sv, id, hex_text) == 1) return;
        } else {
            if (cs2d_sv_lua_hook_sayteam(sv, id, utf8_text) == 1) return;
            if (cs2d_sv_lua_hook_sayteamutf8(sv, id, hex_text) == 1) return;
        }
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

    /* Bot AI hears every chat/radio message (Standard AI.lua ai_hear_chat / ai_hear_radio).
     * Mode 3 is the radio message: its text is the radio id as a decimal string. */
    if (mode == 3) {
        cs2d_sv_ai_hear_radio(sv, id, atoi(utf8_text));
    } else if (mode == 1 || mode == 2) {
        cs2d_sv_ai_hear_chat(sv, id, utf8_text, mode == 2);
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
    /* Normal CS2D knife run speed is ~260 px/s; allow burst up to ~650 px/s or 160 px single jump
     * (limit calibrated, not read from the original). */
    if (dist > 160.0f && speed > 650.0f) {
        /* The original never punishes a single packet: an over-limit move adds 1 + excess to Tpl +0x178
         * and 1 to +0x17c, a valid move subtracts 1 from each (floor 0), and only +0x178 > 30 AND
         * +0x17c > 50 triggers "Speedhack detected (speed)" (FUN_1401ca500 0x1401cb13a-0x1401cb228).
         * So a stale packet that crosses a teleport / respawn [15] / [21] in flight only resets the client. */
        pl->speed_score += 1 + (int)(dist - 160.0f);
        pl->speed_events++;
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 10);
            cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_x);
            cs2d_pkt_w_short(p, (uint16_t)(int)pl->last_valid_y);
        }
        if (pl->speed_score <= 30 || pl->speed_events <= 50) return false;
        cs2d_server_print("MP_ANTISPEEDER: Speedhack detected (speed)! %s", pl->name);
        pl->speed_score = pl->speed_events = 0;
        /* mp_antispeeder punishment (help.php: 0 off, 1 kick+tempban, 2 kick, 3 kill, 4/5/6 slap 10/25/50).
         * The position reset above stays the basic protection; 1 is treated as a kick (no tempban list yet). */
        switch (sv->set.mp_antispeeder) {
        case 1: cs2d_sv_tempban_ip(sv, pl->ip, sv->set.mp_tempbantime, "Speedhack");
                cs2d_sv_kick(sv, id, "Speedhack"); break;
        case 2: cs2d_sv_kick(sv, id, "Speedhack"); break;
        case 3: if (pl->alive) cs2d_sv_kill(sv, id, 0, 0, 0); break;
        case 4: cs2d_sv_damage(sv, id, 0, 10, 255); break;
        case 5: cs2d_sv_damage(sv, id, 0, 25, 255); break;
        case 6: cs2d_sv_damage(sv, id, 0, 50, 255); break;
        default: break;
        }
        return false;
    }

    /* Move is valid: the strike counters decay by 1 (0x1401cb153 / 0x1401cb17a) */
    if (pl->speed_score > 0)  pl->speed_score--;
    if (pl->speed_events > 0) pl->speed_events--;

    /* commit */
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
    cs2d_sv_lua_hook_move(sv, id, (int)new_x, (int)new_y, op == 11 ? 1 : 0);

    if (new_tx != old_tx || new_ty != old_ty) {
        cs2d_sv_lua_hook_movetile(sv, id, new_tx, new_ty);
        int map_teleport = -1;
        /* Walkover entities on the tile the player entered (Tpl.tilepos, the client-side
         * cs2d_ents_enter_tile does the same). Only entities in state 0 are active. */
        if (sv->map_loaded) {
            for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
                const cs2d_entity_t *te = &sv->map.entities[i];
                if (te->x != new_tx || te->y != new_ty || sv->map.entity_state[i] != 0) continue;
                if (te->type == CS2D_ENT_TRIGGER_MOVE) {
                    /* Trigger_Move (91): ints[0] filters by team (0 everyone, 1 T, 2 CT).
                     * Each affected entity gets the Lua triggerentity hook. */
                    int filter = te->int_settings[0];
                    if (filter != 0 && filter != pl->team) continue;
                    if (cs2d_sv_lua_hook_triggerentity(sv, te->x, te->y) == 1) continue;
                    cs2d_sv_trigger_names(sv, te->trigger, 0);
                } else if (te->type == CS2D_ENT_FUNC_TELEPORT && map_teleport < 0) {
                    map_teleport = (int)i;
                }
            }
        }

        /* Func_Teleport (70): walkover teleports to the centre of the target tile
         * (ints[0], ints[1]) read from the map's entity properties. The original does
         * this in Tpl.tilepos and only tells the clients through [15]. */
        if (map_teleport >= 0) {
            const cs2d_entity_t *te = &sv->map.entities[map_teleport];
            cs2d_sv_set_pos(sv, id,
                            (float)(te->int_settings[0] * CS2D_TILE_SIZE + CS2D_TILE_SIZE / 2),
                            (float)(te->int_settings[1] * CS2D_TILE_SIZE + CS2D_TILE_SIZE / 2));
            return true;
        }
    }

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
    /* Lua msg/msg2 (and WQ2D's utf8.convert) may pass "UTF-8:\xNN..." instead of
     * raw UTF-8; decode the escape form before the console echo and the wire. */
    char decoded[2048];
    cs2d_net_utf8_unescape(text, decoded, (int)sizeof(decoded));
    text = decoded;

    /* FUN_140153900 (0x1401546a0): a message ending in "@C" is a center message.
     * The dedicated strips those two characters and sends [240, 0, 2, ...]; the
     * client's mode-2 path calls Thud_msg.add_center (FUN_1401de400 case 2). */
    int mode = 1;
    size_t text_len = strlen(text);
    if (text_len >= 2 && text[text_len - 2] == '@' && text[text_len - 1] == 'C') {
        decoded[text_len - 2] = '\0';
        mode = 2;
    }

    if (sv) cs2d_sv_set_active(sv);
    if (target_id == 0) {
        cs2d_server_print("%s", text);
    } else {
        cs2d_server_print("(MSG to #%d) %s", target_id, text);
    }

    /* Lua and config strings are UTF-8; the wire field is UTF-16LE (the byte-wise
     * expansion here used to turn "não" into "nÃ£o"). */
    uint16_t units[256];
    int len = cs2d_net_utf8_to_utf16(text, units, 255);

    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (target_id > 0 && i != target_id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 240);
        cs2d_pkt_w_byte(p, 0);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
        cs2d_pkt_w_short(p, (uint16_t)len);
        for (int j = 0; j < len; j++) {
            cs2d_pkt_w_short(p, units[j]);
        }
    }
}

/* [48] FX (FUN_14014cf50): [48, type u8, x+0x8000 u16, y+0x8000 u16, p1 u16, p2 u16,
 * (type >= 100) r,g,b u8, (type >= 200) len u8 + path]. */
void cs2d_sv_send_fx(cs2d_server_t *sv, int type, int x, int y, int p1, int p2, int r, int g, int b)
{
    if (!sv) return;
    cs2d_pkt_t *p = NULL;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 48);
        cs2d_pkt_w_byte(p, (uint8_t)type);
        cs2d_pkt_w_short(p, (uint16_t)(x + 0x8000));
        cs2d_pkt_w_short(p, (uint16_t)(y + 0x8000));
        cs2d_pkt_w_short(p, (uint16_t)p1);
        cs2d_pkt_w_short(p, (uint16_t)p2);
        if (type >= 100) {
            cs2d_pkt_w_byte(p, (uint8_t)r);
            cs2d_pkt_w_byte(p, (uint8_t)g);
            cs2d_pkt_w_byte(p, (uint8_t)b);
        }
        /* types < 200 carry no path (the effect command never sends one) */
    }
}

/* flashplayer <player> <intensity> (FUN_14011daf0, cs2d_dedicated.exe): intensity clamped 0..100, then a reliable
 * [48, 5, player+0x8000, 0x8000, intensity, 0] to every joined client; each client tests the player id itself
 * (0 = everyone). */
void cs2d_sv_flash_player(cs2d_server_t *sv, int player, int intensity)
{
    if (!sv) return;
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    cs2d_sv_send_fx(sv, 5, player, 0, intensity, 0, 0, 0, 0);
}

/* shake <player> <power> (FUN_14011d2c0): only for an existing player, an unreliable
 * [48, 2, 0x8000, 0x8000, player, power] to that player alone; the power is not clamped on the server (the client
 * caps it at 1000). */
void cs2d_sv_shake(cs2d_server_t *sv, int player, int power)
{
    if (!sv || player < 1 || player > CS2D_SV_MAX_PLAYERS || !sv->players[player].used) return;
    cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, player);
    if (!c || !sv->players[player].joined) return;
    cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
    cs2d_pkt_w_byte(p, 48);
    cs2d_pkt_w_byte(p, 2);
    cs2d_pkt_w_short(p, 0x8000);
    cs2d_pkt_w_short(p, 0x8000);
    cs2d_pkt_w_short(p, (uint16_t)player);
    cs2d_pkt_w_short(p, (uint16_t)power);
}

/* explosion <x> <y> <size> <damage> [player]: [48] type 1 for the visuals and radial damage
 * (linear falloff, at least 1 HP; damage/falloff estimated from help.php). */
void cs2d_sv_explosion(cs2d_server_t *sv, int x, int y, int size, int damage, int source)
{
    if (!sv || size <= 0) return;
    cs2d_sv_send_fx(sv, 1, x, y, size, 0, 0, 0, 0);
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *pl = &sv->players[i];
        if (!pl->used || !pl->alive) continue;
        float dx = pl->x - (float)x, dy = pl->y - (float)y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > (float)size) continue;
        int dmg = (int)((float)damage * (1.0f - dist / (float)size));
        if (dmg < 1) dmg = 1;
        cs2d_sv_damage(sv, i, source, dmg, 0xff);
    }
    /* buildings on the explosion tiles */
    for (int ty = y / CS2D_TILE_SIZE - size / CS2D_TILE_SIZE; ty <= y / CS2D_TILE_SIZE + size / CS2D_TILE_SIZE; ty++) {
        for (int tx = x / CS2D_TILE_SIZE - size / CS2D_TILE_SIZE; tx <= x / CS2D_TILE_SIZE + size / CS2D_TILE_SIZE; tx++) {
            cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&sv->dynobj, tx, ty);
            if (o) cs2d_sv_object_damage(sv, o->id, damage, source);
        }
    }
}

/* sv_sound / sv_stopsound: [240, 0, mode u8, len u16, UTF-16LE path] (mode 10 play, 11 stop).
 * help.php; the original dedicated sends it through FUN_140153900 (parser call at 0x140144c28). */
void cs2d_sv_server_sound(cs2d_server_t *sv, int mode, const char *path, int target)
{
    if (!sv || !path) return;
    char decoded[1024];
    cs2d_net_utf8_unescape(path, decoded, (int)sizeof(decoded));
    path = decoded;
    uint16_t units[256];
    int len = cs2d_net_utf8_to_utf16(path, units, 255);
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (target > 0 && i != target) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 240);
        cs2d_pkt_w_byte(p, 0);
        cs2d_pkt_w_byte(p, (uint8_t)mode);
        cs2d_pkt_w_short(p, (uint16_t)len);
        for (int j = 0; j < len; j++) cs2d_pkt_w_short(p, units[j]);
    }
}

/* sv_sound2 / sv_soundpos: [65, x + 0x8000 u16, y + 0x8000 u16, len u8, path]
 * (FUN_140107560, called from the parser at 0x140144d28 / 0x140144e58). */
void cs2d_sv_positional_sound(cs2d_server_t *sv, int x, int y, const char *path, int target)
{
    if (!sv || !path) return;
    int len = (int)strlen(path);
    if (len > 255) len = 255;
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        if (target > 0 && i != target) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (!c || !sv->players[i].joined) continue;
        cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
        cs2d_pkt_w_byte(p, 65);
        cs2d_pkt_w_short(p, (uint16_t)(x + 0x8000));
        cs2d_pkt_w_short(p, (uint16_t)(y + 0x8000));
        cs2d_pkt_w_byte(p, (uint8_t)len);
        for (int j = 0; j < len; j++) cs2d_pkt_w_byte(p, (uint8_t)path[j]);
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
    uint16_t item_id = sv_alloc_item_id(sv);
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
        /* FUN_140138a40 mode 100 (client FUN_14014f820): the button must be a Trigger_Use (93) in
         * state 0, pass its "Triggered by" filter (ints[2]) and sit within 45 px of the player.
         * The original does NOT toggle the button itself: it calls Te.triggerentities(1), which
         * runs the names in the button's trigger field (the Lua usebutton hook fires first). The
         * re-use delay is Te.ints[3] ms, kept in Te.timer and compared against the current tick;
         * pressing does not change the state, so a button stays usable forever (a state 1 button
         * is one a trigger disabled). Toggling it here used to lock every button after one use. */
        float tx_px = (float)x * 32.0f + 16.0f;
        float ty_px = (float)y * 32.0f + 16.0f;
        float dx = pl->x - tx_px, dy = pl->y - ty_px;
        if (dx * dx + dy * dy > 45.0f * 45.0f || !sv->map_loaded) return;

        for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
            const cs2d_entity_t *e = &sv->map.entities[i];
            if (e->x != x || e->y != y || e->type != CS2D_ENT_TRIGGER_USE) continue;
            int team_filter = e->int_settings[2];
            if (team_filter != 0 && team_filter != pl->team) return;
            if (sv->map.entity_state[i] != 0) return;

            uint32_t now = cs2d_sv_now();
            uint32_t delay_ms = (e->int_settings[3] > 0) ? (uint32_t)e->int_settings[3] : 0u;
            if (delay_ms > 0 && sv->entity_reuse_ms[i] != 0 && now < sv->entity_reuse_ms[i]) return;
            sv->entity_reuse_ms[i] = delay_ms > 0 ? now + delay_ms : 0u;

            cs2d_sv_lua_hook_usebutton(sv, id, (int)x, (int)y);
            if (e->trigger[0] != '\0') cs2d_sv_trigger_names(sv, e->trigger, 0);

            uint8_t pkt[11];
            pkt[0] = 26;
            pkt[1] = (uint8_t)id;
            pkt[2] = 100;
            memcpy(&pkt[3], &data, 4);
            memcpy(&pkt[7], &x, 2);
            memcpy(&pkt[9], &y, 2);
            sv_broadcast_joined(sv, pkt, 11);
            return;
        }
    } else if (mode == 150) {
        /* Supply selection (CS2D.exe FUN_14028dba0, dedicated FUN_140138b32) */
        cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&sv->dynobj, (int)x, (int)y);
        if (o && (o->typ == CS2D_DO_SUPPLY || o->typ == CS2D_DO_SUPER_SUPPLY)) {
            int max_slot = (o->typ == CS2D_DO_SUPPLY) ? 6 : 9;
            if (data >= 1 && data <= max_slot && o->player == id) {
                float tx_px = (float)x * 32.0f + 16.0f;
                float ty_px = (float)y * 32.0f + 16.0f;
                float dx = pl->x - tx_px, dy = pl->y - ty_px;
                if (dx * dx + dy * dy <= 60.0f * 60.0f) {
                    o->mode = (int)data;
                    uint8_t pkt[11];
                    pkt[0] = 26;
                    pkt[1] = (uint8_t)id;
                    pkt[2] = 150;
                    memcpy(&pkt[3], &data, 4);
                    memcpy(&pkt[7], &x, 2);
                    memcpy(&pkt[9], &y, 2);
                    sv_broadcast_joined(sv, pkt, 11);
                }
            }
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
    if (!sv->set.mp_vulnerablehostages) return;   /* help.php: 0 = invulnerable hostages */

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
            /* mp_hostagepenalty: kick after X hostage kills (help.php) */
            atk->hostage_kills++;
            if (sv->set.mp_hostagepenalty > 0 && atk->hostage_kills >= sv->set.mp_hostagepenalty) {
                cs2d_sv_kick(sv, attacker_id, "Too many hostage kills");
            }
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

                if (sv->set.mp_mapgoalscore) pl->score += 2;
                pl->hostages_rescued++;
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

static bool sv_tile_blocks(const cs2d_server_t *sv, int tx, int ty);
static bool sv_tile_blocks_bullet(const cs2d_server_t *sv, int tx, int ty);

static bool sv_projectile_is_impact(int type)
{
    switch (type) {
        case CS2D_WEP_ROCKETLAUNCHER:
        case CS2D_WEP_RPG7:
        case CS2D_WEP_GRENADELAUNCHER:
        case CS2D_WEP_MOLOTOV:
        case CS2D_WEP_SNOWBALL:
        case CS2D_WEP_GUTBOMB:
            return true;
        default:
            return false;
    }
}

static void sv_projectile_props(int type, float *out_speed, float *out_lifetime, float *out_flydist)
{
    float speed = 350.0f;
    float lifetime = 1.8f;
    float flydist = 385.0f;

    switch (type) {
        case CS2D_WEP_RPG7:
            speed = 750.0f;
            lifetime = 10.0f;
            flydist = 10000.0f;
            break;
        case CS2D_WEP_ROCKETLAUNCHER:
            speed = 700.0f;
            lifetime = 10.0f;
            flydist = 10000.0f;
            break;
        case CS2D_WEP_GRENADELAUNCHER:
            speed = 550.0f;
            lifetime = 5.0f;
            flydist = 600.0f;
            break;
        case CS2D_WEP_HE:
        case CS2D_WEP_FLASHBANG:
        case CS2D_WEP_SMOKE:
        case CS2D_WEP_GAS:
            speed = 350.0f;
            lifetime = 1.8f; /* 1800 ms in CS2D */
            flydist = 385.0f;
            break;
        case CS2D_WEP_FLARE:
            speed = 350.0f;
            lifetime = 60.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_MOLOTOV:
            speed = 320.0f;
            lifetime = 10.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_SNOWBALL:
            speed = 450.0f;
            lifetime = 5.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_AIRSTRIKE:
            speed = 350.0f;
            lifetime = 7.0f;
            flydist = 385.0f;
            break;
        case CS2D_WEP_GUTBOMB:
            speed = 320.0f;
            lifetime = 10.0f;
            flydist = 385.0f;
            break;
        default:
            speed = 350.0f;
            lifetime = 1.8f;
            flydist = 385.0f;
            break;
    }

    if (out_speed) *out_speed = speed;
    if (out_lifetime) *out_lifetime = lifetime;
    if (out_flydist) *out_flydist = flydist;
}

/* Projectiles that collide with players/hostages/NPCs (help.php "projectile_impact": the RPG,
 * rocket launcher, snowball and gut bomb; every other projectile only hits walls/ground). */
static bool sv_projectile_hits_entities(int type)
{
    return type == CS2D_WEP_ROCKETLAUNCHER || type == CS2D_WEP_RPG7 ||
           type == CS2D_WEP_SNOWBALL || type == CS2D_WEP_GUTBOMB;
}

/* FUN_140167fa0 / FUN_1401f1130: projectile detonation, applying object and player explosion damage */
void cs2d_sv_detonate_projectile(cs2d_server_t *sv, cs2d_sv_projectile_t *p)
{
    if (!sv || !p || !p->active) return;
    p->active = false;

    /* help.php "projectile": fired when a projectile explodes (id, weapon, x, y, projectileid). */
    cs2d_sv_lua_hook_projectile(sv, (int)p->player, p->type, (int)p->x, (int)p->y, (int)p->id);

    float radius_bld = 40.0f;
    float radius_pl = 160.0f;
    int damage = 105;

    switch (p->type) {
        case CS2D_WEP_RPG7:
            radius_bld = 45.0f;
            radius_pl = 140.0f;
            damage = 325;
            break;
        case CS2D_WEP_ROCKETLAUNCHER:
            radius_bld = 45.0f;
            radius_pl = 140.0f;
            damage = 100;
            break;
        case CS2D_WEP_GRENADELAUNCHER:
            radius_bld = 40.0f;
            radius_pl = 160.0f;
            damage = 105;
            break;
        case CS2D_WEP_HE:
            radius_bld = 40.0f;
            radius_pl = 160.0f;
            damage = 105;
            break;
        case CS2D_WEP_MOLOTOV:
            radius_bld = 45.0f;
            radius_pl = 50.0f;
            damage = 15;
            break;
        case CS2D_WEP_SNOWBALL:
            radius_bld = 0.0f;
            radius_pl = 24.0f;
            damage = 35;
            break;
        case CS2D_WEP_GUTBOMB:
            radius_bld = 40.0f;
            radius_pl = 96.0f;
            damage = 50;
            break;
        case CS2D_WEP_AIRSTRIKE:
            radius_bld = 250.0f;
            radius_pl = 300.0f;
            damage = 1500;
            break;
        case CS2D_WEP_FLASHBANG:
            /* Bot AI: ai_flash = Rand(4,6) seconds for bots in range and in line of sight
             * (CS2D.exe Tpro.c:3227..3237). */
            cs2d_sv_ai_flashbang(sv, p->x, p->y);
            return;
        case CS2D_WEP_SMOKE:
        case CS2D_WEP_GAS:
        case CS2D_WEP_FLARE:
        default:
            return;
    }

    /* 1. Buildings & dynamic objects: Tdo.damage falloff (FUN_1401f1130) */
    if (radius_bld > 0.0f && damage > 0) {
        cs2d_sv_explosion_objects(sv, p->x, p->y, radius_bld, damage, (int)p->player);
    }

    /* 2. Players radial damage with linear falloff (FUN_140167fa0 / FUN_1401658c0) */
    if (radius_pl > 0.0f && damage > 0) {
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            cs2d_sv_player_t *v = &sv->players[i];
            if (!v->used || !v->alive) continue;
            float dx = v->x - p->x;
            float dy = v->y - p->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > radius_pl) continue;

            int dmg = (int)((radius_pl - dist) * (float)damage / radius_pl);
            if (dmg <= 0) continue;

            /* Friendly fire check */
            if (p->player >= 1 && p->player <= CS2D_SV_MAX_PLAYERS) {
                const cs2d_sv_player_t *atk = &sv->players[p->player];
                if (atk->used && sv->set.sv_friendlyfire == 0 && sv->set.sv_gamemode != 1 &&
                    i != (int)p->player && v->team == atk->team) {
                    continue;
                }
            }
            cs2d_sv_damage(sv, i, (int)p->player, dmg, p->type);
        }

        /* 3. Hostages radial damage */
        for (int h = 0; h < sv->hostage_count; h++) {
            cs2d_sv_hostage_t *hos = &sv->hostages[h];
            if (!hos->active || hos->rescued || hos->health <= 0) continue;
            float dx = hos->x - p->x;
            float dy = hos->y - p->y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > radius_pl) continue;
            int dmg = (int)((radius_pl - dist) * (float)damage / radius_pl);
            if (dmg > 0) cs2d_sv_damage_hostage(sv, (int)hos->id, (int)p->player, dmg);
        }
    }
}

/* FUN_1401d4910: [27, player u8, type u8, x u16, y u16, dir u16, flydist u16] (11 bytes) */
void cs2d_sv_projectile(cs2d_server_t *sv, int id, int type, uint16_t x, uint16_t y, uint16_t dir, uint16_t flydist)
{
    if (id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    /* Grenade types: 51..54, 72..73, 75..77 */
    if (sv_is_grenade_type(type)) {
        if (type < CS2D_MAX_WEAPONS) pl->grenade_rebuy[type] = 1;
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
    /* cs2d_dedicated FUN_140190000: relays opcode 27 to all joined players EXCEPT the shooter,
     * because the client already predicted and spawned the projectile locally (Tpro.spawnprojectile).
     * Sending it back to the sender causes duplicate visual projectiles. */
    sv_broadcast_joined_except(sv, id, pkt, 11);

    /* Allocate slot in server projectile world (FUN_140095b80) */
    int slot = -1;
    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
        if (!sv->projectiles[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
    }

    float speed, lifetime, def_flydist;
    sv_projectile_props(type, &speed, &lifetime, &def_flydist);

    float fdist = (float)flydist;
    if (fdist <= 0.0f) fdist = def_flydist;

    /* Network direction: 16-bit 0..65535 normalized to 0..360 degrees (FUN_1401ce010 0.005493164) */
    float dir_deg = (float)dir * (360.0f / 65536.0f);

    uint32_t now = cs2d_sv_now();
    int proj_id = sv_next_projectile_id(sv, id);
    if (proj_id == 0) return;

    cs2d_sv_projectile_t *p = &sv->projectiles[slot];
    p->active = true;
    p->id = (uint16_t)proj_id;
    p->player = (uint8_t)id;
    p->type = (uint8_t)type;
    p->x = (float)x;
    p->y = (float)y;
    p->dir = dir_deg;
    p->flydist = fdist;
    p->speed = speed;
    p->spawn_ms = now;
    p->detonate_ms = (type == CS2D_WEP_HE || type == CS2D_WEP_FLASHBANG) ? (now + 1800) : 0;
    p->is_flying = true;
    p->expire_ms = now + (uint32_t)(lifetime * 1000.0f);
}

/* FUN_1401cfd00 / FUN_140168ac0: [41, proj_id u8, id u8] (3 bytes) */
void cs2d_sv_kill_projectile(cs2d_server_t *sv, int id, int proj_id)
{
    uint8_t pkt[3];
    pkt[0] = 41;
    pkt[1] = (uint8_t)proj_id;
    pkt[2] = (uint8_t)id;
    sv_broadcast_joined(sv, pkt, 3);

    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
        cs2d_sv_projectile_t *p = &sv->projectiles[i];
        if (p->active && p->player == id && p->id == proj_id) {
            cs2d_sv_detonate_projectile(sv, p);
            break;
        }
    }
}

/* FUN_140179060: projectile physics, wall ricochet and detonation update */
void cs2d_sv_step_projectiles(cs2d_server_t *sv)
{
    if (!sv) return;
    uint32_t now = cs2d_sv_now();
    float dt = (sv->last_proj_ms > 0 && now >= sv->last_proj_ms)
               ? (float)(now - sv->last_proj_ms) * 0.001f : 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    sv->last_proj_ms = now;

    for (int i = 0; i < CS2D_SV_MAX_PROJECTILES; i++) {
        cs2d_sv_projectile_t *p = &sv->projectiles[i];
        if (!p->active) continue;

        if (p->is_flying) {
            float step = p->speed * dt;
            if (step > p->flydist) step = p->flydist;

            double rad = (double)p->dir * 0.017453292519943295;
            float vx = (float)sin(rad) * p->speed;
            float vy = -(float)cos(rad) * p->speed;

            float next_x = p->x + vx * dt;
            float next_y = p->y + vy * dt;

            int tx_cur = (int)(p->x * 0.03125f);
            int ty_cur = (int)(p->y * 0.03125f);
            int tx_next = (int)(next_x * 0.03125f);
            int ty_next = (int)(next_y * 0.03125f);

            bool hit_solid_x = sv_tile_blocks_bullet(sv, tx_next, ty_cur);
            bool hit_solid_y = sv_tile_blocks_bullet(sv, tx_cur, ty_next);
            bool hit_solid = hit_solid_x || hit_solid_y || sv_tile_blocks_bullet(sv, tx_next, ty_next);

            bool hit_player = false;
            bool hit_hostage = false;
            int hit_npc = 0;
            if (sv_projectile_hits_entities(p->type)) {
                for (int pl_idx = 1; pl_idx <= CS2D_SV_MAX_PLAYERS; pl_idx++) {
                    if (pl_idx == (int)p->player) continue;
                    const cs2d_sv_player_t *pl = &sv->players[pl_idx];
                    if (!pl->used || !pl->alive || pl->team < 1) continue;
                    if (fabsf(pl->x - next_x) < 20.0f && fabsf(pl->y - next_y) < 20.0f) {
                        hit_player = true;
                        break;
                    }
                }
                if (!hit_player) {
                    for (int h = 0; h < sv->hostage_count; h++) {
                        const cs2d_sv_hostage_t *hos = &sv->hostages[h];
                        if (!hos->active || hos->rescued || hos->health <= 0) continue;
                        if (fabsf(hos->x - next_x) < 20.0f && fabsf(hos->y - next_y) < 20.0f) {
                            hit_hostage = true;
                            break;
                        }
                    }
                }
                if (!hit_player && !hit_hostage) {
                    for (int k = 0; k < CS2D_DYNOBJ_MAX; k++) {
                        const cs2d_dynobj_t *n = &sv->dynobj.objs[k];
                        if (!n->active || n->typ != CS2D_DO_NPC || n->health <= 0) continue;
                        if (fabsf(n->x - next_x) < 20.0f && fabsf(n->y - next_y) < 20.0f) {
                            hit_npc = n->id;
                            break;
                        }
                    }
                }
            }

            if (hit_solid || hit_player || hit_hostage || hit_npc != 0) {
                /* help.php "projectile_impact": mode 0 wall, 1 player, 2 hostage, 3 NPC. */
                int mode = hit_npc ? 3 : (hit_player ? 1 : (hit_hostage ? 2 : 0));
                cs2d_sv_lua_hook_projectile_impact(sv, (int)p->player, p->type, (int)next_x, (int)next_y, mode,
                                                   (int)p->id);
                if (sv_projectile_is_impact(p->type)) {
                    p->x = next_x;
                    p->y = next_y;
                    cs2d_sv_detonate_projectile(sv, p);
                    continue;
                } else {
                    /* Bouncing grenades: invert velocity components and lose 50% flydist */
                    if (hit_solid_x) vx = -vx * 0.5f;
                    if (hit_solid_y) vy = -vy * 0.5f;
                    p->dir = (float)(atan2((double)vx, (double)-vy) * 57.29577951308232);
                    p->flydist *= 0.5f;
                    p->speed *= 0.65f;
                }
            } else {
                p->x = next_x;
                p->y = next_y;
                p->flydist -= step;
            }

            if (p->flydist <= 0.0f) {
                p->is_flying = false;
                p->speed = 0.0f;
                /* The projectile reached its maximum/target flight distance: ground impact (100). */
                cs2d_sv_lua_hook_projectile_impact(sv, (int)p->player, p->type, (int)p->x, (int)p->y, 100, (int)p->id);
                if (sv_projectile_is_impact(p->type)) {
                    cs2d_sv_detonate_projectile(sv, p);
                    continue;
                }
            }
        }

        /* Check fuse expiration (HE / Flashbang: 1.8s) */
        if (p->detonate_ms > 0 && now >= p->detonate_ms) {
            cs2d_sv_detonate_projectile(sv, p);
            continue;
        }

        /* Check airstrike expiration (7s countdown on the ground) */
        if (p->type == CS2D_WEP_AIRSTRIKE && p->expire_ms > 0 && now >= p->expire_ms) {
            cs2d_sv_detonate_projectile(sv, p);
            continue;
        }

        /* General expiration */
        if (p->expire_ms > 0 && now >= p->expire_ms) {
            p->active = false;
        }
    }
}

/* FUN_1401d28b0 -> FUN_140106350: [57, player u8, state u8] (3 bytes) */
void cs2d_sv_flashlight(cs2d_server_t *sv, int id, int state)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;
    if (sv->set.mp_flashlight == 0) return; /* 0x1401063b7 */

    pl->flashlight = (uint8_t)state;
    uint8_t pkt[3] = { 57, (uint8_t)id, (uint8_t)state };
    sv_broadcast_joined(sv, pkt, 3);
    cs2d_sv_lua_hook_flashlight(sv, id, state);
}

/* FUN_1401d2d40 -> FUN_1401b2bf0: [60, player u8, state u8] (3 bytes, spectator flashlight) */
void cs2d_sv_spec_flash(cs2d_server_t *sv, int id, uint8_t state)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used) return;

    pl->specflash = state;
    uint8_t pkt[3] = { 60, (uint8_t)id, state };
    sv_broadcast_joined(sv, pkt, 3);
}

/* FUN_1401cead0 -> FUN_14016e0a0: entity event [31, x u16, y u16, mode u8] */
void cs2d_sv_entity_event(cs2d_server_t *sv, int id, uint16_t x, uint16_t y, uint8_t mode)
{
    if (!sv) return;
    cs2d_sv_lua_hook_triggerentity(sv, (int)x, (int)y);
    if (sv->map_loaded) {
        for (uint32_t i = 0; i < sv->map.entity_count && i < CS2D_MAX_ENTITIES; i++) {
            const cs2d_entity_t *e = &sv->map.entities[i];
            if (e->x == x && e->y == y) {
                /* [31] carries the new state (the client computed it); keep it for the join
                 * snapshot (section 4) and for entity(id, "state"). */
                if (sv->map.entity_state[i] != mode) {
                    sv->map.entity_state[i] = mode;
                    sv->map.entity_state_version++;
                }
                if (e->trigger[0] != '\0') {
                    cs2d_sv_trigger_names(sv, e->trigger, 0);
                }
                break;
            }
        }
    }
    uint8_t pkt[6] = { 31, (uint8_t)(x & 0xff), (uint8_t)(x >> 8), (uint8_t)(y & 0xff), (uint8_t)(y >> 8), mode };
    sv_broadcast_joined_except(sv, id, pkt, 6);
}

/* FUN_1401d11a0 -> FUN_140177230: portalfire [50, color u8, tx i32, ty i32] */
void cs2d_sv_portal_fire(cs2d_server_t *sv, int id, uint8_t color, int32_t tx, int32_t ty)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    int c = color & 1;
    pl->portal_x[c] = tx;
    pl->portal_y[c] = ty;
    pl->portal_active[c] = true;

    /* Broadcast relay: [50, player_id u8, color u8, tx i32, ty i32] (11 bytes) */
    uint8_t pkt[11];
    pkt[0] = 50;
    pkt[1] = (uint8_t)id;
    pkt[2] = (uint8_t)c;
    memcpy(&pkt[3], &tx, 4);
    memcpy(&pkt[7], &ty, 4);
    sv_broadcast_joined(sv, pkt, 11);
}

/* FUN_1401d26a0 -> FUN_1401694e0: weapon impulse [56, value u8] */
void cs2d_sv_weapon_impulse(cs2d_server_t *sv, int id, uint8_t val)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    uint8_t pkt[3] = { 56, (uint8_t)id, val };
    sv_broadcast_joined_except(sv, id, pkt, 3);
}

/* FUN_1401d3840 -> FUN_140192080: firedirection [68, dir u16] */
void cs2d_sv_fire_direction(cs2d_server_t *sv, int id, uint16_t dir)
{
    if (!sv || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive) return;

    pl->rot = (float)dir / 182.04445f;
    pl->rot_flag = 1;

    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_player_t *spec = &sv->players[i];
        if (!spec->used || spec->alive || spec->spectating_player != id) continue;
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
        if (c && spec->joined) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, false, false);
            cs2d_pkt_w_byte(p, 70);
            cs2d_pkt_w_byte(p, (uint8_t)id);
            cs2d_pkt_w_short(p, dir);
        }
    }
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
    pl->ping = 0xfffe;            /* internal/network value; player(bot,"ping") reports 0 */
    pl->joined = true;
    pl->maxhealth = 100;
    pl->os = 1;
    /* Original dedicated: bots are shown as 0.0.0.0 (probe: player(1,"ip") -> "0.0.0.0"), which also
     * keeps the WQ2D geoip timer from calling geoip_lookup with an empty string. */
    snprintf(pl->ip, sizeof(pl->ip), "0.0.0.0");

    if (name && name[0]) {
        cs2d_sv_sanitize_name(sv, name, pl->name, sizeof(pl->name));
    } else {
        const char *prefix = cs2d_sv_settings_get(&sv->set, "bot_prefix");
        if (prefix && prefix[0]) {
            snprintf(pl->name, sizeof(pl->name), "%.*sBot %d",
                     (int)sizeof(pl->name) - 16, prefix, slot);
        } else {
            snprintf(pl->name, sizeof(pl->name), "Bot %d", slot);
        }
    }

    int t = (team >= 1 && team <= 2) ? team
            : (cs2d_sv_count_team(sv, 1) <= cs2d_sv_count_team(sv, 2) ? 1 : 2);
    pl->team = t;
    /* bot_add without a look picks a random skin (the original probe: looks 3 and 2). */
    pl->look = (look >= 0 && look <= 3) ? look : cs2d_sv_rand(0, 3);
    pl->team_chosen = true;

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

    /* The join notice [248] carries no team: a client only learns it from [20, id, team, look]
     * (FUN_1401624c0 success path). It must come AFTER [248]: the client resets the slot to
     * Spectator when it handles [248], so an earlier [20] was wiped and the bot stayed a "spectator"
     * (walking, audible steps, shooting) on the clients while the server already ran its AI. */
    uint8_t team_msg[4] = { 20, (uint8_t)slot, (uint8_t)t, (uint8_t)pl->look };
    for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
        cs2d_sv_conn_t *oc = cs2d_sv_conn_of_player(sv, i);
        if (!oc || !sv->players[i].joined) continue;
        cs2d_pkt_t *pt = cs2d_sv_packet(sv, oc, true, false);
        for (int k = 0; k < 4; k++) cs2d_pkt_w_byte(pt, team_msg[k]);
    }

    /* help.php: bots have no connection process, so connect is INSTANTLY followed by join */
    cs2d_sv_lua_hook_connect(sv, slot);
    cs2d_sv_lua_hook_join(sv, slot);

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
        if (b->expire_ms != 0 && (int32_t)(cs2d_sv_now() - b->expire_ms) >= 0) continue;
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

/* mp_tempbantime: temporary IP ban (help.php: 1..1440 minutes) */
void cs2d_sv_tempban_ip(cs2d_server_t *sv, const char *ip, int minutes, const char *reason)
{
    if (!sv || !ip || !ip[0] || minutes <= 0) return;
    if (minutes > 1440) minutes = 1440;
    if (sv->ban_count >= CS2D_SV_MAX_BANS) return;
    cs2d_sv_ban_t *b = &sv->bans[sv->ban_count++];
    memset(b, 0, sizeof(*b));
    b->active = true;
    snprintf(b->ip, sizeof(b->ip), "%s", ip);
    snprintf(b->reason, sizeof(b->reason), "%s", (reason && reason[0]) ? reason : "Temporary ban");
    b->expire_ms = cs2d_sv_now() + (uint32_t)minutes * 60000u;
    cs2d_server_print("Temporarily banned IP: %s for %d min (%s)", b->ip, minutes, b->reason);
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

    char clean_map[64];
    cs2d_sv_sanitize_map_name(clean_map, sizeof(clean_map), mapname);
    if (!clean_map[0]) return false;

    char path[256];
    snprintf(path, sizeof(path), "maps/%s.map", clean_map);
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
    memset(sv->entity_reuse_ms, 0, sizeof(sv->entity_reuse_ms));
    snprintf(sv->set.sv_map, sizeof(sv->set.sv_map), "%s", clean_map);

    /* Recalculate SHA */
    cs2d_net_sha256_file(path, sv->map_sha);

    cs2d_sv_init_hostages(sv);
    cs2d_dynobj_init(&sv->dynobj, &sv->map);
    cs2d_dynobj_spawn_map_objects(&sv->dynobj);
    cs2d_sv_fix_building_health(sv);
    if (sv->lua && sv->set.mp_luamap) {
        cs2d_sv_lua_load_map(sv, clean_map);
    }
    sv->score_t = sv->score_ct = 0;
    sv->round_number = 0;          /* mp_roundlimit counts rounds played on this map */
    sv->mapchange_pending = false;
    sv->map_start_ms = cs2d_sv_now();

    cs2d_sv_round_start(sv, 0);

    cs2d_sv_lua_hook_mapchange(sv, clean_map);
    cs2d_server_print("Map changed to '%s'", clean_map);
    return true;
}

/* 0x14013b430: next map from sys/mapcycle.cfg */
const char *cs2d_sv_next_map(const cs2d_server_t *sv)
{
    if (!sv) return "de_dust2";
    if (sv->mapcycle_count > 0) {
        for (int i = 0; i < sv->mapcycle_count; i++) {
            if (_stricmp(sv->mapcycle[i], sv->set.sv_map) == 0) {
                if (i + 1 < sv->mapcycle_count) return sv->mapcycle[i + 1];
                return sv->mapcycle[0];
            }
        }
        return sv->mapcycle[0];
    }
    if (sv->map_count > 0) {
        for (int i = 0; i < sv->map_count; i++) {
            if (_stricmp(sv->maps[i], sv->set.sv_map) == 0) {
                if (i + 1 < sv->map_count) return sv->maps[i + 1];
                return sv->maps[0];
            }
        }
        return sv->maps[0];
    }
    return sv->set.sv_map[0] ? sv->set.sv_map : "de_dust2";
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
        int max_hp = cs2d_sv_building_health(sv, o->typ);
        if (o->health < max_hp) o->health = max_hp;
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
    if (sv_list_has_id(sv->set.mp_unbuildable, typ)) return;   /* mp_unbuildable (help.php) */

    int price = cs2d_sv_building_price(sv, typ);
    /* 1. funds: money < price refuses with 0562 (category 2 - mines, portals - is exempt) */
    if (def->category != 2 && pl->money < price) {
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
    if (price > 16000 || price < 0 || typ == CS2D_DO_SITE) {
        sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_NOT_ALLOWED);
        return;
    }

    /* 6. per player limit (0713). A Turret counts its Dual / Triple upgrades too (limit = limit[8]). */
    int limit = (typ == CS2D_DO_TURRET) ? cs2d_sv_building_limit(sv, CS2D_DO_TURRET)
                                        : cs2d_sv_building_limit(sv, typ);
    if (typ == CS2D_DO_TURRET) {
        int n = cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_TURRET, false) +
                cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_DUAL_TURRET, false) +
                cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, CS2D_DO_TRIPLE_TURRET, false);
        if (limit <= n) { sv_build_refuse(sv, id, pl->team, tx, ty, SV_BUILD_LIMIT); return; }
    } else if (cs2d_dynobj_count_type(&sv->dynobj, id, pl->team, typ, false) >= limit) {
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
    pl->money -= price;
    if (pl->money < 0) pl->money = 0;

    /* 10. place: Barbed Wire directly (mode 60 = the hit wobble), the rest as a Construction Site whose
     * mode is the wanted type (Tdo.inibuilding(-1, player, 10, typ, tx, ty) + a Ttimer). */
    int site_typ  = (typ == CS2D_DO_BARBED_WIRE) ? CS2D_DO_BARBED_WIRE : CS2D_DO_SITE;
    int site_mode = (typ == CS2D_DO_BARBED_WIRE) ? 60 : typ;
    cs2d_dynobj_t *o = cs2d_dynobj_spawn(&sv->dynobj, -1, site_typ, tx * 32, ty * 32, rot, site_mode,
                                         pl->team, id);
    if (!o) return; /* the 1024 object pool is full */
    o->health = cs2d_sv_building_health(sv, site_typ);
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
    if (player >= 1 && player <= CS2D_SV_MAX_PLAYERS && sv->players[player].used && def &&
        def->kill_reward > 0 && sv->set.mp_killbuildingmoney) {
        cs2d_sv_set_playerdata(sv, player, 2, sv->players[player].money + def->kill_reward);
    }
    if (o->typ == CS2D_DO_SITE && o->t1_ms) o->t1_ms = 0;
    bool snark = (o->typ == CS2D_DO_NPC && o->player == 3);
    float snark_x = o->x, snark_y = o->y;
    cs2d_dynobj_kill(&sv->dynobj, obj_id, NULL); /* Tdo.kill: the explosion of the client is a Tp */
    sv_broadcast_obj_remove(sv, obj_id);
    if (snark) {
        /* Tdo.kill case 0x1e / 3 (CS2D.exe 0x1401fa64f): the server calls FUN_1401f1130(x, y, 75, 30,
         * attacker 0, weapon 0xf9, falloff 0) - the radius / damage are the floats at 0x140bd6aa8 /
         * 0x140bd6a98. Players within 75 px take (75 - d) * 30 / 75; objects (other NPCs included)
         * go through the same Tdo.damage falloff. Done after the removal so the snark is not hit. */
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            const cs2d_sv_player_t *pl = &sv->players[i];
            if (!pl->used || !pl->alive || pl->team < 1 || pl->health < 1) continue;
            float d = sqrtf((pl->x - snark_x) * (pl->x - snark_x) + (pl->y - snark_y) * (pl->y - snark_y));
            if (d > 75.0f) continue;
            int dmg = (int)((75.0f - d) * 30.0f / 75.0f);
            if (dmg >= 1) cs2d_sv_damage(sv, i, 0, dmg, 0xf9);
        }
        cs2d_sv_explosion_objects(sv, snark_x, snark_y, 75.0f, 30, 0);
    }
}

/*
 * Tdo.damage (0x140035110): damage > 0 hurts, damage < 0 repairs. `player` is the attacker (0 = none).
 * The portals (22 / 23) and the Lua images (40) never take damage. A building of the attacker's own team
 * that he does not own is left alone while friendly fire is off (DAT_140a14d98 == 0; == 2 would allow it
 * after five team building kills). Losing health drops the Wrench upgrade progress.
 */
/* mp_killbuildings (help.php): what happens to a player's buildings when he leaves the game.
 * 0 kill all, 1 kill all except walls, 2 kill dispensers/turrets/supplies only, 3 keep everything. */
void cs2d_sv_kill_player_buildings(cs2d_server_t *sv, int player, int mode)
{
    if (!sv || player < 1 || player > CS2D_SV_MAX_PLAYERS || mode >= 3) return;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active || o->player != player) continue;
        bool kill = true;
        if (mode == 1 && (o->typ == CS2D_DO_WALL_1 || o->typ == CS2D_DO_WALL_2 || o->typ == CS2D_DO_WALL_3)) {
            kill = false;
        } else if (mode == 2 && !(o->typ == CS2D_DO_DISPENSER || o->typ == CS2D_DO_TURRET ||
                                  o->typ == CS2D_DO_SUPPLY || o->typ == CS2D_DO_DUAL_TURRET ||
                                  o->typ == CS2D_DO_TRIPLE_TURRET || o->typ == CS2D_DO_SUPER_SUPPLY)) {
            kill = false;
        }
        if (!kill) continue;
        int id = o->id;
        cs2d_dynobj_kill(&sv->dynobj, id, NULL);
        sv_broadcast_obj_remove(sv, id);
    }
}

void cs2d_sv_object_damage(cs2d_server_t *sv, int obj_id, int damage, int player)
{
    cs2d_dynobj_t *o = cs2d_dynobj_get(&sv->dynobj, obj_id);
    if (!o) return;
    const cs2d_dynobj_def_t *def = cs2d_dynobj_def(o->typ);
    if (!def) return;
    if (o->typ == CS2D_DO_ORANGE_PORTAL || o->typ == CS2D_DO_BLUE_PORTAL || o->typ == CS2D_DO_IMAGE) return;
    if (cs2d_sv_lua_hook_objectdamage(sv, obj_id, damage, player) == 1) return;

    if (player >= 1 && player <= CS2D_SV_MAX_PLAYERS && def->category == 1 && damage > 0) {
        cs2d_sv_player_t *a = &sv->players[player];
        if (a->used && o->player != player && sv_obj_same_team(sv, o, a, player)) {
            /* mp_killteambuildings: 0 unkillable, 1 killable, 2 killable + penalty (help.php) */
            if (sv->set.mp_killteambuildings == 0) return;
            if (sv->set.mp_killteambuildings == 2) {
                a->score--;
                a->money -= 3300;
                if (a->money < 0) a->money = 0;
            }
        }
    }

    int max_hp = cs2d_sv_building_health(sv, o->typ);
    /* Tdo.damage skips the max-health array for typ 0x1e (NPC); their health is the NPC type
     * default (150 / 30 / 20 / 200 / 100), so the building table must not clamp it. */
    bool npc = (o->typ == CS2D_DO_NPC);
    o->health -= damage;
    if (!npc && o->health != max_hp) o->upgrade = 0;
    if (o->health <= 0) {
        o->health = 0;
        sv_object_kill(sv, o, player);
        return;
    }
    if (!npc && o->health > max_hp) o->health = max_hp;
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
    if (def->upgrade_points <= 0 || def->upgrade_to <= 0) return 1;

    int next = o->upgrade + points;
    if (next > def->upgrade_points) next = def->upgrade_points;
    /* help.php objectupgrade(id, player, progress, total): returning 1 cancels this upgrade step
     * (and therefore the money the caller would charge). */
    if (cs2d_sv_lua_hook_objectupgrade(sv, obj_id, player, next, def->upgrade_points) == 1) return 0;

    if (o->upgrade < def->upgrade_points) {
        o->upgrade = next;
        sv_broadcast_obj_health(sv, o);
    }
    if (o->upgrade >= def->upgrade_points) {
        int max_hp = cs2d_sv_building_health(sv, def->upgrade_to);
        o->typ = (uint8_t)def->upgrade_to;
        o->upgrade = 0;
        o->health = max_hp;
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

        if (o->typ == CS2D_DO_LASER_MINE) {
            if (o->mode == 1 && cs2d_dynobj_in_lasermine(o, tx, ty)) {
                const cs2d_weapon_def_t *w = cs2d_weapon_get(CS2D_WEP_LASERMINE);
                float mx = o->x + 16.0f, my = o->y + 16.0f;
                float radius = (w && w->range > 0) ? (float)w->range : 200.0f;
                int dmg = (w && w->damage > 0) ? w->damage : 150;
                int attacker = o->player;
                int obj_id = o->id;
                sv_object_kill(sv, o, 0);   /* Tdo.kill(obj, 0) */
                for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                    cs2d_sv_player_t *pp = &sv->players[p];
                    if (!pp->used || !pp->alive) continue;
                    float ddx = pp->x - mx, ddy = pp->y - my;
                    float dist = sqrtf(ddx * ddx + ddy * ddy);
                    if (dist > radius) continue;
                    int d = (int)((radius - dist) * (float)dmg / radius);
                    if (d > 0) cs2d_sv_damage_ex(sv, p, attacker, d, CS2D_WEP_LASERMINE, obj_id);
                }
                cs2d_sv_explosion_objects(sv, mx, my, radius, dmg, attacker);
                return true;
            }
            continue;
        }

        if ((int)(o->x * 0.03125f) != tx || (int)(o->y * 0.03125f) != ty) continue;

        if (o->typ == CS2D_DO_BARBED_WIRE) {
            cs2d_sv_damage_ex(sv, id, o->player, 20, 0xff, o->id);
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
            cs2d_sv_damage_ex(sv, id, other_side ? o->player : 0, 20, 0xfe, o->id);
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
            int obj_id = o->id;
            sv_object_kill(sv, o, 0);   /* Tdo.kill(obj, 0): the client shows the Tp.explosion */
            for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                cs2d_sv_player_t *pp = &sv->players[p];
                if (!pp->used || !pp->alive) continue;
                float ddx = pp->x - mx, ddy = pp->y - my;
                float dist = sqrtf(ddx * ddx + ddy * ddy);
                if (dist > radius) continue;
                int d = (int)((radius - dist) * (float)dmg / radius);
                if (d > 0) cs2d_sv_damage_ex(sv, p, 0, d, CS2D_WEP_MINE, obj_id);
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

/* Same as sv_tile_blocks but for a bullet ray: an Obstacle tile blocks movement only and lets bullets
 * through (cs2d_map_mode_collision: "blocks movement, allows bullets"); walls, map edges and solid
 * objects still stop it. */
static bool sv_tile_blocks_bullet(const cs2d_server_t *sv, int tx, int ty)
{
    const cs2d_map_t *map = &sv->map;
    if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return true;
    if (cs2d_map_prop_class(cs2d_map_get_tile_mode(map, (uint32_t)tx, (uint32_t)ty)) == 0) return true;
    if (map->object_collision && map->object_collision[(size_t)ty * map->width + tx]) return true;
    return false;
}

/* Tdo image hit zone (imagehitzone / [59], help.php "hitzone"). The image x/y is its centre; the
 * zone is the axis-aligned rect x + xo .. x + xo + w, y + yo .. y + yo + h (no rotation).
 * Returns 0 = no zone, 1 = non-blocking zone (shot continues) or 2 = blocking zone (mode > 100,
 * the shot stops). The "hitzone" hook may replace *damage: 0 keeps it, > 0 replaces it and < 0
 * sets it to 0. */
int cs2d_sv_hitzone_at(cs2d_server_t *sv, float px, float py, float prev_x, float prev_y,
                       int player_id, int object_id, int weapon, int *damage)
{
    if (!sv || !damage) return 0;
    int result = 0;
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        cs2d_dynobj_t *o = &sv->dynobj.objs[i];
        if (!o->active || o->typ != CS2D_DO_IMAGE || o->hit_mode == 0) continue;
        if (o->mode == 2) continue;   /* help.php: hit zones do not work on HUD images */
        float x0 = o->x + (float)o->hit_xo;
        float y0 = o->y + (float)o->hit_yo;
        if (px < x0 || px >= x0 + (float)o->hit_w) continue;
        if (py < y0 || py >= y0 + (float)o->hit_h) continue;
        bool was_inside = (prev_x >= x0 && prev_x < x0 + (float)o->hit_w &&
                           prev_y >= y0 && prev_y < y0 + (float)o->hit_h);
        if (!was_inside) {
            int ret = cs2d_sv_lua_hook_hitzone(sv, o->id, player_id, object_id, weapon, px, py, *damage);
            if (ret > 0) *damage = ret;
            else if (ret < 0) *damage = 0;
        }
        if (o->hit_mode > 100) { result = 2; break; }
        result = 1;
    }
    return result;
}

/* A bullet from (x, y) along `rot` up to `dist` px (Tshoot.Shoot): the first player hit, else 0. A player
 * is a 12 px box (the Lua "freeline" help: "mode 2+ checks player hitboxes (12px radius)"). obj_out (may
 * be NULL) gets the object that stopped the bullet, 0 for a bare wall. damage_io (may be NULL) carries
 * the shot damage through the "hitzone" hook; a blocking image hit zone stops the ray. */
static int sv_trace_shot(cs2d_server_t *sv, float x, float y, float rot, float dist, int skip,
                         int attacker_object, int weapon, int *damage_io, int *obj_out)
{
    double a = (double)rot * 0.017453292519943295;
    float dx = (float)sin(a), dy = -(float)cos(a);
    int sx = (int)(x * 0.03125f), sy = (int)(y * 0.03125f);
    if (obj_out) *obj_out = 0;
    float prev_px = x, prev_py = y;
    for (float d = 4.0f; d <= dist; d += 4.0f) {
        float px = x + dx * d, py = y + dy * d;
        int tx = (int)(px * 0.03125f), ty = (int)(py * 0.03125f);
        if (damage_io && cs2d_sv_hitzone_at(sv, px, py, prev_px, prev_py, skip, attacker_object, weapon,
                                            damage_io) == 2)
            break;                                  /* a blocking image hit zone stops the shot */
        prev_px = px;
        prev_py = py;
        /* the muzzle sits inside the shooting building's own tile: that tile never blocks the bullet */
        if (!(tx == sx && ty == sy) && sv_tile_blocks_bullet(sv, tx, ty)) {
            if (obj_out) {
                const cs2d_dynobj_t *o = cs2d_dynobj_at_tile(&sv->dynobj, tx, ty);
                if (o) *obj_out = o->id;
            }
            break;
        }
        /* An NPC on a floor tile does not change the map collision, but the original Tshoot scans
         * the Tdo on the hit tile before the players, so it stops the bullet and takes the damage. */
        const cs2d_dynobj_t *npc = cs2d_dynobj_at_tile(&sv->dynobj, tx, ty);
        if (npc && npc->typ == CS2D_DO_NPC && npc->health > 0) {
            if (obj_out) *obj_out = npc->id;
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
        int tdmg = sv->set.mp_turretdamage;
        if (tdmg <= 0) tdmg = SV_TURRET_DAMAGE;
        int hit = sv_trace_shot(sv, bx, by, rot, 400.0f, o->player, o->id, 0xfd, &tdmg, NULL);
        if (hit == 0) continue;
        /* Do not shoot your own team (the scan only picks enemies, a bystander can still be in the way) */
        const cs2d_sv_player_t *t = &sv->players[hit];
        bool enemy = (sv->set.sv_gamemode == 1) || (o->team == 1 && t->team > 1) || (o->team > 1 && t->team == 1);
        if (!enemy) continue;
        cs2d_sv_damage_ex(sv, hit, o->player, tdmg, 0xfd, o->id);
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
    int dmg = wep->damage;
    (void)sv_trace_shot(sv, pl->x, pl->y, pl->rot, cs2d_weapon_shot_reach(wep), id, 0, wep->id, &dmg, &obj_id);
    if (obj_id == 0) return;               /* the bullet flew into a bare wall (or a player stopped it) */
    if (dmg <= 0) return;                  /* a hit zone zeroed the damage */
    const cs2d_dynobj_t *o = cs2d_dynobj_get(&sv->dynobj, obj_id);
    /* NPCs are shootable (Tshoot.Shoot checks the Tdo on the tile before the players); the
     * debris gate only describes buildings. A Barricade still takes close combat / explosions only. */
    if (!o) return;
    if (o->typ != CS2D_DO_NPC && (!cs2d_dynobj_hit_debris(o->typ) || o->typ == CS2D_DO_BARRICADE)) return;
    cs2d_sv_object_damage(sv, obj_id, dmg, id);
}

/*
 * The player hits of a primary attack, resolved by the server for a shooter without a client (a bot). It runs
 * the shot the way the client's Tpl.attack does (cs2d_scene_game.c shot_bullets): M3 / XM1014 fire five
 * pellets at 0, -5, +5, -9, +9 degrees, each turned by SeedTable[Tpl.random] % (2 * accuracy) - accuracy
 * (Tpl.shoot 0x140209810, the counter advances (n + 1) % 200 per pellet); every other weapon shoots straight
 * along the aim. Each bullet is the server trace (sv_trace_shot, Tshoot.Shoot) up to cs2d_weapon_shot_reach;
 * the first player it meets takes the weapon damage if he is an enemy (a bystander only stops the bullet).
 * Walls / buildings are cs2d_sv_shot_at_building's job and are not repeated here.
 */
void cs2d_sv_shot_at_players(cs2d_server_t *sv, int id, const cs2d_weapon_def_t *wep)
{
    static const float pellets[5] = { 0.0f, -5.0f, 5.0f, -9.0f, 9.0f };
    if (!wep || wep->damage <= 0 || id < 1 || id > CS2D_SV_MAX_PLAYERS) return;
    cs2d_sv_player_t *pl = &sv->players[id];
    if (!pl->used || !pl->alive || pl->team < 1) return;
    if (sv_round_frozen(sv)) return;   /* same gate as cs2d_sv_fire */

    /* the SeedTable is process-wide and keyed by the seed sent in [252,6] */
    static int seeded = -1;
    if (seeded != sv->seed) { cs2d_random_seed(sv->seed); seeded = sv->seed; }

    bool shotgun = (pl->weapon == CS2D_WEP_M3 || pl->weapon == CS2D_WEP_XM1014);
    int acc = shotgun ? cs2d_weapon_accuracy((uint8_t)pl->weapon) : 0;
    float reach = cs2d_weapon_shot_reach(wep);
    for (int i = 0; i < (shotgun ? 5 : 1); i++) {
        float a = pl->rot + (shotgun ? pellets[i] : 0.0f);
        if (acc > 0) {
            if (pl->shot_random < 1) pl->shot_random = 1;
            int s = cs2d_random_table(pl->shot_random);
            if (s >= 0) a += (float)(s % (2 * acc) - acc);
            pl->shot_random = (pl->shot_random + 1) % 200;
        }
        int dmg = wep->damage;
        int hit = sv_trace_shot(sv, pl->x, pl->y, a, reach, id, 0, wep->id, &dmg, NULL);
        if (hit <= 0 || dmg <= 0) continue;
        const cs2d_sv_player_t *t = &sv->players[hit];
        bool enemy = (sv->set.sv_gamemode == 1) || (pl->team == 1 && t->team > 1) || (pl->team > 1 && t->team == 1);
        if (!enemy) continue;
        if (sv->set.sv_gamemode == 4 && t->team == 1 && wep->damage_z1 > 0) dmg = wep->damage_z1;
        cs2d_sv_damage(sv, hit, id, dmg, wep->id);
    }
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
                o->health = cs2d_sv_building_health(sv, typ);
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

        /* Laser Mine: update beam length (target) and check if anyone intersects it */
        if (o->typ == CS2D_DO_LASER_MINE && o->mode == 1) {
            cs2d_dynobj_update_lasermine(&sv->dynobj, &sv->map, o);
            bool triggered = false;
            for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                const cs2d_sv_player_t *pl = &sv->players[p];
                if (!pl->used || !pl->alive || pl->team < 1) continue;
                int ptx = (int)(pl->x * 0.03125f), pty = (int)(pl->y * 0.03125f);
                if (cs2d_dynobj_in_lasermine(o, ptx, pty)) {
                    triggered = true;
                    break;
                }
            }
            if (triggered) {
                const cs2d_weapon_def_t *w = cs2d_weapon_get(CS2D_WEP_LASERMINE);
                float mx = o->x + 16.0f, my = o->y + 16.0f;
                float radius = (w && w->range > 0) ? (float)w->range : 200.0f;
                int dmg = (w && w->damage > 0) ? w->damage : 150;
                int attacker = o->player;
                int obj_id = o->id;
                sv_object_kill(sv, o, 0);
                for (int p = 1; p <= CS2D_SV_MAX_PLAYERS; p++) {
                    cs2d_sv_player_t *pp = &sv->players[p];
                    if (!pp->used || !pp->alive) continue;
                    float ddx = pp->x - mx, ddy = pp->y - my;
                    float dist = sqrtf(ddx * ddx + ddy * ddy);
                    if (dist > radius) continue;
                    int d = (int)((radius - dist) * (float)dmg / radius);
                    if (d > 0) cs2d_sv_damage_ex(sv, p, attacker, d, CS2D_WEP_LASERMINE, obj_id);
                }
                cs2d_sv_explosion_objects(sv, mx, my, radius, dmg, attacker);
                continue;
            }
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
 * The target is found by raycasting along the shot ray up to the Wrench's reach (Tdo.getatpos along the ray;
 * the Wrench's range is 15 px in the weapon table set_wep(74, ...)).
 */
static cs2d_dynobj_t *sv_wrench_target(const cs2d_server_t *sv, const cs2d_sv_player_t *pl)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get(CS2D_WEP_WRENCH);
    float reach = (w && w->range > 0) ? (float)w->range : 15.0f;
    double a = (double)pl->rot * 0.017453292519943295;
    float dx = (float)sin(a), dy = -(float)cos(a);
    int px_tile = (int)(pl->x * 0.03125f), py_tile = (int)(pl->y * 0.03125f);

    /* First check building on the player's own tile */
    cs2d_dynobj_t *obj = cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)&sv->dynobj, px_tile, py_tile);
    if (obj) return obj;

    /* Tshoot.Shoot: raycast probe in 2 px steps up to reach */
    int last_tx = px_tile, last_ty = py_tile;
    for (float d = 2.0f; d <= reach; d += 2.0f) {
        float ex = pl->x + dx * d, ey = pl->y + dy * d;
        int tx = (int)(ex * 0.03125f), ty = (int)(ey * 0.03125f);
        if (tx != last_tx || ty != last_ty) {
            last_tx = tx;
            last_ty = ty;
            obj = cs2d_dynobj_at_tile((cs2d_dynobj_world_t *)&sv->dynobj, tx, ty);
            if (obj) return obj;
            if (sv_tile_blocks(sv, tx, ty)) break;
        }
    }
    float ex = pl->x + dx * reach, ey = pl->y + dy * reach;
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

    int max_hp = cs2d_sv_building_health(sv, o->typ);
    if (o->health < max_hp) {
        if (pl->money <= 49) return;                 /* needs $50 */
        o->health += 15;                             /* Tdo.damage(-15, player, 0) */
        if (o->health != max_hp) o->upgrade = 0;
        if (o->health > max_hp) o->health = max_hp;
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

/* reqcld(id, mode [, param]): FUN_140129a50 / FUN_1401780d0
 * Sends reliable packet [53, mode u8 (, param string if mode 4 or 5)] */
/*
 * A bot answers reqcld immediately (the original does: probe `reqcld(1, mode, "gfx/c4/c4.bmp")`
 * emits clientdata for modes 0..6). Values measured on cs2d_dedicated.exe:
 *   0/1/2 -> 0,0      3 -> 1,0 (advanced light engine on)     4 -> 0, checksum of the file
 *   5 -> false,""     6 -> 0,0                                 missing file -> 0,""
 */
static void sv_bot_answer_reqcld(cs2d_server_t *sv, int id, int mode, const char *param)
{
    int data1 = 0;
    int data2 = 0;
    const char *sdata = NULL;
    char hash[65] = "";

    switch (mode) {
    case 3: data1 = 1; break;
    case 4:
        if (param && param[0]) cs2d_net_sha256_file(param, hash);
        sdata = hash;
        break;
    case 5:
        sdata = "";
        break;
    default:
        break;
    }
    cs2d_sv_lua_hook_clientdata(sv, id, mode, data1, data2, sdata);
}

void cs2d_sv_reqcld(cs2d_server_t *sv, int id, int mode, const char *param)
{
    if (!sv) return;
    if (id > 0 && id <= CS2D_SV_MAX_PLAYERS) {
        cs2d_sv_player_t *pl = &sv->players[id];
        if (pl->used && pl->bot) {
            sv_bot_answer_reqcld(sv, id, mode, param);
            return;
        }
        cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, id);
        if (c) {
            cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
            cs2d_pkt_w_byte(p, 53);
            cs2d_pkt_w_byte(p, (uint8_t)mode);
            if ((mode == 4 || mode == 5) && param && param[0]) {
                cs2d_pkt_w_str(p, param);
            }
        }
    } else if (id == 0) {
        for (int i = 1; i <= CS2D_SV_MAX_PLAYERS; i++) {
            if (!sv->players[i].used || !sv->players[i].joined) continue;
            if (sv->players[i].bot) {
                sv_bot_answer_reqcld(sv, i, mode, param);
                continue;
            }
            cs2d_sv_conn_t *c = cs2d_sv_conn_of_player(sv, i);
            if (c) {
                cs2d_pkt_t *p = cs2d_sv_packet(sv, c, true, false);
                cs2d_pkt_w_byte(p, 53);
                cs2d_pkt_w_byte(p, (uint8_t)mode);
                if ((mode == 4 || mode == 5) && param && param[0]) {
                    cs2d_pkt_w_str(p, param);
                }
            }
        }
    }
}

/* 0x14011ed00: settile on the dedicated server (apply, update collision and broadcast reliable [62]) */
bool cs2d_sv_settile(cs2d_server_t *sv, const cs2d_settile_t *st)
{
    if (!sv || !st || (!sv->map_loaded && !sv->map.tiles)) return false;
    cs2d_customtile_batch_t batch;
    cs2d_customtile_begin(&batch);
    cs2d_customtile_addcheck(&sv->map, &batch, st);
    int bounds[4];
    cs2d_customtile_end(&sv->map, &batch, bounds);

    /* Broadcast reliable [62, x u16, y u16, tile u8, rot u8, r u8, g u8, b u8, overlay u8, blend u8] (FUN_14011ed00) */
    uint8_t pkt[12];
    pkt[0] = 62;
    pkt[1] = (uint8_t)(st->x & 0xff);
    pkt[2] = (uint8_t)((st->x >> 8) & 0xff);
    pkt[3] = (uint8_t)(st->y & 0xff);
    pkt[4] = (uint8_t)((st->y >> 8) & 0xff);
    pkt[5] = st->tile;
    pkt[6] = st->rot;
    pkt[7] = st->r;
    pkt[8] = st->g;
    pkt[9] = st->b;
    pkt[10] = st->overlay;
    pkt[11] = st->blend;

    sv_broadcast_joined(sv, pkt, 12);
    return true;
}
