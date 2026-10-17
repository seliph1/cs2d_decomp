#ifndef CS2D_SERVER_H
#define CS2D_SERVER_H

/*
 * Dedicated server (port of cs2d_dedicated.exe 1.0.1.8 p1).
 *
 * Phase 1: Tudp transport, connections, the join handshake [252,0..10] (FUN_1401dc530), the
 * snapshot [252,7,x], the server info [240] and the periodic ping messages (0x1401e505d).
 * Addresses in comments are cs2d_dedicated.exe unless they say CS2D.exe.
 */

#include "cs2d_types.h"
#include "cs2d_network.h"
#include "cs2d_platform.h"
#include "cs2d_map.h"
#include "cs2d_dynobj.h"

#define CS2D_SV_MAX_PLAYERS  32
#define CS2D_SV_MAX_PENDING  256   /* unacknowledged reliable datagrams per connection */
#define CS2D_SV_MAX_MAPS     256
#define CS2D_SV_MAX_CVARS    128

typedef struct {
    char key[32];
    char val[128];
} cs2d_sv_cvar_t;

/* Server settings: the cvar globals the [252,6] writer reads (FUN_1401dc530 near 0x1401ded48).
 * Defaults are the values the shipped sys/server.cfg produces (proxy capture of [252,6]). */
typedef struct {
    char    sv_name[64];         /* PTR_PTR_1405a9b10 */
    char    sv_map[64];          /* PTR_PTR_1405a18c0 (current map name) */
    char    sv_password[64];     /* PTR_DAT_1405a66d0 */
    int     sv_hostport;
    int     sv_maxplayers;       /* PTR_DAT_1405a66c0 +4 */
    int     sv_fow;              /* PTR_DAT_1405a66b8 */
    int     sv_specmode;         /* PTR_DAT_1405a6688 +1 */
    int     sv_gamemode;         /* PTR_DAT_1405a65e8 +1 */
    int     sv_usgnonly;         /* PTR_DAT_1405a66a0 +1 */
    int     sv_friendlyfire;     /* PTR_DAT_1405a66b0 +1 */
    int     sv_forcelight;       /* PTR_DAT_1405a6680 +1 */
    int     sv_daylighttime;     /* PTR_DAT_1405a6668 +4 (sent + 1) */
    int     sv_voicechat;        /* PTR_DAT_1405a6678 +1 */
    int     sv_offscreendamage;  /* PTR_DAT_1405a6580 +4 */
    int     sv_maxclientsperip;  /* DAT_1405a6514 (0 = no limit) */
    int     sv_antiaccel;        /* DAT_1405a64e4: > 0 pings with [229], else [249] */
    int     mp_roundtime;        /* PTR_DAT_1405a6640 +4 (minutes) */
    int     mp_freezetime;       /* PTR_DAT_1405a6638 +4 */
    int     mp_c4timer;          /* PTR_DAT_1405a65f0 +4 */
    int     mp_respawndelay;     /* PTR_DAT_1405a65e0 +4 */
    int     mp_deadchat;         /* 0 = alive cannot hear dead, 1 = dead can chat with all */
    int     mp_infammo;          /* PTR_DAT_1405a65d8 +1 */
    int     s_65d0;              /* PTR_DAT_1405a65d0 +1: not identified yet (1 by default) */
    char    mp_supply_items[128];/* PTR_DAT_1405a65c8: the id list, sent as one byte per item */
    char    s_65c0[64];          /* PTR_DAT_1405a65c0: string, not identified yet ("" by default) */
    int     s_65b8;              /* PTR_DAT_1405a65b8 +1: 127 by default, not identified yet */
    int     s_65b0;              /* PTR_DAT_1405a65b0 +4: 1 by default */
    int     mp_flashlight;       /* PTR_DAT_1405a65a8 +4 */
    int     mp_hovertext;        /* PTR_DAT_1405a65a0 +4 */
    int     mp_recoil;           /* PTR_DAT_1405a6590 +4 */
    int     mp_hudscale;         /* PTR_DAT_1405a6588 +4 */
    int     lagcompensation;     /* DAT_1405a64cc */
    int     ticks;               /* DAT_1405a64c8 */
    int     leaderboard;         /* PTR_DAT_1405a6578 +4 (sent only with DAT_1405a63e8) */
    int     physics;             /* PTR_DAT_1405a6598 +4 */
    int     sv_spraytransfer;    /* PTR_DAT_1405a6690 +1: [28,0,1] + the other players' sprays at join */
    int     sv_maptransfer;      /* Allow sending maps and related files to players */
    int     mp_startmoney;       /* PTR_DAT_1405a6628 +4 */
    int     mp_postspawn;        /* DAT_1405a64c4: seconds into a round a first team choice still spawns */
    int     mp_autoteambalance;  /* PTR_DAT_1405a65f8 +1 */
    char    sv_rcon[64];         /* PTR_DAT_1405a66e0: RCON password (max 50 chars, 0x14014908d) */
    char    sv_rconusers[128];   /* PTR_DAT_1405a6500: USGN IDs allowed to use RCON */
    /* Lua / gameplay settings (Phase 6) */
    char    mp_luaserver[64];
    int     mp_luamap;
    int     mp_randomspawn;
    int     mp_curtailedexplosions;
    int     mp_zombiespeedmod;
    int     mp_kevlar;
    int     mp_teleportreload;   /* DAT_140a14d48 (CS2D.exe .data = 5): seconds to charge / recharge a teleporter */
    int     mp_dispenser_health; /* DAT_140a14dac (= 10): health a Dispenser grants per frame to a player in range */
    int     mp_dispenser_money;  /* DAT_140a14db0 (= 500): money a Dispenser grants per frame (clamped at 16000) */
    int     mp_dmspawnmoney;     /* DAT_1405a6554 (= 2000): money each spawn of a respawn mode, max every 15 s */
    /* Generic cvar store */
    cs2d_sv_cvar_t cvars[CS2D_SV_MAX_CVARS];
    int     cvar_count;
} cs2d_sv_settings_t;

