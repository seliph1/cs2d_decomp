#include "cs2d_game.h"
#include <stdlib.h>
#include "cs2d_weapons.h"
#include "cs2d_player.h"
#include "cs2d_map.h"
#include <stdio.h>
#include <string.h>

void cs2d_game_init(cs2d_game_t *game, cs2d_gamemode_t mode)
{
    if (!game) return;
    memset(game, 0, sizeof(*game));

    game->gamemode = mode;
    game->round_state = CS2D_ROUND_WAITING;
    game->cfg_roundtime = 120.0f;
    game->cfg_freezetime = 3.0f;
    game->cfg_c4timer = 35.0f;
    game->cfg_startmoney = 800;
    game->cfg_maxmoney = 16000;
    /* Player slots stay empty (id 0) until cs2d_game_add_player, like Tpl.list */
}

bool cs2d_game_load_map(cs2d_game_t *game, const char *map_path)
{
    if (!game || !map_path) return false;
    cs2d_map_free(&game->map);
    if (!cs2d_map_load_file(&game->map, map_path)) return false;

    /* Map mode detection reconstructed from FUN_140113d50 (lines 511-544) */
    bool has_vip = false, has_escape = false;
    bool has_hostage = false, has_rescue = false;
    bool has_bombspot = false, has_ctf = false, has_dom = false;

    for (uint32_t i = 0; i < game->map.entity_count; i++) {
        switch (game->map.entities[i].type) {
            case CS2D_ENT_INFO_VIP:       has_vip = true; break;
            case CS2D_ENT_INFO_ESCAPE:    has_escape = true; break;
            case CS2D_ENT_INFO_HOSTAGE:   has_hostage = true; break;
            case CS2D_ENT_INFO_RESCUE:    has_rescue = true; break;
            case CS2D_ENT_INFO_BOMBSPOT:  has_bombspot = true; break;
            case CS2D_ENT_INFO_CTF_FLAG:  has_ctf = true; break;
            case CS2D_ENT_INFO_DOM_POINT: has_dom = true; break;
            default: break;
        }
    }

    if (has_vip && has_escape)         game->gamemode = CS2D_MODE_AS;
    else if (has_hostage && has_rescue) game->gamemode = CS2D_MODE_CS;
    else if (has_bombspot)             game->gamemode = CS2D_MODE_DE;
    else if (has_ctf)                  game->gamemode = CS2D_MODE_CTF;
    else if (has_dom)                  game->gamemode = CS2D_MODE_DOM;

    return true;
}

bool cs2d_game_add_player(cs2d_game_t *game, uint8_t id, const char *name, cs2d_team_t team)
{
    if (!game || id < 1 || id > CS2D_MAX_PLAYERS) return false;
    cs2d_player_init(&game->players[id], id, name);
    game->players[id].team = team;
    return true;
}

int cs2d_game_count_alive(const cs2d_game_t *game, cs2d_team_t team)
{
    if (!game) return 0;
    int count = 0;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        if (game->players[i].team == team && game->players[i].alive) {
            count++;
        }
    }
    return count;
}

static cs2d_player_t *game_player(cs2d_game_t *game, int id)
{
    return (id >= 1 && id <= CS2D_MAX_PLAYERS) ? &game->players[id] : NULL;
}

/*
 * FUN_1401eaf60 (client path, DAT_140ddf400 == 0): the victim dies and gets a death unless the weapon is 0xfc.
 * A kill between T and CT/VIP gives the killer +1 (FUN_1401cd430; Zombies gives a zombie killer
 * DAT_140a14d7c, not reconstructed) and restarts the assistant's flash; its assist count comes with [42, pid, 9].
 * Otherwise FUN_1401e2d00: no self-kill, weapon not 0xff nor 72, 73, 76, 77, 87 (mask 0x8033 from 72): a team kill
 * costs a point in a team game, a kill without teams scores +1. The client's +300 / -3300 money is not applied:
 * the server sends the money with [42].
 */
