#ifndef CS2D_TYPES_H
#define CS2D_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CS2D_MAX_PLAYERS        32
#define CS2D_MAX_WEAPONS        100
#define CS2D_MAX_ENTITIES       16384 /* fun_roleplay has 9901 */
#define CS2D_MAX_NAME_LEN       32
#define CS2D_TILE_SIZE          32
#define CS2D_DEFAULT_PORT       36963
#define CS2D_MAX_CHAT_LEN       128

/* Teams */
typedef enum {
    CS2D_TEAM_SPEC = 0,
    CS2D_TEAM_T    = 1,
    CS2D_TEAM_CT   = 2,
    CS2D_TEAM_VIP  = 3
} cs2d_team_t;

/* Tile Modes (from cs2d_spec_map_format.txt section 2) */
typedef enum {
    CS2D_TILE_FLOOR_NOSOUND      = 0,
    CS2D_TILE_WALL               = 1,
    CS2D_TILE_OBSTACLE           = 2,
    CS2D_TILE_WALL_NOSHADOW      = 3,
    CS2D_TILE_OBSTACLE_NOSHADOW  = 4,
    CS2D_TILE_WALL_FLOORLEVEL    = 5,
    CS2D_TILE_FLOOR_DIRT         = 10,
    CS2D_TILE_FLOOR_SNOW         = 11,
    CS2D_TILE_FLOOR_STEP         = 12,
    CS2D_TILE_FLOOR_TILE         = 13,
    CS2D_TILE_FLOOR_WADE         = 14,
    CS2D_TILE_FLOOR_METAL        = 15,
    CS2D_TILE_FLOOR_WOOD         = 16,
    CS2D_TILE_DEADLY_NORMAL      = 50,
    CS2D_TILE_DEADLY_TOXIC       = 51,
    CS2D_TILE_DEADLY_EXPLOSION   = 52,
    CS2D_TILE_DEADLY_ABYSS       = 53
} cs2d_tile_mode_t;

/* Game Modes */
typedef enum {
    CS2D_MODE_STANDARD = 0,
    CS2D_MODE_DE       = 1, /* Bomb Defuse */
    CS2D_MODE_CS       = 2, /* Hostage Rescue */
    CS2D_MODE_AS       = 3, /* VIP Assassination */
    CS2D_MODE_CTF      = 4, /* Capture The Flag */
    CS2D_MODE_DOM      = 5, /* Domination */
    CS2D_MODE_DM       = 6, /* Deathmatch */
    CS2D_MODE_ZOMBIE   = 7  /* Zombie Infection */
} cs2d_gamemode_t;

/* Round States */
typedef enum {
    CS2D_ROUND_WAITING     = 0,
    CS2D_ROUND_FREEZETIME  = 1,
    CS2D_ROUND_PLAYING     = 2,
    CS2D_ROUND_ENDED       = 3
} cs2d_round_state_t;

/* Weapon Inventory Slots */
typedef enum {
    CS2D_SLOT_MELEE     = 0,
    CS2D_SLOT_PRIMARY   = 1,
    CS2D_SLOT_SECONDARY = 2,
    CS2D_SLOT_GRENADE   = 3,
    CS2D_SLOT_ITEM      = 4,
    CS2D_SLOT_BOMB      = 5,
    CS2D_SLOT_COUNT     = 6
} cs2d_weapon_slot_t;

