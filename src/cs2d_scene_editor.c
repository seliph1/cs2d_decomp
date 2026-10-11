#include "cs2d_client.h"
#include "cs2d_editor.h"
#include "cs2d_map.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Editor scene: the map editor (GUI built by FUN_1401a85f0, loop FUN_1402bf960, CS2D.exe).
 *
 * Layout at 850x480 (sidebar window "sidebar": x 0, y -32, 152 wide; the coordinates below are on screen):
 *   row 1 icons (y 7):  New (1) 5, Load (2) 27, Save (3) 49, Map Properties (4) 83, Display (10) 105, Quit (0) 127
 *   row 2 icons (y 32): Tiles (8) 5, Entities (9) 27, Tools (4) 49; tiles mode only: Tile (5) 83, Rect (6) 105, Fill (7) 127
 *   tiles:    tile list (5, 58), 5 columns of 25 px, (h - 110) / 25 rows, scrollbar on the right;
 *             "Tile Properties" (5, h - 47) 142x17; rotation icon (5, h - 25); "BR: n" (27, h - 25) 60 wide;
 *             color box (90, h - 25) 57x20
 *   entities: filter combo (5, 58) 142 wide, entity list (5, 83) 142 x (h - 88)
 * The map is drawn from the screen origin (tile 0 under the sidebar), the camera starts at (0, 0).
 * Colors are measured on screenshots of the original: sidebar 54, icon buttons 27 inside / 150 frame,
 * grid yellow at 25 %, hovered tile frame (191,191,0) with a (255,255,0) line on the tile edge.
 */

#define EDITOR_SCROLL_SPEED 600.0f /* pixels/second, doubled while Shift is held (not measured) */
#define ED_ICON              20.0f /* Tgui_ibtn: 20x20 */
#define ED_CELL              25.0f /* Tgui_tilelist cell: a 32 px tile at scale 0.78125 */
#define ED_LIST_ROW          20.0f /* Entity list row (screenshot) */
#define ED_GRAY        (150.0f / 255.0f)

static cs2d_texture_t s_tex_icons;   /* gfx/gui_icons.bmp: 24 icons of 16x16, maskMode 3 (0x140a0de88) */
static cs2d_texture_t s_tex_shade;   /* gfx/gui_shade.bmp (0x140a0de78) */
static cs2d_texture_t s_tex_shadow;  /* incbindata/shadow.bmp, behind entity icons (0x140a0e038) */

/* Mouse state seen by the custom sidebar widgets */
static bool s_mouse_down;
static bool s_mouse_hit;

typedef struct {
    int         frame;
    float       x, y;
    const char *tip;   /* sys/language 0421..0432 */
} ed_icon_t;

static const ed_icon_t s_icons_row1[] = {
    { 1, 5.0f, 7.0f, "New Map" }, { 2, 27.0f, 7.0f, "Load Map" }, { 3, 49.0f, 7.0f, "Save Map" },
    { 4, 83.0f, 7.0f, "Map Properties" }, { 10, 105.0f, 7.0f, "Display Settings" }, { 0, 127.0f, 7.0f, "Quit Editor" }
};
static const ed_icon_t s_icons_row2[] = {
    { 8, 5.0f, 32.0f, "Edit Tiles" }, { 9, 27.0f, 32.0f, "Edit Entities" }, { 4, 49.0f, 32.0f, "Tools / More Options" },
    { 5, 83.0f, 32.0f, "Draw Tile by Tile" }, { 6, 105.0f, 32.0f, "Draw Rect of Tiles" }, { 7, 127.0f, 32.0f, "Fill Area with Tiles" }
};

static bool point_in(float px, float py, float x, float y, float w, float h)
{
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

static int tile_list_rows(int screen_h) { return (screen_h - 110) / 25; }

static int map_tile_count(const cs2d_client_t *client) { return (int)client->editor_map.tile_count + 1; }

static void editor_tooltip(cs2d_client_t *client, const char *text, float dx, float dy)
{
    snprintf(client->editor.tooltip, sizeof(client->editor.tooltip), "%s", text);
    client->editor.tooltip_x = client->mouse_screen_x + dx;
    client->editor.tooltip_y = client->mouse_screen_y + dy;
}

/* --- Map file handling --- */

static void editor_load_tileset(cs2d_client_t *client, bool load_defaults)
{
    cs2d_map_t *map = &client->editor_map;
    cs2d_texture_free(&client->editor_tileset);
    cs2d_texture_load_tileset(&client->editor_tileset, map->tileset);
    if (client->editor_tileset.width > 0) {
        map->tile_count = cs2d_ed_tile_max(client->editor_tileset.width, client->editor_tileset.height);
    }
    if (load_defaults) {
        char inf[128];
        cs2d_ed_inf_path(map->tileset, inf, sizeof(inf));
        cs2d_ed_inf_load(map, inf);
    }
    cs2d_render_invalidate_shadows();
}

static void editor_new_map(cs2d_client_t *client, const char *tileset)
{
    cs2d_map_free(&client->editor_map);
    snprintf(client->editor_map_name, sizeof(client->editor_map_name), "unnamed");
    /* Size and tileset of a new map are not measured on the original (50x50 cs2dnorm.bmp) */
    cs2d_ed_new_map(&client->editor_map, 50, 50, tileset);
    editor_load_tileset(client, true);
}

static bool editor_load_map(cs2d_client_t *client, const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "maps/%s.map", name);
    cs2d_map_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (!cs2d_map_load_file(&loaded, path)) {
        cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to load map '%s'", path);
        return false;
    }
    cs2d_map_free(&client->editor_map);
    client->editor_map = loaded;
    snprintf(client->editor_map_name, sizeof(client->editor_map_name), "%s", name);
    editor_load_tileset(client, false);
    client->editor_cam_x = 0.0f;
    client->editor_cam_y = 0.0f;
    cs2d_client_console_print(client, 170, 170, 170, "editor: map '%s' (%ux%u, tileset: %s)",
                              name, client->editor_map.width, client->editor_map.height, client->editor_map.tileset);
    return true;
}

static void editor_save_map(cs2d_client_t *client, const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "maps/%s.map", name);
    cs2d_map_t *map = &client->editor_map;
    snprintf(map->save_program, sizeof(map->save_program), "CS2D Client 1.0.1.8");
    if (cs2d_map_save_file(map, path)) {
        snprintf(client->editor_map_name, sizeof(client->editor_map_name), "%s", name);
        cs2d_client_console_print(client, 170, 170, 170, "editor: saved '%s'", path);
    } else {
        cs2d_client_console_print(client, 255, 45, 45, "Error: Unable to save map '%s'", path);
    }
}

/* Map names in maps (*.map files without the extension), sorted */
static int cmp_names(const void *a, const void *b) { return _stricmp((const char *)a, (const char *)b); }

