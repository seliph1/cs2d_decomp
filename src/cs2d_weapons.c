#include "cs2d_weapons.h"
#include "cs2d_sound.h"
#include "cs2d_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/*
 * Authentic CS2D Weapon Table (IDs 0..99)
 * Reconstructed directly from FUN_1400e0fb0 (0x1400e0fb0) in cs2d_dedicated.exe
 * and sys/weapons.cfg / sys/weapons_recoil.cfg (FUN_140112bc0).
 *
 * Global array mappings in cs2d_dedicated.exe:
 *   PTR_DAT_14059d4c8 : name
 *   PTR_DAT_14059d4a8 : dmg
 *   PTR_DAT_14059d4a0 : dmg_z1 (zoom 1 / alt damage)
 *   PTR_DAT_14059d498 : dmg_z2 (zoom 2 damage)
 *   PTR_DAT_14059d490 : dmg_zomb (default 1.0f)
 *   PTR_DAT_14059d488 : rate / firedelay (ticks)
 *   PTR_DAT_14059d480 : reload (ticks)
 *   PTR_DAT_14059d470 : ammo (max reserve)
 *   PTR_DAT_14059d468 : ammoin (clip size)
 *   PTR_DAT_14059d460 : price
 *   PTR_DAT_14059d458 : range (default 300)
 *   PTR_DAT_14059d450 : dispersion
 *   PTR_DAT_14059d448 : speed_offset
 *   PTR_DAT_14059d428 : slot (1=Primary, 2=Secondary, 3=Melee, 4=Grenade, 5=Special/Bomb, 6=Flag)
 *   PTR_DAT_14059d408 : recoil_accuracy
 *   PTR_DAT_14059d400 : recoil_recover
 *   PTR_DAT_14059d3f8 : recoil_attack
 */
static cs2d_weapon_def_t g_weapon_table[CS2D_MAX_WEAPONS];

/* CS2D.exe Tpl.attack (FUN_1402164d0, imul 0x27 at 0x140216b8f) re-arms the fire timer with firetimer.reset(firedelay * 39) in
 * milliseconds (Ttimer.reset: time = ms + g_time_ms), so one firedelay tick is 39 ms, not a 50 Hz tick */
#define FIRE_DELAY_TICK_SEC 0.039f
static bool g_weapons_initialized = false;

static void set_wep(
    uint8_t id, const char *name, cs2d_weapon_slot_t slot, uint8_t cs2d_slot,
    int price, int dmg, int dmg_z1, int dmg_z2, int clip, int reserve,
    int rate_ticks, int reload_ticks, int range, int dispersion,
    float speed_offset, int rec_acc, int rec_rec, int rec_atk, bool is_auto
) {
    if (id >= CS2D_MAX_WEAPONS) return;
    cs2d_weapon_def_t *w = &g_weapon_table[id];
    memset(w, 0, sizeof(*w));
    w->id                = id;
    w->name              = name;
    w->slot              = slot;
    w->cs2d_slot         = cs2d_slot;
    w->price             = price;
    w->damage            = dmg;
    w->damage_z1         = dmg_z1;
    w->damage_z2         = dmg_z2;
    w->damage_zomb       = 1.0f;
    w->armor_penetration = 0.65f; /* mp_kevlar absorbs 0.35f by default in FUN_1401658c0 */
    w->clip_size         = clip;
    w->max_reserve       = reserve;
    w->fire_delay_ticks  = rate_ticks;
    w->reload_ticks      = reload_ticks;
    w->fire_delay        = (rate_ticks > 0) ? ((float)rate_ticks * FIRE_DELAY_TICK_SEC) : 0.20f;
    w->reload_time       = (reload_ticks > 0) ? ((float)reload_ticks * 0.01f) : 0.0f;
    w->speed_offset      = speed_offset;
    w->speed_modifier    = 1.0f + (speed_offset * 0.02f);
    w->dispersion        = dispersion;
    w->spread            = (float)dispersion * 0.005f;
    w->recoil_accuracy   = rec_acc;
    w->recoil_recover    = rec_rec;
    w->recoil_attack     = rec_atk;
    w->range             = range;
    w->is_automatic      = is_auto;
}

