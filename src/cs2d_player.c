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
    /* Tpl::add 0x1400ad920: a new player starts with no scoreboard flash */
    player->score_inc_time   = 100000.0f;
    player->assists_inc_time = 100000.0f;
    player->dead_time        = 100000.0f;
}

void cs2d_player_spawn(cs2d_player_t *player, cs2d_team_t team, cs2d_vec2_t spawn_pos)
{
    if (!player) return;

    player->team = team;
    player->pos = spawn_pos;
    player->vel.x = 0.0f;
    player->vel.y = 0.0f;
    player->angle = 0.0f;
    player->draw_angle = 0.0f;
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
    player->recoil = 0.0f;
    player->slash_timer = 0.0f;
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

    /* Visual recoil decay: 0.5 px per 60 Hz frame = 30.0 px/s (Tpl.updateAll 0x14021d1b1, 0x140bd69e8 = 0.5f) */
    if (player->recoil > 0.0f) {
        player->recoil -= 0.5f * (delta_time * 60.0f);
        if (player->recoil < 0.0f) player->recoil = 0.0f;
    }

    /* Melee swing decay: 1.0 per 60 Hz frame = 60.0/s; draw_angle = angle + 6.0f * slash_timer
     * (Tpl.updateAll 0x14021d68a..0x14021d6d2, 0x140bd69e4 = 6.0f, and 0x14021da21..0x14021da6a) */
    if (player->slash_timer > 0.0f) {
        player->slash_timer -= delta_time * 60.0f;
        if (player->slash_timer < 0.0f) player->slash_timer = 0.0f;
    }
    player->draw_angle = player->angle + ((player->slash_timer > 0.0f) ? (6.0f * player->slash_timer) : 0.0f);
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
    player->fire_cooldown = 0.0f;
    player->reload_timer = 0.0f;
    player->slash_timer = 0.0f;
    player->draw_angle = player->angle;
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
    player->fire_cooldown = 0.0f;
    player->reload_timer = 0.0f; /* Interrupt reload on switch */
    player->slash_timer = 0.0f;
    player->draw_angle = player->angle;
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

void cs2d_player_clear_inventory(cs2d_player_t *player)
{
    if (!player) return;
    memset(player->inventory, 0, sizeof(player->inventory));
    memset(player->clip_ammo, 0, sizeof(player->clip_ammo));
    memset(player->reserve_ammo, 0, sizeof(player->reserve_ammo));
    player->current_weapon = CS2D_WEP_NONE;
    player->reload_timer   = 0.0f;
}

/* Best weapon left after losing the held one: primary, secondary, melee, then anything */
static void select_fallback_weapon(cs2d_player_t *player)
{
    static const cs2d_weapon_slot_t order[] = { CS2D_SLOT_PRIMARY, CS2D_SLOT_SECONDARY, CS2D_SLOT_MELEE };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (player->inventory[order[i]] != CS2D_WEP_NONE) {
            player->current_weapon = player->inventory[order[i]];
            return;
        }
    }
    player->current_weapon = CS2D_WEP_NONE;
    for (int s = 0; s < CS2D_SLOT_COUNT; s++) {
        if (player->inventory[s] != CS2D_WEP_NONE) {
            player->current_weapon = player->inventory[s];
            return;
        }
    }
}

bool cs2d_player_strip_weapon(cs2d_player_t *player, uint8_t weapon_id)
{
    if (!player || weapon_id == CS2D_WEP_NONE) return false;
    bool found = false;
    for (int s = 0; s < CS2D_SLOT_COUNT; s++) {
        if (player->inventory[s] == weapon_id) {
            player->inventory[s] = CS2D_WEP_NONE;
            found = true;
        }
    }
    if (!found) return false;
    player->clip_ammo[weapon_id]    = 0;
    player->reserve_ammo[weapon_id] = 0;
    if (player->current_weapon == weapon_id) {
        player->reload_timer = 0.0f;
        select_fallback_weapon(player);
    }
    return true;
}