static void editor_list_maps(cs2d_ed_state_t *ed)
{
    ed->file_count = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("maps\\*.map", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (ed->file_count >= CS2D_ED_MAX_FILES) break;
        char *dot = strrchr(fd.cFileName, '.');
        if (dot) *dot = '\0';
        snprintf(ed->files[ed->file_count++], sizeof(ed->files[0]), "%.63s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(ed->files, (size_t)ed->file_count, sizeof(ed->files[0]), cmp_names);
}

static void editor_open_file_window(cs2d_client_t *client, bool save)
{
    cs2d_ed_state_t *ed = &client->editor;
    editor_list_maps(ed);
    ed->file_open   = true;
    ed->file_save   = save;
    ed->file_scroll.top = 0;
    snprintf(ed->file_name, sizeof(ed->file_name), "%s", save ? client->editor_map_name : "");
    ed->file_name_cursor = (int)strlen(ed->file_name);
}

/* Tile Properties window shows a tile (FUN_1402b79a0) */
static void editor_tileprop_show(cs2d_client_t *client, int tile)
{
    cs2d_ed_state_t *ed = &client->editor;
    const cs2d_map_t *map = &client->editor_map;
    if (tile < 0) tile = 0;
    if (tile > map->tile_count) tile = map->tile_count;
    ed->tileprop_tile   = tile;
    ed->brush.tile      = (uint8_t)tile;
    ed->tileprop_option = cs2d_ed_prop_option(map->tile_modes[tile]);
    snprintf(ed->tileprop_height, sizeof(ed->tileprop_height), "%u", map->tile_heights[tile]);
    ed->tileprop_height_cursor = (int)strlen(ed->tileprop_height);
    uint8_t m = map->tile_3d_mods[tile];
    ed->tileprop_flip_y = (m & 1) != 0;
    ed->tileprop_mirror = (m & 2) != 0;
    ed->tileprop_nonrot = (m & 4) != 0;
}

/* The window's values go to the tile (FUN_1402b85f0) */
static void editor_tileprop_apply(cs2d_client_t *client)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_map_t *map = &client->editor_map;
    int t = ed->tileprop_tile;
    map->tile_modes[t]   = cs2d_ed_prop_modes[ed->tileprop_option];
    map->tile_heights[t] = (uint16_t)atoi(ed->tileprop_height);
    map->tile_3d_mods[t] = (uint8_t)((ed->tileprop_flip_y ? 1 : 0) + (ed->tileprop_mirror ? 2 : 0) +
                                     (ed->tileprop_nonrot ? 4 : 0));
}

/* --- Scene --- */

static void editor_enter(cs2d_client_t *client)
{
    cs2d_ed_state_t *ed = &client->editor;
    memset(ed, 0, sizeof(*ed));
    ed->mode     = CS2D_ED_MODE_TILES;
    ed->tool     = CS2D_ED_TOOL_TILE;
    ed->ent_type = -1;
    ed->ent_hover = -1;
    ed->brush.r = ed->brush.g = ed->brush.b = 255;

    memset(&client->editor_map, 0, sizeof(client->editor_map));
    if (!client->editor_map_name[0] || !editor_load_map(client, client->editor_map_name)) {
        editor_new_map(client, "cs2dnorm.bmp");
    }
    client->editor_cam_x = 0.0f;
    client->editor_cam_y = 0.0f;

    cs2d_texture_load_grid(&s_tex_icons, "gfx/gui_icons.bmp", 3, 16, 16);
    cs2d_texture_load_ex(&s_tex_shade, "gfx/gui_shade.bmp", 3);
    cs2d_texture_load_ex(&s_tex_shadow, "incbindata/shadow.bmp", 3);
}

static void editor_leave(cs2d_client_t *client)
{
    cs2d_texture_free(&client->editor_tileset);
    cs2d_texture_free(&s_tex_icons);
    cs2d_texture_free(&s_tex_shade);
    cs2d_texture_free(&s_tex_shadow);
    cs2d_map_free(&client->editor_map);
    memset(&client->editor_map, 0, sizeof(client->editor_map));
    client->editor.file_open = client->editor.tileprop_open = client->editor.color_open = false;
}

static bool editor_window_open(const cs2d_ed_state_t *ed)
{
    return ed->tileprop_open || ed->color_open || ed->file_open;
}

/* Pointer over the sidebar or an editor window (the map does not react then) */
static bool pointer_blocked(const cs2d_client_t *client)
{
    return client->mouse_screen_x < (float)CS2D_ED_SIDEBAR_W || client->editor.pointer_on_ui;
}

/* Tile under the pointer, false when it is outside the map */
static bool hovered_tile(const cs2d_client_t *client, int *tx, int *ty)
{
    float wx = client->mouse_screen_x + client->editor_cam_x;
    float wy = client->mouse_screen_y + client->editor_cam_y;
    *tx = (int)floorf(wx / CS2D_TILE_SIZE);
    *ty = (int)floorf(wy / CS2D_TILE_SIZE);
    return *tx >= 0 && *ty >= 0 && (uint32_t)*tx < client->editor_map.width && (uint32_t)*ty < client->editor_map.height;
}

static void editor_update(cs2d_client_t *client, float delta_time)
{
    cs2d_ed_state_t *ed = &client->editor;
    ed->time += delta_time;
    if (cs2d_client_any_ui_open(client)) return;

    if (!editor_window_open(ed)) {
        float speed = EDITOR_SCROLL_SPEED * (client->key_walk ? 2.0f : 1.0f) * delta_time;
        if (client->key_forward) client->editor_cam_y -= speed;
        if (client->key_back)    client->editor_cam_y += speed;
        if (client->key_left)    client->editor_cam_x -= speed;
        if (client->key_right)   client->editor_cam_x += speed;
    }

    /* Keep at least part of the map on screen */
    float max_x = (float)(client->editor_map.width  * CS2D_TILE_SIZE) - 64.0f;
    float max_y = (float)(client->editor_map.height * CS2D_TILE_SIZE) - 64.0f;
    float min_x = 64.0f - (float)client->config.window_width;
    float min_y = 64.0f - (float)client->config.window_height;
    if (client->editor_cam_x < min_x) client->editor_cam_x = min_x;
    if (client->editor_cam_y < min_y) client->editor_cam_y = min_y;
    if (client->editor_cam_x > max_x) client->editor_cam_x = max_x;
    if (client->editor_cam_y > max_y) client->editor_cam_y = max_y;

    /* Draw Tile by Tile while the button is held */
    int tx, ty;
    if (ed->painting && ed->mode == CS2D_ED_MODE_TILES && ed->tool == CS2D_ED_TOOL_TILE && !pointer_blocked(client) &&
        hovered_tile(client, &tx, &ty)) {
        cs2d_ed_set_tile(&client->editor_map, tx, ty, &ed->brush);
    }
}

/* --- Sidebar widgets (drawn by the scene, clicks handled in editor_sidebar_click) --- */

/* Tgui_ibtn.draw (0x14012b660): dark inside, 150 frame (255 when selected), icon 150 (255 hovered / selected),
 * plus an additive gui_shade glow that grows 0.03 per frame while hovered (max 0.3) and fades 0.01 */
static void draw_icon_button(cs2d_client_t *client, int glow_slot, int frame, float x, float y, bool selected,
                             const char *tip)
{
    cs2d_ed_state_t *ed = &client->editor;
    bool hover = !editor_window_open(ed) && point_in(client->mouse_screen_x, client->mouse_screen_y, x, y, ED_ICON, ED_ICON);
    float frame_c = selected ? 1.0f : ED_GRAY;
    cs2d_render_rect(x + 1.0f, y + 1.0f, ED_ICON - 2.0f, ED_ICON - 2.0f, 0.0f, 0.0f, 0.0f, 0.5f, true);
    cs2d_render_rect(x + 1.0f, y, ED_ICON - 2.0f, 1.0f, frame_c, frame_c, frame_c, 1.0f, true);
    cs2d_render_rect(x, y + 1.0f, 1.0f, ED_ICON - 2.0f, frame_c, frame_c, frame_c, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + ED_ICON - 1.0f, ED_ICON - 2.0f, 1.0f, frame_c, frame_c, frame_c, 1.0f, true);
    cs2d_render_rect(x + ED_ICON - 1.0f, y + 1.0f, 1.0f, ED_ICON - 2.0f, frame_c, frame_c, frame_c, 1.0f, true);
    float ic = (hover || selected) ? 1.0f : ED_GRAY;
    cs2d_render_sprite_frame(&s_tex_icons, 16, 16, frame, x + 10.0f, y + 10.0f, 16.0f, 16.0f, 0.0f, ic, ic, ic, 1.0f, 0);

    float *glow = &ed->btn_glow[glow_slot];
    *glow = hover ? fminf(*glow + 0.03f, 0.3f) : fmaxf(*glow - 0.01f, 0.0f);
    if (*glow > 0.0f) {
        cs2d_render_sprite_frame(&s_tex_shade, 10, 10, 0, x + ED_ICON * 0.5f, y + 10.0f, ED_ICON - 2.0f, 10.0f,
                                 0.0f, 1.0f, 1.0f, 1.0f, *glow, 4);
    }
    if (hover) editor_tooltip(client, tip, 15.0f, 5.0f);
}

/* Sidebar / file list scrollbar (16 wide, frame 150, thumb 90; no thumb while everything fits) */
static const cs2d_ui_scrollbar_style_t s_ed_scrollbar = {
    .width = 16.0f, .pad = 2.0f, .min_thumb = 10.0f, .hide_full_thumb = true,
    .track_bg = { 6, 6, 6, 255 }, .track_border = { 150, 150, 150, 255 },
    .thumb = { 90, 90, 90, 255 }, .thumb_hot = { 90, 90, 90, 255 }
};

/* List geometry shared by drawing, clicks and the scrollbar */
static int tile_list_total_rows(const cs2d_client_t *client) { return (map_tile_count(client) + 4) / 5; }
static float tile_list_w(void) { return 5.0f * ED_CELL + 18.0f; }
static float ent_list_h(int screen_h) { return (float)screen_h - 88.0f; }
static int ent_list_visible(int screen_h) { return (int)((ent_list_h(screen_h) - 2.0f) / ED_LIST_ROW); }
static float file_list_w(int screen_w) { return (float)screen_w - 192.0f; }
static float file_list_h(int screen_h) { return (float)screen_h - 101.0f; }
static int file_list_visible(int screen_h) { return (int)((file_list_h(screen_h) - 2.0f) / ED_LIST_ROW); }

/* Tgui_tilelist.draw (0x14019bb80) */
static void draw_tile_list(cs2d_client_t *client, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    const float x = 5.0f, y = 58.0f;
    int rows = tile_list_rows(screen_h);
    float w = tile_list_w(), h = (float)rows * ED_CELL + 2.0f;

    cs2d_render_rect(x + 1.0f, y + 1.0f, w - 17.0f, h - 2.0f, 0.0f, 0.0f, 0.0f, 0.5f, true);
    cs2d_render_rect(x, y, w - 16.0f, 1.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, true);
    cs2d_render_rect(x, y, 1.0f, h - 1.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, true);
    cs2d_render_rect(x, y + h - 1.0f, w - 16.0f, 1.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, true);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < 5; c++) {
            int t = (ed->tile_scroll.top + r) * 5 + c;
            if (t >= map_tile_count(client)) break;
            float cx = x + 1.0f + (float)c * ED_CELL, cy = y + 1.0f + (float)r * ED_CELL;
            bool hover = !editor_window_open(ed) && point_in(client->mouse_screen_x, client->mouse_screen_y, cx, cy, ED_CELL, ED_CELL);
            cs2d_render_tile_frame(&client->editor_tileset, (uint8_t)t, 0, NULL, cx, cy, ED_CELL,
                                   hover ? 1.0f : 200.0f / 255.0f, 1.0f);
            if (hover) {
                char tip[96];
                snprintf(tip, sizeof(tip), "Tile #%d, %s", t, cs2d_ed_tile_mode_name(client->editor_map.tile_modes[t]));
                editor_tooltip(client, tip, 5.0f, 5.0f);
            }
            if (t == ed->brush.tile) {
                /* Additive yellow pulse |sin(t)| * 0.5 (the angle counter speed is not measured) */
                float a = fabsf(sinf(ed->time * 3.0f)) * 0.5f;
                cs2d_render_rect(cx, cy, ED_CELL, ED_CELL, 1.0f, 1.0f, 0.0f, a, true);
            }
        }
    }
}

