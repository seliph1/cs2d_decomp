#ifndef CS2D_CLIENT_H
#define CS2D_CLIENT_H

#include "cs2d_platform.h"

#include "cs2d_types.h"
#include "cs2d_render.h"
#include "cs2d_config.h"
#include "cs2d_ui.h"
#include "cs2d_scene.h"
#include "cs2d_entities.h"
#include "cs2d_fx.h"
#include "cs2d_gen.h"
#include "cs2d_serverlist.h"
#include "cs2d_transfer.h"
#include "cs2d_editor.h"
#include "cs2d_spectate.h"
#include "cs2d_dynobj.h"
#include "cs2d_player.h"
#include "cs2d_bind.h"
#include "cs2d_hudmen.h"
#include "cs2d_projectile.h"
#include "cs2d_hudtxt.h"
#include "cs2d_hostage.h"

#define CS2D_CONSOLE_MAX_LINES     1024 /* Ring buffer: the oldest line is dropped when full */
#define CS2D_CONSOLE_LINE_LEN      192
#define CS2D_CONSOLE_HIST_MAX      32
#define CS2D_SERVER_INFO_MAX_LINES 128
#define CS2D_LEADERBOARD_MAX       100 /* Ranks 1..100 (LeaderboardRequest.add: p1 - 1 < 100) */
#define CS2D_VOTE_MAX_MAPS         64

/* Ingame menu (Tgui_hudmen menu 2) and vote screen (menu 100) */
enum {
    CS2D_GAMEMENU_MAIN = 2,
    CS2D_GAMEMENU_VOTE = 100
};

/* LeaderboardPlayer (FUN_14005b790), filled by [230,1] */
typedef struct {
    bool    have;
    char    name[64];
    uint8_t id_type;    /* 0 U.S.G.N., 1 Steam */
    int64_t id;
    int     score, kills, deaths, assists, mvp, time_played;
} cs2d_lb_row_t;

typedef struct {
    char    text[CS2D_CONSOLE_LINE_LEN];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} cs2d_console_line_t;

/* In-game chat message line (Thud_msg.add_chat / draw_chat 0x14024d8a0) */
#define CS2D_CHAT_MAX_LINES 32
#define CS2D_CHAT_LINE_LEN  160
typedef struct {
    char        name[CS2D_MAX_NAME_LEN + 32]; /* Player name with optional "*DEAD* " or "(TEAM)" prefix */
    char        text[CS2D_CHAT_LINE_LEN];     /* Message text */
    uint8_t     name_rgb[3];                  /* Team color */
    uint8_t     text_rgb[3];                  /* Message color */
    uint32_t    time_ms;                      /* When received */
    float       fade;                         /* 1.0 down to 0.0 */
} cs2d_chat_line_t;

/* Kill message (Thud_msg fields of add_kill / draw_kill 0x14010f8d0): txt 0 killer (or the one who died), 1 weapon
 * name (or "died"), 2 victim, 3 assist ("killer + assist"); wpn is the weapon whose _k.bmp replaces txt 1 */
typedef struct {
    char     txt[4][CS2D_MAX_NAME_LEN + 32];
    uint8_t  rgb[4][3];
    int      wpn;      /* wpnimg[1]: 1..99 has an image, 0 text only */
    uint32_t timer_ms; /* g_time_ms when added */
    uint32_t show_ms;  /* hud_deathnotice_time * 1000 */
    float    fade;     /* 1, then -0.05 per 60 Hz frame once shown */
    float    offset;   /* Row height: TextHeight("I") + 4 while shown (17 at first), then -1 per frame */
} cs2d_killmsg_t;

/* New Game "Map" list (Tgui_ml::Load 0x1400af1d0): the .map files in maps/ grouped under the sys/maptypes.lst titles */
#define CS2D_MAPLIST_MAX_ROWS  512
#define CS2D_MAPLIST_MAX_TYPES 32
typedef struct {
    char name[64];  /* Map name (lowercase, without ".map") or group title */
    bool header;    /* Group title row (not selectable) */
} cs2d_maplist_row_t;

typedef struct {
    cs2d_maplist_row_t rows[CS2D_MAPLIST_MAX_ROWS];
    int                count;
} cs2d_maplist_t;


struct cs2d_client {
    cs2d_config_t   config;

    /* Scene state: the active scene and a deferred switch applied at the start of the next update */
    cs2d_scene_id_t scene;
    cs2d_scene_id_t pending_scene;
    bool            scene_switch_pending;
    char            pending_map[64];

