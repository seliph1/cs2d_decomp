#include "cs2d_radar.h"
#include <math.h>

/*
 * Radar icon feeder, port of FUN_140291bd0 (0x140291bd0, docs/dump/cs2d/_global/140290000.c).
 * Field names from the BlitzMax debug scopes (tools/agent/bbscope.py): Te.typ/x/y/state (+0x10/+0x14/+0x18/+0x28),
 * Tpl.team/x/y/health/bomb/flag/radarblink (+0x230/+0x240/+0x244/+0x260/+0x2f4/+0x2f8/+0x98), Titem.player/x/y.
 *
 * Not reconstructed (see OPEN_ISSUES "Radar"):
 *   - hostages (Thostage.list, frame 3, 0xffff00ff, CT side only): our client keeps no hostage objects;
 *   - Tpl.radarblink (red pulse) is never set by us, and DAT_140ddf4b4 (yellow "lag" icon for players whose
 *     net_lastmsg < 4000 ms, red planted bomb, flags hidden) is assumed 0;
 *   - DAT_140ddf078 / DAT_140ddf074 (> 0 gate the bomb / flag items) are assumed to be the Info_BombSpot /
 *     Info_CTF_Flag counts.
 */

#define DEG2RAD 0.017453292519943295

static int push(cs2d_radar_blip_t *out, int n, float x, float y, int frame, uint32_t rgba)
{
    if (n >= CS2D_RADAR_MAX_BLIPS) return n;
    out[n].x = x;
    out[n].y = y;
    out[n].frame = frame;
    out[n].rgba = rgba;
    return n + 1;
}

static bool carries(const cs2d_player_t *p, int item)
{
    return item >= 0 && item < CS2D_MAX_WEAPONS && p->owned[item] != 0;
}

int cs2d_radar_collect(const cs2d_game_t *game, const cs2d_player_t *local, const cs2d_radar_env_t *env,
                       cs2d_radar_blip_t out[CS2D_RADAR_MAX_BLIPS])
{
    int n = 0;
    if (!game || !local || !env) return 0;
    const cs2d_map_t *map = &game->map;
    int my_team = (int)local->team;
    int bombspots = 0, flags = 0;

    /* 1. Te.list: Dom_Point, CTF flag bases and Info_RadarIcon at the tile center */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        float x = (float)(e->x * 32 + 16), y = (float)(e->y * 32 + 16);
        int state = map->entity_state[i];
        int i0 = e->int_settings[0];
        switch (e->type) {
            case CS2D_ENT_INFO_BOMBSPOT: bombspots++; break;
            case CS2D_ENT_INFO_DOM_POINT:
                /* owner 1 red, 2 blue, anything else yellow */
                n = push(out, n, x, y, 2, i0 == 1 ? 0xff0000ffu : i0 == 2 ? 0x0000ffffu : 0xffff00ffu);
                break;
            case CS2D_ENT_INFO_CTF_FLAG:
                flags++;
                /* only while the flag is home (state 0): team 0 red, else blue */
                if (state == 0) n = push(out, n, x, y, 2, i0 != 0 ? 0x0000ffffu : 0xff0000ffu);
                break;
            case CS2D_ENT_INFO_RADARICON:
                /* ints[0] frame (0..23), ints[1..3] RGB, opaque; hidden while state != 0 */
                if (state == 0 && i0 >= 0 && i0 < 0x18) {
                    uint32_t c = ((uint32_t)(e->int_settings[1] & 0xff) << 24) |
                                 ((uint32_t)(e->int_settings[2] & 0xff) << 16) |
                                 ((uint32_t)(e->int_settings[3] & 0xff) << 8) | 0xffu;
                    n = push(out, n, x, y, i0, c);
                }
                break;
            default: break;
        }
    }

    /* 2. Tpl.list: visible when on my side (CT and VIP are one side), carrying a flag, or outside team games */
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &game->players[i];
        if (p->id == 0 || !p->alive || p->health <= 0 || p->team == CS2D_TEAM_SPEC) continue;
        int t = (int)p->team;
        bool flag = carries(p, CS2D_WEP_REDFLAG) || carries(p, CS2D_WEP_BLUEFLAG);
        if (!(t == my_team || (t > 1 && my_team > 1) || flag || !env->team_game)) continue;
        float x = (float)(int)p->pos.x, y = (float)(int)p->pos.y;
        uint32_t c;
        if (flag) {
            /* the carrier shows the colour of the flag it took */
            c = t == CS2D_TEAM_T ? 0x9696ffffu : 0xff9696ffu;
        } else if (p->id == local->id) {
            /* green, alpha pulsing with |sin(ms / 3)| */
            double s = sin((double)(env->ms / 3) * DEG2RAD);
            c = 0x00ff0000u | (uint32_t)(int)(fabs(s) * 205.0 + 50.0);
        } else if (carries(p, CS2D_WEP_BOMB) || p->is_vip || t == CS2D_TEAM_VIP) {
            c = 0xff8000ffu; /* orange (radarblink > 0 would pulse red) */
        } else {
            c = 0xc8c8ffffu;
        }
        n = push(out, n, x, y, 0, c);
    }

    /* 3. Terrorists see the bomb: the first one on the ground, or the planted one */
    if (bombspots > 0 && my_team == CS2D_TEAM_T) {
        int want = game->bomb_planted ? 63 : CS2D_WEP_BOMB; /* 63: planted C4 item */
        uint32_t c = game->bomb_planted ? 0xffff00ffu : 0xffc800ffu;
        for (int i = 0; i < env->item_count; i++) {
            const cs2d_ground_item_t *it = &env->items[i];
            if (!it->active || it->type != want) continue;
            n = push(out, n, it->x, it->y, 1, c);
            break;
        }
    }

    /* 4. Dropped flags (items 70 red / 71 blue) for everybody */
    if (flags > 0) {
        for (int i = 0; i < env->item_count; i++) {
            const cs2d_ground_item_t *it = &env->items[i];
            if (!it->active) continue;
            if (it->type == CS2D_WEP_REDFLAG)       n = push(out, n, it->x, it->y, 2, 0xff0000ffu);
            else if (it->type == CS2D_WEP_BLUEFLAG) n = push(out, n, it->x, it->y, 2, 0x0000ffffu);
        }
    }
    return n;
}