/* Weapon IDs (verified from sys/weapons.cfg & cs2d_dedicated.exe) */
typedef enum {
    CS2D_WEP_NONE           = 0,
    CS2D_WEP_USP            = 1,
    CS2D_WEP_GLOCK          = 2,
    CS2D_WEP_DEAGLE         = 3,
    CS2D_WEP_P228           = 4,
    CS2D_WEP_ELITE          = 5,
    CS2D_WEP_FIVESEVEN      = 6,
    CS2D_WEP_M3             = 10,
    CS2D_WEP_XM1014         = 11,
    CS2D_WEP_MP5            = 20,
    CS2D_WEP_TMP            = 21,
    CS2D_WEP_P90            = 22,
    CS2D_WEP_MAC10          = 23,
    CS2D_WEP_UMP45          = 24,
    CS2D_WEP_AK47           = 30,
    CS2D_WEP_SG552          = 31,
    CS2D_WEP_M4A1           = 32,
    CS2D_WEP_AUG            = 33,
    CS2D_WEP_SCOUT          = 34,
    CS2D_WEP_AWP            = 35,
    CS2D_WEP_G3SG1          = 36,
    CS2D_WEP_SG550          = 37,
    CS2D_WEP_GALIL          = 38,
    CS2D_WEP_FAMAS          = 39,
    CS2D_WEP_M249           = 40,
    CS2D_WEP_SHIELD         = 41,
    CS2D_WEP_LASER          = 45,
    CS2D_WEP_FLAMETHROWER   = 46,
    CS2D_WEP_RPG7           = 47,
    CS2D_WEP_ROCKETLAUNCHER = 48,
    CS2D_WEP_GRENADELAUNCHER= 49,
    CS2D_WEP_KNIFE          = 50,
    CS2D_WEP_HE             = 51,
    CS2D_WEP_FLASHBANG      = 52,
    CS2D_WEP_SMOKE          = 53,
    CS2D_WEP_FLARE          = 54,
    CS2D_WEP_BOMB           = 55,
    CS2D_WEP_DEFUSEKIT      = 56,
    CS2D_WEP_KEVLAR         = 57,
    CS2D_WEP_KEVLARHELM     = 58,
    CS2D_WEP_NIGHTVISION    = 59,
    CS2D_WEP_GASMASK        = 60,
    CS2D_WEP_PRIMARYAMMO    = 61,
    CS2D_WEP_SECONDARYAMMO  = 62,
    CS2D_WEP_PLANTEDBOMB    = 63, /* Ground item only: the planted C4 (gfx/weapons/plantedbomb_d.bmp) */
    CS2D_WEP_MEDIKIT        = 64, /* 64..68: consumed when collected (Titem.give FUN_14020ce60) */
    CS2D_WEP_BANDAGE        = 65,
    CS2D_WEP_COINS          = 66,
    CS2D_WEP_MONEY          = 67,
    CS2D_WEP_GOLD           = 68,
    CS2D_WEP_MACHETE        = 69,
    CS2D_WEP_REDFLAG        = 70,
    CS2D_WEP_BLUEFLAG       = 71,
    CS2D_WEP_GAS            = 72,
    CS2D_WEP_MOLOTOV        = 73,
    CS2D_WEP_WRENCH         = 74,
    CS2D_WEP_SNOWBALL       = 75,
    CS2D_WEP_AIRSTRIKE      = 76,
    CS2D_WEP_MINE           = 77,
    CS2D_WEP_CLAW           = 78,
    CS2D_WEP_CHAINSAW       = 85,
    CS2D_WEP_GUTBOMB        = 86,
    CS2D_WEP_LASERMINE      = 87,
    CS2D_WEP_PORTALGUN      = 88,
    CS2D_WEP_SATCHEL        = 89, /* Satchel Charge (init_weapons name table: 0x2c8 / 8) */
    CS2D_WEP_M134           = 90,
    CS2D_WEP_FNF2000        = 91
} cs2d_weapon_id_t;