    /* Lobby scene (its textures are private to cs2d_scene_lobby.c) */
    int             lobby_hover;

    /* Editor scene */
    cs2d_map_t      editor_map;
    cs2d_texture_t  editor_tileset;
    char            editor_map_name[64];
    float           editor_cam_x;
    float           editor_cam_y;
    cs2d_ed_state_t editor;

    /* Game scene */
    cs2d_game_t     game;
    char            game_map_name[64]; /* Map loaded by the game scene (e.g. "prison_funst3r") */
    cs2d_entity_world_t ents;          /* Name/trigger entity logic for game.map (simulated offline) */
    cs2d_fx_t       fx;                /* Breakable debris / explosion effects */
    cs2d_gen_t      gen;               /* Gen_Particles / Gen_Sprites / Gen_Weather / Gen_FX particles */
    cs2d_ground_item_t ground_items[CS2D_MAX_GROUND_ITEMS];
    cs2d_hostage_world_t hostages;     /* Thostage.list: map spawns, join snapshot [252,7,2], [29] / [26] */
    cs2d_dynobj_world_t dynobj;        /* Dynamic objects (buildings): Env_Building offline, the server's online */
    cs2d_hudtxt_t   hudtxt;            /* Lua HUD texts ([234] hudtxt, [54] hud text tweens) */
    cs2d_projectile_world_t projectiles; /* Projectiles & fields (grenades, rockets, molotov, flare, gas, etc.) */

    /* Construction (Tpl.attack2 with the Wrench, 0x1401fd6c0): the first right click shows the build helper,
     * the second opens the "Construct" menu (Tgui_hudmen menu 60) for the tile under the cursor */
    bool            build_mode;        /* DAT_140ddf474 */
    bool            build_menu_open;
    int             build_tx;          /* DAT_140ddf470 / DAT_140ddf46c: tile under the cursor */
    int             build_ty;
    float           build_rot;         /* DAT_140ddf468: player rotation when the menu opened */
    /* Supply item selection (Tgui_hudmen menu 61 Supply / 62 Super Supply), opened by the use key on an own supply */
    int             supply_menu;       /* 0 closed, 61 or 62 */
    int             supply_tx;         /* DAT_140ddf464 / DAT_140ddf460: the supply's tile */
    int             supply_ty;
    cs2d_buymenu_t  buy;               /* Buy menu (Tgui_hudmen menus 20..26, "buy" / "buyequip" commands) */
    int             game_menu;         /* CS2D_GAMEMENU_*: ESC menu or vote screen, 0 closed */
    cs2d_luamenu_t  luamenu;           /* Lua server menu (Tgui_hudmen menu 120, opcode 235 CS2D_NET_LUAMENU) */

    /* Respawn game modes: mp_respawndelay ([252,6], 0x140a14e40[1]) and the local player's death time
     * (Tpl +0x350 timer) for "Respawn possible in $1 sec" */
    int             net_respawn_delay;
    uint32_t        death_ms;

    /* Thud_msg.add_center: red messages in the screen center (5 s, DAT_140a14c74) */
    char            center_msg[4][96];
    uint8_t         center_rgb[4][3];
    uint32_t        center_ms[4];

    /* Thud_msg.add_kill (0x1401aef60): kill messages in the top right corner, oldest first (5 at most) */
#define CS2D_KILLMSG_MAX 5
    cs2d_killmsg_t  killmsgs[CS2D_KILLMSG_MAX];
    int             killmsg_count;

    /* In-game chat (Thud_msg.add_chat / draw_chat 0x14024d8a0, FUN_14029f7b0) */
#define CS2D_CHAT_MAX_LINES 32
#define CS2D_CHAT_LINE_LEN  160
    cs2d_chat_line_t chat_lines[CS2D_CHAT_MAX_LINES];
    int              chat_line_count;
    bool             chat_open;               /* Chat input prompt active */
    int              chat_mode;               /* 1 = all (Say : ), 2 = team (Say (Team) : ) */
    char             chat_input[CS2D_CHAT_LINE_LEN];
    int              chat_cursor;
    bool             chat_just_opened;        /* Suppress first text input from trigger key */

