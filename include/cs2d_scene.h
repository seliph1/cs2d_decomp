#ifndef CS2D_SCENE_H
#define CS2D_SCENE_H

#include <stdbool.h>

/*
 * CS2D runs as one of three top-level scenes:
 *   - Lobby  : main menu (splash.bmp background, Quick Play / New Game / ... / Quit)
 *   - Game   : a loaded map with players, HUD and networking
 *   - Editor : the map editor
 * The console, the Create Server / Options windows, the FPS overlay and the
 * mouse pointer are shared by every scene and stay owned by cs2d_client.
 */
typedef enum {
    CS2D_SCENE_LOBBY  = 0,
    CS2D_SCENE_GAME   = 1,
    CS2D_SCENE_EDITOR = 2,
    CS2D_SCENE_COUNT  = 3
} cs2d_scene_id_t;

typedef struct cs2d_client cs2d_client_t;

typedef struct {
    const char *name;

    /* Acquire / release the scene's own resources (map, textures, sockets) */
    void (*enter)(cs2d_client_t *client);
    void (*leave)(cs2d_client_t *client);

    void (*update)(cs2d_client_t *client, float delta_time);

    /* Draws the scene itself; shared UI windows, FPS overlay and pointer are drawn on top by the client */
    void (*render)(cs2d_client_t *client, int screen_w, int screen_h);

    /* Input that reaches the scene only while no shared UI window is open */
    void (*handle_key)(cs2d_client_t *client, int key, bool pressed);
    void (*handle_mouse)(cs2d_client_t *client, float mx, float my, int button, bool pressed);
    void (*handle_escape)(cs2d_client_t *client);
} cs2d_scene_t;

extern const cs2d_scene_t cs2d_scene_lobby;
extern const cs2d_scene_t cs2d_scene_game;
extern const cs2d_scene_t cs2d_scene_editor;

const cs2d_scene_t *cs2d_scene_get(cs2d_scene_id_t id);

/* Editor scene hooks called by the client: its UI widgets and windows (built every frame while the editor
 * runs) and what it draws over them; its lists take the mouse wheel through cs2d_ui_scroll_wheel */
void cs2d_scene_editor_build_ui(cs2d_client_t *client, int screen_w, int screen_h);
void cs2d_scene_editor_render_overlay(cs2d_client_t *client, int screen_w, int screen_h);

/* Game scene helpers used by the console ("map" while already in game) */
bool cs2d_scene_game_change_map(cs2d_client_t *client, const char *map_name);

/* Offline "restart": new round now - players respawn on Info_T / Info_CT, entity states reset and
 * Trigger_Start fires, freeze time (mp_freezetime) starts in round based game modes */
void cs2d_scene_game_restart_round(cs2d_client_t *client);

/* Starts running along a BFS tile path to (tx, ty); false when unreachable */
bool cs2d_scene_game_walk_to(cs2d_client_t *client, int tx, int ty);

/* Server state for the entity at tile (tx, ty): the first entity there still in state 0
 * takes it (the server lists entities in map order) */
void cs2d_scene_game_apply_entity_state(cs2d_client_t *client, int tx, int ty, uint8_t state);

/* Spectator console commands (bound to space / leftctrl): next player to follow, free look <-> follow */
void cs2d_scene_game_specplayer(cs2d_client_t *client);
void cs2d_scene_game_specmode(cs2d_client_t *client);

/* Weapon selection commands (bound in sys/controls.cfg): invnext, invprev, slot0..slot10. False for other commands */
bool cs2d_scene_game_inv_cmd(cs2d_client_t *client, const char *cmd);

/* Buy commands (bound in sys/controls.cfg): buy, buyequip, buyammo1, buyammo2, autobuy, rebuy. False for others */
bool cs2d_scene_game_buy_cmd(cs2d_client_t *client, const char *cmd);

/* Draws a shot or melee swing of another player (server fire record [7, player, seed]) */
void cs2d_scene_game_remote_shot(cs2d_client_t *client, cs2d_player_t *pl);
/* [8, player, mode] from the server: another player's weapon mode (silencer, burst, zoom, laser) or strong slash */
void cs2d_scene_game_remote_attack2(cs2d_client_t *client, cs2d_player_t *pl, uint8_t mode);

#endif /* CS2D_SCENE_H */