/* One reliable datagram waiting for its ACK */
typedef struct {
    uint16_t seq;
    uint16_t len;
    uint32_t last_send;
    uint8_t  data[1400];
} cs2d_sv_pending_t;

/* A Tudp connection (Tudp_con, PTR_DAT_1405f8088 while it is handled) */
typedef struct {
    bool               active;
    struct sockaddr_in addr;
    uint16_t           next_seq;        /* next reliable id (even, starts at 2) */
    uint8_t            seen[8192];      /* incoming reliable ids already handled */
    uint32_t           last_recv;
    int                join_step;       /* 0 none, 1 [252,0] answered, 2 player created, 3 map ok, 4 in game */
    int                player;          /* player id, 0 before [252,1] succeeded */
    /* The packet being built (Createpacket(reliable, new), vtable +0x98) */
    cs2d_pkt_t         out;
    bool               out_reliable;
    bool               out_open;
    cs2d_sv_pending_t *pending;         /* CS2D_SV_MAX_PENDING slots */
    uint32_t           bytes_in, bytes_out;
} cs2d_sv_conn_t;

#define CS2D_SV_MAX_GROUND_ITEMS 256

/* A ground item entity on the dedicated server (Titem) */
typedef struct {
    bool     active;
    uint16_t id;          /* unique ground item entity id */
    uint8_t  type;        /* weapon id */
    uint16_t clip;
    uint16_t reserve;
    uint8_t  sub;         /* mode / flags */
    uint16_t tile_x;
    uint16_t tile_y;
    bool     dropped;     /* created by a drop ([24]), fades after 15s ([40]) */
    uint32_t drop_time_ms;
    int      dropped_by;   /* player id who dropped the item; cannot re-pickup on the same tile until moving away */
    uint16_t drop_tx;
    uint16_t drop_ty;
} cs2d_sv_item_t;

#define CS2D_SV_MAX_HOSTAGES 32
#define CS2D_SV_MAX_BANS     64

/* A hostage entity on the dedicated server (Thostage) */
typedef struct {
    bool     active;
    uint8_t  id;           /* 1..N */
    float    x, y;
    float    rot;
    int      health;
    int      look;
    int      follow;       /* 0 = stay, >0 = CT player id */
    uint16_t target_tx;
    uint16_t target_ty;
    bool     rescued;
    bool     used;         /* first use gives +$150 bonus */
} cs2d_sv_hostage_t;

/* A ban entry on the dedicated server */
typedef struct {
    bool    active;
    char    ip[32];
    int32_t usgn;
    int64_t steam_id;
    char    reason[64];
} cs2d_sv_ban_t;