/* Entity list (Tgui_list at (5, 83)); the filter combo is a cs2d_ui_select built in cs2d_scene_editor_build_ui */
static void draw_entity_list(cs2d_client_t *client, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    const float x = 5.0f, y = 83.0f, w = 142.0f;
    float h = ent_list_h(screen_h);
    int types[128];
    int n = cs2d_ed_entity_list(ed->ent_filter, types, 128);
    int visible = ent_list_visible(screen_h);
    cs2d_ui_scroll_clamp(&ed->ent_scroll, visible, n);

    cs2d_render_rect(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.3f, true);
    cs2d_render_rect(x, y, w, h, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
    cs2d_render_scissor_begin((int)x + 1, (int)y + 1, (int)w - 18, (int)h - 2);
    for (int i = 0; i < visible + 1 && ed->ent_scroll.top + i < n; i++) {
        int t = types[ed->ent_scroll.top + i];
        float ry = y + 1.0f + (float)i * ED_LIST_ROW;
        bool hover = !editor_window_open(ed) && point_in(client->mouse_screen_x, client->mouse_screen_y, x, ry, w - 17.0f, ED_LIST_ROW);
        if (t == ed->ent_type) {
            cs2d_render_rect(x + 1.0f, ry, w - 18.0f, ED_LIST_ROW, 89.0f / 255.0f, 89.0f / 255.0f, 89.0f / 255.0f, 1.0f, true);
        }
        float c = (t == ed->ent_type || hover) ? 1.0f : ED_GRAY;
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 5.0f, ry + 3.0f, cs2d_entity_type_name((cs2d_entity_type_t)t), c, c, c, 1.0f);
    }
    cs2d_render_scissor_end();
}