    int             fire_seed;         /* Shot counter sent with every fire packet (-1: none sent yet) */
    /* burstFire (Create 0x14004ef60, Update 0x14018c820): the two delayed shots of a burst-mode attack */
#define CS2D_MAX_BURSTS 16
    struct {
        uint8_t player, weapon;        /* Dropped when the shooter no longer holds weapon */
        float   angle;                 /* Aim at the attack + 10 + Rand(-5, 5); fired at angle - 10 + Rand(-5, 5) */
        float   delay;                 /* Seconds left */
    }               bursts[CS2D_MAX_BURSTS];
    uint32_t        attack2_ms;        /* attack2 timer (DAT_140a1b008): no new attack2 before this GetTickCount */
    bool            snapshot_states_reset; /* Join snapshot: entity states cleared before applying section 4 */
    uint32_t        ents_round;        /* game.round_number the entity logic was last reset for */
    int             last_tile_x;       /* Local player tile, to detect Trigger_Move / Func_Teleport entry */
    int             last_tile_y;

    /* "walkto" test command: follow a BFS tile path at running speed */
    float           aim_distance;      /* Pixels from the local player to the cursor (sent with rotations) */
    int             net_sent_x;        /* Last position / rotation sent to the server (-1 = none yet) */
    int             net_sent_y;
    int             net_sent_rot;
    bool            walk_active;
    cs2d_vec2_t     walk_path[1024];
    int             walk_len;
    int             walk_pos;
    uint8_t         local_player_id;
    float           cam_x;
    float           cam_y;
    cs2d_spec_t     spec;              /* Spectator camera while the local player is not alive */
    float           spec_tick;         /* Seconds since the last 1 s tick (DAT_140ddf540): [32] and showcase */
    float           item_tick;         /* Seconds since the last 1 s tick (DAT_140ddf540): ground item drop timer */
    cs2d_invsel_t   invsel;            /* Weapon selection HUD (invnext / invprev / slotN) */
    cs2d_binds_t    binds;             /* sys/controls.cfg */

    /* Input State */
    bool            key_forward;
    bool            key_back;
    bool            key_left;
    bool            key_right;
    bool            key_walk;
    bool            key_use;           /* Use held (DAT_140ddf488): releasing it aborts defusing with [30,4] */
    bool            mouse_fire;
    bool            mouse_attack2;
    float           mouse_screen_x;
    float           mouse_screen_y;

    /* Textures */
    cs2d_texture_t  tex_tileset;
    cs2d_texture_t  tex_ct;
    cs2d_texture_t  tex_t;
    cs2d_texture_t  tex_ak47;
    cs2d_texture_t  tex_m4a1;
    cs2d_texture_t  tex_deagle;

    /* Network */
    bool            connected;
    SOCKET          net_sock;
    struct sockaddr_in server_addr;
    float           net_sync_timer;
    uint16_t        net_send_seq;
    int             join_stage;
    char                net_raw_secret[128];
    char                net_map_name[128];
    char                net_map_checksum[65];
    char                net_server_name[64];
    int                 net_sv_fow;        /* sv_fow from [252,6] (enables the spectator "Fog" button) */
    int                 net_sv_gamemode;   /* sv_gamemode from [252,6]: 0 Normal, 1 Deathmatch, 2 Team DM, 3 Construction, 4 Zombies */
    uint64_t            bomb_beep_ms;      /* Next planted C4 beep (0x140a1afc8 timer), 0 = beep now */
    int                 net_flashlight;    /* mp_flashlight (0x140a14e10[1], [252,6] / [237,14]): 1 = players may switch theirs on */
    int                 net_hovertext;     /* mp_hovertext (0x140a14e00[1], [252,6] / [237,16]): 1|2 players, 4 hostages, 8 buildings, 16 items */
    uint8_t             net_supply_items[255]; /* mp_supply_items (0x140a14e28[1], [252,6] / [237,11]): one item id per char */
    int                 net_supply_count;
    bool                net_infammo;       /* mp_infammo (0x140a14e38[1], [252,6] after the respawn delay) */
    bool                net_forcelight;    /* sv_forcelight (0x140a14ee0[1], [252,6] / [237,19]): light engine at least 1 */
    int                 net_daylight;      /* sv_daylighttime (0x140a14ec8[1], [252,6] / [237,20]): sun angle, -1 = map */
    int                 net_hudscale;      /* mp_hudscale (0x140a14de8[1], [252,6] / [237,22]): 0 pixel, 1 850x480, 2 640x480 stretched, 3 640x480 centered */
    int                 net_maxplayers;    /* [252,6] (0x140a14f20) */
    bool                net_ff;            /* mp_friendlyfire (0x140a14f10) */
    int                 net_voicechat;     /* sv_voicechat (0x140a14ed8) */
    bool                net_recoil;        /* mp_recoil influences accuracy (0x140a14df0) */
    bool                net_offscreen;     /* sv_offscreendamage (0x140a14de0) */
    bool                net_physics;       /* physics (0x140a14df8) */
    bool                net_lua;           /* Lua scripts (DAT_140ddf3e4) */
    int                 net_os;            /* 1 Windows, 2 MacOS, 3 Linux (DAT_140ddf3f0) */
    int                 net_lag_ticks;     /* DAT_140ddf13c */
    int                 net_lagcomp;       /* DAT_140ddf140 */
    bool                net_leaderboard;   /* Leaderboard available (0x140a14dd8) */
    int                 net_ticks;         /* Server ticks from [247] (DAT_140ddf3e0), -1 until received */
    int                 net_seed;          /* Spread seed from [252,6] (TRandom.GenerateSeed, 0x1402d0560) */
    char                net_password[64];  /* Server password sent in [252,1] (connect <ip> <port> [password]) */
    uint8_t             net_seen_seq[8192]; /* Reliable sequence numbers already handled (1 bit each) */
    cs2d_xfer_t         xfer;               /* Join-time file transfer [252,50] */
    bool                download_open;      /* "Downloading" window (Tgui_joinmen menu 10) */
    int                 join_menu;          /* Tgui_joinmen.menu: 0 none, 1 password prompt, 2 error prompt */
    int                 join_error;         /* Tgui_joinmen.error: the refused join status */
    int                 join_opt;           /* Error 11 choice (Tgui_opt 0x140a10960): temp map, overwrite, cancel */
    char                join_pw_text[64];
    int                 join_pw_cursor;

