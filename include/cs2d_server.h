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

#define CS2D_SV_MAX_PLAYERS  32
#define CS2D_SV_MAX_PENDING  256   /* unacknowledged reliable datagrams per connection */
#define CS2D_SV_MAX_MAPS     256

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
    int     mp_startmoney;       /* PTR_DAT_1405a6628 +4 */
    int     mp_postspawn;        /* DAT_1405a64c4: seconds into a round a first team choice still spawns */
    int     mp_autoteambalance;  /* PTR_DAT_1405a65f8 +1 */
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
    bool     joined;          /* +0xf4: finished [252,5], receives the ping list */
    /* Game state (phase 2) */
    bool     alive;           /* health object +0x260 > 0 */
    int      money;           /* +0x278 object */
    uint8_t  items[64];       /* +0x3f0 int array: the item types given at spawn, in order */
    int      item_count;
    int      weapon;          /* +0x2e0 held weapon type (0 = none) */
    int      move_flag;       /* +0xfc: 1 ran, 2 walked since the last relay (FUN_1401a68e0) */
    int      rot_flag;        /* +0x118: 1 turned since the last relay */
    int      relay_ctr;       /* +0x164: 0..2, far players get every 3rd update */
    float    aim;             /* +0x3a0: aim distance ([70]) */
    bool     team_chosen;     /* +0x2d8: first team choice done (the mp_postspawn spawn happens once) */
    int      team_changes;    /* +0x2a0 */
    int      pending_team;    /* +0x400: team chosen with look 5, waiting for the skin */
    int      special;         /* vtable +0x200: the special byte */
    bool     bot;             /* +0x198 */
    int      ping;            /* +0xd0 */
    char     ip[32];
    int      port;
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
} cs2d_server_t;

void cs2d_sv_settings_default(cs2d_sv_settings_t *s);
/* Reads "key value" lines of a server.cfg; unknown keys are ignored. False when the file is missing */
bool cs2d_sv_settings_load(cs2d_sv_settings_t *s, const char *path);

bool cs2d_server_start(cs2d_server_t *sv, const char *map, int port, bool offline);
void cs2d_server_frame(cs2d_server_t *sv);
void cs2d_server_stop(cs2d_server_t *sv);

/* Internal helpers shared with cs2d_sv_game.c */
cs2d_pkt_t     *cs2d_sv_packet(cs2d_server_t *sv, cs2d_sv_conn_t *c, bool reliable, bool fresh);
cs2d_sv_conn_t *cs2d_sv_conn_of_player(cs2d_server_t *sv, int id);
uint32_t        cs2d_sv_now(void);

/* cs2d_sv_game.c: team, spawn, movement relay (phase 2) */
void cs2d_sv_team(cs2d_server_t *sv, int id, int team, int look);          /* FUN_1401624c0 */
void cs2d_sv_spawn(cs2d_server_t *sv, int id);                             /* FUN_140160740 */
void cs2d_sv_relay_movement(cs2d_server_t *sv);                            /* FUN_1401a68e0 */
int  cs2d_sv_count_team(const cs2d_server_t *sv, int team);                /* FUN_140026150 / FUN_140026210 */
void cs2d_sv_write_spawn(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl); /* [21] */
int  cs2d_sv_rand(int lo, int hi);                                         /* FUN_1403977c0: Rand(lo, hi) */

/* Console output: the dedicated prints every line with its colour prefix stripped */
void cs2d_server_print(const char *fmt, ...);

/* [252,6,0,...]: the game settings of the join (FUN_1401dc530 0x1401ded48), exposed for the tests */
void cs2d_sv_build_join_settings(cs2d_pkt_t *p, const cs2d_sv_settings_t *s, const cs2d_map_t *m, int score_t,
                                 int score_ct, int32_t round_ms, int32_t seed);
/* One player record of snapshot section 1 (FUN_1401dc530) */
void cs2d_sv_write_player_record(cs2d_pkt_t *p, int id, const cs2d_sv_player_t *pl);
/* FUN_14010d970: the name the server gives a joining player (trim, filter, unique) */
void cs2d_sv_sanitize_name(const cs2d_server_t *sv, const char *in, char *out, size_t out_size);

#endif /* CS2D_SERVER_H */