static void draw_sidebar(cs2d_client_t *client, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_render_rect(0.0f, 0.0f, (float)CS2D_ED_SIDEBAR_W, (float)screen_h, 54.0f / 255.0f, 54.0f / 255.0f, 54.0f / 255.0f, 1.0f, true);

    for (int i = 0; i < 6; i++) {
        draw_icon_button(client, i, s_icons_row1[i].frame, s_icons_row1[i].x, s_icons_row1[i].y, false, s_icons_row1[i].tip);
    }
    for (int i = 0; i < 6; i++) {
        if (i >= 3 && ed->mode != CS2D_ED_MODE_TILES) break;
        bool sel = (i < 3) ? ((int)ed->mode == i) : ((int)ed->tool == s_icons_row2[i].frame);
        draw_icon_button(client, 6 + i, s_icons_row2[i].frame, s_icons_row2[i].x, s_icons_row2[i].y, sel, s_icons_row2[i].tip);
    }

    if (ed->mode == CS2D_ED_MODE_TILES) {
        draw_tile_list(client, screen_h);
        float by = (float)screen_h - 25.0f;
        draw_icon_button(client, 12, 15 + (ed->brush.mod & 3), 5.0f, by, false, "Tile Rotation");
        /* Tgui_col: the brush color, white without the 0x80 flag */
        bool colored = (ed->brush.mod & 0xc0) == 0x80;
        float r = colored ? ed->brush.r / 255.0f : 1.0f, g = colored ? ed->brush.g / 255.0f : 1.0f,
              b = colored ? ed->brush.b / 255.0f : 1.0f;
        cs2d_render_rect(90.0f, by, 57.0f, 20.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
        cs2d_render_rect(91.0f, by + 1.0f, 55.0f, 18.0f, r, g, b, 1.0f, true);
        if (!editor_window_open(ed) && point_in(client->mouse_screen_x, client->mouse_screen_y, 90.0f, by, 57.0f, 20.0f)) {
            editor_tooltip(client, "Color", 15.0f, 5.0f);
        }
    } else if (ed->mode == CS2D_ED_MODE_ENTITIES) {
        draw_entity_list(client, screen_h);
    }
}

/* Clicks on the custom sidebar widgets; true when the click belongs to the sidebar */
static bool editor_sidebar_click(cs2d_client_t *client, float mx, float my, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    if (mx >= (float)CS2D_ED_SIDEBAR_W) return false;

    for (int i = 0; i < 6; i++) {
        if (!point_in(mx, my, s_icons_row1[i].x, s_icons_row1[i].y, ED_ICON, ED_ICON)) continue;
        switch (s_icons_row1[i].frame) {
            case 1: editor_new_map(client, client->editor_map.tileset[0] ? client->editor_map.tileset : "cs2dnorm.bmp"); break;
            case 2: editor_open_file_window(client, false); break;
            case 3: editor_open_file_window(client, true); break;
            case 0: cs2d_client_set_scene(client, CS2D_SCENE_LOBBY); break;
            default:
                cs2d_client_console_print(client, 255, 220, 0, "editor: '%s' is not reconstructed yet", s_icons_row1[i].tip);
                break;
        }
        return true;
    }
    for (int i = 0; i < 6; i++) {
        if (i >= 3 && ed->mode != CS2D_ED_MODE_TILES) break;
        if (!point_in(mx, my, s_icons_row2[i].x, s_icons_row2[i].y, ED_ICON, ED_ICON)) continue;
        if (i < 3) {
            ed->mode = (cs2d_ed_mode_t)i;
            if (i == 2) cs2d_client_console_print(client, 255, 220, 0, "editor: 'Tools / More Options' is not reconstructed yet");
        } else {
            ed->tool = (cs2d_ed_tool_t)s_icons_row2[i].frame;
        }
        return true;
    }

    if (ed->mode == CS2D_ED_MODE_TILES) {
        const float x = 5.0f, y = 58.0f;
        int rows = tile_list_rows(screen_h);
        if (point_in(mx, my, x + 1.0f, y + 1.0f, 5.0f * ED_CELL - 1.0f, (float)rows * ED_CELL - 1.0f)) {
            int c = (int)((mx - x - 1.0f) / ED_CELL), r = (int)((my - y - 1.0f) / ED_CELL);
            int t = (ed->tile_scroll.top + r) * 5 + c;
            if (t < map_tile_count(client)) {
                ed->brush.tile = (uint8_t)t;
                ed->brush.mod  = 0; /* Tgui_tilelist: a click selects the tile and clears the modifier */
                if (ed->tileprop_open) editor_tileprop_show(client, t);
            }
            return true;
        }
        float by = (float)screen_h - 25.0f;
        if (point_in(mx, my, 5.0f, by, ED_ICON, ED_ICON)) {
            cs2d_ed_brush_rotate(&ed->brush);
            return true;
        }
        if (point_in(mx, my, 90.0f, by, 57.0f, 20.0f)) {
            ed->color_open = true;
            bool colored = (ed->brush.mod & 0xc0) == 0x80;
            ed->color_r = colored ? ed->brush.r : 255;
            ed->color_g = colored ? ed->brush.g : 255;
            ed->color_b = colored ? ed->brush.b : 255;
            return true;
        }
    } else if (ed->mode == CS2D_ED_MODE_ENTITIES) {
        const float x = 5.0f, y = 83.0f, w = 142.0f;
        float h = ent_list_h(screen_h);
        if (point_in(mx, my, x, y, w - 17.0f, h)) {
            int types[128];
            int n = cs2d_ed_entity_list(ed->ent_filter, types, 128);
            int i = ed->ent_scroll.top + (int)((my - y - 1.0f) / ED_LIST_ROW);
            if (i >= 0 && i < n) ed->ent_type = types[i];
            return true;
        }
    }
    return true; /* Sidebar background */
}

/* --- Map view --- */

/* Te.drawineditor (0x14026a260): shadow, type-colored icon (additive), pulsing glow, short label, zones */
static void draw_entities(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    const cs2d_map_t *map = &client->editor_map;
    float cam_x = roundf(client->editor_cam_x), cam_y = roundf(client->editor_cam_y);
    ed->ent_hover = -1;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        float px = (float)e->x * CS2D_TILE_SIZE - cam_x, py = (float)e->y * CS2D_TILE_SIZE - cam_y;
        if (px < -96.0f || py < -96.0f || px > (float)screen_w + 64.0f || py > (float)screen_h + 64.0f) continue;
        uint8_t rgb[3];
        cs2d_ed_entity_color((int)e->type, rgb);
        float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;

        int zone = cs2d_ed_entity_zone((int)e->type);
        if (zone > 0) {
            float z = (float)zone * CS2D_TILE_SIZE;
            cs2d_render_rect(px - z, py - z, 2.0f * z + 32.0f, 2.0f * z + 32.0f, 1.0f, 1.0f, 0.0f, 0.1f, true);
        }
        cs2d_render_sprite_frame(&s_tex_shadow, 32, 32, 0, px + 17.0f, py + 17.0f, 38.4f, 38.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0);
        cs2d_render_sprite_frame(&s_tex_icons, 16, 16, 9, px + 16.0f, py + 16.0f, 16.0f, 16.0f, 0.0f, r, g, b, 1.0f, 4);
        float pulse = sinf((px + py + ed->time * 180.0f) * 0.017453292f) * 0.5f;
        if (pulse > 0.0f) {
            cs2d_render_sprite_frame(&s_tex_shadow, 32, 32, 0, px + 17.0f, py + 17.0f, 25.6f, 25.6f, 0.0f, r, g, b, pulse, 4);
        }
        cs2d_render_text_ex(CS2D_FONT_SMALL, px + 19.0f, py + 21.0f, cs2d_ed_entity_label((int)e->type), r, g, b, 1.0f);

        if (!pointer_blocked(client) && point_in(client->mouse_screen_x, client->mouse_screen_y, px, py, 32.0f, 32.0f)) {
            ed->ent_hover = (int)i;
            editor_tooltip(client, cs2d_entity_type_name(e->type), 5.0f, 5.0f);
        }
    }
}