/* Authentic CS2D Map Entity Types (all 50 entities 0..96 from cs2d.com/entities.php & CS2D.exe) */
typedef enum {
    CS2D_ENT_INFO_T           = 0,  /* Info_T (Terrorist Spawn) */
    CS2D_ENT_INFO_CT          = 1,  /* Info_CT (Counter-Terrorist Spawn) */
    CS2D_ENT_INFO_VIP         = 2,  /* Info_VIP (VIP Spawn) */
    CS2D_ENT_INFO_HOSTAGE     = 3,  /* Info_Hostage (Hostage Spawn) */
    CS2D_ENT_INFO_RESCUE      = 4,  /* Info_RescuePoint (Hostage Rescue Zone) */
    CS2D_ENT_INFO_RESCUEPOINT = 4,  /* Info_RescuePoint alias */
    CS2D_ENT_INFO_BOMBSPOT    = 5,  /* Info_BombSpot (Bomb Plant Zone) */
    CS2D_ENT_INFO_ESCAPE      = 6,  /* Info_EscapePoint (VIP Escape Zone) */
    CS2D_ENT_INFO_ESCAPEPOINT = 6,  /* Info_EscapePoint alias */
    CS2D_ENT_INFO_TARGET      = 7,  /* Info_Target */
    CS2D_ENT_INFO_ANIMATION   = 8,  /* Info_Animation */
    CS2D_ENT_INFO_STORM       = 9,  /* Info_Storm */
    CS2D_ENT_INFO_TILEFX      = 10, /* Info_TileFX */
    CS2D_ENT_INFO_NOBUYING    = 11, /* Info_NoBuying */
    CS2D_ENT_INFO_NOWEAPONS   = 12, /* Info_NoWeapons */
    CS2D_ENT_INFO_NOFOW       = 13, /* Info_NoFOW */
    CS2D_ENT_INFO_QUAKE       = 14, /* Info_Quake */
    CS2D_ENT_INFO_CTF_FLAG    = 15, /* Info_CTF_Flag */
    CS2D_ENT_INFO_OLDRENDER   = 16, /* Info_OldRender */
    CS2D_ENT_INFO_DOM_POINT   = 17, /* Info_Dom_Point */
    CS2D_ENT_INFO_NOBUILDINGS = 18, /* Info_NoBuildings (ints[0]=w, ints[1]=h) */
    CS2D_ENT_INFO_BOTNODE     = 19, /* Info_BotNode */
    CS2D_ENT_INFO_TEAMGATE    = 20, /* Info_TeamGate (ints[0]=w, ints[1]=h) */
    CS2D_ENT_ENV_ITEM         = 21, /* Env_Item */
    CS2D_ENT_ENV_SPRITE       = 22, /* Env_Sprite */
    CS2D_ENT_ENV_SOUND        = 23, /* Env_Sound */
    CS2D_ENT_ENV_DECAL        = 24, /* Env_Decal */
    CS2D_ENT_ENV_BREAKABLE    = 25, /* Env_Breakable */
    CS2D_ENT_ENV_EXPLODE      = 26, /* Env_Explode */
    CS2D_ENT_ENV_HURT         = 27, /* Env_Hurt (ints[2]=w, ints[3]=h) */
    CS2D_ENT_ENV_IMAGE        = 28, /* Env_Image */
    CS2D_ENT_ENV_OBJECT       = 29, /* Env_Object */
    CS2D_ENT_ENV_BUILDING     = 30, /* Env_Building */
    CS2D_ENT_ENV_NPC          = 31, /* Env_NPC */
    CS2D_ENT_ENV_ROOM         = 32, /* Env_Room (ints[0]=w, ints[1]=h) */
    CS2D_ENT_ENV_LIGHT        = 33, /* Env_Light */
    CS2D_ENT_ENV_LIGHTSTRIPE  = 34, /* Env_LightStripe */
    CS2D_ENT_ENV_CUBE3D       = 35, /* Env_Cube3D */
    CS2D_ENT_GEN_PARTICLES    = 50, /* Gen_Particles */
    CS2D_ENT_GEN_SPRITES      = 51, /* Gen_Sprites */
    CS2D_ENT_GEN_WEATHER      = 52, /* Gen_Weather */
    CS2D_ENT_GEN_FX           = 53, /* Gen_FX */
    CS2D_ENT_FUNC_TELEPORT    = 70, /* Func_Teleport */
    CS2D_ENT_FUNC_DYNWALL     = 71, /* Func_DynWall */
    CS2D_ENT_FUNC_MESSAGE     = 72, /* Func_Message */
    CS2D_ENT_FUNC_GAMEACTION  = 73, /* Func_GameAction */
    CS2D_ENT_INFO_NOWEATHER   = 80, /* Info_NoWeather (ints[0]=w, ints[1]=h) */
    CS2D_ENT_INFO_RADARICON   = 81, /* Info_RadarIcon */
    CS2D_ENT_TRIGGER_START    = 90, /* Trigger_Start */
    CS2D_ENT_TRIGGER_MOVE     = 91, /* Trigger_Move */
    CS2D_ENT_TRIGGER_HIT      = 92, /* Trigger_Hit */
    CS2D_ENT_TRIGGER_USE      = 93, /* Trigger_Use */
    CS2D_ENT_TRIGGER_DELAY    = 94, /* Trigger_Delay */
    CS2D_ENT_TRIGGER_ONCE     = 95, /* Trigger_Once */
    CS2D_ENT_TRIGGER_IF       = 96, /* Trigger_If */
    CS2D_ENT_NONE             = 255
} cs2d_entity_type_t;

/* 2D Vector */
typedef struct {
    float x;
    float y;
} cs2d_vec2_t;