    /* Declarative UI Context & Window States */
    cs2d_ui_context_t   ui;
    bool                console_open;
    bool                create_server_open;
    bool                options_open;
    bool                server_info_open;
    cs2d_console_line_t server_info_lines[CS2D_SERVER_INFO_MAX_LINES];
    int                 server_info_line_count;
    cs2d_ui_scroll_t    server_info_scroll;        /* Top visible line (0 = top of serverinfo) */
    int                 server_info_visible_lines;
    bool                briefing_open;             /* "Map Briefing": maps/<map>.txt (Tgui_tb 0x140a164b8) */
    cs2d_console_line_t briefing_lines[CS2D_SERVER_INFO_MAX_LINES];
    int                 briefing_line_count;
    cs2d_ui_scroll_t    briefing_scroll;
    int                 briefing_visible_lines;
    bool                leaderboard_open;          /* "Server Leaderboard" (Tgui_leadlist 0x140a15490) */
    cs2d_lb_row_t       lb_rows[CS2D_LEADERBOARD_MAX + 1];
    bool                lb_requested[CS2D_LEADERBOARD_MAX + 1]; /* LeaderboardRequest.list_top */
    uint8_t             lb_queue[CS2D_LEADERBOARD_MAX];         /* LeaderboardRequest.list: ranks to request */
    int                 lb_queue_count;
    int                 lb_queue_timer;            /* LeaderboardRequest.timer: 6 updates before each [230,1] */
    float               lb_frame_accum;
    int32_t             lb_timestamp;              /* LeaderboardPlayer.timestamp, 0 = no answer */
    cs2d_ui_scroll_t    lb_scroll;
    char                vote_maps[CS2D_VOTE_MAX_MAPS][64]; /* Server map cycle 0x140a0fd98 ([252,7,9]) */
    int                 vote_map_count;
    bool                team_menu_open;
    int                 team_menu_page;            /* 0 = Select Team, 1 = Terrorists, 2 = Counter-Terrorists */
    bool                servers_open;              /* "Servers" window (Find Servers) */
    int                 servers_tab;               /* cs2d_sv_tab_t */
    cs2d_ui_scroll_t    servers_scroll;            /* First visible row */
    int                 servers_visible_rows;
    uint32_t            servers_sel_ip;            /* Selected row (clicking it again joins) */
    uint16_t            servers_sel_port;
    int                 servers_filters;           /* Active filters in sys/filters.cfg */
    bool                connect_ip_open;           /* "Connect to IP" dialog */
    char                connect_ip_text[64];
    int                 connect_ip_cursor;
    cs2d_sv_browser_t   servers;
    int                 create_server_tab;
    int                 options_tab;
    char                srv_name[64];
    int                 srv_name_cursor;
    char                srv_password[64];
    int                 srv_password_cursor;
    int                 srv_port;
    int                 srv_max_players;
    int                 srv_gamemode_idx;
    int                 srv_fow_idx;
    bool                srv_reg_usgn;
    bool                srv_req_login;
    bool                srv_lan_mode;
    bool                srv_friendly_fire;
    bool                srv_auto_balance;
    char                srv_selected_map[64];      /* Map name picked in the "Map" tab */
    cs2d_maplist_t      srv_maplist;               /* Reloaded each time the window opens */
    bool                srv_maplist_loaded;
    cs2d_ui_scroll_t    srv_maplist_scroll;
    int                 opt_name_cursor;
    int                 opt_spray_idx;
    int                 opt_move_dir_mode;
    int                 opt_lang_idx;
    bool                opt_gore;
    bool                opt_auto_switch;
    bool                opt_hud_msgs;
    bool                opt_action_texts;
    bool                opt_simple_radar;
    bool                opt_dmg_indicator;
    bool                opt_chat_history;