/* Hovered tile frame: a (191,191,0) band from 2 px outside to 1 px inside, (255,255,0) on the tile edge */
static void draw_tile_cursor(float x, float y, float w, float h)
{
    const float band = 191.0f / 255.0f;
    cs2d_render_rect(x - 2.0f, y - 2.0f, w + 4.0f, 4.0f, band, band, 0.0f, 1.0f, true);
    cs2d_render_rect(x - 2.0f, y + h - 2.0f, w + 4.0f, 4.0f, band, band, 0.0f, 1.0f, true);
    cs2d_render_rect(x - 2.0f, y + 2.0f, 4.0f, h - 4.0f, band, band, 0.0f, 1.0f, true);
    cs2d_render_rect(x + w - 2.0f, y + 2.0f, 4.0f, h - 4.0f, band, band, 0.0f, 1.0f, true);
    cs2d_render_rect(x, y, w, h, 1.0f, 1.0f, 0.0f, 1.0f, false);
}

static void draw_status(cs2d_client_t *client, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    const cs2d_map_t *map = &client->editor_map;
    int tx, ty;
    if (pointer_blocked(client) || !hovered_tile(client, &tx, &ty)) return;
    const float a = 0.7f; /* Measured: white text at (180,180,180), type colors at 70 % */
    float x = (float)CS2D_ED_SIDEBAR_W + 4.0f, y = (float)screen_h - 18.0f;
    char text[160];
    snprintf(text, sizeof(text), "Tile Position %d|%d  -  Tile #%u", tx, ty, cs2d_map_get_tile(map, (uint32_t)tx, (uint32_t)ty));
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, text, 1.0f, 1.0f, 1.0f, a);
    x += cs2d_render_text_width_ex(CS2D_FONT_REGULAR, text);
    if (ed->ent_hover < 0) return;

    const cs2d_entity_t *e = &map->entities[ed->ent_hover];
    int depth = 0;
    for (int i = 0; i <= ed->ent_hover; i++) {
        if (map->entities[i].x == e->x && map->entities[i].y == e->y) depth++;
    }
    uint8_t rgb[3];
    cs2d_ed_entity_color((int)e->type, rgb);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, "  -  ", 1.0f, 1.0f, 1.0f, a);
    x += cs2d_render_text_width_ex(CS2D_FONT_REGULAR, "  -  ");
    const char *name = cs2d_entity_type_name(e->type);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, name, rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, a);
    x += cs2d_render_text_width_ex(CS2D_FONT_REGULAR, name);
    snprintf(text, sizeof(text), " (Depth %d)  -  Name: '%s'  -  ", depth, e->name);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, text, 1.0f, 1.0f, 1.0f, a * 0.6f);
    x += cs2d_render_text_width_ex(CS2D_FONT_REGULAR, text);
    snprintf(text, sizeof(text), "Trigger: '%s'", e->trigger);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, text, 100.0f / 255.0f, 200.0f / 255.0f, 1.0f, a);
}

static void editor_render(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    const cs2d_map_t *map = &client->editor_map;
    float cam_x = roundf(client->editor_cam_x);
    float cam_y = roundf(client->editor_cam_y);
    ed->tooltip[0] = '\0';

    cs2d_render_rect(0.0f, 0.0f, (float)screen_w, (float)screen_h, 0.0f, 0.0f, 0.0f, 1.0f, true);
    cs2d_render_map_ex(map, &client->editor_tileset, cam_x, cam_y, screen_w, screen_h);

    /* Grid: yellow at 25 % on every tile edge of the map */
    float map_w = (float)(map->width * CS2D_TILE_SIZE), map_h = (float)(map->height * CS2D_TILE_SIZE);
    for (uint32_t tx = 0; tx <= map->width; tx++) {
        float x = (float)(tx * CS2D_TILE_SIZE) - cam_x;
        if (x < 0.0f || x > (float)screen_w) continue;
        cs2d_render_rect(x, fmaxf(-cam_y, 0.0f), 1.0f, fminf(map_h - cam_y, (float)screen_h), 1.0f, 1.0f, 0.0f, 0.25f, true);
    }
    for (uint32_t ty = 0; ty <= map->height; ty++) {
        float y = (float)(ty * CS2D_TILE_SIZE) - cam_y;
        if (y < 0.0f || y > (float)screen_h) continue;
        cs2d_render_rect(fmaxf(-cam_x, 0.0f), y, fminf(map_w - cam_x, (float)screen_w), 1.0f, 1.0f, 1.0f, 0.0f, 0.25f, true);
    }

    draw_entities(client, screen_w, screen_h);

    int tx, ty;
    if (!pointer_blocked(client) && hovered_tile(client, &tx, &ty)) {
        if (ed->rect_drag) {
            int x0 = ed->rect_x0 < tx ? ed->rect_x0 : tx, x1 = ed->rect_x0 < tx ? tx : ed->rect_x0;
            int y0 = ed->rect_y0 < ty ? ed->rect_y0 : ty, y1 = ed->rect_y0 < ty ? ty : ed->rect_y0;
            draw_tile_cursor((float)(x0 * CS2D_TILE_SIZE) - cam_x, (float)(y0 * CS2D_TILE_SIZE) - cam_y,
                             (float)((x1 - x0 + 1) * CS2D_TILE_SIZE), (float)((y1 - y0 + 1) * CS2D_TILE_SIZE));
        } else {
            draw_tile_cursor((float)(tx * CS2D_TILE_SIZE) - cam_x, (float)(ty * CS2D_TILE_SIZE) - cam_y,
                             (float)CS2D_TILE_SIZE, (float)CS2D_TILE_SIZE);
        }
    }

    draw_sidebar(client, screen_h);
    draw_status(client, screen_h);
}

/* --- Windows (cs2d_ui, built every frame while the editor runs) --- */