/* Weapon Definition (from FUN_1400e0fb0, FUN_14009b4c0 & FUN_140112bc0 in cs2d_dedicated.exe) */
typedef struct {
    uint8_t            id;
    const char        *name;              /* PTR_DAT_14059d4c8 */
    cs2d_weapon_slot_t slot;              /* mapped inventory slot */
    uint8_t            cs2d_slot;         /* PTR_DAT_14059d428 (0..6) */
    int                price;             /* PTR_DAT_14059d460 */
    int                damage;            /* PTR_DAT_14059d4a8 (dmg) */
    int                damage_z1;         /* PTR_DAT_14059d4a0 (dmg_z1 / alt) */
    int                damage_z2;         /* PTR_DAT_14059d498 (dmg_z2) */
    float              damage_zomb;       /* PTR_DAT_14059d490 (dmg_zomb, default 1.0f) */
    float              armor_penetration; /* 0.0 - 1.0 */
    int                clip_size;         /* PTR_DAT_14059d468 (ammoin) */
    int                max_reserve;       /* PTR_DAT_14059d470 (ammo) */
    int                fire_delay_ticks;  /* PTR_DAT_14059d488 (rate / firedelay) */
    int                reload_ticks;      /* PTR_DAT_14059d480 (reload) */
    float              fire_delay;        /* seconds between shots */
    float              reload_time;       /* seconds to reload */
    float              speed_offset;      /* PTR_DAT_14059d448 (speed delta) */
    float              speed_modifier;    /* 1.0 = base speed */
    int                dispersion;        /* PTR_DAT_14059d450 (dispersion) */
    float              spread;            /* normalized spread */
    int                recoil_accuracy;   /* PTR_DAT_14059d408 (from weapons_recoil.cfg) */
    int                recoil_recover;    /* PTR_DAT_14059d400 (from weapons_recoil.cfg) */
    int                recoil_attack;     /* PTR_DAT_14059d3f8 (from weapons_recoil.cfg) */
    int                visual_recoil;     /* 0x1409eda18 in CS2D.exe: visual backward kickback px on shot */
    int                muzzle;            /* 0x1409f5d20 in CS2D.exe: tracer / muzzle flash start offset px */
    int                range;             /* PTR_DAT_14059d458 (range, default 300) */
    bool               is_automatic;
    bool               shell_reload;      /* 0x1409efcf8 in CS2D.exe: reloads one shell per stage (M3, XM1014) */
} cs2d_weapon_def_t;

/* Player State (reconstructed from Tpl in CS2D.exe / cs2d_dedicated.exe) */
typedef struct {
    uint8_t     id;                  /* 1..32 */
    char        name[CS2D_MAX_NAME_LEN];
    cs2d_team_t team;
    uint8_t     look;                /* skin index (0..3) */
    cs2d_vec2_t pos;                 /* pixel position (Tpl +0x23c, +0x240) */
    cs2d_vec2_t vel;                 /* pixel velocity */
    float       angle;               /* degrees -180..180 or 0..360 (Tpl +0x248) */
    float       draw_angle;          /* rendered torso/weapon angle: angle + 6*slash_timer (Tpl +0x24c) */
    int         health;              /* 0..100 (or up to 250) */
    int         max_health;          /* default 100 */
    int         armor;               /* 0..200 (201..206 special armors in CS2D) */
    int         money;               /* 0..16000 */
    bool        alive;
    bool        bot;
    bool        has_helmet;
    bool        has_defuse_kit;
    bool        has_nightvision;
    bool        has_gasmask;
    bool        flashlight;          /* Tpl.flashlight (+0x300): switched on ([57], special byte 32) */
    bool        has_flag;           /* Tpl.flag: carries the enemy CTF flag (Titem.give 70 / 71, not an inventory item) */
    bool        is_vip;
    bool        is_planting;
    bool        is_defusing;
    bool        is_reloading;
    float       plant_progress;      /* 0.0 .. 3.0 */
    float       defuse_progress;     /* 0.0 .. 10.0 (or 5.0 with kit) */
    float       recoil;              /* visual backward kickback offset in px on shot (Tpl +0x308) */
    float       slash_timer;         /* melee swing timer: 8.0 -> 0.0 at 60 Hz (Tpl +0x31c) */
    float       feet_frame;          /* 0..7 leg animation frame (Tpl +0x380) */
    float       feet_angle;          /* leg movement direction deg (Tpl +0x388) */
    float       feet_timer;          /* leg movement active timer (Tpl +0x378) */

    /* Inventory */
    uint8_t     current_weapon;
    uint8_t     inventory[CS2D_SLOT_COUNT];     /* Main item of each slot (the last one given) */
    uint8_t     owned[CS2D_MAX_WEAPONS];        /* Every item held, 1 per id (Titem.list): HE, Flashbang and
                                                 * Smoke share CS2D_SLOT_GRENADE; grenade counts are clip_ammo */
    int16_t     clip_ammo[CS2D_MAX_WEAPONS];
    int16_t     reserve_ammo[CS2D_MAX_WEAPONS];
    uint8_t     weapon_mode[CS2D_MAX_WEAPONS];  /* Titem.mode (+0x1c) of each held item: attack2 state (silencer /
                                                 * burst on, zoom level 1..2, laser colour 0..3) */
    /* Tpl.random: the index Tpl.shoot reads TRandom.SeedTable with; starts at 0, is clamped to 1 and advances
     * (random + 1) % 200 per Tpl.shoot call (0x140209810, i.e. once per pellet) */
    int         shot_random;
    float       fire_cooldown;
    float       reload_timer;
    float       reload_ani;          /* Tpl.reloadani (+0x330): 15 at a clip reload's start / end, 8 per shell; torso
                                      * frame 3 while > 0 (Tpl.getFrame), decays 1 per 60 Hz frame */
    bool        reload_net;          /* Online: shells come from the server's [16,pid,4], not from the local timer */
    float       reload_overdue;      /* Online shell reload: seconds since the shell timer ran out with no [16,pid,4] */

    /* Stats */
    int         kills;
    int         deaths;
    int         assists;
    int         score;
    int         mvp;                 /* Tpl +0x294 */
    int         ping;                /* Tpl +0xd0, from [247]; 0xffff "Server", 0xfffe "Bot" */
    float       score_inc_time;      /* 60 Hz frames since the score changed (Tpl +0x288; scoreboard flash < 97.5) */
    float       assists_inc_time;    /* Same for the assists (Tpl +0x290) */
    float       dead_time;           /* 60 Hz frames since death (Tpl +0x29c; scoreboard fade < 130) */
    uint32_t    usgn_id;
    int         votekick;            /* Tpl.votekick: voted player id, 0 = none */
    int         votemap;             /* Tpl.votemap + 1: voted map cycle index + 1, 0 = none */
} cs2d_player_t;