bool cs2d_player_select_weapon(cs2d_player_t *player, uint8_t weapon_id)
{
    if (!player || weapon_id == CS2D_WEP_NONE) return false;
    for (int s = 0; s < CS2D_SLOT_COUNT; s++) {
        if (player->inventory[s] == weapon_id) {
            if (player->current_weapon != weapon_id) {
                player->fire_cooldown = 0.0f;
                player->reload_timer = 0.0f;
                player->slash_timer = 0.0f;
                player->draw_angle = player->angle;
            }
            player->current_weapon = weapon_id;
            return true;
        }
    }
    return false;
}

/* Tile at pixel (px, py) blocks players: wall / obstacle collision (tile modes 1..9, DynWall, Breakable) */
static bool move_blocked(const cs2d_map_t *map, float px, float py)
{
    int tx = (int)(px * 0.03125f);
    int ty = (int)(py * 0.03125f);
    if (px < 0.0f || py < 0.0f || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) return true;
    uint8_t c = map->collision[(size_t)ty * map->width + (size_t)tx];
    return c == 1 || c == 2;
}

/* HUD slots wrap 0..9 (FUN_140166fb0) */
static int invsel_wrap(int slot)
{
    if (slot < 0) return 9;
    if (slot > 9) return 0;
    return slot;
}

int cs2d_invsel_count(const cs2d_player_t *player, int slot)
{
    int n = 0;
    for (int s = 0; s < CS2D_SLOT_COUNT; s++) {
        uint8_t w = player->inventory[s];
        if (w != CS2D_WEP_NONE && cs2d_weapon_hud_slot(w) == slot && cs2d_weapon_hud_order(w) < 10) n++;
    }
    return n;
}

uint8_t cs2d_invsel_item(const cs2d_player_t *player, int slot, int index)
{
    int i = 0;
    for (int order = 0; order < 10; order++) {
        for (int s = 0; s < CS2D_SLOT_COUNT; s++) {
            uint8_t w = player->inventory[s];
            if (w == CS2D_WEP_NONE || cs2d_weapon_hud_slot(w) != slot || cs2d_weapon_hud_order(w) != order) continue;
            if (i == index) return w;
            i++;
        }
    }
    return CS2D_WEP_NONE;
}

void cs2d_invsel_cycle(cs2d_invsel_t *sel, const cs2d_player_t *player, int dir)
{
    /* Closed: start on the held weapon's slot and position */
    if (sel->slot == 0 && player->current_weapon != CS2D_WEP_NONE) {
        sel->slot = cs2d_weapon_hud_slot(player->current_weapon);
        int n = cs2d_invsel_count(player, sel->slot);
        for (int i = 0; i < n; i++) {
            if (cs2d_invsel_item(player, sel->slot, i) == player->current_weapon) { sel->index = i; break; }
        }
    }
    /* At most 101 steps (0x65), so an empty inventory cannot loop forever */
    for (int tries = 0x65; tries > 0; tries--) {
        int n = cs2d_invsel_count(player, sel->slot);
        if (dir == 1) {
            if (sel->index + 1 < n) { sel->index++; return; }
            sel->slot  = invsel_wrap(sel->slot + 1);
            sel->index = -1;
        } else {
            if (sel->index - 1 >= 0 && n != 0) { sel->index--; return; }
            sel->slot  = invsel_wrap(sel->slot - 1);
            sel->index = cs2d_invsel_count(player, sel->slot); /* one past the last: the next step takes the last */
        }
    }
}

uint8_t cs2d_invsel_slot(cs2d_invsel_t *sel, const cs2d_player_t *player, int slot, bool fastswitch)
{
    if (slot < 0 || slot > 9) slot = 0;
    if (fastswitch && cs2d_invsel_count(player, slot) == 1) {
        sel->slot = 0;
        return cs2d_invsel_item(player, slot, 0);
    }
    if (sel->slot == slot) {
        int next = sel->index + 1;
        sel->index = (next < cs2d_invsel_count(player, slot)) ? next : 0;
    } else {
        sel->index = 0;
        sel->slot  = slot;
    }
    return CS2D_WEP_NONE;
}