#define ED_AT(ui, ax, ay) CS2D_UI_ELEMENT(ui, 0, { .floating = { .enabled = true, .x = (ax), .y = (ay), .z_index = 11 } })

static void label_at(cs2d_ui_context_t *ui, float x, float y, const char *text)
{
    ED_AT(ui, x, y) { cs2d_ui_text(ui, text, CS2D_FONT_REGULAR, CS2D_UI_RGB(150, 150, 150)); }
}

/* Tile Properties: Tgui_win (155, 5) 480x470 */
static void build_tileprop_window(cs2d_client_t *client)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_ui_context_t *ui = &client->ui;
    const float wx = 155.0f, wy = 5.0f;
    if (!cs2d_ui_window_begin(ui, CS2D_UI_ID("EdTilePropWin"), "Tile Properties", wx, wy, 480.0f, 470.0f, &ed->tileprop_open)) return;

    bool changed = false;
    ED_AT(ui, wx + 16.0f, wy + 32.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdTpPrev"), "<", 20.0f, 32.0f) && ed->tileprop_tile > 0) {
            editor_tileprop_show(client, ed->tileprop_tile - 1);
        }
    }
    ED_AT(ui, wx + 78.0f, wy + 32.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdTpNext"), ">", 20.0f, 32.0f) && ed->tileprop_tile < client->editor_map.tile_count) {
            editor_tileprop_show(client, ed->tileprop_tile + 1);
        }
    }
    char title[96];
    snprintf(title, sizeof(title), "Tile #%d, %s", ed->tileprop_tile,
             cs2d_ed_tile_mode_name(client->editor_map.tile_modes[ed->tileprop_tile]));
    label_at(ui, wx + 108.0f, wy + 38.0f, title);

    for (int i = 0; i < CS2D_ED_PROP_OPTIONS; i++) {
        ED_AT(ui, wx + 16.0f, wy + 74.0f + 20.0f * (float)i) {
            int before = ed->tileprop_option;
            cs2d_ui_radio(ui, CS2D_UI_IDI("EdTpOpt", i), cs2d_ed_prop_labels[i], i, &ed->tileprop_option);
            if (ed->tileprop_option != before) changed = true;
        }
    }
    label_at(ui, wx + 314.0f, wy + 160.0f, "3D Height (pixels):");
    ED_AT(ui, wx + 314.0f, wy + 180.0f) {
        char before[8];
        memcpy(before, ed->tileprop_height, sizeof(before));
        cs2d_ui_textbox_ex(ui, CS2D_UI_ID("EdTpHeight"), ed->tileprop_height, sizeof(ed->tileprop_height),
                           &ed->tileprop_height_cursor, 150.0f, CS2D_UI_TEXTBOX_NUMERIC);
        if (memcmp(before, ed->tileprop_height, sizeof(before)) != 0) changed = true;
    }
    static const char *checks[3] = { "3D Flip Y", "3D Mirror", "Non-Rotated." };
    bool *values[3] = { &ed->tileprop_flip_y, &ed->tileprop_mirror, &ed->tileprop_nonrot };
    for (int i = 0; i < 3; i++) {
        ED_AT(ui, wx + 314.0f, wy + 220.0f + 20.0f * (float)i) {
            if (cs2d_ui_checkbox(ui, CS2D_UI_IDI("EdTpChk", i), checks[i], values[i])) changed = true;
        }
    }
    if (changed) editor_tileprop_apply(client);

    char inf[128];
    cs2d_ed_inf_path(client->editor_map.tileset, inf, sizeof(inf));
    ED_AT(ui, wx + 16.0f, wy + 409.0f) {
        /* The original asks for confirmation first (message box 0x1fc, not reconstructed) */
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdTpSaveDef"), "Save as default Properties", 250.0f, 20.0f)) {
            if (cs2d_ed_inf_save(&client->editor_map, inf)) {
                cs2d_client_console_print(client, 170, 170, 170, "editor: saved tile properties to '%s'", inf);
            }
        }
    }
    ED_AT(ui, wx + 16.0f, wy + 434.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdTpLoadDef"), "Load default Properties", 250.0f, 20.0f)) {
            if (cs2d_ed_inf_load(&client->editor_map, inf)) editor_tileprop_show(client, ed->tileprop_tile);
        }
    }
    ED_AT(ui, wx + 364.0f, wy + 434.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdTpClose"), "Close", 100.0f, 20.0f)) ed->tileprop_open = false;
    }
    cs2d_ui_window_end(ui);
}

/* Color: Tgui_win (95, 144) 451x193; palette of 16 Tgui_col 48x20 at (16 + 53 i, 102 / 127) */
static const uint8_t s_palette[16][3] = {
    { 0, 0, 0 }, { 128, 128, 128 }, { 128, 0, 0 }, { 128, 128, 0 }, { 0, 128, 0 }, { 0, 128, 128 }, { 0, 0, 128 }, { 128, 0, 128 },
    { 255, 255, 255 }, { 192, 192, 192 }, { 255, 0, 0 }, { 255, 255, 0 }, { 0, 255, 0 }, { 0, 255, 255 }, { 0, 0, 255 }, { 255, 0, 255 }
};

static void build_color_window(cs2d_client_t *client)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_ui_context_t *ui = &client->ui;
    const float wx = 95.0f, wy = 144.0f;
    if (!cs2d_ui_window_begin(ui, CS2D_UI_ID("EdColorWin"), "Color", wx, wy, 451.0f, 193.0f, &ed->color_open)) return;
    static const char *names[3] = { "Red:", "Green:", "Blue:" };
    int *values[3] = { &ed->color_r, &ed->color_g, &ed->color_b };
    for (int i = 0; i < 3; i++) {
        char v[8];
        snprintf(v, sizeof(v), "%d", *values[i]);
        label_at(ui, wx + 66.0f, wy + 32.0f + 20.0f * (float)i, names[i]);
        label_at(ui, wx + 126.0f, wy + 32.0f + 20.0f * (float)i, v);
    }
    ED_AT(ui, wx + 230.0f, wy + 157.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdColorOk"), "Okay", 100.0f, 20.0f)) {
            cs2d_ed_brush_set_color(&ed->brush, (uint8_t)ed->color_r, (uint8_t)ed->color_g, (uint8_t)ed->color_b);
            ed->color_open = false;
        }
    }
    ED_AT(ui, wx + 335.0f, wy + 157.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdColorCancel"), "Cancel", 100.0f, 20.0f)) ed->color_open = false;
    }
    cs2d_ui_window_end(ui);
}