/* Entity (exact structure from cs2d_spec_map_format.txt section 5) */
typedef struct {
    char               name[64];
    cs2d_entity_type_t type;
    int32_t            x;            /* tile x coordinate (0..width-1) */
    int32_t            y;            /* tile y coordinate (0..height-1) */
    char               trigger[64];
    int32_t            int_settings[10];
    char               str_settings[10][64];
    int                param1;       /* alias for int_settings[0] */
    int                param2;       /* alias for int_settings[1] */
} cs2d_entity_t;

/* A tile changed at runtime by settile (Tcustomtile, scope 0x140a021c0): its map values before the first change */
typedef struct {
    uint16_t x, y;
    uint8_t  old_tile;      /* Tcustomtile.oldvalue */
    uint8_t  old_mod;       /* Tcustomtile.oldmodifier */
    uint8_t  old_data[4];   /* Tcustomtile.olddata, in the tile_data layout */
} cs2d_customtile_t;

/* Map (exact structure from cs2d_spec_map_format.txt sections 1-5) */
typedef struct {
    /* Section (1) Header */
    char          header[64];
    uint8_t       scroll_map;
    uint8_t       use_modifiers;
    uint8_t       save_tile_heights;
    uint8_t       use_64x64_tiles;
    uint8_t       header_unused_bytes[6];
    int32_t       sys_uptime;
    int32_t       author_usgn_offset;
    int32_t       daylight_time;
    int32_t       header_unused_ints[7];
    char          author[64];
    char          save_program[64];
    char          check_string[64];
    char          tileset[64];
    uint8_t       tile_count;        /* highest tile index (actual count = tile_count + 1) */
    uint32_t      width;             /* map_xsize + 1 (in tiles) */
    uint32_t      height;            /* map_ysize + 1 (in tiles) */
    char          bg_image[64];
    int32_t       bg_scroll_x;
    int32_t       bg_scroll_y;
    uint8_t       bg_r;
    uint8_t       bg_g;
    uint8_t       bg_b;

    /* Section (2) Tile Modes & Section (3) Tile Heights */
    uint8_t       tile_modes[256];
    uint16_t      tile_heights[256];
    uint8_t       tile_3d_mods[256];

    /* Section (4) Map Grid (stored row-major [y * width + x], loaded column-major per spec) */
    uint8_t      *tiles;             /* tile frame indices [width * height] */
    uint8_t      *modifiers;         /* tile modifier bytes [width * height] (TileProp.Get 0x140010450):
                                      * bits 0-1 rotation (90 deg steps), bits 2-5 brightness b (b * 10 %, 0 = 100 %),
                                      * 0x40 blend frame, 0x80 color, 0xc0 string (unused) */
    uint8_t      *tile_data;         /* Tmap_d per tile [width * height * 4]: r, g, b, frame (overlay / blend) */
    uint8_t      *collision;         /* 0=floor, 1=wall, 2=obstacle, 3=deadly [width * height] */

    /* Section (5) Entities */
    cs2d_entity_t entities[CS2D_MAX_ENTITIES];
    uint32_t      entity_count;

    /* Runtime entity state (cs2d_entities.c): 0 = active/visible/closed, 1 = inactive/hidden/open.
     * Triggering an entity toggles it; the server sends the entities in state 1 when a client joins. */
    uint8_t       entity_state[CS2D_MAX_ENTITIES];
    uint32_t      entity_state_version;  /* Bumped on every state change (render caches) */
    uint32_t      entity_solid_version;  /* Bumped when a DynWall / Breakable changes (shadow map) */
    uint32_t      entity_table_version;  /* Bumped when the entity table itself changes (map load,
                                          * cs2d_ed_entity_add / _remove): the renderer's entity index cache */
    uint8_t      *base_collision;        /* Tile-only collision; entity overlays are applied on top */
    uint8_t      *object_collision;      /* Building overlay (cs2d_dynobj.c), applied over the entities; 0 = none */
    uint8_t      *object_shadow;         /* 1 where a solid building casts a wall shadow (shadow map) */

    /* Tcustomtile.list (0x140ddeb98): the tiles settile changed (cs2d_customtile.c) */
    cs2d_customtile_t *custom_tiles;
    uint32_t      custom_tile_count;
    uint32_t      custom_tile_cap;
} cs2d_map_t;

