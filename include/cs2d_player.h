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

/* Empties the inventory (no weapon held); the server's spawn packet lists what the player really has */
void cs2d_player_clear_inventory(cs2d_player_t *player);

/* Removes weapon_id (and its ammo); if it was held, the best weapon left is taken. False if not owned. */
bool cs2d_player_strip_weapon(cs2d_player_t *player, uint8_t weapon_id);

/* Holds weapon_id if the player owns it */
bool cs2d_player_select_weapon(cs2d_player_t *player, uint8_t weapon_id);

/*
 * Weapon selection HUD (invnext / invprev / slot1..slot10, CS2D.exe). The HUD lists the owned items of one slot
 * (cs2d_weapon_hud_slot), ordered by cs2d_weapon_hud_order then inventory order (FUN_14018c580).
 */
typedef struct cs2d_invsel_s {
    int   slot;                     /* Open slot 1..9, 0 = closed (DAT_140ddf338) */
    int   index;                    /* Highlighted item in the slot (DAT_140ddf334) */
    float rot[CS2D_MAX_WEAPONS];    /* Icon rotation of each item while its slot is open (Titem +0x28) */
} cs2d_invsel_t;

/* Owned items in HUD slot `slot` (Titem.countatplayer) */
int cs2d_invsel_count(const cs2d_player_t *player, int slot);
/* index-th item of the slot in HUD order, CS2D_WEP_NONE past the end (FUN_14018c580) */
uint8_t cs2d_invsel_item(const cs2d_player_t *player, int slot, int index);
/* invnext (dir 1) / invprev (dir -1): opens the HUD on the held weapon, then steps (FUN_140166fb0) */
void cs2d_invsel_cycle(cs2d_invsel_t *sel, const cs2d_player_t *player, int dir);
/* slotN: with hud_fastswitch (or force) and one item in the slot, returns it to hold and closes the HUD; else opens
 * the slot or steps to its next item and returns CS2D_WEP_NONE (FUN_1401cc180). slot10 is slot 0. */
uint8_t cs2d_invsel_slot(cs2d_invsel_t *sel, const cs2d_player_t *player, int slot, bool fastswitch);
/* attack / attack2 while the HUD is open: the highlighted item (or CS2D_WEP_NONE), and the HUD closes (FUN_140219820) */
uint8_t cs2d_invsel_confirm(cs2d_invsel_t *sel, const cs2d_player_t *player);

/* Check map collision and move player */
bool cs2d_player_move(cs2d_player_t *player, const cs2d_map_t *map, float dx, float dy);

#endif /* CS2D_PLAYER_H */

