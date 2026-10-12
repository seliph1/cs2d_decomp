#ifndef CS2D_CLIENT_H
#define CS2D_CLIENT_H

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "cs2d_types.h"
#include "cs2d_render.h"
#include "cs2d_config.h"
#include "cs2d_ui.h"
#include "cs2d_scene.h"
#include "cs2d_entities.h"
#include "cs2d_fx.h"
#include "cs2d_serverlist.h"
#include "cs2d_transfer.h"
#include "cs2d_editor.h"
#include "cs2d_spectate.h"
#include "cs2d_dynobj.h"
#include "cs2d_player.h"
#include "cs2d_bind.h"
#include "cs2d_hudmen.h"

#define CS2D_CONSOLE_MAX_LINES     1024 /* Ring buffer: the oldest line is dropped when full */
#define CS2D_CONSOLE_LINE_LEN      192
#define CS2D_CONSOLE_HIST_MAX      32
#define CS2D_SERVER_INFO_MAX_LINES 128

/* Weapon / item lying on the ground (server: spawnitem 33, drop 24, collect 25) */
#define CS2D_MAX_GROUND_ITEMS 256
typedef struct {
    bool     active;
    uint16_t id;        /* Server item id */
    uint8_t  type;      /* Weapon / item type */
    float    x, y;      /* Tile center in px */
    int16_t  clip, reserve;
} cs2d_ground_item_t;

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
    cs2d_ground_item_t ground_items[CS2D_MAX_GROUND_ITEMS];
    cs2d_dynobj_world_t dynobj;        /* Dynamic objects (buildings): Env_Building offline, the server's online */

    /* Construction (Tpl.attack2 with the Wrench, 0x1401fd6c0): the first right click shows the build helper,
     * the second opens the "Construct" menu (Tgui_hudmen menu 60) for the tile under the cursor */
    bool            build_mode;        /* DAT_140ddf474 */
    bool            build_menu_open;
    int             build_tx;          /* DAT_140ddf470 / DAT_140ddf46c: tile under the cursor */
    int             build_ty;
    float           build_rot;         /* DAT_140ddf468: player rotation when the menu opened */
    cs2d_buymenu_t  buy;               /* Buy menu (Tgui_hudmen menus 20..26, "buy" / "buyequip" commands) */

    /* Respawn game modes: mp_respawndelay ([252,6], 0x140a14e40[1]) and the local player's death time
     * (Tpl +0x350 timer) for "Respawn possible in $1 sec" */
    int             net_respawn_delay;
    uint32_t        death_ms;

    /* Thud_msg.add_center: red messages in the screen center (5 s, DAT_140a14c74) */
    char            center_msg[4][96];
    uint8_t         center_rgb[4][3];
    uint32_t        center_ms[4];

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
    cs2d_invsel_t   invsel;            /* Weapon selection HUD (invnext / invprev / slotN) */
    cs2d_binds_t    binds;             /* sys/controls.cfg */

    /* Input State */
    bool            key_forward;
    bool            key_back;
    bool            key_left;
    bool            key_right;
    bool            key_walk;
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
    int                 net_hovertext;     /* mp_hovertext (0x140a14e00[1], [252,6] / [237,16]): 1|2 players, 4 hostages, 8 buildings */
    char                net_password[64];  /* Server password sent in [252,1] (connect <ip> <port> [password]) */
    uint8_t             net_seen_seq[8192]; /* Reliable sequence numbers already handled (1 bit each) */
    cs2d_xfer_t         xfer;               /* Join-time file transfer [252,50] */
    bool                download_open;      /* "Downloading" window (Tgui_joinmen menu 10) */

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
    int                 srv_selected_map;
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
void cs2d_client_console_exec(cs2d_client_t *client, const char *cmd_line);
/* In-game chat (Thud_msg.add_chat / draw_chat 0x14024d8a0, FUN_1401de400) */
void cs2d_client_add_chat(cs2d_client_t *client, uint8_t sender_id, uint8_t mode, uint8_t r, uint8_t g, uint8_t b, const char *text);
void cs2d_client_send_chat(cs2d_client_t *client, uint8_t mode, const char *text);
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
void cs2d_client_poll_network(cs2d_client_t *client);

#endif /* CS2D_CLIENT_H */