    bool                show_fps;
    bool                show_scores;       /* "showscores" held (bind "tab", DAT_140ddf480): scoreboard */
    bool                quit_requested;
    cs2d_console_line_t console_lines[CS2D_CONSOLE_MAX_LINES];
    int                 console_line_head;      /* Ring index of the oldest line */
    int                 console_line_count;
    cs2d_ui_scroll_t    console_scroll;         /* First visible line; at the bottom the view follows new output */
    int                 console_visible_lines;  /* Lines that fit in the log box (set by the console layout) */
    char                console_input[CS2D_CONSOLE_LINE_LEN];
    int                 console_cursor;
    char                console_pending_cmd[CS2D_CONSOLE_LINE_LEN]; /* Submitted with Enter, run on the next update */
    char                console_history[CS2D_CONSOLE_HIST_MAX][CS2D_CONSOLE_LINE_LEN];
    int                 console_history_count;
    int                 console_history_pos;
    int                 ui_wheel_delta;

    /* FPS & Network Traffic Telemetry */
    int                 fps_current;
    int                 fps_frames;
    float               fps_timer;
    uint32_t            net_bytes_up_accum;
    uint32_t            net_bytes_down_accum;
    uint32_t            net_bps_up;
    uint32_t            net_bps_down;
};

/* Initialize client state and configuration; starts in the Game scene on map_name, or in the Lobby when NULL/empty */
bool cs2d_client_init(cs2d_client_t *client, const char *map_name);

/* Request a scene switch; it takes effect at the start of the next cs2d_client_update */
void cs2d_client_set_scene(cs2d_client_t *client, cs2d_scene_id_t scene);

/* Load map_name: changes map in place while in game, otherwise switches to the Game scene */
void cs2d_client_start_game(cs2d_client_t *client, const char *map_name);

/* True while a shared UI window (console, Create Server, Options, Server Info, Select Team) captures input */
bool cs2d_client_any_ui_open(const cs2d_client_t *client);

/* Fill ml from maps_dir (*.map) grouped by the prefixes in types_path (sys/maptypes.lst); returns the map count */
int cs2d_maplist_load(cs2d_maplist_t *ml, const char *maps_dir, const char *types_path);

/* Open the "Servers" window (Find Servers): loads the cached / favorite / recent lists and starts querying */
void cs2d_client_open_servers(cs2d_client_t *client);

/* Load welcome lines from sys/serverinfo.txt and parse UTF-16LE opcode 240 mode 51 lines */
bool cs2d_client_load_serverinfo(cs2d_client_t *client, const char *filepath);
void cs2d_client_serverinfo_add_utf16(cs2d_client_t *client, const uint8_t *utf16_le, int char_len);

/* Select team (0=Spec, 1=T, 2=CT) and skin look (0..3, 4=Auto, 5=Spec); sends opcode 20 online */
void cs2d_client_select_team(cs2d_client_t *client, uint8_t team, uint8_t look);

/* ESC pressed while no shared UI window is open */
void cs2d_client_handle_escape(cs2d_client_t *client);

/* Shutdown client and free resources */
void cs2d_client_shutdown(cs2d_client_t *client);

/* Process keyboard / mouse inputs */
void cs2d_client_handle_key(cs2d_client_t *client, int key, bool pressed);
void cs2d_client_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed);

