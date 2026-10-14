#ifndef CS2D_RADAR_H
#define CS2D_RADAR_H

#include <stdbool.h>
#include <stdint.h>
#include "cs2d_types.h"

/*
 * Radar icon queue (TSmartRadar.DrawIcon 0x140010370): world pixel position, frame of
 * gfx/hud_radar_icons.png and the colour packed R<<24 | G<<16 | B<<8 | A. The queue holds at
 * most 49 icons (DrawCount < 0x31); cs2d_draw_line_3 (0x1401262c0) draws them in order.
 */
#define CS2D_RADAR_MAX_BLIPS 49

typedef struct {
    float    x, y;
    int      frame;
    uint32_t rgba;
} cs2d_radar_blip_t;

/* What the icon feeder reads besides the game state (globals of FUN_140291bd0) */
typedef struct {
    const cs2d_ground_item_t *items;      /* Titem.list on the ground */
    int                       item_count;
    bool                      team_game;  /* 0x140a015d0[sv_gamemode] != 0 */
    uint32_t                  ms;         /* g_time_ms / timeGetTime - g_time_base */
} cs2d_radar_env_t;

/* Fills out[] like FUN_140291bd0 (0x140291bd0) for the local player; returns the icon count */
int cs2d_radar_collect(const cs2d_game_t *game, const cs2d_player_t *local, const cs2d_radar_env_t *env,
                       cs2d_radar_blip_t out[CS2D_RADAR_MAX_BLIPS]);

#endif /* CS2D_RADAR_H */
