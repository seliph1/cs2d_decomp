#ifndef CS2D_SCOREBOARD_H
#define CS2D_SCOREBOARD_H

#include "cs2d_types.h"

/*
 * Scoreboard (Thud_scoreboard, debug scope 0x1409df4e0), shown while "showscores" (bind "tab") is held:
 * DAT_140ddf480, drawn after the chat with Thud_scoreboard.draw(1) (0x140251220).
 */

/* Sizes of Thud_scoreboard.draw for a screen; the shipped settings (no size setting, 0x140ddf1e0 = 0) */
typedef struct {
    int   rx, ry, rw, rh;      /* Window: screen minus 10 px on every side */
    float scale;               /* HUD font height / 13, 1.1538461 below 15 */
    int   font_h, font_h_small;
    int   value_w;             /* Width of a value column (scale * 42) */
    int   team_h, team_pad;    /* Team header height, gap below it */
    int   slot_h;              /* Player row height */
    float icon;                /* Status icon size (slot_h - 4) */
    int   half_w;              /* Width of a column of players */
    int   slots;               /* Rows that fit in a column (calculatePossibleSlotCount) */
} cs2d_scoreboard_layout_t;

void cs2d_scoreboard_layout(int screen_w, int screen_h, cs2d_scoreboard_layout_t *out);

/* Tpl.sortval: score * 500000 + assists * 5000 + mvp * 500 - deaths */
int cs2d_scoreboard_sortval(const cs2d_player_t *p);

/* Players in a team (team > 0) by descending sortval; ties keep the list order. Returns the count. */
int cs2d_scoreboard_order(const cs2d_player_t *players, int *out_ids);

/* Advances the flash timers of every player by one frame of delta_time seconds (Tpl.updateAll, 60 Hz units) */
void cs2d_scoreboard_tick(cs2d_player_t *players, float delta_time);

typedef struct {
    const cs2d_player_t *players;  /* 1-indexed, CS2D_MAX_PLAYERS + 1 */
    int         local_id;
    bool        teams;             /* 0x140a015d0[sv_gamemode] == 1 */
    bool        zombies;           /* sv_gamemode 4: "Survivors" / "Zombies" in green */
    bool        host;              /* DAT_140ddf400: offline game (the local player is the "Server") */
    const char *server_name;
    const char *map_name;
    int         score_t, score_ct; /* DAT_140ddf450 / DAT_140ddf44c */
    int         ticks;             /* Server ticks from [247] (DAT_140ddf3e0), < 0 = unknown */
    int         spec_mode, spec_target;
    float       mouse_x, mouse_y;
} cs2d_scoreboard_view_t;

void cs2d_scoreboard_render(const cs2d_scoreboard_view_t *v, int screen_w, int screen_h);

#endif /* CS2D_SCOREBOARD_H */