void cs2d_game_apply_kill(cs2d_game_t *game, int victim, int killer, int assist, int weapon, bool teams)
{
    cs2d_player_t *v = game ? game_player(game, victim) : NULL;
    if (!v) return;
    v->health = 0;
    v->alive  = false;
    if (weapon != 0xfc) v->deaths++;

    cs2d_player_t *k = game_player(game, killer);
    if (!k) return;
    bool enemies = (v->team == CS2D_TEAM_T && k->team >= CS2D_TEAM_CT) ||
                   (v->team >= CS2D_TEAM_CT && k->team == CS2D_TEAM_T);
    if (enemies) {
        k->score++;
        k->score_inc_time = 0.0f;
        cs2d_player_t *a = (assist > 0) ? game_player(game, assist) : NULL;
        if (a) a->assists_inc_time = 0.0f;
        return;
    }
    unsigned w = (unsigned)weapon - 72u;
    if (k == v || weapon == 0xff || (w < 16 && ((0x8033u >> w) & 1))) return;
    k->score += teams ? -1 : 1;
    k->score_inc_time = 0.0f;
}

/*
 * FUN_1402520b0 (client path): the first byte of [22] is the round end reason. Bits of 1 << reason:
 * 0x1004010040101000 (12, 20, 30, 40, 50, 60) Terrorists win; 0x804 (2, 11), 21, 22, 0x2008020080000000
 * (31, 41, 51, 61) Counter-Terrorists win; 4 and 5 reset both team scores and every player's score, assists,
 * deaths and MVPs; 1, 3, 10 and the rest change nothing.
 */
void cs2d_game_apply_round_reason(cs2d_game_t *game, int reason)
{
    if (!game || reason < 0 || reason > 61) return;
    uint64_t bit = 1ULL << reason;
    if (bit & 0x1004010040101000ULL) {
        game->score_t++;
    } else if ((bit & 0x2008020080000804ULL) || reason == 21 || reason == 22) {
        game->score_ct++;
    } else if (bit & 0x30ULL) {
        game->score_t = game->score_ct = 0;
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            cs2d_player_t *p = &game->players[i];
            p->score = p->assists = p->deaths = p->mvp = 0;
        }
    }
}

void cs2d_game_start_round(cs2d_game_t *game)
{
    if (!game) return;

    game->round_state = CS2D_ROUND_FREEZETIME;
    game->round_number++;
    game->freeze_time = game->cfg_freezetime;
    game->round_time = game->cfg_roundtime;
    game->bomb_planted = false;
    game->c4_time = 0.0f;
    game->bomb_planter = 0;

    /* Respawn all players on a random free Info_T / Info_CT of their team (the dedicated server
     * picks spawn points at random as well) */
    bool used_t[64] = { false }, used_ct[64] = { false };
    size_t count_t  = cs2d_map_count_spawns(&game->map, CS2D_TEAM_T);
    size_t count_ct = cs2d_map_count_spawns(&game->map, CS2D_TEAM_CT);
    if (count_t > 64) count_t = 64;
    if (count_ct > 64) count_ct = 64;

    uint8_t first_t_id = 0;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        cs2d_player_t *pl = &game->players[i];
        if (pl->team != CS2D_TEAM_T && pl->team != CS2D_TEAM_CT) continue;
        bool   is_t  = (pl->team == CS2D_TEAM_T);
        bool  *used  = is_t ? used_t : used_ct;
        size_t count = is_t ? count_t : count_ct;

        cs2d_vec2_t spawn_pos = { is_t ? 100.0f : 500.0f, is_t ? 100.0f : 500.0f };
        if (count > 0) {
            size_t pick = (size_t)rand() % count;
            for (size_t k = 0; k < count && used[pick]; k++) pick = (pick + 1) % count; /* next free one */
            used[pick] = true;
            cs2d_map_get_spawn(&game->map, pl->team, pick, &spawn_pos);
        }
        cs2d_player_spawn(pl, pl->team, spawn_pos);
        if (is_t && first_t_id == 0) first_t_id = (uint8_t)i;
    }

    /* In Bomb Defuse mode, give C4 to the first Terrorist */
    if (game->gamemode == CS2D_MODE_DE && first_t_id != 0) {
        cs2d_player_give_weapon(&game->players[first_t_id], CS2D_WEP_BOMB);
    }
}