uint8_t cs2d_invsel_confirm(cs2d_invsel_t *sel, const cs2d_player_t *player)
{
    uint8_t w = cs2d_invsel_item(player, sel->slot, sel->index);
    sel->slot  = 0;
    sel->index = 0;
    return w;
}

/*
 * Reconstructed from Tpl method +0x198 (FUN_1401f7600, collision branch): the player is a 24x24 box
 * (half-size 12). X is resolved first, then Y using the resolved X. Each axis advances 1 px at a time
 * while both leading corners of the next pixel (offset 13) are free; the final sub-pixel step lands
 * exactly on the target. Pushing diagonally into a wall therefore keeps the free axis moving: the
 * player slides along the wall at the speed of the parallel component.
 */
bool cs2d_player_move(cs2d_player_t *player, const cs2d_map_t *map, float dx, float dy)
{
    if (!player || !player->alive) return false;

    float x  = player->pos.x;
    float y  = player->pos.y;
    float tx = x + dx;
    float ty = y + dy;

    if (!map || !map->collision) {
        player->pos.x = tx;
        player->pos.y = ty;
        return dx != 0.0f || dy != 0.0f;
    }

    if (tx > x) {
        float cur = x;
        for (;;) {
            float next = cur + 1.0f;
            if (cur < 11.0f || y < 12.0f ||
                move_blocked(map, cur + 13.0f, y - 12.0f) || move_blocked(map, cur + 13.0f, y + 12.0f)) break;
            if (tx < next) { cur = tx; break; }
            cur = next;
        }
        x = cur;
    } else if (tx < x) {
        float cur = x;
        for (;;) {
            float next = cur - 1.0f;
            if (cur < 13.0f || y < 12.0f ||
                move_blocked(map, cur - 13.0f, y - 12.0f) || move_blocked(map, cur - 13.0f, y + 12.0f)) break;
            if (next < tx) { cur = tx; break; }
            cur = next;
        }
        x = cur;
    }

    if (ty > y) {
        float cur = y;
        for (;;) {
            float next = cur + 1.0f;
            if (cur < 11.0f || x < 12.0f ||
                move_blocked(map, x + 12.0f, cur + 13.0f) || move_blocked(map, x - 12.0f, cur + 13.0f)) break;
            if (ty < next) { cur = ty; break; }
            cur = next;
        }
        y = cur;
    } else if (ty < y) {
        float cur = y;
        for (;;) {
            float next = cur - 1.0f;
            if (cur < 13.0f || x < 12.0f ||
                move_blocked(map, x + 12.0f, cur - 13.0f) || move_blocked(map, x - 12.0f, cur - 13.0f)) break;
            if (next < ty) { cur = ty; break; }
            cur = next;
        }
        y = cur;
    }

    bool moved = (x != player->pos.x || y != player->pos.y);
    player->pos.x = x;
    player->pos.y = y;
    return moved;
}

/*
 * FUN_1402a12d0, player branch: without teams everybody is an enemy. In a team game a Spectator (or no local player)
 * sees the name in yellow, a team mate (T with T, CT / VIP together) is a friend, anybody else an enemy. An enemy's
 * health is shown only under sv_specmode 1 while the local player is not alive in a team; the others always.
 * (An enemy covered by smoke, Tpro.smokegren_cover, gets no text at all: there are no smoke grenades yet.)
 */
cs2d_pl_hover_t cs2d_player_hover_kind(const cs2d_player_t *local, const cs2d_player_t *target, bool team_game,
                                       int specmode, bool local_alive, bool *show_health)
{
    cs2d_pl_hover_t kind = CS2D_PL_HOVER_ENEMY;
    if (team_game) {
        if (!local || local->team == CS2D_TEAM_SPEC) kind = CS2D_PL_HOVER_NEUTRAL;
        else if ((local->team == CS2D_TEAM_T) == (target->team == CS2D_TEAM_T)) kind = CS2D_PL_HOVER_FRIEND;
    }
    *show_health = kind != CS2D_PL_HOVER_ENEMY || (specmode == 1 && !local_alive);
    return kind;
}
