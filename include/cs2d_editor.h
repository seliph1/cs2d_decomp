#ifndef CS2D_EDITOR_H
#define CS2D_EDITOR_H

#include "cs2d_types.h"
#include "cs2d_ui.h"

/*
 * Map editor model, reconstructed from CS2D.exe (no rendering here, see cs2d_scene_editor.c):
 *   sidebar and windows    FUN_1401a85f0 (GUI builder), editor loop FUN_1402bf960
 *   set one tile           FUN_1402bd6d0, flood fill FUN_1402bdcc0 (+ Ted_fill), tile color FUN_1402b8bf0
 *   tile modifier byte     TileProp.Get 0x140010450 (see cs2d_map_t.modifiers)
 *   tile properties        FUN_1402b85f0 (window -> mode), FUN_1402b79a0 (tile -> window), FUN_14019ba30 (names)
 *   tileset defaults       gfx/tiles/<tileset>.inf: reader FUN_14019c550, writer FUN_1402b7860
 *   entity tables          filled in the init code at 0x140318856 (tools/agent/initarrays.py)
 */

#define CS2D_ED_SIDEBAR_W 152 /* Tgui_win "sidebar": x 0, y -32, 0x98 wide */

/* Sidebar mode (FUN_14019cce0: 0 tiles, 1 entities, 2 tools) */
typedef enum {
    CS2D_ED_MODE_TILES    = 0,
    CS2D_ED_MODE_ENTITIES = 1,
    CS2D_ED_MODE_TOOLS    = 2
} cs2d_ed_mode_t;

/* Tile drawing tool: the gui_icons.bmp frame of its button */
typedef enum {
    CS2D_ED_TOOL_TILE = 5, /* Draw Tile by Tile */
    CS2D_ED_TOOL_RECT = 6, /* Draw Rect of Tiles */
    CS2D_ED_TOOL_FILL = 7  /* Fill Area with Tiles */
} cs2d_ed_tool_t;

/* The tile list selection: tile (+0x58), modifier byte (+0x5c) and its Tmap_d color (+0x60) */
typedef struct {
    uint8_t tile;
    uint8_t mod;
    uint8_t r, g, b;
} cs2d_ed_brush_t;

/* Tile rotation button: rotation + 1, wrapping after 270 degrees */
void cs2d_ed_brush_rotate(cs2d_ed_brush_t *brush);
/* "BR" button: brightness level + 1, 9 wraps to 0 (= 100 %) */
void cs2d_ed_brush_brightness(cs2d_ed_brush_t *brush);
/* Brightness in percent shown on the button: level * 10, or 100 for level 0 */
int  cs2d_ed_brightness_percent(uint8_t mod);
/* Color window "Okay": white clears the color flag, anything else sets modifier 0x80 */
void cs2d_ed_brush_set_color(cs2d_ed_brush_t *brush, uint8_t r, uint8_t g, uint8_t b);

/* Paints one tile with the brush; false when the tile already looks like that */
bool cs2d_ed_set_tile(cs2d_map_t *map, int x, int y, const cs2d_ed_brush_t *brush);
/* 4-neighbour flood fill over tiles with the same tile and modifier; returns the painted count */
int  cs2d_ed_fill(cs2d_map_t *map, int x, int y, const cs2d_ed_brush_t *brush);
/* Paints every tile of the rectangle between two corners (any order); returns the painted count */
int  cs2d_ed_rect(cs2d_map_t *map, int x0, int y0, int x1, int y1, const cs2d_ed_brush_t *brush);

/* Tile properties: the 16 options of the window and the tile mode each one sets */
#define CS2D_ED_PROP_OPTIONS 16
extern const char   *const cs2d_ed_prop_labels[CS2D_ED_PROP_OPTIONS];
extern const uint8_t       cs2d_ed_prop_modes[CS2D_ED_PROP_OPTIONS];
int         cs2d_ed_prop_option(uint8_t mode);       /* Option index for a mode (4 "Floor - Soundless" if unknown) */
const char *cs2d_ed_tile_mode_name(uint8_t mode);    /* FUN_14019ba30 */
/* Highest tile index of a tileset image (32x32 cells, at most 255) */
uint8_t     cs2d_ed_tile_max(int image_w, int image_h);