/* Tpl fields the join snapshot and the ping list read (FUN_1401dc530, 0x1401e505d) */
typedef struct {
    bool     used;
    int      conn;            /* index into conns, -1 for a bot */
    char     name[64];        /* +0x10 */
    char     spray[64];       /* +0x3c8 (puVar[0x79]) */
    int32_t  usgn;            /* +0x20 */
    int      os;              /* +0x78 */
    char     steam_name[64];  /* +0x68 */
    int64_t  steam_id;        /* +0x60 */
    char     usgn_name[64];   /* +0x28 */
    int      team;            /* +0x230 */
    int      spec_state;      /* +0x234 */
    int      look;            /* +0x23c */
    int      score, deaths, assists, mvp;
    int      rank;            /* +0x90 */
    float    x, y, rot;
    int      health, armor;
    int      maxhealth;       /* +0x268: max HP (default 100, [42] mode 8) */
    int      speedmod;        /* +0x390: speed modifier (default 0, [42] mode 7 sends speedmod + 100) */
    bool     joined;          /* +0xf4: finished [252,5], receives the ping list */
    /* Game state (phase 2 & 3) */
    bool     alive;           /* health object +0x260 > 0 */
    int      money;           /* +0x278 object */
    uint8_t  items[64];       /* +0x3f0 int array: the item types given at spawn, in order */
    int      item_count;
    int      weapon;          /* +0x2e0 held weapon type (0 = none) */
    int16_t  clip[CS2D_MAX_WEAPONS];
    int16_t  reserve[CS2D_MAX_WEAPONS];
    bool     is_reloading;    /* +0x320: reload state */
    uint32_t reload_end_ms;   /* +0x328: reload timer expiry timestamp */
    int      spectating_player; /* +0x238: 0 = free look, > 0 = player id */
    int      move_flag;       /* +0xfc: 1 ran, 2 walked since the last relay (FUN_1401a68e0) */
    int      rot_flag;        /* +0x118: 1 turned since the last relay */
    int      relay_ctr;       /* +0x164: 0..2, far players get every 3rd update */
    float    aim;             /* +0x3a0: aim distance ([70]) */
    bool     team_chosen;     /* +0x2d8: first team choice done (the mp_postspawn spawn happens once) */
    int      team_changes;    /* +0x2a0: team switches since last spawn */
    int      pending_team;    /* +0x400: team chosen with look 5, waiting for the skin */
    int      name_changes;    /* +0x2a4: name changes since last spawn (FUN_14010ddb0) */
    char     pending_name[64];/* +0x2a8: deferred name change while dead (applied on spawn) */
    bool     rcon_auth;       /* +0xf0: authenticated via [236] / [241] */
    int      special;         /* vtable +0x200: the special byte */
    /* Bomb planting / defusing (Phase 5) */
    bool     is_planting;
    uint32_t plant_start_ms;
    bool     is_defusing;
    float    last_valid_x, last_valid_y;
    uint32_t last_move_ms;
    uint32_t death_ms;
    bool     bot;             /* +0x198 */
    int      ping;            /* +0xd0 */
    char     ip[32];
    int      port;
    /* Spray logo data (sub-op 10 [28,2,...] / [28] relay) */
    uint8_t  spray_logo[1400];
    int      spray_logo_len;
    /* Construction: the [34] rate limit (cs2d_dedicated 0x1401cf2b9: opcode 34, 20 per 3000 ms) */
    int      build_ctr;
    uint32_t build_window_ms;
    /* Seconds since the last mp_dmspawnmoney grant (the Tpl +0x3fc counter, capped at 499) */
    int      money_secs;
} cs2d_sv_player_t;