/* Load / Save Map: Tgui_win (155, 5) (w - 160) x (h - 10); list (16, 25), name box (16, h - 71) */
static void build_file_window(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_ui_context_t *ui = &client->ui;
    const float wx = 155.0f, wy = 5.0f;
    float ww = (float)screen_w - 160.0f;
    if (!cs2d_ui_window_begin(ui, CS2D_UI_ID("EdFileWin"), ed->file_save ? "Save Map" : "Load Map",
                              wx, wy, ww, (float)screen_h - 10.0f, &ed->file_open)) return;
    bool go = false;
    const float lx = wx + 16.0f, ly = wy + 25.0f;
    int visible = file_list_visible(screen_h);
    cs2d_ui_rect_t area = { lx, ly, file_list_w(screen_w), file_list_h(screen_h) };
    if (!client->console_open) cs2d_ui_scroll_wheel(ui, &ed->file_scroll, &area, 3, visible, ed->file_count);
    cs2d_ui_scrollbar_at(ui, CS2D_UI_ID("EdFileScroll"), &ed->file_scroll, visible, ed->file_count,
                         lx + area.w - 17.0f, ly, area.h, 11, &s_ed_scrollbar);
    ED_AT(ui, wx + 16.0f, wy + (float)screen_h - 71.0f) {
        go = cs2d_ui_textbox(ui, CS2D_UI_ID("EdFileName"), ed->file_name, sizeof(ed->file_name), &ed->file_name_cursor,
                             (float)screen_w - 192.0f, false);
    }
    ED_AT(ui, wx + (float)screen_w - 381.0f, wy + (float)screen_h - 46.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdFileOk"), "Okay", 100.0f, 20.0f)) go = true;
    }
    ED_AT(ui, wx + (float)screen_w - 276.0f, wy + (float)screen_h - 46.0f) {
        if (cs2d_ui_button(ui, CS2D_UI_ID("EdFileCancel"), "Cancel", 100.0f, 20.0f)) ed->file_open = false;
    }
    cs2d_ui_window_end(ui);

    if (go && ed->file_name[0]) {
        ed->file_open = false;
        if (ed->file_save) {
            editor_save_map(client, ed->file_name);
        } else {
            editor_load_map(client, ed->file_name);
        }
    }
}

static bool rect_has_pointer(const cs2d_client_t *client, cs2d_ui_id_t id)
{
    cs2d_ui_rect_t r = cs2d_ui_get_box(&client->ui, id);
    return r.w > 0.0f && point_in(client->mouse_screen_x, client->mouse_screen_y, r.x, r.y, r.w, r.h);
}

void cs2d_scene_editor_build_ui(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    cs2d_ui_context_t *ui = &client->ui;

    /* Sidebar widgets that exist in the UI library */
    if (ed->mode == CS2D_ED_MODE_TILES) {
        ED_AT(ui, 5.0f, (float)screen_h - 47.0f) {
            if (cs2d_ui_button(ui, CS2D_UI_ID("EdTileProps"), "Tile Properties", 142.0f, 17.0f)) {
                ed->tileprop_open = true;
                editor_tileprop_show(client, ed->brush.tile);
            }
        }
        char br[16];
        snprintf(br, sizeof(br), "BR: %d", cs2d_ed_brightness_percent(ed->brush.mod));
        ED_AT(ui, 27.0f, (float)screen_h - 25.0f) {
            if (cs2d_ui_button(ui, CS2D_UI_ID("EdBrightness"), br, 60.0f, 20.0f)) cs2d_ed_brush_brightness(&ed->brush);
        }
    } else if (ed->mode == CS2D_ED_MODE_ENTITIES) {
        ED_AT(ui, 5.0f, 58.0f) {
            int before = ed->ent_filter;
            cs2d_ui_select(ui, CS2D_UI_ID("EdEntFilter"), (const char **)cs2d_ed_entity_filters, CS2D_ED_ENT_FILTERS,
                           &ed->ent_filter, 142.0f);
            if (ed->ent_filter != before) ed->ent_scroll.top = 0;
        }
    }

    /* List scrollbars (z 1: over the sidebar, under the editor windows) and the mouse wheel over the lists */
    bool wheel = !client->console_open && !ed->color_open;
    if (ed->mode == CS2D_ED_MODE_TILES) {
        const float x = 5.0f, y = 58.0f;
        int rows = tile_list_rows(screen_h), total = tile_list_total_rows(client);
        cs2d_ui_rect_t area = { x, y, tile_list_w(), (float)rows * ED_CELL + 2.0f };
        if (wheel && !ed->file_open) cs2d_ui_scroll_wheel(ui, &ed->tile_scroll, &area, 1, rows, total);
        cs2d_ui_scrollbar_at(ui, CS2D_UI_ID("EdTileScroll"), &ed->tile_scroll, rows, total,
                             x + area.w - 17.0f, y, area.h, 1, &s_ed_scrollbar);
    } else if (ed->mode == CS2D_ED_MODE_ENTITIES) {
        const float x = 5.0f, y = 83.0f, w = 142.0f;
        int types[128];
        int n = cs2d_ed_entity_list(ed->ent_filter, types, 128), visible = ent_list_visible(screen_h);
        cs2d_ui_rect_t area = { x, y, w, ent_list_h(screen_h) };
        if (wheel && !ed->file_open) cs2d_ui_scroll_wheel(ui, &ed->ent_scroll, &area, 1, visible, n);
        cs2d_ui_scrollbar_at(ui, CS2D_UI_ID("EdEntScroll"), &ed->ent_scroll, visible, n,
                             x + w - 17.0f, y, area.h, 1, &s_ed_scrollbar);
    }

    if (ed->tileprop_open) build_tileprop_window(client);
    if (ed->color_open) build_color_window(client);
    if (ed->file_open) build_file_window(client, screen_w, screen_h);

    ed->pointer_on_ui = rect_has_pointer(client, CS2D_UI_ID("EdTilePropWin")) && ed->tileprop_open;
    ed->pointer_on_ui |= rect_has_pointer(client, CS2D_UI_ID("EdColorWin")) && ed->color_open;
    ed->pointer_on_ui |= rect_has_pointer(client, CS2D_UI_ID("EdFileWin")) && ed->file_open;
}

