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

/*
 * Entity Properties window (FUN_1402aa220: param 0 fills the widgets from the entity, 1 writes them back).
 * The window has shared widget pools (labels 0x140a0e880, textboxes 0e878, options 0e870, checkboxes 0e868,
 * combos 0e858, one color box 0e860, the rotation dial 0e8a0); each type places some of them. A field below is
 * one placed widget; x / y are relative to the window.
 */
typedef enum {
    CS2D_EDF_LABEL,      /* Tgui_lbl text */
    CS2D_EDF_INT,        /* Textbox <-> ints[slot] */
    CS2D_EDF_STR,        /* Textbox <-> strs[slot] */
    CS2D_EDF_STR_INT,    /* Textbox <-> strs[slot] holding an integer (bbStringToInt both ways) */
    CS2D_EDF_RADIO,      /* Tgui_opt, items "a|b|c" 20 px apart <-> ints[slot] */
    CS2D_EDF_CHECK,      /* Tgui_chk <-> ints[slot] */
    CS2D_EDF_CHECK_STR,  /* Tgui_chk <-> strs[slot] != "" (written "1" / "") */
    CS2D_EDF_COMBO,      /* Tgui_combo, items "a|b|c" <-> ints[slot] = index */
    CS2D_EDF_COMBO_ITEM, /* Tgui_combo "id - name" of the items (Env_Item) <-> ints[slot] = id */
    CS2D_EDF_COMBO_BLD,  /* Tgui_combo "id - name" of the buildings (Env_Building) <-> ints[slot] = id */
    CS2D_EDF_COLOR,      /* Tgui_col <-> ints[slot..slot + 2] */
    CS2D_EDF_COLOR_STR,  /* Tgui_col <-> strs[slot] = r << 16 | g << 8 | b (Gen_Particles) */
    CS2D_EDF_DELAY       /* Trigger_Delay: ints[0] seconds or, with a '.', ints[1] milliseconds */
} cs2d_edf_kind_t;

/* Value conversions of a field (FUN_1402aa220 apply branch) */
enum {
    CS2D_EDF_INV      = 0x001, /* Check: value = 1 - stored (or "" for CHECK_STR) */
    CS2D_EDF_OFF1     = 0x002, /* Radio / combo: stored = index + 1 */
    CS2D_EDF_MIN0     = 0x004, /* Clamped to >= 0 */
    CS2D_EDF_MIN1     = 0x008, /* Clamped to >= 1 */
    CS2D_EDF_ABS      = 0x010, /* Absolute value */
    CS2D_EDF_MAX250   = 0x020, /* Clamped to 0..250 (Env_Light range) */
    CS2D_EDF_MAX359   = 0x040, /* Clamped to 0..359 (Env_Light angles); also copied to ints[slot + 3] */
    CS2D_EDF_PAD0     = 0x080, /* Empty string shown as "0" */
    CS2D_EDF_PLUS1    = 0x100, /* Shown as stored + 1.0, stored as text - 1.0 (FUN_1402aa0d0, decimal) */
    CS2D_EDF_ONEMINUS = 0x200, /* Float shown and stored as 1.0 - value (Env_Room alpha) */
    CS2D_EDF_SIZE     = 0x400  /* Float clamped to 0.1..2.0 (Env_Object size) */
};

typedef struct {
    uint8_t     kind;   /* cs2d_edf_kind_t */
    uint16_t    flags;
    int16_t     x, y, w;
    int8_t      slot;
    const char *text;   /* Label / checkbox text, or "|"-separated items */
} cs2d_edf_t;

#define CS2D_ED_PROP_FIELDS 40

/* Fields of a type (count 0 for types with only Name / Trigger) */
const cs2d_edf_t *cs2d_ed_prop_fields(int type, int *count);
/* ints[] slot of the Rotation dial (Tgui_rot at (91, 107)), or -1 when the type hides it */
int  cs2d_ed_prop_rot_slot(int type);
/* Items of an item / building combo ("id - name"); returns the count. Item ids 50, 55, 63, 70, 71 are left out */
int  cs2d_ed_prop_combo_ids(int kind, int *ids, char names[][48], int max);

