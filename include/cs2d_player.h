#ifndef CS2D_PLAYER_H
#define CS2D_PLAYER_H

#include "cs2d_types.h"

/* Initialize a player slot */
void cs2d_player_init(cs2d_player_t *player, uint8_t id, const char *name);

/* Spawn player into a team at specified location */
void cs2d_player_spawn(cs2d_player_t *player, cs2d_team_t team, cs2d_vec2_t spawn_pos);

/* Per-tick update (handles timers, reload completion) */
void cs2d_player_update(cs2d_player_t *player, float delta_time);

/* Apply damage to player: returns true if fatal */
bool cs2d_player_apply_damage(cs2d_player_t *player, int hp_dmg, int armor_dmg);

/* Give a weapon to player: equips it and sets default clip & reserve */
bool cs2d_player_give_weapon(cs2d_player_t *player, uint8_t weapon_id);

/* Purchase a weapon: checks money and gives weapon */
bool cs2d_player_buy_weapon(cs2d_player_t *player, uint8_t weapon_id);

/* Switch current active weapon to inventory slot */
bool cs2d_player_switch_weapon(cs2d_player_t *player, cs2d_weapon_slot_t slot);

/* Drop active weapon from current slot */
uint8_t cs2d_player_drop_weapon(cs2d_player_t *player);

/* Check map collision and move player */
bool cs2d_player_move(cs2d_player_t *player, const cs2d_map_t *map, float dx, float dy);

#endif /* CS2D_PLAYER_H */