typedef struct {
    SOCKET             sock;
    bool               offline;
    cs2d_sv_settings_t set;
    cs2d_map_t         map;
    bool               map_loaded;
    char               map_sha[65];
    char               secret[16];      /* PTR_PTR_1405a6800: the join secret */
    uint32_t           start_ms;        /* DAT_1407e0184 */
    uint32_t           round_start_ms;  /* PTR_DAT_1405ac780 +8 */
    int32_t            seed;            /* DAT_1407df3d4 */
    int                score_t, score_ct;
    cs2d_sv_conn_t     conns[CS2D_SV_MAX_PLAYERS + 8];
    cs2d_sv_player_t   players[CS2D_SV_MAX_PLAYERS + 1];
    cs2d_sv_item_t     items[CS2D_SV_MAX_GROUND_ITEMS];
    int                next_item_id;
    /* the one-second timer and its counters (0x1401e505d) */
    uint32_t           second_ms;
    int                ping_ctr, pinglist_ctr;
    int                frames, fps;     /* DAT_1407dfe28 */
    uint32_t           bytes_in_sec, bytes_out_sec; /* DAT_1407dfe34 / DAT_1407dfe30 (bits) */
    uint32_t           bytes_in_acc, bytes_out_acc;
    /* serverinfo lines (sys/serverinfo.txt) */
    char             (*info_lines)[256];
    int                info_count;
    /* section 12 map list */
    char               maps[CS2D_SV_MAX_MAPS][64];
    int                map_count;
    /* Round state (phase 2) */
    int                round_end;       /* DAT_1407dfe6c: pending round end reason, 0 = round running */
    int                round_end_delay; /* DAT_1407dfe70 (ms) */
    uint32_t           round_end_ms;
    int                round_end_type;  /* the type FUN_14015c840 was called with (winner/MVP source) */
    int                round_winner;    /* _DAT_1407dfe78: 1 T, 2 CT, 0 draw (FUN_14015c080) */
    int                round_mvp;       /* _DAT_1407dfe7c: MVP player id */
    /* Standard game mode (sv_gamemode 0) mission state (phase 5) */
    int                round_phase;     /* DAT_1407dfea0: 0 freezetime, 1 playing (2 = decided; our
                                         * port keeps phase 1 and gates the time out on bomb_planted) */
    uint32_t           round_phase_ms;  /* start of the current phase */
    uint32_t           round_clock_ms;  /* round start / clock start (mp_roundtime counts from here) */
    int                mission_vips;      /* DAT_1407dfac8: map("mission_vips") */
    int                mission_hostages;  /* DAT_1407dfac4: map("mission_hostages") */
    int                mission_bombspots; /* DAT_1407dfac0: map("mission_bombspots") */
    int                vip_id;          /* this round's VIP player (team 3), 0 = none */
    int                hostages_rescued;/* hostages that reached a rescue point this round */
    int                round_number;    /* DAT_1407dfe9c: rounds played on this map */
    void              *lua;             /* cs2d_sv_lua_t (phase 4) */
    /* Hostages (Phase 5) */
    cs2d_sv_hostage_t  hostages[CS2D_SV_MAX_HOSTAGES];
    int                hostage_count;
    /* Bomb state (Phase 5) */
    bool               bomb_planted;
    uint16_t           bomb_item_id;
    uint16_t           bomb_tx, bomb_ty;
    uint32_t           bomb_plant_time;
    uint32_t           bomb_explode_time;
    int                bomb_planter;
    int                defuser_id;
    uint32_t           defuse_start_ms;
    uint32_t           defuse_duration_ms;
    /* Ban list (Phase 5) */
    cs2d_sv_ban_t      bans[CS2D_SV_MAX_BANS];
    int                ban_count;
    /* Dynamic Objects (Phase 6, Tdo) */
    cs2d_dynobj_world_t dynobj;
} cs2d_server_t;

void cs2d_sv_settings_default(cs2d_sv_settings_t *s);
/* Reads "key value" lines of a server.cfg; unknown keys are stored in cvars. False when the file is missing */
bool cs2d_sv_settings_load(cs2d_sv_settings_t *s, const char *path);
const char *cs2d_sv_settings_get(const cs2d_sv_settings_t *s, const char *key);
void cs2d_sv_settings_set(cs2d_sv_settings_t *s, const char *key, const char *val);

bool cs2d_server_start(cs2d_server_t *sv, const char *map, int port, bool offline);
void cs2d_server_frame(cs2d_server_t *sv);
void cs2d_server_stop(cs2d_server_t *sv);

/* Internal helpers shared with cs2d_sv_game.c */
cs2d_pkt_t     *cs2d_sv_packet(cs2d_server_t *sv, cs2d_sv_conn_t *c, bool reliable, bool fresh);
cs2d_sv_conn_t *cs2d_sv_conn_of_player(cs2d_server_t *sv, int id);
uint32_t        cs2d_sv_now(void);
int             cs2d_sv_recv_payload(cs2d_server_t *sv, cs2d_sv_conn_t *c, const uint8_t *buf, int n); /* test entry: bytes left unread */