bool cs2d_game_start_plant(cs2d_game_t *game, uint8_t player_id)
{
    if (!game || player_id < 1 || player_id > CS2D_MAX_PLAYERS) return false;
    cs2d_player_t *pl = &game->players[player_id];

    if (!pl->alive || pl->team != CS2D_TEAM_T) return false;
    if (game->round_state != CS2D_ROUND_PLAYING || game->bomb_planted) return false;
    if (pl->inventory[CS2D_SLOT_BOMB] != CS2D_WEP_BOMB) return false;

    if (!cs2d_map_is_in_bombspot(&game->map, pl->pos.x, pl->pos.y)) return false;

    pl->is_planting = true;
    pl->plant_progress = 0.0f;
    return true;
}

bool cs2d_game_tick_plant(cs2d_game_t *game, uint8_t player_id, float delta_time)
{
    if (!game || player_id < 1 || player_id > CS2D_MAX_PLAYERS) return false;
    cs2d_player_t *pl = &game->players[player_id];

    if (!pl->alive || !pl->is_planting) return false;

    pl->plant_progress += delta_time;
    if (pl->plant_progress >= 3.0f) { /* Plant takes 3 seconds */
        pl->is_planting = false;
        pl->plant_progress = 0.0f;
        pl->inventory[CS2D_SLOT_BOMB] = CS2D_WEP_NONE; /* Consume bomb */

        game->bomb_planted = true;
        game->bomb_pos = pl->pos;
        game->bomb_planter = player_id;
        game->c4_time = game->cfg_c4timer;

        /* $300 bonus to planter, $800 to team */
        pl->money += 300;
        if (pl->money > game->cfg_maxmoney) pl->money = game->cfg_maxmoney;

        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            if (game->players[i].team == CS2D_TEAM_T) {
                game->players[i].money += 800;
                if (game->players[i].money > game->cfg_maxmoney) {
                    game->players[i].money = game->cfg_maxmoney;
                }
            }
        }
        return true;
    }
    return false;
}

bool cs2d_game_start_defuse(cs2d_game_t *game, uint8_t player_id)
{
    if (!game || player_id < 1 || player_id > CS2D_MAX_PLAYERS) return false;
    cs2d_player_t *pl = &game->players[player_id];

    if (!pl->alive || pl->team != CS2D_TEAM_CT) return false;
    if (game->round_state != CS2D_ROUND_PLAYING || !game->bomb_planted) return false;

    /* Distance to bomb must be <= 64 pixels */
    float dx = pl->pos.x - game->bomb_pos.x;
    float dy = pl->pos.y - game->bomb_pos.y;
    if ((dx * dx + dy * dy) > (64.0f * 64.0f)) return false;

    pl->is_defusing = true;
    pl->defuse_progress = 0.0f;
    return true;
}

bool cs2d_game_tick_defuse(cs2d_game_t *game, uint8_t player_id, float delta_time)
{
    if (!game || player_id < 1 || player_id > CS2D_MAX_PLAYERS) return false;
    cs2d_player_t *pl = &game->players[player_id];

    if (!pl->alive || !pl->is_defusing) return false;

    float req_time = pl->has_defuse_kit ? 5.0f : 10.0f;
    pl->defuse_progress += delta_time;

    if (pl->defuse_progress >= req_time) {
        pl->is_defusing = false;
        pl->defuse_progress = 0.0f;
        game->bomb_planted = false;

        /* $300 bonus to defuser */
        pl->money += 300;
        if (pl->money > game->cfg_maxmoney) pl->money = game->cfg_maxmoney;

        cs2d_game_end_round(game, CS2D_TEAM_CT, "Bomb has been defused");
        return true;
    }
    return false;
}

