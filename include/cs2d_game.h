#ifndef CS2D_GAME_H
#define CS2D_GAME_H

#include "cs2d_types.h"

/* Initialize CS2D game state */
void cs2d_game_init(cs2d_game_t *game, cs2d_gamemode_t mode);

/* Load a map into the game state */
bool cs2d_game_load_map(cs2d_game_t *game, const char *map_path);

/* Start a new round: resets timers, spawns alive players, resets bomb */
void cs2d_game_start_round(cs2d_game_t *game);

/* Per-tick game loop update */
void cs2d_game_update(cs2d_game_t *game, float delta_time);

/* Attempt to start planting bomb by a player */
bool cs2d_game_start_plant(cs2d_game_t *game, uint8_t player_id);

/* Tick bomb planting progress (returns true when plant complete) */
bool cs2d_game_tick_plant(cs2d_game_t *game, uint8_t player_id, float delta_time);

/* Attempt to start defusing planted bomb */
bool cs2d_game_start_defuse(cs2d_game_t *game, uint8_t player_id);

/* Tick bomb defuse progress (returns true when defused) */
bool cs2d_game_tick_defuse(cs2d_game_t *game, uint8_t player_id, float delta_time);

/* End the round, award money and update loss streaks */
void cs2d_game_end_round(cs2d_game_t *game, cs2d_team_t winner, const char *reason);

/* Check win conditions (elimination, timer expiration, bomb explosion/defusal) */
void cs2d_game_check_win_conditions(cs2d_game_t *game);

/* Count alive players on a team */
int cs2d_game_count_alive(const cs2d_game_t *game, cs2d_team_t team);

/* Add a player to the server */
bool cs2d_game_add_player(cs2d_game_t *game, uint8_t id, const char *name, cs2d_team_t team);

/* Free resources allocated by game (e.g. map) */
void cs2d_game_free(cs2d_game_t *game);

#endif /* CS2D_GAME_H */