/* cs2d_sv_game.c: team, spawn, movement relay, rounds, playerdata, admin actions (phase 2 & 3) */
void cs2d_sv_team(cs2d_server_t *sv, int id, int team, int look);          /* FUN_1401624c0 (force = false) */
void cs2d_sv_team_ex(cs2d_server_t *sv, int id, int team, int look, bool force); /* FUN_1401624c0 */
void cs2d_sv_spawn(cs2d_server_t *sv, int id);                             /* FUN_140160740 */
void cs2d_sv_spawn_at(cs2d_server_t *sv, int id, float custom_x, float custom_y); /* FUN_14017c910 */
void cs2d_sv_relay_movement(cs2d_server_t *sv);                            /* FUN_1401a68e0 */
int  cs2d_sv_count_team(const cs2d_server_t *sv, int team);                /* FUN_140026150 / FUN_140026210 */
void cs2d_sv_write_spawn(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl); /* [21] */
int  cs2d_sv_rand(int lo, int hi);                                         /* FUN_1403977c0: Rand(lo, hi) */
void cs2d_sv_round_end(cs2d_server_t *sv, int mode, int delay_ms);         /* FUN_14015c840 */
void cs2d_sv_round_start(cs2d_server_t *sv, int reason);                   /* FUN_1402520b0 / FUN_140123530 */
void cs2d_sv_step_round(cs2d_server_t *sv);                                /* check round end conditions & timer */
void cs2d_sv_check_win(cs2d_server_t *sv);                                 /* FUN_14015ce30(0): the elimination rules */
/* Standard game mode (sv_gamemode 0): mission objectives and rounds (FUN_14015ce30, FUN_14015c840) */
void cs2d_sv_count_missions(cs2d_server_t *sv);                            /* DAT_1407dfac0/4/8 counts */
void cs2d_sv_round_end_ex(cs2d_server_t *sv, int type, int mode, int delay_ms, int player_id); /* FUN_14015c840 */
void cs2d_sv_assign_vip(cs2d_server_t *sv);                                /* 0x1401717a1: team 3 + 200 armor */
void cs2d_sv_vip_check(cs2d_server_t *sv);                                 /* VIP death (0xa) / escape (0xb) */
void cs2d_sv_hostage_escaped(cs2d_server_t *sv, int h_id, int ct_id);      /* rescue point: >50 % -> 0x1f */
void cs2d_sv_select_weapon(cs2d_server_t *sv, int id, int weapon, int mode); /* FUN_140158060: [9] */
void cs2d_sv_fire(cs2d_server_t *sv, int id, int seed);                    /* FUN_140191a70: [7] */
void cs2d_sv_attack2(cs2d_server_t *sv, int id, int mode);                 /* FUN_1401fd590: [8] */
void cs2d_sv_damage(cs2d_server_t *sv, int victim_id, int attacker_id, int dmg, int weapon); /* [17] */
void cs2d_sv_kill(cs2d_server_t *sv, int victim_id, int killer_id, int assist_id, int weapon); /* FUN_1401647d0: [19] */
void cs2d_sv_buy(cs2d_server_t *sv, int id, int weapon_id);                /* FUN_1401cd710: [23] */
void cs2d_sv_drop(cs2d_server_t *sv, int id, int type, int clip, int reserve, int sub);       /* FUN_14016bbd0: [24] */
void cs2d_sv_reload(cs2d_server_t *sv, int id, int mode);                                      /* FUN_14018b1c0: [16] */
void cs2d_sv_chat(cs2d_server_t *sv, int id, int mode, int char_len, const uint16_t *chars); /* FUN_140153900: [240] */
void cs2d_sv_spec_target(cs2d_server_t *sv, int id, int target_pid);                           /* 0x140108260: [67] */
void cs2d_sv_spec_pos(cs2d_server_t *sv, int id, int sx, int sy);                             /* FUN_1401ced00: [32] */
void cs2d_sv_step_items(cs2d_server_t *sv);                                                   /* check item pickups and 15s drop fadeout [40] */
void cs2d_sv_step_reloads(cs2d_server_t *sv);                                                 /* FUN_140194650: tick reload timers */
bool cs2d_sv_validate_move(cs2d_server_t *sv, int id, float new_x, float new_y, int op);       /* FUN_1401c99a0 / FUN_1401ca500 */
void cs2d_sv_step_respawns(cs2d_server_t *sv);                                                 /* FUN_14017cbb0 */
void cs2d_sv_request_respawn(cs2d_server_t *sv, int id);                                       /* FUN_1401cf8d0: [39] */
void cs2d_sv_change_name(cs2d_server_t *sv, int id, const char *new_name, bool force, bool hide_msg); /* FUN_14010ddb0: [238] */
void cs2d_sv_set_playerdata(cs2d_server_t *sv, int id, int mode, int val);                     /* FUN_140159840: [42] */
void cs2d_sv_set_pos(cs2d_server_t *sv, int id, float x, float y);                             /* FUN_140102950: [15] */
void cs2d_sv_set_teamscores(cs2d_server_t *sv, int score_t, int score_ct);                     /* FUN_140051ac0: [64] */
void cs2d_sv_msg(cs2d_server_t *sv, const char *text, int target_id);                          /* FUN_140153900: [240,0,1] */
void cs2d_sv_slap(cs2d_server_t *sv, int id, bool death);                                      /* FUN_140167e40 */