/* gfx/tiles/<tileset without extension>.inf */
void cs2d_ed_inf_path(const char *tileset, char *out, size_t out_size);
/* Reads modes, heights and 3D modifiers into the map (V1..V4); false when missing or unknown */
bool cs2d_ed_inf_load(cs2d_map_t *map, const char *path);
/* Writes "...Tileset Informations V 4.0": per tile {mode u8, height u16, 3D modifier u8} */
bool cs2d_ed_inf_save(const cs2d_map_t *map, const char *path);

/* Entities */
#define CS2D_ED_ENT_FILTERS 6 /* sys/language 0433: All|Info|Environment|Generate|Function|Trigger */
extern const char *const cs2d_ed_entity_filters[CS2D_ED_ENT_FILTERS];
/* Valid entity types for a filter, in list order; returns the count */
int         cs2d_ed_entity_list(int filter, int *out_types, int max);
/* Short label drawn next to the icon (Spr, Brkbl, ...) */
const char *cs2d_ed_entity_label(int type);
/* Icon color of a type */
void        cs2d_ed_entity_color(int type, uint8_t rgb[3]);
/* Zone radius in tiles drawn around Info_T / Info_CT / Info_RescuePoint / Info_BombSpot, else 0 */
int         cs2d_ed_entity_zone(int type);
int         cs2d_ed_entity_at(const cs2d_map_t *map, int x, int y);   /* Index or -1 */
int         cs2d_ed_entity_add(cs2d_map_t *map, int type, int x, int y); /* New index or -1 */
void        cs2d_ed_entity_remove(cs2d_map_t *map, int index);

/* Editor scene state (sidebar widgets and windows of FUN_1401a85f0) */
#define CS2D_ED_MAX_FILES 512
typedef struct {
    cs2d_ed_mode_t  mode;
    cs2d_ed_tool_t  tool;
    cs2d_ed_brush_t brush;
    cs2d_ui_scroll_t tile_scroll; /* First visible row of the tile list */
    int   ent_filter;             /* Entity list filter (combo) */
    int   ent_type;               /* Entity type placed by a click, -1 none */
    cs2d_ui_scroll_t ent_scroll;  /* First visible row of the entity list */
    int   ent_hover;              /* Map entity under the pointer, -1 none */
    bool  painting;               /* Left button held over the map (tile by tile) */
    bool  rect_drag;              /* Draw Rect: button held since (rect_x0, rect_y0) */
    int   rect_x0, rect_y0;
    float btn_glow[24];           /* Tgui_ibtn +0x44 hover glow per icon button */
    char  tooltip[160];
    float tooltip_x, tooltip_y;
    bool  pointer_on_ui;          /* Pointer over an editor window (set while the UI is built) */
    float time;                   /* Seconds, drives the selection pulse */

    bool  tileprop_open;          /* Tile Properties window */
    int   tileprop_tile;
    int   tileprop_option;
    char  tileprop_height[8];
    int   tileprop_height_cursor;
    bool  tileprop_flip_y, tileprop_mirror, tileprop_nonrot;

    bool  color_open;             /* Color window */
    int   color_r, color_g, color_b;

    bool  file_open;              /* Load / Save Map window */
    bool  file_save;
    char  file_name[64];
    int   file_name_cursor;
    char  files[CS2D_ED_MAX_FILES][64];
    int   file_count;
    cs2d_ui_scroll_t file_scroll;
} cs2d_ed_state_t;

/* Blank map of w x h tiles (tile 0, no modifiers, no entities) on a tileset */
bool cs2d_ed_new_map(cs2d_map_t *map, uint32_t w, uint32_t h, const char *tileset);

#endif /* CS2D_EDITOR_H */
