#include "cs2d_player.h"
#include "cs2d_weapons.h"
#include "cs2d_map.h"
#include <string.h>
#include <stdio.h>

void cs2d_player_init(cs2d_player_t *player, uint8_t id, const char *name)
{
    if (!player) return;
    memset(player, 0, sizeof(*player));
    player->id = id;
    if (name) {
        strncpy(player->name, name, CS2D_MAX_NAME_LEN - 1);
        player->name[CS2D_MAX_NAME_LEN - 1] = '\0';
    } else {
        snprintf(player->name, CS2D_MAX_NAME_LEN, "Player_%u", id);
    }
    player->team = CS2D_TEAM_SPEC;
    player->money = 800;
    player->alive = false;
}

void cs2d_player_spawn(cs2d_player_t *player, cs2d_team_t team, cs2d_vec2_t spawn_pos)
{
    if (!player) return;

    player->team = team;
    player->pos = spawn_pos;
    player->vel.x = 0.0f;
    player->vel.y = 0.0f;
    player->angle = 0.0f;
    player->health = 100;
    player->armor = 0;
    player->has_helmet = false;
    player->has_defuse_kit = false;
    player->is_vip = false;
    player->alive = true;
    player->is_planting = false;
    player->is_defusing = false;
    player->plant_progress = 0.0f;
    player->defuse_progress = 0.0f;
    player->fire_cooldown = 0.0f;
    player->reload_timer = 0.0f;

    /* Clear inventory */
    memset(player->inventory, 0, sizeof(player->inventory));

    /* Always give knife */
    cs2d_player_give_weapon(player, CS2D_WEP_KNIFE);

    /* Team default sidearms: CT = USP, T = Glock */
    if (team == CS2D_TEAM_CT) {
        cs2d_player_give_weapon(player, CS2D_WEP_USP);
        cs2d_player_switch_weapon(player, CS2D_SLOT_SECONDARY);
    } else if (team == CS2D_TEAM_T) {
        cs2d_player_give_weapon(player, CS2D_WEP_GLOCK);
        cs2d_player_switch_weapon(player, CS2D_SLOT_SECONDARY);
    } else {
        cs2d_player_switch_weapon(player, CS2D_SLOT_MELEE);
    }
}

void cs2d_player_update(cs2d_player_t *player, float delta_time)
{
    if (!player || !player->alive) return;

    if (player->fire_cooldown > 0.0f) {
        player->fire_cooldown -= delta_time;
        if (player->fire_cooldown < 0.0f) player->fire_cooldown = 0.0f;
    }

    if (player->reload_timer > 0.0f) {
        player->reload_timer -= delta_time;
        if (player->reload_timer <= 0.0f) {
            player->reload_timer = 0.0f;
            cs2d_weapon_finish_reload(player);
        }
    }
}

bool cs2d_player_apply_damage(cs2d_player_t *player, int hp_dmg, int armor_dmg)
{
    if (!player || !player->alive) return false;

    player->armor -= armor_dmg;
    if (player->armor < 0) player->armor = 0;

    player->health -= hp_dmg;
    if (player->health <= 0) {
        player->health = 0;
        player->alive = false;
        player->deaths++;
        return true; /* Fatal */
    }

    return false;
}

bool cs2d_player_give_weapon(cs2d_player_t *player, uint8_t weapon_id)
{
    if (!player || weapon_id == CS2D_WEP_NONE) return false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(weapon_id);
    if (!wep) return false;

    /* Items like Kevlar and Defuse Kit */
    if (wep->slot == CS2D_SLOT_ITEM) {
        if (weapon_id == CS2D_WEP_KEVLAR) {
            player->armor = 100;
        } else if (weapon_id == CS2D_WEP_KEVLARHELM) {
            player->armor = 100;
            player->has_helmet = true;
        } else if (weapon_id == CS2D_WEP_DEFUSEKIT) {
            player->has_defuse_kit = true;
        }
        return true;
    }

    /* Assign to inventory slot */
    player->inventory[wep->slot] = weapon_id;
    player->clip_ammo[weapon_id] = (int16_t)wep->clip_size;
    player->reserve_ammo[weapon_id] = (int16_t)wep->max_reserve;

    /* Set as active weapon */
    player->current_weapon = weapon_id;
    return true;
}

bool cs2d_player_buy_weapon(cs2d_player_t *player, uint8_t weapon_id)
{
    if (!player || !player->alive) return false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(weapon_id);
    if (!wep) return false;

    if (player->money < wep->price) return false;

    if (!cs2d_player_give_weapon(player, weapon_id)) return false;

    player->money -= wep->price;
    return true;
}

bool cs2d_player_switch_weapon(cs2d_player_t *player, cs2d_weapon_slot_t slot)
{
    if (!player || !player->alive) return false;
    if (slot >= CS2D_SLOT_COUNT) return false;

    uint8_t wep_id = player->inventory[slot];
    if (wep_id == CS2D_WEP_NONE) return false;

    player->current_weapon = wep_id;
    player->reload_timer = 0.0f; /* Interrupt reload on switch */
    return true;
}

uint8_t cs2d_player_drop_weapon(cs2d_player_t *player)
{
    if (!player || !player->alive) return CS2D_WEP_NONE;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep || wep->slot == CS2D_SLOT_MELEE) return CS2D_WEP_NONE;

    uint8_t dropped = player->current_weapon;
    player->inventory[wep->slot] = CS2D_WEP_NONE;

    /* Switch to knife or secondary */
    if (player->inventory[CS2D_SLOT_SECONDARY] != CS2D_WEP_NONE) {
        player->current_weapon = player->inventory[CS2D_SLOT_SECONDARY];
    } else {
        player->current_weapon = player->inventory[CS2D_SLOT_MELEE];
    }

    return dropped;
}

bool cs2d_player_move(cs2d_player_t *player, const cs2d_map_t *map, float dx, float dy)
{
    if (!player || !player->alive) return false;

    float new_x = player->pos.x + dx;
    float new_y = player->pos.y + dy;

    /* If map collision check is provided */
    if (map && map->tiles) {
        int radius = 10; /* player collision radius in pixels */
        if (cs2d_map_is_solid(map, new_x - radius, new_y) ||
            cs2d_map_is_solid(map, new_x + radius, new_y) ||
            cs2d_map_is_solid(map, new_x, new_y - radius) ||
            cs2d_map_is_solid(map, new_x, new_y + radius)) {
            return false;
        }
    }

    player->pos.x = new_x;
    player->pos.y = new_y;
    return true;
}