/* Widget values of the open window */
typedef struct {
    char name[64], trigger[64];
    int  rot;
    char text[CS2D_ED_PROP_FIELDS][64];   /* Textboxes */
    int  cursor[CS2D_ED_PROP_FIELDS];
    int  value[CS2D_ED_PROP_FIELDS];      /* Radio / check / combo index */
    int  rgb[3];                          /* The color box */
} cs2d_ed_prop_t;

void cs2d_ed_prop_load(cs2d_ed_prop_t *p, const cs2d_entity_t *e);
void cs2d_ed_prop_apply(const cs2d_ed_prop_t *p, cs2d_entity_t *e);

/*
 * Minimap pixmap, 1 px per tile (FUN_14019d7c0 in the editor, 0x140190760 in game, TmapPreview): tile 0 black;
 * by g_prop_class of the mode: wall (0) white, floor (1) (50,50,50) or (0,100,150) for mode 14, obstacle (2)
 * (125,125,125), deadly (-1) red. With entities, each one is a pixel in its type color (editor_entities).
 * out: width * height pixels, (a << 24) | (b << 16) | (g << 8) | r, row-major.
 */
void cs2d_ed_minimap_build(const cs2d_map_t *map, bool entities, uint32_t *out);

/* Display Settings (sys/editor.cfg, written by FUN_1402b7570): flags in checkbox order */
enum {
    CS2D_ED_DISP_MINIMAP,        /* editor_minimap (0x140a14cd4) */
    CS2D_ED_DISP_GRID,           /* editor_grid (0x140a14cd0): grid lines, G toggles it */
    CS2D_ED_DISP_SHADOWS,        /* editor_shadows (0x140a14ccc): map shadows */
    CS2D_ED_DISP_ENTITIES,       /* editor_entities (0x140a14cc8): entity icons outside the Entities mode */
    CS2D_ED_DISP_ENTITYGFX,      /* editor_entitygraphics (0x140a14cc4) */
    CS2D_ED_DISP_GAMEPLAYVIEW,   /* editor_gameplayview (0x140a14cc0) */
    CS2D_ED_DISP_COUNT
};
extern const char *const cs2d_ed_disp_keys[CS2D_ED_DISP_COUNT];
extern const char *const cs2d_ed_disp_labels[CS2D_ED_DISP_COUNT];
/* Missing file or keys keep the defaults (1 1 1 0 1 1, the shipped sys/editor.cfg) */
bool cs2d_ed_disp_load(bool flags[CS2D_ED_DISP_COUNT], const char *path);
bool cs2d_ed_disp_save(const bool flags[CS2D_ED_DISP_COUNT], const char *path);

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
    bool  ent_show_all;           /* KeyDown(Space): Te.drawineditor p3, entity helpers for every entity */
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

    bool  disp[CS2D_ED_DISP_COUNT];      /* Active display settings */
    bool  disp_open;                     /* Display Settings window */
    bool  disp_edit[CS2D_ED_DISP_COUNT]; /* Its checkboxes (applied by Okay) */

    bool  entprop_open;           /* Entity Properties window */
    int   entprop_index;          /* Map entity it edits */
    cs2d_ed_prop_t entprop;
    bool  color_for_entprop;      /* The Color window edits entprop.rgb instead of the brush */
    bool  rot_drag;               /* Left button held on the rotation dial */
    bool  minimap_drag;           /* Left button held on the minimap */
} cs2d_ed_state_t;

/* Blank map of w x h tiles (tile 0, no modifiers, no entities) on a tileset */
bool cs2d_ed_new_map(cs2d_map_t *map, uint32_t w, uint32_t h, const char *tileset);

#endif /* CS2D_EDITOR_H */
