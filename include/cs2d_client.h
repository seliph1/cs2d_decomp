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

#define CS2D_CONSOLE_MAX_LINES 1024 /* Ring buffer: the oldest line is dropped when full */
#define CS2D_CONSOLE_LINE_LEN  192
#define CS2D_CONSOLE_HIST_MAX  32

typedef struct {
    char    text[CS2D_CONSOLE_LINE_LEN];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} cs2d_console_line_t;

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

    /* Game scene */
    cs2d_game_t     game;
    char            game_map_name[64]; /* Map loaded by the game scene (e.g. "prison_funst3r") */
    cs2d_entity_world_t ents;          /* Name/trigger entity logic for game.map (simulated offline) */
    bool            snapshot_states_reset; /* Join snapshot: entity states cleared before applying section 4 */
    uint32_t        ents_round;        /* game.round_number the entity logic was last reset for */
    int             last_tile_x;       /* Local player tile, to detect Trigger_Move / Func_Teleport entry */
    int             last_tile_y;

    /* "walkto" test command: follow a BFS tile path at running speed */
    bool            walk_active;
    cs2d_vec2_t     walk_path[1024];
    int             walk_len;
    int             walk_pos;
    uint8_t         local_player_id;
    float           cam_x;
    float           cam_y;

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
    char            net_raw_secret[128];
    char            net_map_name[128];
    char            net_map_checksum[65];

    /* Declarative UI Context & Window States */
    cs2d_ui_context_t   ui;
    bool                console_open;
    bool                create_server_open;
    bool                options_open;
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
    int                 console_scroll;         /* Lines scrolled up from the bottom (0 = follow new output) */
    int                 console_visible_lines;  /* Lines that fit in the log box (set by the console layout) */
    float               console_sb_grab_y;      /* Scrollbar thumb drag offset */
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

/* True while a shared UI window (console, Create Server, Options) captures input */
bool cs2d_client_any_ui_open(const cs2d_client_t *client);

/* ESC pressed while no shared UI window is open */
void cs2d_client_handle_escape(cs2d_client_t *client);

/* Shutdown client and free resources */
void cs2d_client_shutdown(cs2d_client_t *client);

/* Process keyboard / mouse inputs */
void cs2d_client_handle_key(cs2d_client_t *client, int key, bool pressed);
void cs2d_client_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed);

/* Console UI & Command Interpreter */
void cs2d_client_console_print(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...);
void cs2d_client_console_exec(cs2d_client_t *client, const char *cmd_line);
/* i-th stored console line, 0 = oldest */
const cs2d_console_line_t *cs2d_client_console_line(const cs2d_client_t *client, int i);
void cs2d_client_handle_text_input(cs2d_client_t *client, const char *text);
void cs2d_client_handle_console_key(cs2d_client_t *client, int sdl_key);
void cs2d_client_handle_mouse_wheel(cs2d_client_t *client, int delta_y);

/* Update client game tick (runs at 60 FPS) */
void cs2d_client_update(cs2d_client_t *client, float delta_time);

/* Lay out the shared UI windows (console, Create Server, Options) without drawing them */
void cs2d_client_build_ui(cs2d_client_t *client, int screen_w, int screen_h);

/* Render entire frame (map, players, particles, HUD, Console, FPS, crosshair) */
void cs2d_client_render(cs2d_client_t *client, int screen_w, int screen_h);

/* Network functions */
bool cs2d_client_connect(cs2d_client_t *client, const char *ip, int port);
void cs2d_client_send_udp(cs2d_client_t *client, const uint8_t *payload, size_t len, bool reliable);
void cs2d_client_poll_network(cs2d_client_t *client);

#endif /* CS2D_CLIENT_H */
