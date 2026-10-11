#include "cs2d_client.h"
#include "cs2d_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Editor scene: the map editor.
 * Currently a viewer: it loads maps/<editor_map_name>.map (or a blank map), scrolls the
 * view with WASD / arrow keys and draws the tile grid. Editing tools are not reconstructed yet.
 */

#define EDITOR_SCROLL_SPEED 600.0f /* pixels/second, doubled while Shift is held */
#define EDITOR_BAR_H        18.0f

static bool editor_new_blank_map(cs2d_map_t *map, uint32_t w, uint32_t h, const char *tileset)
{
    memset(map, 0, sizeof(*map));
    snprintf(map->header, sizeof(map->header), "Unreal Software's CS2D Map File (max)");
    snprintf(map->tileset, sizeof(map->tileset), "%s", tileset);
    map->width  = w;
    map->height = h;

    size_t total = (size_t)w * (size_t)h;
    map->tiles     = (uint8_t *)calloc(total, 1);
    map->modifiers = (uint8_t *)calloc(total, 1);
    map->collision = (uint8_t *)calloc(total, 1);
    if (!map->tiles || !map->modifiers || !map->collision) {
        cs2d_map_free(map);
        return false;
    }
    return true;
}

static void editor_enter(cs2d_client_t *client)
{
    cs2d_map_t *map = &client->editor_map;
    memset(map, 0, sizeof(*map));

    bool loaded = false;
    if (client->editor_map_name[0]) {
        char map_path[128];
        snprintf(map_path, sizeof(map_path), "maps/%s.map", client->editor_map_name);
        loaded = cs2d_map_load_file(map, map_path);
        if (!loaded) {
            cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map '%s'", map_path);
        }
    }
    if (!loaded) {
        snprintf(client->editor_map_name, sizeof(client->editor_map_name), "unnamed");
        editor_new_blank_map(map, 50, 50, "cs2dnorm.bmp");
    }

    cs2d_texture_load_tileset(&client->editor_tileset, map->tileset);
    cs2d_render_invalidate_shadows();

    client->editor_cam_x = 0.0f;
    client->editor_cam_y = -EDITOR_BAR_H;
    cs2d_client_console_print(client, 170, 170, 170, "editor: map '%s' (%ux%u, tileset: %s)",
                              client->editor_map_name, map->width, map->height, map->tileset);
}

static void editor_leave(cs2d_client_t *client)
{
    cs2d_texture_free(&client->editor_tileset);
    cs2d_map_free(&client->editor_map);
    memset(&client->editor_map, 0, sizeof(client->editor_map));
}

static void editor_update(cs2d_client_t *client, float delta_time)
{
    if (cs2d_client_any_ui_open(client)) return;

    float speed = EDITOR_SCROLL_SPEED * (client->key_walk ? 2.0f : 1.0f) * delta_time;
    if (client->key_forward) client->editor_cam_y -= speed;
    if (client->key_back)    client->editor_cam_y += speed;
    if (client->key_left)    client->editor_cam_x -= speed;
    if (client->key_right)   client->editor_cam_x += speed;

    /* Keep at least part of the map on screen */
    float max_x = (float)(client->editor_map.width  * CS2D_TILE_SIZE) - 64.0f;
    float max_y = (float)(client->editor_map.height * CS2D_TILE_SIZE) - 64.0f;
    float min_x = 64.0f - (float)client->config.window_width;
    float min_y = 64.0f - (float)client->config.window_height;
    if (client->editor_cam_x < min_x) client->editor_cam_x = min_x;
    if (client->editor_cam_y < min_y) client->editor_cam_y = min_y;
    if (client->editor_cam_x > max_x) client->editor_cam_x = max_x;
    if (client->editor_cam_y > max_y) client->editor_cam_y = max_y;
}

static void editor_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    const cs2d_map_t *map = &client->editor_map;
    float cam_x = client->editor_cam_x;
    float cam_y = client->editor_cam_y;

    cs2d_render_rect(0.0f, 0.0f, (float)screen_w, (float)screen_h, 0.0f, 0.0f, 0.0f, 1.0f, true);
    cs2d_render_map_ex(map, &client->editor_tileset, cam_x, cam_y, screen_w, screen_h);
    cs2d_render_map_overhead(map, cam_x, cam_y, screen_w, screen_h);

    /* Tile grid over the visible part of the map */
    float map_w = (float)(map->width  * CS2D_TILE_SIZE);
    float map_h = (float)(map->height * CS2D_TILE_SIZE);
    int tx0 = (int)(cam_x / CS2D_TILE_SIZE);
    int ty0 = (int)(cam_y / CS2D_TILE_SIZE);
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    for (int tx = tx0; tx <= (int)map->width; tx++) {
        float x = (float)(tx * CS2D_TILE_SIZE) - cam_x;
        if (x > (float)screen_w) break;
        cs2d_render_line(x, -cam_y, x, map_h - cam_y, 1.0f, 1.0f, 1.0f, 0.12f);
    }
    for (int ty = ty0; ty <= (int)map->height; ty++) {
        float y = (float)(ty * CS2D_TILE_SIZE) - cam_y;
        if (y > (float)screen_h) break;
        cs2d_render_line(-cam_x, y, map_w - cam_x, y, 1.0f, 1.0f, 1.0f, 0.12f);
    }

    /* Status bar: map name, size and hovered tile */
    char status[160];
    int mtx = (int)((client->mouse_screen_x + cam_x) / CS2D_TILE_SIZE);
    int mty = (int)((client->mouse_screen_y + cam_y) / CS2D_TILE_SIZE);
    if (mtx >= 0 && mty >= 0 && (uint32_t)mtx < map->width && (uint32_t)mty < map->height) {
        snprintf(status, sizeof(status), "Map Editor - %s (%ux%u) | Tile %d,%d: #%u",
                 client->editor_map_name, map->width, map->height, mtx, mty,
                 cs2d_map_get_tile(map, (uint32_t)mtx, (uint32_t)mty));
    } else {
        snprintf(status, sizeof(status), "Map Editor - %s (%ux%u)",
                 client->editor_map_name, map->width, map->height);
    }
    cs2d_render_rect(0.0f, 0.0f, (float)screen_w, EDITOR_BAR_H, 0.0f, 0.0f, 0.0f, 0.75f, true);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, 6.0f, 2.0f, status, 0.85f, 0.85f, 0.85f, 1.0f);
}

static void editor_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    switch (key) {
        case 'W': case 'w': case VK_UP:    client->key_forward = pressed; break;
        case 'S': case 's': case VK_DOWN:  client->key_back    = pressed; break;
        case 'A': case 'a': case VK_LEFT:  client->key_left    = pressed; break;
        case 'D': case 'd': case VK_RIGHT: client->key_right   = pressed; break;
        case VK_SHIFT:                     client->key_walk    = pressed; break;
        default: break;
    }
}

static void editor_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    (void)client;
    (void)mx;
    (void)my;
    (void)button;
    (void)pressed;
}

static void editor_handle_escape(cs2d_client_t *client)
{
    /* "Quit Editor" (sys/language 0426); the unsaved-map confirmation (0497) is not reconstructed yet */
    cs2d_client_set_scene(client, CS2D_SCENE_LOBBY);
}

const cs2d_scene_t cs2d_scene_editor = {
    .name          = "Editor",
    .enter         = editor_enter,
    .leave         = editor_leave,
    .update        = editor_update,
    .render        = editor_render,
    .handle_key    = editor_handle_key,
    .handle_mouse  = editor_handle_mouse,
    .handle_escape = editor_handle_escape
};