/* Phase 5: Bomb, Hostages, Use, Projectiles, Flashlight, Bots, Bans, Map Change */
void cs2d_sv_plant_start(cs2d_server_t *sv, int id);                          /* FUN_1401588b0: [30, 1] */
void cs2d_sv_plant_abort(cs2d_server_t *sv, int id);                          /* FUN_1401588b0: [30, 2] */
void cs2d_sv_plant_complete(cs2d_server_t *sv, int id);                       /* FUN_1401588b0: [30, 3] */
void cs2d_sv_defuse_start(cs2d_server_t *sv, int id, uint16_t tx, uint16_t ty); /* FUN_140138a40: [26, 3] */
void cs2d_sv_defuse_abort(cs2d_server_t *sv, int id);                         /* FUN_140169830: [30, 4] */
void cs2d_sv_defuse_complete(cs2d_server_t *sv, int id);                      /* FUN_140169830: [30, 5] */
void cs2d_sv_bomb_explode(cs2d_server_t *sv);                                 /* FUN_140169830: [30, 6] */
void cs2d_sv_step_bomb(cs2d_server_t *sv);
void cs2d_sv_use(cs2d_server_t *sv, int id, int mode, int32_t data, uint16_t x, uint16_t y); /* FUN_140138a40: [26] */

/*
 * Construction (sv_gamemode 3): buildings (Tdo category 1) and the Wrench.
 * The server side is built from cs2d_dedicated.exe FUN_140175550 (the [34] receive handler 0x1401cf0c0 calls
 * it) and from the same code in CS2D.exe (docs/dump/cs2d/_global/1401f0000.c FUN_1401fab30) plus Tdo.damage
 * (0x140035110), Tdo.doupgrade (0x140143d60), Tdo.counttype (0x140035680) and Tdo.sametypetoclose (0x14004a2b0).
 */

/* [34, typ, tx u16, ty u16, mode, rot + 1000 u16]: the client asked to build (or, tx == ty == 0xffff, to
 * remove) `typ` on the tile under its cursor. Answers with the 15 byte build result to everyone. */
void cs2d_sv_build(cs2d_server_t *sv, int id, int typ, int tx, int ty, int mode, float rot);

/* Per frame: Construction Site timers ([36,id,1]), Teleporter Entrance charge ([36,id,5]), turret target
 * scan ([36,id,0,target]) and the turret burst damage. */
void cs2d_sv_step_buildings(cs2d_server_t *sv);

/* Tdo.damage: subtract `damage` from object `obj_id` (player `player` gets the credit, 0 = no owner).
 * Applies the friendly-buildings rule, the max health clamp, the [37] health message and the kill. */
void cs2d_sv_object_damage(cs2d_server_t *sv, int obj_id, int damage, int player);
/* Tdo.doupgrade: `points` Wrench hits on object `obj_id` by `player`. 1 when it still stands. */
int  cs2d_sv_object_upgrade(cs2d_server_t *sv, int obj_id, int player, int points);

/* The object half of an explosion (FUN_1401f1130): objects within `radius` of (x, y) take the linear
 * falloff of `damage` through Tdo.damage. Used by the C4 blast; a full Tshoot/explosion pass would feed it
 * the grenades and rockets too. */
void cs2d_sv_explosion_objects(cs2d_server_t *sv, float x, float y, float radius, int damage, int attacker);

/* A shot of player `id` that reaches a building: the Tshoot.Shoot server branch (an enemy wall / turret /
 * dispenser takes the weapon's damage). Called from cs2d_sv_fire for every non-melee shot. */
