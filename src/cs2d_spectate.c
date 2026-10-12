#include "cs2d_spectate.h"
#include <math.h>
#include <string.h>

void cs2d_spec_init(cs2d_spec_t *spec, int server_mode)
{
    memset(spec, 0, sizeof(*spec));
    spec->server_mode = server_mode;
}

/* Candidate filter of FUN_1401698a0: with sv_specmode 2 a team member watches only the own team
 * (CT and VIP count as one side); a Spectator (team 0) watches anybody */
bool cs2d_spec_can_follow(const cs2d_spec_t *spec, int local_team, int team)
{
    if (team < 1) return false;
    if (spec->server_mode != 2 || local_team == 0) return true;
    return local_team == team || (local_team > 1 && team > 1);
}

static const cs2d_player_t *spec_player(const cs2d_game_t *game, int id)
{
    if (id < 1 || id > CS2D_MAX_PLAYERS || game->players[id].id == 0) return NULL;
    return &game->players[id];
}

bool cs2d_spec_pick(cs2d_spec_t *spec, const cs2d_game_t *game, int local_id, int dir)
{
    if (spec->server_mode == 0) return false;
    const cs2d_player_t *local = spec_player(game, local_id);
    int local_team = local ? (int)local->team : 0;

    /* Keep the current target while it is still valid (dir 0) */
    const cs2d_player_t *cur = spec_player(game, spec->target);
    if (cur) {
        if (!cur->alive || cur->team < 1) {
            /* Own Team only keeps watching a dead target until the player switches */
            if (spec->server_mode == 2 && dir == 0) return false;
        } else if (spec->server_mode == 2 && local) {
            if (local_team == 0 && dir == 0) return false;
            bool own = (local_team == 1) ? (cur->team == 1) : (local_team > 1 && cur->team > 1);
            if (own && dir == 0) return false;
        } else if (dir == 0) {
            return false;
        }
    }

    /* Living players that may be followed, in id order (Tpl.list) */
    int list[CS2D_MAX_PLAYERS];
    int n = 0, idx = -1;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = spec_player(game, i);
        if (!p || !p->alive || !cs2d_spec_can_follow(spec, local_team, (int)p->team)) continue;
        if (i == spec->target) idx = n;
        list[n++] = i;
    }
    if (n == 0) {
        spec->target = 0;
        return false;
    }

    if (idx == -1)     spec->target = (dir < 0) ? list[n - 1] : list[0];
    else if (dir < 0)  spec->target = (idx > 1) ? list[idx - 1] : list[n - 1];
    else               spec->target = (idx + 1 < n) ? list[idx + 1] : list[0];
    return true;
}

void cs2d_spec_free_look(int *cam_x, int *cam_y, bool left, bool right, bool up, bool down, bool walk,
                         float frames)
{
    float f = 10.0f * frames; /* 0x140bd6d88 = 10.0 */
    if (walk) f *= 0.5f;      /* 0x140bd69e8 = 0.5 */
    *cam_x = (int)((float)(int)((float)*cam_x - (float)left * f) + (float)right * f);
    *cam_y = (int)((float)(int)((float)*cam_y - (float)up * f) + (float)down * f);
}

void cs2d_spec_follow(int *cam_x, int *cam_y, float px, float py, float half_w, float half_h, float frames)
{
    const float max_delta = 32.0f; /* Tmap_scroll.maxDelta (0x140dde1d0), set at 0x140307f2c */
    const float factor    = 0.3f;  /* Tmap_scroll.smoothScrollFactor (0x140dde1cc) */
    float tx = px - half_w, ty = py - half_h;
    float dx = tx - (float)*cam_x, dy = ty - (float)*cam_y;
    if (fabsf(dx) <= max_delta && fabsf(dy) <= max_delta) {
        tx = dx * factor * frames + (float)*cam_x;
        ty = dy * factor * frames + (float)*cam_y;
    }
    *cam_x = (int)tx;
    *cam_y = (int)ty;
}