void cs2d_weapons_init(void)
{
    memset(g_weapon_table, 0, sizeof(g_weapon_table));

    /* Default loop in FUN_1400e0fb0: dmg_zomb = 1.0f, range = 300 */
    for (int i = 0; i < CS2D_MAX_WEAPONS; i++) {
        g_weapon_table[i].id = (uint8_t)i;
        g_weapon_table[i].damage_zomb = 1.0f;
        g_weapon_table[i].range = 300;
        g_weapon_table[i].speed_modifier = 1.0f;
    }

    /* Pistols (CS2D slot 2 -> CS2D_SLOT_SECONDARY) */
    set_wep(1,  "USP",              CS2D_SLOT_SECONDARY, 2, 500,  24, 0,   0,   12,  100, 9,  220, 300, 15,  0.0f, 2, 70,   2,  false);
    set_wep(2,  "Glock",            CS2D_SLOT_SECONDARY, 2, 400,  21, 0,   0,   20,  120, 9,  220, 300, 15,  0.0f, 2, 70,   2,  false);
    set_wep(3,  "Deagle",           CS2D_SLOT_SECONDARY, 2, 650,  34, 0,   0,   7,   35,  13, 210, 300, 20,  0.0f, 2, 50,   4,  false);
    set_wep(4,  "P228",             CS2D_SLOT_SECONDARY, 2, 600,  22, 0,   0,   13,  52,  9,  270, 300, 15,  0.0f, 2, 70,   2,  false);
    set_wep(5,  "Elite",            CS2D_SLOT_SECONDARY, 2, 1000, 22, 0,   0,   15,  120, 7,  250, 300, 25,  0.0f, 2, 70,   2,  false);
    set_wep(6,  "Five-Seven",       CS2D_SLOT_SECONDARY, 2, 750,  18, 0,   0,   20,  100, 5,  480, 300, 20,  0.0f, 2, 70,   3,  false);

    /* Shotguns (CS2D slot 1 -> CS2D_SLOT_PRIMARY) */
    set_wep(10, "M3",               CS2D_SLOT_PRIMARY,   1, 1700, 26, 0,   0,   8,   32,  25, 90,  160, 45, -1.5f, 8, 80,   15, false);
    set_wep(11, "XM1014",           CS2D_SLOT_PRIMARY,   1, 3000, 14, 0,   0,   7,   32,  7,  70,  160, 40, -1.0f, 8, 80,   10, true);

    /* SMGs (CS2D slot 1 -> CS2D_SLOT_PRIMARY) */
    set_wep(20, "MP5",              CS2D_SLOT_PRIMARY,   1, 1500, 13, 0,   0,   30,  120, 3,  230, 300, 20,  0.0f, 2, 54,   1,  true);
    set_wep(21, "TMP",              CS2D_SLOT_PRIMARY,   1, 1250, 9,  0,   0,   30,  120, 2,  200, 300, 35,  0.0f, 4, 49,   1,  true);
    set_wep(22, "P90",              CS2D_SLOT_PRIMARY,   1, 2350, 11, 0,   0,   50,  100, 2,  350, 300, 25, -0.5f, 1, 54,   1,  true);
    set_wep(23, "Mac 10",           CS2D_SLOT_PRIMARY,   1, 1400, 9,  0,   0,   30,  100, 2,  340, 300, 35,  0.0f, 2, 59,   1,  true);
    set_wep(24, "UMP45",            CS2D_SLOT_PRIMARY,   1, 1700, 15, 0,   0,   25,  100, 4,  350, 300, 25,  0.0f, 1, 70,   1,  true);

    /* Rifles & Snipers (CS2D slot 1 -> CS2D_SLOT_PRIMARY) */
    set_wep(30, "AK-47",            CS2D_SLOT_PRIMARY,   1, 2500, 22, 0,   0,   30,  90,  3,  275, 300, 20, -2.0f, 1, 40,   1,  true);
    set_wep(31, "SG552",            CS2D_SLOT_PRIMARY,   1, 3500, 24, 35,  0,   30,  90,  4,  250, 300, 20, -1.5f, 0, 50,   1,  true);
    set_wep(32, "M4A1",             CS2D_SLOT_PRIMARY,   1, 3100, 22, 20,  0,   30,  90,  3,  220, 300, 15, -1.0f, 0, 40,   1,  true);
    set_wep(33, "Aug",              CS2D_SLOT_PRIMARY,   1, 3500, 24, 35,  0,   30,  90,  4,  420, 300, 20, -1.0f, 0, 50,   1,  true);
    set_wep(34, "Scout",            CS2D_SLOT_PRIMARY,   1, 2750, 45, 50,  100, 10,  90,  25, 180, 450, 25,  0.0f, 1, 70,   1,  false);
    set_wep(35, "AWP",              CS2D_SLOT_PRIMARY,   1, 4750, 50, 65,  150, 10,  30,  65, 400, 550, 25, -3.5f, 1, 70,   1,  false);
    set_wep(36, "G3SG1",            CS2D_SLOT_PRIMARY,   1, 5000, 32, 37,  45,  20,  90,  6,  460, 450, 25, -3.0f, 1, 80,   1,  true);
    set_wep(37, "SG550",            CS2D_SLOT_PRIMARY,   1, 4200, 30, 35,  40,  30,  90,  6,  450, 450, 25, -3.0f, 1, 60,   1,  true);
    set_wep(38, "Galil",            CS2D_SLOT_PRIMARY,   1, 2000, 13, 0,   0,   35,  90,  3,  250, 300, 35, -1.0f, 0, 50,   1,  true);
    set_wep(39, "Famas",            CS2D_SLOT_PRIMARY,   1, 2250, 14, 10,  0,   25,  90,  3,  250, 300, 20, -1.0f, 1, 40,   1,  true);

    /* Heavy & Special Primary Weapons (CS2D slot 1 -> CS2D_SLOT_PRIMARY). Laser .. Grenade Launcher clip, reserve and
     * range from CS2D.exe init_weapons (0x140a017a8, 0x140a017b0, 0x1409f62a8; tools/agent/weapontables.py) */
    set_wep(40, "M249",             CS2D_SLOT_PRIMARY,   1, 5750, 15, 0,   0,   100, 200, 2,  460, 300, 40, -3.5f, 5, 60,   1,  true);
    set_wep(41, "Tactical Shield",  CS2D_SLOT_PRIMARY,   1, 1000, 0,  0,   0,   0,   0,   2,  0,   300, 0,   0.0f, 0, -899, 0,  false);
    set_wep(45, "Laser",            CS2D_SLOT_PRIMARY,   1, 6500, 1000, 0, 0,   15,  45,  30, 200, 1000, 0,  -2.5f, 0, 70,   1,  false);
    set_wep(46, "Flamethrower",     CS2D_SLOT_PRIMARY,   1, 3200, 30, 0,   0,   100, 200, 3,  200, 55,  0,  -1.0f, 0, 70,   1,  true);
    set_wep(47, "RPG Launcher",     CS2D_SLOT_PRIMARY,   1, 4000, 325, 0,  0,   1,   2,   50, 500, 700, 0,  -4.0f, 0, 70,   1,  false);
    set_wep(48, "Rocket Launcher",  CS2D_SLOT_PRIMARY,   1, 5500, 70, 0,   0,   20,  60,  20, 300, 700, 0,  -3.5f, 0, 70,   1,  false);
    set_wep(49, "Grenade Launcher", CS2D_SLOT_PRIMARY,   1, 5500, 75, 0,   0,   20,  60,  20, 300, 425, 0,  -3.5f, 0, 70,   1,  false);

    /* Melee (CS2D slot 3 -> CS2D_SLOT_MELEE) */
    set_wep(50, "Knife",            CS2D_SLOT_MELEE,     3, 0,    45, 90,  0,   0,   0,   9,  0,   15,  0,   0.0f, 0, 0,    0,  false);
    set_wep(69, "Machete",          CS2D_SLOT_MELEE,     3, 0,    175, 0,  0,   0,   0,   18, 0,   25,  0,  -3.0f, 0, 0,    0,  false);
    set_wep(74, "Wrench",           CS2D_SLOT_BOMB,      5, 0,    30, 0,   0,   0,   0,   11, 0,   15,  0,   0.0f, 0, 0,    0,  false);
    set_wep(78, "Claw",             CS2D_SLOT_MELEE,     3, 0,    50, 90,  0,   0,   0,   9,  0,   18,  0,   0.0f, 0, 0,    0,  false);
    set_wep(85, "Chainsaw",         CS2D_SLOT_MELEE,     3, 0,    60, 0,   0,   0,   0,   2,  0,   25,  0,  -1.0f, 0, 0,    0,  true);

    /* Grenades (CS2D slot 4 -> CS2D_SLOT_GRENADE). Clip = the most a player carries (init_weapons 0x140a017a8:
     * Flashbang 2, Snowball 10, Mines 3), reserve 0 (0x140a017b0) */
    set_wep(51, "HE",               CS2D_SLOT_GRENADE,   4, 300,  105, 0,  0,   1,   0,   15, 0,   160, 0,   0.0f, 0, 0,    0,  false);
    set_wep(52, "Flashbang",        CS2D_SLOT_GRENADE,   4, 200,  0,  0,   0,   2,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(53, "Smoke Grenade",    CS2D_SLOT_GRENADE,   4, 300,  0,  0,   0,   1,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(54, "Flare",            CS2D_SLOT_GRENADE,   4, 150,  0,  0,   0,   1,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(72, "Gas Grenade",      CS2D_SLOT_GRENADE,   4, 300,  9,  0,   0,   1,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(73, "Molotov Cocktail", CS2D_SLOT_GRENADE,   4, 300,  4,  0,   0,   1,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(75, "Snowball",         CS2D_SLOT_GRENADE,   4, 50,   35, 0,   0,   10,  0,   15, 0,   16,  0,   0.0f, 0, 0,    0,  false);
    set_wep(76, "Air Strike",       CS2D_SLOT_GRENADE,   4, 500,  1500, 0, 0,   1,   0,   15, 0,   250, 0,   0.0f, 0, 0,    0,  false);
    set_wep(86, "Gut Bomb",         CS2D_SLOT_GRENADE,   4, 150,  50, 0,   0,   1,   0,   15, 0,   96,  0,   0.0f, 0, 0,    0,  false);

    /* Bomb / Special Items (CS2D slot 5 & 0) */
    set_wep(55, "Bomb",             CS2D_SLOT_BOMB,      5, 0,    0,  0,   0,   0,   0,   15, 0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(56, "Defuse Kit",       CS2D_SLOT_ITEM,      0, 200,  0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(57, "Kevlar",           CS2D_SLOT_ITEM,      0, 650,  0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(58, "Kevlar+Helm",      CS2D_SLOT_ITEM,      0, 1000, 0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(59, "Night Vision",     CS2D_SLOT_ITEM,      0, 1250, 0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(60, "Gas Mask",         CS2D_SLOT_ITEM,      0, 250,  0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(61, "Primary Ammo",     CS2D_SLOT_ITEM,      0, 50,   0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(62, "Secondary Ammo",   CS2D_SLOT_ITEM,      0, 50,   0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(63, "Planted Bomb",     CS2D_SLOT_ITEM,      6, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    /* 64..68 (init_weapons names 0x200..0x220 / 8): picked up and consumed (Titem.give FUN_14020ce60) */
    set_wep(64, "Medikit",          CS2D_SLOT_ITEM,      0, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(65, "Bandage",          CS2D_SLOT_ITEM,      0, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(66, "Coins",            CS2D_SLOT_ITEM,      0, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(67, "Money",            CS2D_SLOT_ITEM,      0, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(68, "Gold",             CS2D_SLOT_ITEM,      0, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(70, "Red Flag",         CS2D_SLOT_ITEM,      6, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(71, "Blue Flag",        CS2D_SLOT_ITEM,      6, 0,    0,  0,   0,   0,   0,   0,  0,   300, 0,   0.0f, 0, 0,    0,  false);
    set_wep(77, "Mine",             CS2D_SLOT_BOMB,      5, 400,  150, 0,  0,   3,   0,   15, 0,   96,  0,   0.0f, 0, 0,    0,  false);
    set_wep(87, "Laser Mine",       CS2D_SLOT_BOMB,      5, 400,  150, 0,  0,   3,   0,   15, 0,   96,  0,   0.0f, 0, 0,    0,  false);
    set_wep(88, "Portal Gun",       CS2D_SLOT_PRIMARY,   1, 10000, 0,  0,  0,   0,   0,   5,  0,   300, 0,   0.0f, 0, -899, 0,  false);
    set_wep(89, "Satchel Charge",   CS2D_SLOT_BOMB,      5, 300,  100, 0,  0,   5,   0,   15, 0,   425, 0,   0.0f, 0, 0,    0,  false);

    /* Minigun & FN F2000 (CS2D slot 1 -> CS2D_SLOT_PRIMARY) */
    set_wep(90, "M134",             CS2D_SLOT_PRIMARY,   1, 8100, 13, 0,   0,   200, 200, 2,  600, 300, 40, -6.0f, 8, 0,    1,  true);
    set_wep(91, "FN F2000",         CS2D_SLOT_PRIMARY,   1, 3500, 26, 0,   0,   30,  90,  3,  220, 300, 20, -1.0f, 8, 0,    1,  true);

    /*
     * Per-weapon visual backward recoil kickback in pixels (0x1409eda18 in CS2D.exe,
     * initialized in init_weapons at 0x1402305db..0x1402340a9).
     */
    /* Shell-by-shell reload flag 0x1409efcf8 (init_weapons 0x140230330): 1 only for M3 and XM1014 */
    g_weapon_table[CS2D_WEP_M3].shell_reload     = true;
    g_weapon_table[CS2D_WEP_XM1014].shell_reload = true;

    g_weapon_table[1].visual_recoil  = 2; /* USP (0x140230609) */
    g_weapon_table[2].visual_recoil  = 2; /* Glock (0x1402307f4) */
    g_weapon_table[3].visual_recoil  = 4; /* Deagle (0x1402309b8) */
    g_weapon_table[4].visual_recoil  = 2; /* P228 (0x140230b7c) */
    g_weapon_table[5].visual_recoil  = 2; /* Elite (0x140230d40) */
    g_weapon_table[6].visual_recoil  = 2; /* Five-Seven (0x140230efe) */
    g_weapon_table[10].visual_recoil = 5; /* M3 (0x1402310d3) */
    g_weapon_table[11].visual_recoil = 5; /* XM1014 (0x140231297) */
    g_weapon_table[20].visual_recoil = 1; /* MP5 (0x140231449) */
    g_weapon_table[21].visual_recoil = 1; /* TMP (0x14023161a) */
    g_weapon_table[22].visual_recoil = 1; /* P90 (0x1402317c8) */
    g_weapon_table[23].visual_recoil = 1; /* Mac 10 (0x140231991) */
    g_weapon_table[24].visual_recoil = 1; /* UMP45 (0x140231b39) */
    g_weapon_table[30].visual_recoil = 2; /* AK-47 (0x140231ceb) */
    g_weapon_table[31].visual_recoil = 1; /* SG552 (0x140231ef1) */
    g_weapon_table[32].visual_recoil = 1; /* M4A1 (0x1402320c4) */
    g_weapon_table[33].visual_recoil = 1; /* Aug (0x1402322b6) */
    g_weapon_table[34].visual_recoil = 2; /* Scout (0x1402324db) */
    g_weapon_table[35].visual_recoil = 6; /* AWP (0x140232737) */
    g_weapon_table[36].visual_recoil = 4; /* G3SG1 (0x140232971) */
    g_weapon_table[37].visual_recoil = 4; /* SG550 (0x140232ba8) */
    g_weapon_table[38].visual_recoil = 2; /* Galil (0x140232d53) */
    g_weapon_table[39].visual_recoil = 2; /* Famas (0x140232f2c) */
    g_weapon_table[40].visual_recoil = 2; /* M249 (0x140233462) */
    g_weapon_table[45].visual_recoil = 6; /* Laser (0x1402336a5) */
    g_weapon_table[47].visual_recoil = 2; /* RPG Launcher (0x1402339d7) */
    g_weapon_table[48].visual_recoil = 2; /* Rocket Launcher (0x140233b80) */
    g_weapon_table[49].visual_recoil = 2; /* Grenade Launcher (0x140233d29) */
    g_weapon_table[88].visual_recoil = 5; /* Portal Gun (0x140233ed4) */
    g_weapon_table[90].visual_recoil = 1; /* M134 (0x1402340a2) */
    g_weapon_table[91].visual_recoil = 1; /* FN F2000 (0x1402330dc) */

    /*
     * Per-weapon muzzle offset in pixels (0x1409f5d20 in CS2D.exe, filled by the same init_weapons pass as
     * visual_recoil, FUN_140230330). The tracer, the muzzle flash and its light start at
     * pos + offset * (sin(angle), -cos(angle)) (shot draw at 0x1402f0000), not at the player centre.
     * Only weapons that fire have a value: items, grenades, the Tactical Shield and the armours stay 0.
     */
    static const struct { uint8_t id; uint8_t px; } s_muzzle[] = {
        { CS2D_WEP_USP,             19 }, { CS2D_WEP_GLOCK,           19 }, { CS2D_WEP_DEAGLE,          19 },
        { CS2D_WEP_P228,            19 }, { CS2D_WEP_ELITE,           19 }, { CS2D_WEP_FIVESEVEN,       19 },
        { CS2D_WEP_M3,              28 }, { CS2D_WEP_XM1014,          28 },
        { CS2D_WEP_MP5,             22 }, { CS2D_WEP_TMP,             20 }, { CS2D_WEP_P90,             22 },
        { CS2D_WEP_MAC10,           20 }, { CS2D_WEP_UMP45,           22 },
        { CS2D_WEP_AK47,            26 }, { CS2D_WEP_SG552,           30 }, { CS2D_WEP_M4A1,            26 },
        { CS2D_WEP_AUG,             30 }, { CS2D_WEP_SCOUT,           30 }, { CS2D_WEP_AWP,             33 },
        { CS2D_WEP_G3SG1,           30 }, { CS2D_WEP_SG550,           30 }, { CS2D_WEP_GALIL,           24 },
        { CS2D_WEP_FAMAS,           24 }, { CS2D_WEP_M249,            28 },
        { CS2D_WEP_LASER,           30 }, { CS2D_WEP_FLAMETHROWER,    25 }, { CS2D_WEP_RPG7,            30 },
        { CS2D_WEP_ROCKETLAUNCHER,  30 }, { CS2D_WEP_GRENADELAUNCHER, 30 }, { CS2D_WEP_PORTALGUN,      30 },
        { CS2D_WEP_M134,            32 }, { CS2D_WEP_FNF2000,         23 },
        { CS2D_WEP_KNIFE,           25 }, { CS2D_WEP_MACHETE,         25 }, { CS2D_WEP_WRENCH,          25 },
        { CS2D_WEP_CLAW,            25 }, { CS2D_WEP_CHAINSAW,        30 },
    };
    for (size_t i = 0; i < sizeof(s_muzzle) / sizeof(s_muzzle[0]); i++) {
        g_weapon_table[s_muzzle[i].id].muzzle = s_muzzle[i].px;
    }

    g_weapons_initialized = true;

    /* Load sys/weapons_recoil.cfg or sys/weapons.cfg if present (FUN_140112bc0) */
    if (!cs2d_weapons_load_cfg("sys/weapons_recoil.cfg")) {
        cs2d_weapons_load_cfg("sys/weapons.cfg");
    }
}

/*
 * Reconstructed from FUN_140112bc0 (0x140112bc0 - loadweaponcfg) in cs2d_dedicated.exe
 */
bool cs2d_weapons_load_cfg(const char *filepath)
{
    if (!g_weapons_initialized) {
        cs2d_weapons_init();
    }
    if (!filepath) return false;

    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    int current_wep = -1;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        /* Trim leading whitespace */
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        /* Skip empty lines or '//' / '#' comments (FUN_140112bc0) */
        if (*p == '\0' || *p == '#' || (p[0] == '/' && p[1] == '/')) continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = p;
        char *val_str = eq + 1;

        /* Trim key trailing whitespace */
        char *kend = eq - 1;
        while (kend >= key && isspace((unsigned char)*kend)) *kend-- = '\0';

        /* Trim val leading/trailing whitespace */
        while (*val_str && isspace((unsigned char)*val_str)) val_str++;
        char *vend = val_str + strlen(val_str) - 1;
        while (vend >= val_str && isspace((unsigned char)*vend)) *vend-- = '\0';

        int val = atoi(val_str);

        if (_stricmp(key, "weapon") == 0) {
            if (val > 0 && val < CS2D_MAX_WEAPONS && g_weapon_table[val].name != NULL) {
                current_wep = val;
            } else {
                current_wep = -1;
            }
        } else if (current_wep != -1) {
            cs2d_weapon_def_t *w = &g_weapon_table[current_wep];
            if (_stricmp(key, "recoil_accuracy") == 0) {
                if (val > 99) val = 99;
                if (val < 0)  val = 0;
                w->recoil_accuracy = val;
            } else if (_stricmp(key, "recoil_recover") == 0) {
                if (val > 99)   val = 99;
                if (val < -899) val = -899;
                w->recoil_recover = val;
            } else if (_stricmp(key, "recoil_attack") == 0) {
                if (val > 99) val = 99;
                if (val < 0)  val = 0;
                w->recoil_attack = val;
            } else if (_stricmp(key, "firedelay") == 0) {
                if (val > 255) val = 255;
                if (val < 2)   val = 2;
                w->fire_delay_ticks = val;
                w->fire_delay = (float)val * FIRE_DELAY_TICK_SEC;
            }
        }
    }

    fclose(f);
    return true;
}

/*
 * Weapon selection HUD tables of CS2D.exe, filled per weapon by init_weapons (0x14022fc00..0x140234200, the id in
 * DAT_140ddf57c): HUD slot 0x140a017b8 (1..6; 0 = never listed, e.g. armors and flags), order inside the slot
 * 0x1409f3ce0 (0..9) and the ammo flag 0x140a017b0 (-1 = no ammo text: Portal Gun). Unlisted entries stay 0.
 */
typedef struct { uint8_t id, slot, order; int8_t ammo; } hud_slot_entry_t;
static const hud_slot_entry_t g_hud_slots[] = {
    {1, 2, 0, 0}, {2, 2, 0, 0}, {3, 2, 0, 0}, {4, 2, 0, 0}, {5, 2, 0, 0}, {6, 2, 0, 0},
    {10, 1, 0, 0}, {11, 1, 0, 0}, {20, 1, 0, 0}, {21, 1, 0, 0}, {22, 1, 0, 0}, {23, 1, 0, 0}, {24, 1, 0, 0},
    {30, 1, 0, 0}, {31, 1, 0, 0}, {32, 1, 0, 0}, {33, 1, 0, 0}, {34, 1, 0, 0}, {35, 1, 0, 0}, {36, 1, 0, 0},
    {37, 1, 0, 0}, {38, 1, 0, 0}, {39, 1, 0, 0}, {40, 1, 0, 0}, {41, 1, 0, 0}, {45, 1, 0, 0}, {46, 1, 0, 0},
    {47, 1, 0, 0}, {48, 1, 0, 0}, {49, 1, 0, 0}, {88, 1, 0, -1}, {90, 1, 0, 0}, {91, 1, 0, 0}, {92, 1, 0, 0},
    {50, 3, 0, 0}, {69, 3, 0, 0}, {74, 3, 0, 0}, {78, 3, 0, 0}, {85, 3, 0, 0},
    {51, 4, 0, 0}, {52, 4, 1, 0}, {53, 4, 2, 0}, {54, 4, 3, 0}, {72, 4, 4, 0}, {73, 4, 5, 0}, {75, 4, 6, 0},
    {76, 4, 7, 0}, {86, 4, 0, 0},
    {55, 5, 0, 0}, {77, 5, 8, 0}, {87, 5, 9, 0},
    {89, 6, 0, 0},
};

static const hud_slot_entry_t *hud_slot_entry(uint8_t weapon_id)
{
    for (size_t i = 0; i < sizeof(g_hud_slots) / sizeof(g_hud_slots[0]); i++) {
        if (g_hud_slots[i].id == weapon_id) return &g_hud_slots[i];
    }
    return NULL;
}

int cs2d_weapon_hud_slot(uint8_t weapon_id)
{
    const hud_slot_entry_t *e = hud_slot_entry(weapon_id);
    return e ? e->slot : 0;
}

int cs2d_weapon_hud_order(uint8_t weapon_id)
{
    const hud_slot_entry_t *e = hud_slot_entry(weapon_id);
    return e ? e->order : 0;
}

bool cs2d_weapon_hud_ammo(uint8_t weapon_id)
{
    const hud_slot_entry_t *e = hud_slot_entry(weapon_id);
    return !e || e->ammo >= 0;
}

const cs2d_weapon_def_t *cs2d_weapon_get(uint8_t weapon_id)
{
    if (!g_weapons_initialized) {
        cs2d_weapons_init();
    }
    if (weapon_id >= CS2D_MAX_WEAPONS) return NULL;
    if (g_weapon_table[weapon_id].name == NULL) return NULL;
    return &g_weapon_table[weapon_id];
}

/*
 * Reconstructed from FUN_14009b4c0 (0x14009b4c0 - Lua itemtype(id, prop))
 */
int cs2d_itemtype_int(uint8_t weapon_id, const char *prop)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get(weapon_id);
    if (!w || !prop) return 0;

    if (_stricmp(prop, "dmg") == 0)             return w->damage;
    if (_stricmp(prop, "dmg_z1") == 0)          return w->damage_z1;
    if (_stricmp(prop, "dmg_z2") == 0)          return w->damage_z2;
    if (_stricmp(prop, "rate") == 0)            return w->fire_delay_ticks;
    if (_stricmp(prop, "reload") == 0)          return w->reload_ticks;
    if (_stricmp(prop, "ammo") == 0)            return w->max_reserve;
    if (_stricmp(prop, "ammoin") == 0)          return w->clip_size;
    if (_stricmp(prop, "price") == 0)           return w->price;
    if (_stricmp(prop, "range") == 0)           return w->range;
    if (_stricmp(prop, "dispersion") == 0)      return w->dispersion;
    if (_stricmp(prop, "slot") == 0)            return (int)w->cs2d_slot;
    if (_stricmp(prop, "recoil") == 0)          return w->recoil_accuracy;
    if (_stricmp(prop, "recoil_accuracy") == 0) return w->recoil_accuracy;
    if (_stricmp(prop, "recoil_recover") == 0)  return w->recoil_recover;
    if (_stricmp(prop, "recoil_attack") == 0)   return w->recoil_attack;
    return 0;
}

bool cs2d_weapon_can_fire(const cs2d_player_t *player)
{
    if (!player || !player->alive) return false;
    if (player->fire_cooldown > 0.0f) return false;
    if (player->current_weapon == CS2D_WEP_NONE) return false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep) return false;
    /* A shell reload does not block the attack: Tpl.attack (FUN_1402164d0, 0x140216690) clears reload and fires
     * while shells are loaded. A clip reload empties nothing but keeps the gun down until it ends */
    if (player->reload_timer > 0.0f && !(wep->shell_reload && player->is_reloading)) return false;

    if (wep->slot == CS2D_SLOT_MELEE || player->current_weapon == CS2D_WEP_WRENCH) return true;
    if (player->current_weapon == CS2D_WEP_PORTALGUN) return true; /* No ammo: ammoin 0, ammo -1 (0x140a017b0) */

    return player->clip_ammo[player->current_weapon] > 0;
}

void cs2d_weapon_item_ammo(uint8_t weapon_id, int *clip, int *reserve)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get(weapon_id);
    int max_clip = w ? w->clip_size : 0, max_res = w ? w->max_reserve : 0, slot = cs2d_weapon_hud_slot(weapon_id);
    if (slot >= 4 && slot <= 6 && weapon_id != CS2D_WEP_BOMB) {
        *clip = *clip < 0 ? 1 : (*clip > max_clip ? max_clip : *clip);
        if (*clip < 1) *clip = 1;
        *reserve = 0;
        return;
    }
    *reserve = *reserve < 0 ? max_res : (*reserve > max_res ? max_res : *reserve);
    *clip    = *clip < 0 ? max_clip : (*clip > max_clip ? max_clip : *clip);
}

int cs2d_weapon_attack2_type(uint8_t weapon_id)
{
    switch (weapon_id) {
        case CS2D_WEP_USP: case CS2D_WEP_M4A1:                       return CS2D_ATTACK2_SILENCER;
        case CS2D_WEP_GLOCK: case CS2D_WEP_FAMAS:                    return CS2D_ATTACK2_BURST;
        case CS2D_WEP_SG552: case CS2D_WEP_AUG:                      return CS2D_ATTACK2_ZOOM1;
        case CS2D_WEP_SCOUT: case CS2D_WEP_AWP: case CS2D_WEP_G3SG1:
        case CS2D_WEP_SG550:                                         return CS2D_ATTACK2_ZOOM2;
        default:                                                     return 0;
    }
}

uint8_t cs2d_weapon_mode(const cs2d_player_t *player)
{
    if (!player || player->current_weapon >= CS2D_MAX_WEAPONS) return 0;
    return player->weapon_mode[player->current_weapon];
}

int cs2d_weapon_accuracy(uint8_t weapon_id)
{
    /* 0x1409f5d18 (tools/agent/weapontables.py); unlisted ids are 0 */
    static const struct { uint8_t id, acc; } t[] = {
        {10, 10}, {11, 13}, {20, 2}, {21, 4}, {22, 3}, {23, 4}, {24, 3}, {30, 3}, {32, 2}, {36, 1}, {37, 1},
        {38, 1}, {39, 1}, {40, 8}, {90, 5}, {91, 2}, {92, 2}
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (t[i].id == weapon_id) return t[i].acc;
    }
    return 0;
}

void cs2d_weapon_unzoom_on_switch(cs2d_player_t *player, uint8_t weapon_id)
{
    if (!player || weapon_id >= CS2D_MAX_WEAPONS || weapon_id == player->current_weapon) return;
    int k = cs2d_weapon_attack2_type(weapon_id);
    if (k == CS2D_ATTACK2_ZOOM1 || k == CS2D_ATTACK2_ZOOM2) player->weapon_mode[weapon_id] = 0;
}

bool cs2d_weapon_fire(cs2d_player_t *player)
{
    if (!cs2d_weapon_can_fire(player)) return false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep) return false;

    /* Tpl.attack (0x14021a7b9): burst mode re-arms the fire timer 3.3 times longer */
    player->fire_cooldown = wep->fire_delay;
    if (cs2d_weapon_attack2_type(player->current_weapon) == CS2D_ATTACK2_BURST && cs2d_weapon_mode(player)) {
        player->fire_cooldown *= 3.3f;
    }

    if (wep->slot != CS2D_SLOT_MELEE && player->current_weapon != CS2D_WEP_WRENCH && wep->clip_size > 0) {
        player->clip_ammo[player->current_weapon]--;
    }
    /* Tpl.attack 0x140216690: reload = 0 before the shot (interrupts a shell reload) */
    player->is_reloading   = false;
    player->reload_timer   = 0.0f;
    player->reload_overdue = 0.0f;

    /* Visual backward recoil kickback on shot (Tpl.attack 0x1402166a7 & 0x140216a70:
     * player->recoil = (float)visual_recoil[id] + Rnd(0.0, 1.0)) */
    if (wep->visual_recoil > 0) {
        player->recoil = (float)wep->visual_recoil + ((float)(rand() & 0x7FFF) / 32768.0f);
    }

    /* Melee swing timer (Tpl.slash 0x14018cebc, Tpl.attack2 0x1401fdd37, Wrench 0x1401fad67:
     * slash_timer = 8.0f for all melee swings except Chainsaw id == 85 at 0x14018ce92) */
    if ((wep->slot == CS2D_SLOT_MELEE || player->current_weapon == CS2D_WEP_WRENCH) &&
        player->current_weapon != CS2D_WEP_CHAINSAW) {
        player->slash_timer = 8.0f;
        player->draw_angle  = player->angle + 6.0f * player->slash_timer;
    }
    return true;
}

float cs2d_weapon_shot_reach(const cs2d_weapon_def_t *wep)
{
    if (!wep) return 0.0f;
    if (wep->slot == CS2D_SLOT_MELEE) return (float)wep->range + 6.0f;
    return (float)wep->range + 15.0f;
}

float cs2d_weapon_reload_stage(const cs2d_weapon_def_t *wep)
{
    /* FUN_140214780: reloadtimer.reset(reload x 5) ms per stage. A shell reload repeats it once per shell; a clip
     * reload keeps reload_time (reload x 10 ms, see knowledge/buy-menu.md) */
    if (!wep) return 0.0f;
    return wep->shell_reload ? (float)wep->reload_ticks * 0.005f : wep->reload_time;
}

bool cs2d_weapon_reload(cs2d_player_t *player)
{
    if (!player || !player->alive) return false;
    if (player->reload_timer > 0.0f || player->is_reloading) return false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep || wep->slot == CS2D_SLOT_MELEE || wep->clip_size <= 0) return false;

    int current_clip = player->clip_ammo[player->current_weapon];
    int reserve = player->reserve_ammo[player->current_weapon];

    if (current_clip >= wep->clip_size || reserve <= 0) {
        return false;
    }

    player->reload_timer   = cs2d_weapon_reload_stage(wep);
    player->reload_overdue = 0.0f;
    player->is_reloading   = true;
    /* FUN_140214780 mode 1, shell weapon (0x1402159c8 / 0x14021597b): only reload = 1 and the timer; no sound and
     * no reloadani, the first shell comes when the timer runs out */
    if (wep->shell_reload) return true;
    /* Mode 1, clip weapon: reloadani = 15, w_clipout (global 0x140a0d6e0) at the player, vol 1.0, flags 0.
     * A mod item with its own reload sound (items[typ] + 0x20) replaces it; not modelled */
    player->reload_ani = 15.0f;
    cs2d_sound_play_at("sfx/weapons/w_clipout.wav", player->pos.x, player->pos.y, 1.0f, 0);
    return true;
}

bool cs2d_weapon_reload_shell(cs2d_player_t *player)
{
    if (!player || !player->is_reloading) return false;
    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep || !wep->shell_reload) return false;

    /* FUN_140214780 mode 4: timer re-armed, reloadani = 8, w_roundin (0x140a0d6d8) at the player, one shell from the
     * reserve while the clip is not full; the reload ends with a full clip or an empty reserve */
    player->reload_timer   = cs2d_weapon_reload_stage(wep);
    player->reload_overdue = 0.0f;
    player->reload_ani     = 8.0f;
    cs2d_sound_play_at("sfx/weapons/w_roundin.wav", player->pos.x, player->pos.y, 1.0f, 0);
    int16_t *clip = &player->clip_ammo[player->current_weapon], *reserve = &player->reserve_ammo[player->current_weapon];
    if (*reserve > 0 && *clip < wep->clip_size) {
        (*reserve)--;
        (*clip)++;
    }
    if (*clip >= wep->clip_size || *reserve <= 0) {
        player->is_reloading = false;
        player->reload_timer = 0.0f;
    }
    return true;
}

void cs2d_weapon_finish_reload(cs2d_player_t *player)
{
    if (!player || player->reload_timer > 0.0f) return;
    player->is_reloading = false;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (!wep) return;

    int needed = wep->clip_size - player->clip_ammo[player->current_weapon];
    int available = player->reserve_ammo[player->current_weapon];

    int transfer = (available < needed) ? available : needed;
    if (transfer <= 0) return;
    player->clip_ammo[player->current_weapon] += transfer;
    player->reserve_ammo[player->current_weapon] -= transfer;
    /* FUN_140214780 mode 2 (clip reload finished): reloadani = 15, w_clipin (0x140a0d6e8) */
    player->reload_ani = 15.0f;
    cs2d_sound_play_at("sfx/weapons/w_clipin.wav", player->pos.x, player->pos.y, 1.0f, 0);
}

/*
 * Reconstructed from FUN_1401658c0 (0x1401658c0) in cs2d_dedicated.exe:
 *
 *   iVar6 = target->armor;
 *   if (iVar6 < 201) {
 *       if (iVar6 > 0) {
 *           iVar8 = (int)((float)raw_dmg * mp_kevlar); // mp_kevlar = DAT_1405a6508 (default 0.35f)
 *           new_armor = iVar6 - raw_dmg;
 *           if (new_armor < 0) {
 *               iVar8 = (int)((float)iVar6 * mp_kevlar);
 *               new_armor = 0;
 *           }
 *           hp_dmg = raw_dmg - iVar8;
 *       }
 *   } else {
 *       switch (iVar6 - 201) {
 *           case 0: hp_dmg = (int)(raw_dmg * 0.75f); break; // 201 Light Armor
 *           case 1: hp_dmg = (int)(raw_dmg * 0.50f); break; // 202 Armor
 *           case 2: hp_dmg = (int)(raw_dmg * 0.25f); break; // 203 Heavy Armor
 *           case 3: hp_dmg = (int)(raw_dmg * 0.50f); break; // 204 Medic Armor
 *           case 4: hp_dmg = (int)(raw_dmg * 0.05f); break; // 205 Super Armor
 *       }
 *   }
 */
void cs2d_calculate_damage(
    const cs2d_weapon_def_t *weapon,
    float                    distance,
    bool                     is_headshot,
    int                      target_armor,
    bool                     target_has_helm,
    int                     *out_hp_damage,
    int                     *out_armor_damage
)
{
    (void)distance;
    (void)target_has_helm;
    if (!weapon || !out_hp_damage || !out_armor_damage) return;

    /* Base damage (or zoom2/zoom1 damage for scoped sniper headshots) */
    int raw_dmg = weapon->damage;
    if (is_headshot) {
        if (weapon->damage_z2 > 0) {
            raw_dmg = weapon->damage_z2;
        } else if (weapon->damage_z1 > raw_dmg) {
            raw_dmg = weapon->damage_z1;
        } else {
            raw_dmg = raw_dmg * 2;
        }
    }

    const float mp_kevlar = 0.35f; /* DAT_1405a6508 default in cs2d_dedicated.exe */

    if (target_armor <= 0) {
        *out_hp_damage = raw_dmg;
        *out_armor_damage = 0;
    } else if (target_armor < 201) {
        int absorbed = (int)((float)raw_dmg * mp_kevlar);
        int new_armor = target_armor - raw_dmg;
        if (new_armor < 0) {
            absorbed = (int)((float)target_armor * mp_kevlar);
            new_armor = 0;
        }
        *out_hp_damage = raw_dmg - absorbed;
        *out_armor_damage = target_armor - new_armor;
    } else {
        /* Special CS2D armors (201..205) do not lose armor points */
        *out_armor_damage = 0;
        switch (target_armor - 201) {
            case 0:  *out_hp_damage = (int)((float)raw_dmg * 0.75f); break; /* 201: Light Armor */
            case 1:  *out_hp_damage = (int)((float)raw_dmg * 0.50f); break; /* 202: Armor */
            case 2:  *out_hp_damage = (int)((float)raw_dmg * 0.25f); break; /* 203: Heavy Armor */
            case 3:  *out_hp_damage = (int)((float)raw_dmg * 0.50f); break; /* 204: Medic Armor */
            case 4:  *out_hp_damage = (int)((float)raw_dmg * 0.05f); break; /* 205: Super Armor */
            default: *out_hp_damage = raw_dmg; break;
        }
    }
}