void cs2d_game_end_round(cs2d_game_t *game, cs2d_team_t winner, const char *reason)
{
    (void)reason;
    if (!game || game->round_state == CS2D_ROUND_ENDED) return;

    game->round_state = CS2D_ROUND_ENDED;
    game->state_timer = 3.0f; /* Round end intermission: 3 s, measured on cs2d_dedicated 1.0.1.8 (Func_GameAction win) */

    if (winner == CS2D_TEAM_CT) {
        game->score_ct++;
        game->loss_streak_ct = 0;
        game->loss_streak_t++;
    } else if (winner == CS2D_TEAM_T) {
        game->score_t++;
        game->loss_streak_t = 0;
        game->loss_streak_ct++;
    }

    /* Calculate awards */
    int win_reward = 3250;
    int loss_streak = (winner == CS2D_TEAM_CT) ? game->loss_streak_t : game->loss_streak_ct;
    int loss_reward = 1400 + (loss_streak - 1) * 500;
    if (loss_reward > 3400) loss_reward = 3400;
    if (loss_reward < 1400) loss_reward = 1400;

    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        cs2d_player_t *pl = &game->players[i];
        if (pl->team == CS2D_TEAM_SPEC) continue;

        if (pl->team == winner) {
            pl->money += win_reward;
        } else {
            pl->money += loss_reward;
        }
        if (pl->money > game->cfg_maxmoney) pl->money = game->cfg_maxmoney;
    }
}

void cs2d_game_check_win_conditions(cs2d_game_t *game)
{
    if (!game || game->round_state != CS2D_ROUND_PLAYING) return;

    int alive_t = cs2d_game_count_alive(game, CS2D_TEAM_T);
    int alive_ct = cs2d_game_count_alive(game, CS2D_TEAM_CT);

    /* Bomb mode rules */
    if (game->gamemode == CS2D_MODE_DE) {
        if (game->bomb_planted) {
            if (alive_ct == 0) {
                cs2d_game_end_round(game, CS2D_TEAM_T, "Terrorists Win - Counter-Terrorists eliminated");
                return;
            }
            /* If all Ts dead, CTs still have to defuse */
        } else {
            if (alive_t == 0) {
                cs2d_game_end_round(game, CS2D_TEAM_CT, "Counter-Terrorists Win - Terrorists eliminated");
                return;
            }
            if (alive_ct == 0) {
                cs2d_game_end_round(game, CS2D_TEAM_T, "Terrorists Win - Counter-Terrorists eliminated");
                return;
            }
            if (game->round_time <= 0.0f) {
                cs2d_game_end_round(game, CS2D_TEAM_CT, "Counter-Terrorists Win - Target protected");
                return;
            }
        }
    } else {
        /* Standard deathmatch/elimination */
        if (alive_t == 0) {
            cs2d_game_end_round(game, CS2D_TEAM_CT, "Counter-Terrorists Win");
            return;
        }
        if (alive_ct == 0) {
            cs2d_game_end_round(game, CS2D_TEAM_T, "Terrorists Win");
            return;
        }
        if (game->round_time <= 0.0f) {
            cs2d_game_end_round(game, CS2D_TEAM_CT, "Round Draw / Time Expired");
            return;
        }
    }
}

void cs2d_game_update(cs2d_game_t *game, float delta_time)
{
    if (!game) return;

    /* Update all players */
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        if (game->players[i].alive) {
            cs2d_player_update(&game->players[i], delta_time);
        }
    }

    /* State machine */
    switch (game->round_state) {
        case CS2D_ROUND_FREEZETIME:
            game->freeze_time -= delta_time;
            if (game->freeze_time <= 0.0f) {
                game->round_state = CS2D_ROUND_PLAYING;
            }
            break;

        case CS2D_ROUND_PLAYING:
            game->round_time -= delta_time;

            if (game->bomb_planted) {
                game->c4_time -= delta_time;
                if (game->c4_time <= 0.0f) {
                    game->c4_time = 0.0f;
                    game->bomb_planted = false;
                    cs2d_game_end_round(game, CS2D_TEAM_T, "Target Bombed! Terrorists Win");
                    return;
                }
            }

            cs2d_game_check_win_conditions(game);
            break;

        case CS2D_ROUND_ENDED:
            game->state_timer -= delta_time;
            if (game->state_timer <= 0.0f) {
                cs2d_game_start_round(game);
            }
            break;

        case CS2D_ROUND_WAITING:
        default:
            break;
    }
}

void cs2d_game_free(cs2d_game_t *game)
{
    if (!game) return;
    cs2d_map_free(&game->map);
}