/* Weapon / item lying on the ground (server: spawnitem 33, drop 24, collect 25) */
#define CS2D_MAX_GROUND_ITEMS 256
/* Titem: a *dropped* item (Titem.dropped, +0x34) ages one step per **1 s tick** (DAT_140ddf540, the main
 * loop's 1000 ms timer 0x140236801) in the tail block both Titem.draw overloads share (0x140147b80 /
 * 0x1401483a0) and fades at the threshold 0x140a14d94 (= 15, .data), i.e. after 15 s. The whole block also
 * needs DAT_140ddf400 != 0, which is 1 only while this instance runs the server, so a network client never
 * expires items itself. A Lua fadeout hook returning 1 sets droptimer to 10000 instead, which stops the timer
 * (Titem.fadeout 0x14004cd20). */
#define CS2D_GROUND_ITEM_FADE 15
#define CS2D_GROUND_ITEM_VETO 10000
typedef struct {
    bool     active;
    uint16_t id;        /* Server item id */
    uint8_t  type;      /* Weapon / item type */
    float    x, y;      /* Tile center in px */
    int16_t  clip, reserve;
    bool     dropped;   /* Titem.dropped: created by a drop ([24]), not a map / server spawn */
    int      droptimer; /* Titem.droptimer: frames since the drop, or CS2D_GROUND_ITEM_VETO */
} cs2d_ground_item_t;

/* Game Simulation State */
typedef struct {
    cs2d_gamemode_t    gamemode;
    cs2d_round_state_t round_state;
    uint32_t           round_number;     /* Incremented by every round start (entity logic resets on change) */
    float              state_timer;
    float              round_time;       /* current remaining round time */
    float              freeze_time;      /* freeze time remaining */
    float              c4_time;          /* bomb countdown */

    /* Settings */
    float              cfg_roundtime;    /* default 120s */
    float              cfg_freezetime;   /* default 3s */
    float              cfg_c4timer;      /* default 35s */
    int                cfg_startmoney;   /* default 800 */
    int                cfg_maxmoney;     /* default 16000 */

    /* Objectives */
    bool               bomb_planted;
    cs2d_vec2_t        bomb_pos;
    uint8_t            bomb_planter;

    /* Scores & Economy */
    int                score_t;
    int                score_ct;
    int                loss_streak_t;
    int                loss_streak_ct;

    /* Players */
    cs2d_player_t      players[CS2D_MAX_PLAYERS + 1]; /* 1-indexed */
    cs2d_map_t         map;
} cs2d_game_t;

#endif /* CS2D_TYPES_H */

