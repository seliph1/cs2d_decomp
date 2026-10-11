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

/* Game scene helpers used by the console ("map" while already in game) */
bool cs2d_scene_game_change_map(cs2d_client_t *client, const char *map_name);

#endif /* CS2D_SCENE_H */
