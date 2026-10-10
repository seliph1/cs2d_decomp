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

#define CS2D_CONSOLE_MAX_LINES 256
#define CS2D_CONSOLE_LINE_LEN  192
#define CS2D_CONSOLE_HIST_MAX  32

typedef struct {
    char    text[CS2D_CONSOLE_LINE_LEN];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} cs2d_console_line_t;

typedef struct {
    cs2d_config_t   config;
    cs2d_game_t     game;
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
    char                srv_port[16];
    int                 srv_port_cursor;
    int                 srv_max_players_idx;
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
    int                 console_line_count;
    int                 console_scroll;
    char                console_input[CS2D_CONSOLE_LINE_LEN];
    int                 console_cursor;
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
} cs2d_client_t;

/* Initialize client state, load configuration and map */
bool cs2d_client_init(cs2d_client_t *client, const char *map_name);

/* Shutdown client and free resources */
void cs2d_client_shutdown(cs2d_client_t *client);

/* Process keyboard / mouse inputs */
void cs2d_client_handle_key(cs2d_client_t *client, int key, bool pressed);
void cs2d_client_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed);

/* Console UI & Command Interpreter */
void cs2d_client_console_print(cs2d_client_t *client, uint8_t r, uint8_t g, uint8_t b, const char *fmt, ...);
void cs2d_client_console_exec(cs2d_client_t *client, const char *cmd_line);
void cs2d_client_handle_text_input(cs2d_client_t *client, const char *text);
void cs2d_client_handle_console_key(cs2d_client_t *client, int sdl_key);
void cs2d_client_handle_mouse_wheel(cs2d_client_t *client, int delta_y);

/* Update client game tick (runs at 60 FPS) */
void cs2d_client_update(cs2d_client_t *client, float delta_time);

/* Render entire frame (map, players, particles, HUD, Console, FPS, crosshair) */
void cs2d_client_render(cs2d_client_t *client, int screen_w, int screen_h);

/* Network functions */
bool cs2d_client_connect(cs2d_client_t *client, const char *ip, int port);
void cs2d_client_send_udp(cs2d_client_t *client, const uint8_t *payload, size_t len, bool reliable);
void cs2d_client_poll_network(cs2d_client_t *client);

#endif /* CS2D_CLIENT_H */
