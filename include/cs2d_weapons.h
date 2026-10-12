#ifndef CS2D_WEAPONS_H
#define CS2D_WEAPONS_H

#include "cs2d_types.h"

/* Initialize weapon table (reconstructed from FUN_1400e0fb0 in cs2d_dedicated.exe) */
void cs2d_weapons_init(void);

/* Load weapon configuration from sys/weapons.cfg or sys/weapons_recoil.cfg (FUN_140112bc0) */
bool cs2d_weapons_load_cfg(const char *filepath);

/* Get weapon definition by ID (1..91) */
const cs2d_weapon_def_t *cs2d_weapon_get(uint8_t weapon_id);

/* Weapon selection HUD (CS2D.exe init_weapons tables): slot 1..6 (0 = not listed, 0x140a017b8), order inside the
 * slot 0..9 (0x1409f3ce0), and whether slots 1 / 2 show "clip | reserve" (0x140a017b0 >= 0) */
int  cs2d_weapon_hud_slot(uint8_t weapon_id);
int  cs2d_weapon_hud_order(uint8_t weapon_id);
bool cs2d_weapon_hud_ammo(uint8_t weapon_id);

/* Query integer weapon property by name (reconstructed from Lua itemtype() FUN_14009b4c0) */
int cs2d_itemtype_int(uint8_t weapon_id, const char *prop);

/* Check if player can fire given current time and weapon state */
bool cs2d_weapon_can_fire(const cs2d_player_t *player);

/* Perform weapon fire: decrements ammo, sets fire cooldown */
bool cs2d_weapon_fire(cs2d_player_t *player);

/*
 * Reach of a shot in px, measured against the dedicated server on prison_funst3r: the knife
 * (range 15) breaks a breakable whose edge is 20 px ahead but not 21 px; the AK-47 (range 300)
 * reaches 310 px but not 320 px. The ray probes 1 px steps, so reaching a tile edge d px away
 * takes a reach of d + 1.
 */
float cs2d_weapon_shot_reach(const cs2d_weapon_def_t *wep);

/* Perform reload: returns false if already full or no reserve */
bool cs2d_weapon_reload(cs2d_player_t *player);

/* Complete reload action (called when reload_timer expires) */
void cs2d_weapon_finish_reload(cs2d_player_t *player);

/* Calculate damage applied to target taking armor into account
   (reconstructed from FUN_1401658c0 in cs2d_dedicated.exe):
   - Normal Kevlar (armor <= 200): absorbs mp_kevlar (0.35f) up to current armor points;
     armor loses raw damage points; if raw damage > armor, excess goes 100% into health.
   - Special CS2D Armors (201..205): do not lose durability and scale damage by fixed factors:
     201 (Light Armor): 0.75, 202 (Armor): 0.50, 203 (Heavy Armor): 0.25,
     204 (Medic Armor): 0.50, 205 (Super Armor): 0.05.
*/
void cs2d_calculate_damage(
    const cs2d_weapon_def_t *weapon,
    float                    distance,
    bool                     is_headshot,
    int                      target_armor,
    bool                     target_has_helm,
    int                     *out_hp_damage,
    int                     *out_armor_damage
);

#endif /* CS2D_WEAPONS_H */