void cs2d_sv_shot_at_building(cs2d_server_t *sv, int id, const cs2d_weapon_def_t *wep);

/* The Wrench primary attack (cs2d_sv_fire): repair or upgrade the own building on the player's aim tile.
 * Costs $50 per repair and $100 per upgrade ([42,id,2,money]; Tshoot.Shoot 0x14020745f / 0x1402096xx). */
void cs2d_sv_wrench_hit(cs2d_server_t *sv, int id);

/* The dedicated keeps the building health in points: raises every category 1 object to its max health
 * after a map load (the join snapshot and [37] send Tdo.healthpercent / Tdo.hpupnetvalue). */
void cs2d_sv_fix_building_health(cs2d_server_t *sv);
void cs2d_sv_init_hostages(cs2d_server_t *sv);
void cs2d_sv_step_hostages(cs2d_server_t *sv);
void cs2d_sv_damage_hostage(cs2d_server_t *sv, int hostage_id, int attacker_id, int dmg);
void cs2d_sv_hostage_use(cs2d_server_t *sv, int id, int hostage_id, int mode);

void cs2d_sv_projectile(cs2d_server_t *sv, int id, int type, uint16_t x, uint16_t y, uint16_t dir, uint16_t flydist); /* FUN_1401d4910: [27] */
void cs2d_sv_kill_projectile(cs2d_server_t *sv, int id, int proj_id);         /* FUN_1401cfd00: [41] */
void cs2d_sv_flashlight(cs2d_server_t *sv, int id, int state);                /* FUN_1401d28b0: [57] */

int  cs2d_sv_bot_add(cs2d_server_t *sv, int team, int look, const char *name);
void cs2d_sv_bot_kick(cs2d_server_t *sv, const char *name_or_id);
void cs2d_sv_bot_kill(cs2d_server_t *sv, const char *name_or_id);

bool cs2d_sv_is_banned(const cs2d_server_t *sv, const char *ip, int32_t usgn, int64_t steam_id, char *out_reason, size_t reason_size);
void cs2d_sv_ban_ip(cs2d_server_t *sv, const char *ip, const char *reason);
void cs2d_sv_ban_usgn(cs2d_server_t *sv, int32_t usgn, const char *reason);
void cs2d_sv_ban_steam(cs2d_server_t *sv, int64_t steam_id, const char *reason);
void cs2d_sv_unban(cs2d_server_t *sv, const char *target);

bool cs2d_sv_change_map(cs2d_server_t *sv, const char *mapname);               /* FUN_1401104f0: [239] */

/* cs2d_server.c: kick, RCON ([236]/[241]/[242]), server command parser (FUN_140140a00) */
void cs2d_sv_kick(cs2d_server_t *sv, int id, const char *reason);                              /* FUN_1401502a0: [253] */
void cs2d_sv_rcon_check(cs2d_server_t *sv, int id, const uint8_t *enc_pw, int enc_pw_len);     /* FUN_1401d8d90: [236] */
void cs2d_sv_rcon(cs2d_server_t *sv, int id, const uint8_t *enc_pw, int enc_pw_len,
                  const uint8_t *enc_cmd, int enc_cmd_len);                                    /* FUN_1401d9990: [241] */
void cs2d_sv_public_rcon(cs2d_server_t *sv, const struct sockaddr_in *from,
                         const char *pw, const char *cmd);                                     /* FUN_1401be1c0: [242] */
bool cs2d_sv_exec(cs2d_server_t *sv, const char *cmd_line);                                    /* FUN_140140a00 */

/* Console output: the dedicated prints every line with its colour prefix stripped */
void cs2d_server_print(const char *fmt, ...);
void cs2d_sv_set_active(cs2d_server_t *sv);
cs2d_server_t *cs2d_sv_get_active(void);

/* [252,6,0,...]: the game settings of the join (FUN_1401dc530 0x1401ded48), exposed for the tests */
void cs2d_sv_build_join_settings(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m, int score_t,
                                 int score_ct, int32_t round_ms, int32_t seed);
/* One player record of snapshot section 1 (FUN_1401dc530) */
void cs2d_sv_write_player_record(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl);
/* FUN_14010d970: the name the server gives a joining player (trim, filter, unique) */
void cs2d_sv_sanitize_name(const cs2d_server_t *sv, const char *in, char *out, size_t out_size);

#endif /* CS2D_SERVER_H */