/* Console UI & Command Interpreter */
void cs2d_client_console_print(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...);
/* Thud_msg.add_center: a message in the screen center; the same text again restarts its timer */
void cs2d_client_center_msg(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *text);
/* FUN_140167440 (pid, state, send): switch player pid's flashlight to state. Only alive players in a team while
 * mp_flashlight is 1 (not Terrorists in Zombies). With send, online, the request [57, state] goes to the server, whose
 * [57, pid, state] then sets it; otherwise (and offline) it is set and sfx/player/flashlight.wav plays at volume 0.5 */
void cs2d_client_flashlight(cs2d_client_t *client, int pid, int state, bool send);
void cs2d_client_console_exec(cs2d_client_t *client, const char *cmd_line);
/* Thud_msg.add_kill (0x1401aef60): a kill message (and its console line) for [19]; killer == victim is a suicide,
 * a weapon without name "<victim> died" */
void cs2d_client_add_kill(cs2d_client_t *client, int killer, int weapon, int victim, int assist);
/* In-game chat (Thud_msg.add_chat / draw_chat 0x14024d8a0, FUN_1401de400) */
void cs2d_client_add_chat(cs2d_client_t *client, uint8_t sender_id, uint8_t mode, uint8_t r, uint8_t g, uint8_t b, const char *text);
/* Ingame menu actions: leaderboard [230], votes [43], map briefing (maps/<map>.txt) */
void cs2d_client_leaderboard_reset(cs2d_client_t *client);
void cs2d_client_leaderboard_open(cs2d_client_t *client);
void cs2d_client_send_vote(cs2d_client_t *client, int type, int value);
void cs2d_client_on_vote(cs2d_client_t *client, int voter, int type, int value);
bool cs2d_client_briefing_open(cs2d_client_t *client);
void cs2d_client_send_chat(cs2d_client_t *client, uint8_t mode, const char *text);
void cs2d_client_send_luamenu(cs2d_client_t *client, uint8_t button);
bool cs2d_parse_color_code(const char *in, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b, const char **out_text);
bool cs2d_strip_center_tag(char *text);
void cs2d_client_handle_server_msg(cs2d_client_t *client, uint8_t sender_id, uint8_t mode, uint8_t r, uint8_t g, uint8_t b, const char *text);

/* i-th stored console line, 0 = oldest */
const cs2d_console_line_t *cs2d_client_console_line(const cs2d_client_t *client, int i);
void cs2d_client_handle_text_input(cs2d_client_t *client, const char *text);
void cs2d_client_handle_console_key(cs2d_client_t *client, int sdl_key);
void cs2d_client_handle_mouse_wheel(cs2d_client_t *client, int delta_y);

/* Update client game tick (runs at 60 FPS) */
void cs2d_client_update(cs2d_client_t *client, float delta_time);

/* Lay out the shared UI windows (console, Create Server, Options, Server Info, Select Team) without drawing them */
void cs2d_client_build_ui(cs2d_client_t *client, int screen_w, int screen_h);

/* Render entire frame (map, players, particles, HUD, Console, FPS, crosshair) */
void cs2d_client_render(cs2d_client_t *client, int screen_w, int screen_h);

/* Network functions */
bool cs2d_client_connect(cs2d_client_t *client, const char *ip, int port);
void cs2d_client_send_udp(cs2d_client_t *client, const uint8_t *payload, size_t len, bool reliable);
/* Opcode 40: tell the server a dropped item is gone (Titem.fadeout 0x14004cd20) - [40, id u16] */
void cs2d_client_send_kill_dropped_weapon(cs2d_client_t *client, uint16_t item_id);
/* One step of the ground item drop timer (Titem.draw 0x1401483a0): returns true when the item expires.
 * server_instance = DAT_140ddf400, second_tick = DAT_140ddf540 (see the definition in cs2d_client.c) */
bool cs2d_ground_item_step(cs2d_ground_item_t *it, bool server_instance, bool second_tick);
void cs2d_client_poll_network(cs2d_client_t *client);

/*
 * Field 13 of the [252,1] join request: "\xfa" + "0" + the 32-character install id of
 * sys/core/core.edc (+0x523) + the size of the client binary (CS2D.exe of this install, 14789120
 * bytes = what the original sent in the 2026-10-11 proxy capture). Reads the files on disk.
 */
void cs2d_client_join_core_id(char *out, size_t out_size);

#endif /* CS2D_CLIENT_H */