/* Drawn over the UI windows: tile picture, color preview / sliders / palette, map list, tooltip */
void cs2d_scene_editor_render_overlay(cs2d_client_t *client, int screen_w, int screen_h)
{
    cs2d_ed_state_t *ed = &client->editor;
    float mx = client->mouse_screen_x, my = client->mouse_screen_y;
    bool hit = s_mouse_hit;
    s_mouse_hit = false;

    if (ed->tileprop_open) {
        cs2d_render_tile_frame(&client->editor_tileset, (uint8_t)ed->tileprop_tile, 0, NULL, 155.0f + 41.0f, 5.0f + 32.0f,
                               32.0f, 1.0f, 1.0f);
    }

    if (ed->color_open) {
        const float wx = 95.0f, wy = 144.0f;
        cs2d_render_rect(wx + 16.0f, wy + 32.0f, 45.0f, 60.0f, ed->color_r / 255.0f, ed->color_g / 255.0f,
                         ed->color_b / 255.0f, 1.0f, true);
        cs2d_render_rect(wx + 16.0f, wy + 32.0f, 45.0f, 60.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
        int *values[3] = { &ed->color_r, &ed->color_g, &ed->color_b };
        for (int i = 0; i < 3; i++) {
            /* Tgui_hscr (165, 32 + 20 i) 270 wide, range 0..255 */
            float sx = wx + 165.0f, sy = wy + 32.0f + 20.0f * (float)i;
            cs2d_render_rect(sx, sy, 270.0f, 15.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
            cs2d_render_rect(sx + 1.0f, sy + 1.0f, 268.0f, 13.0f, 0.1f, 0.1f, 0.1f, 1.0f, true);
            float kx = sx + 1.0f + (268.0f - 13.0f) * (float)*values[i] / 255.0f;
            cs2d_render_rect(kx, sy + 1.0f, 13.0f, 13.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, true);
            if (s_mouse_down && point_in(mx, my, sx, sy - 2.0f, 270.0f, 19.0f)) {
                int v = (int)((mx - sx - 7.5f) / (268.0f - 13.0f) * 255.0f + 0.5f);
                *values[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
            }
        }
        for (int i = 0; i < 16; i++) {
            float px = wx + 16.0f + 53.0f * (float)(i % 8), py = wy + (i < 8 ? 102.0f : 127.0f);
            cs2d_render_rect(px, py, 48.0f, 20.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
            cs2d_render_rect(px + 1.0f, py + 1.0f, 46.0f, 18.0f, s_palette[i][0] / 255.0f, s_palette[i][1] / 255.0f,
                             s_palette[i][2] / 255.0f, 1.0f, true);
            if (hit && point_in(mx, my, px, py, 48.0f, 20.0f)) {
                ed->color_r = s_palette[i][0];
                ed->color_g = s_palette[i][1];
                ed->color_b = s_palette[i][2];
            }
        }
    }

    if (ed->file_open) {
        /* The scrollbar (last 16 px) is a UI element built with the window */
        const float lx = 155.0f + 16.0f, ly = 5.0f + 25.0f;
        float lw = file_list_w(screen_w), lh = file_list_h(screen_h);
        int visible = file_list_visible(screen_h);
        cs2d_render_rect(lx, ly, lw - 16.0f, lh, 0.0f, 0.0f, 0.0f, 0.3f, true);
        cs2d_render_rect(lx, ly, lw - 16.0f, lh, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
        cs2d_ui_scroll_clamp(&ed->file_scroll, visible, ed->file_count);
        for (int i = 0; i < visible && ed->file_scroll.top + i < ed->file_count; i++) {
            const char *name = ed->files[ed->file_scroll.top + i];
            float ry = ly + 1.0f + ED_LIST_ROW * (float)i;
            bool sel = _stricmp(name, ed->file_name) == 0;
            if (sel) cs2d_render_rect(lx + 1.0f, ry, lw - 18.0f, ED_LIST_ROW, 89.0f / 255.0f, 89.0f / 255.0f, 89.0f / 255.0f, 1.0f, true);
            float c = sel ? 1.0f : ED_GRAY;
            cs2d_render_text_ex(CS2D_FONT_REGULAR, lx + 5.0f, ry + 3.0f, name, c, c, c, 1.0f);
            if (hit && point_in(mx, my, lx, ry, lw - 17.0f, ED_LIST_ROW)) {
                snprintf(ed->file_name, sizeof(ed->file_name), "%s", name);
                ed->file_name_cursor = (int)strlen(ed->file_name);
            }
        }
    }

    /* Tooltip at the pointer (same style as the server list tooltip) */
    if (ed->tooltip[0]) {
        float tw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, ed->tooltip);
        float x = ed->tooltip_x, y = ed->tooltip_y;
        if (x + tw + 8.0f > (float)screen_w) x = (float)screen_w - tw - 8.0f;
        cs2d_render_rect(x, y, tw + 8.0f, 17.0f, 0.0f, 0.0f, 0.0f, 0.8f, true);
        cs2d_render_rect(x, y, tw + 8.0f, 17.0f, ED_GRAY, ED_GRAY, ED_GRAY, 1.0f, false);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 4.0f, y + 1.0f, ed->tooltip, 1.0f, 1.0f, 1.0f, 1.0f);
    }
}

/* --- Input --- */

static void editor_handle_key(cs2d_client_t *client, int key, bool pressed)
{
    cs2d_ed_state_t *ed = &client->editor;
    switch (key) {
        case 'W': case 'w': case VK_UP:    client->key_forward = pressed; break;
        case 'S': case 's': case VK_DOWN:  client->key_back    = pressed; break;
        case 'A': case 'a': case VK_LEFT:  client->key_left    = pressed; break;
        case 'D': case 'd': case VK_RIGHT: client->key_right   = pressed; break;
        case VK_SHIFT:                     client->key_walk    = pressed; break;
        case VK_DELETE:
            /* Delete the entity under the pointer (the original uses its context menu; not reconstructed) */
            if (pressed && ed->ent_hover >= 0) {
                cs2d_ed_entity_remove(&client->editor_map, ed->ent_hover);
                ed->ent_hover = -1;
            }
            break;
        default: break;
    }
}

static void editor_handle_mouse(cs2d_client_t *client, float mx, float my, int button, bool pressed)
{
    cs2d_ed_state_t *ed = &client->editor;
    int screen_h = client->config.window_height;
    if (button == 1) {
        client->mouse_fire = pressed;
        s_mouse_down = pressed;
        if (pressed) s_mouse_hit = true;
    }
    if (button == 0) return;

    if (button == 1 && !pressed) {
        int tx, ty;
        if (ed->rect_drag && hovered_tile(client, &tx, &ty)) {
            cs2d_ed_rect(&client->editor_map, ed->rect_x0, ed->rect_y0, tx, ty, &ed->brush);
        }
        ed->rect_drag = false;
        ed->painting  = false;
        return;
    }
    if (button != 1 || !pressed) return;

    if (ed->pointer_on_ui) return; /* An editor window takes the click */
    if (editor_sidebar_click(client, mx, my, screen_h)) return;

    int tx, ty;
    if (!hovered_tile(client, &tx, &ty)) return;
    if (ed->mode == CS2D_ED_MODE_TILES) {
        switch (ed->tool) {
            case CS2D_ED_TOOL_TILE:
                ed->painting = true;
                cs2d_ed_set_tile(&client->editor_map, tx, ty, &ed->brush);
                break;
            case CS2D_ED_TOOL_RECT:
                ed->rect_drag = true;
                ed->rect_x0   = tx;
                ed->rect_y0   = ty;
                break;
            case CS2D_ED_TOOL_FILL:
                cs2d_ed_fill(&client->editor_map, tx, ty, &ed->brush);
                break;
        }
    } else if (ed->mode == CS2D_ED_MODE_ENTITIES && ed->ent_type >= 0 && cs2d_ed_entity_at(&client->editor_map, tx, ty) < 0) {
        cs2d_ed_entity_add(&client->editor_map, ed->ent_type, tx, ty);
    }
}

static void editor_handle_escape(cs2d_client_t *client)
{
    cs2d_ed_state_t *ed = &client->editor;
    if (ed->color_open)    { ed->color_open = false; return; }
    if (ed->file_open)     { ed->file_open = false; return; }
    if (ed->tileprop_open) { ed->tileprop_open = false; return; }
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
