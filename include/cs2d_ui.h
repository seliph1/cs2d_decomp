#ifndef CS2D_UI_H
#define CS2D_UI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cs2d_render.h"

/*
 * ============================================================================
 * CS2D Clay-Inspired Declarative UI Layout & Widget Library (C99)
 * ============================================================================
 * Provides a two-phase declarative layout engine (Sizing: FIT, GROW, FIXED,
 * PERCENT; Direction: LEFT_TO_RIGHT, TOP_TO_BOTTOM; Padding; Child Gap;
 * Alignment; Floating Overlays; Scissor Clipping; Render Command List) plus
 * authentic CS2D widgets (Windows, Tab Bars, Buttons, Checkboxes, Radios,
 * Dropdown Selectors, Textboxes, Scrollable Lists, and Section Headers).
 */

#define CS2D_UI_MAX_NODES        768
#define CS2D_UI_MAX_CHILDREN     64
#define CS2D_UI_MAX_STACK        32
#define CS2D_UI_MAX_RENDER_CMDS  1536
#define CS2D_UI_MAX_BOX_CACHE    768

typedef uint32_t cs2d_ui_id_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} cs2d_ui_color_t;

#define CS2D_UI_RGBA(r, g, b, a) ((cs2d_ui_color_t){ (uint8_t)(r), (uint8_t)(g), (uint8_t)(b), (uint8_t)(a) })
#define CS2D_UI_RGB(r, g, b)     ((cs2d_ui_color_t){ (uint8_t)(r), (uint8_t)(g), (uint8_t)(b), 255 })

typedef struct {
    float x;
    float y;
    float w;
    float h;
} cs2d_ui_rect_t;

typedef enum {
    CS2D_UI_SIZING_TYPE_FIT     = 0,
    CS2D_UI_SIZING_TYPE_GROW    = 1,
    CS2D_UI_SIZING_TYPE_FIXED   = 2,
    CS2D_UI_SIZING_TYPE_PERCENT = 3
} cs2d_ui_sizing_type_t;

typedef struct {
    cs2d_ui_sizing_type_t type;
    float                 min_size;
    float                 max_size;
    float                 percent;
} cs2d_ui_sizing_axis_t;

typedef struct {
    cs2d_ui_sizing_axis_t width;
    cs2d_ui_sizing_axis_t height;
} cs2d_ui_sizing_t;

#define CS2D_UI_FIT(min_v, max_v)  ((cs2d_ui_sizing_axis_t){ CS2D_UI_SIZING_TYPE_FIT,     (float)(min_v), (float)(max_v), 0.0f })
#define CS2D_UI_GROW(min_v, max_v) ((cs2d_ui_sizing_axis_t){ CS2D_UI_SIZING_TYPE_GROW,    (float)(min_v), (float)(max_v), 0.0f })
#define CS2D_UI_FIXED(px)          ((cs2d_ui_sizing_axis_t){ CS2D_UI_SIZING_TYPE_FIXED,   (float)(px),    (float)(px),    0.0f })
#define CS2D_UI_PERCENT(frac)      ((cs2d_ui_sizing_axis_t){ CS2D_UI_SIZING_TYPE_PERCENT, 0.0f,           0.0f,           (float)(frac) })

typedef enum {
    CS2D_UI_LEFT_TO_RIGHT = 0,
    CS2D_UI_TOP_TO_BOTTOM = 1
} cs2d_ui_layout_dir_t;

typedef enum {
    CS2D_UI_ALIGN_X_LEFT   = 0,
    CS2D_UI_ALIGN_X_CENTER = 1,
    CS2D_UI_ALIGN_X_RIGHT  = 2
} cs2d_ui_align_x_t;

typedef enum {
    CS2D_UI_ALIGN_Y_TOP    = 0,
    CS2D_UI_ALIGN_Y_CENTER = 1,
    CS2D_UI_ALIGN_Y_BOTTOM = 2
} cs2d_ui_align_y_t;

typedef struct {
    float left;
    float right;
    float top;
    float bottom;
} cs2d_ui_padding_t;

#define CS2D_UI_PAD_ALL(p)       ((cs2d_ui_padding_t){ (float)(p),  (float)(p),  (float)(p),  (float)(p)  })
#define CS2D_UI_PAD_XY(px, py)   ((cs2d_ui_padding_t){ (float)(px), (float)(px), (float)(py), (float)(py) })
#define CS2D_UI_PAD(l, r, t, b)  ((cs2d_ui_padding_t){ (float)(l),  (float)(r),  (float)(t),  (float)(b)  })

typedef struct {
    cs2d_ui_sizing_t     sizing;
    cs2d_ui_padding_t    padding;
    float                child_gap;
    cs2d_ui_layout_dir_t dir;
    cs2d_ui_align_x_t    align_x;
    cs2d_ui_align_y_t    align_y;
} cs2d_ui_layout_config_t;

typedef struct {
    float           width;
    cs2d_ui_color_t color;
} cs2d_ui_border_config_t;

typedef struct {
    bool  enabled;
    float x;
    float y;
    int   z_index;
} cs2d_ui_floating_config_t;

typedef struct {
    bool  enabled;
    float scroll_x;
    float scroll_y;
} cs2d_ui_clip_config_t;

typedef struct {
    const cs2d_texture_t *tex;
    float                 width;
    float                 height;
    cs2d_ui_color_t       tint;
} cs2d_ui_image_config_t;

typedef struct {
    cs2d_ui_layout_config_t   layout;
    cs2d_ui_color_t           bg_color;
    cs2d_ui_color_t           bg_gradient_bottom;
    cs2d_ui_border_config_t   border;
    cs2d_ui_floating_config_t floating;
    cs2d_ui_clip_config_t     clip;
    cs2d_ui_image_config_t    image;
    bool                      draw_cross;
    float                     cross_inset;
    cs2d_ui_color_t           cross_color;
    bool                      draw_cursor;
    float                     cursor_offset_x;
    cs2d_ui_color_t           cursor_color;
} cs2d_ui_element_decl_t;

typedef enum {
    CS2D_UI_CMD_RECT          = 0,
    CS2D_UI_CMD_GRADIENT_RECT = 1,
    CS2D_UI_CMD_BORDER        = 2,
    CS2D_UI_CMD_TEXT          = 3,
    CS2D_UI_CMD_IMAGE         = 4,
    CS2D_UI_CMD_LINE          = 5,
    CS2D_UI_CMD_SCISSOR_START = 6,
    CS2D_UI_CMD_SCISSOR_END   = 7
} cs2d_ui_render_cmd_type_t;

typedef struct {
    cs2d_ui_render_cmd_type_t type;
    cs2d_ui_rect_t            rect;
    cs2d_ui_color_t           color;
    cs2d_ui_color_t           color2;
    float                     border_width;
    cs2d_font_id_t            font_id;
    char                      text[192];
    const cs2d_texture_t     *tex;
    int                       z_index;
} cs2d_ui_render_cmd_t;

typedef struct {
    cs2d_ui_id_t           id;
    int                    parent_idx;
    int                    children[CS2D_UI_MAX_CHILDREN];
    int                    child_count;
    cs2d_ui_element_decl_t decl;

    bool                   is_text;
    cs2d_font_id_t         font_id;
    cs2d_ui_color_t        text_color;
    char                   text[192];
    float                  text_w;
    float                  text_h;

    /* Computed layout geometry */
    cs2d_ui_rect_t         rect;
    int                    effective_z;
} cs2d_ui_node_t;

typedef struct {
    cs2d_ui_id_t   id;
    cs2d_ui_rect_t rect;
    int            z_index;
} cs2d_ui_box_entry_t;

typedef struct {
    int                  screen_w;
    int                  screen_h;

    /* Pointer / Mouse & Keyboard State */
    float                mouse_x;
    float                mouse_y;
    bool                 mouse_down;
    bool                 mouse_pressed;  /* True on the single frame button went down */
    bool                 mouse_released; /* True on the single frame button went up */
    bool                 prev_mouse_down;
    int                  wheel_delta;

    cs2d_ui_id_t         hot_id;
    cs2d_ui_id_t         active_id;
    cs2d_ui_id_t         focused_id;

    /* Text input queue for focused textbox */
    char                 text_input[64];
    int                  last_key;

    /* Current frame node tree */
    cs2d_ui_node_t       nodes[CS2D_UI_MAX_NODES];
    int                  node_count;
    int                  open_stack[CS2D_UI_MAX_STACK];
    int                  open_stack_depth;

    /* Previous frame's solved bounding boxes for immediate hit-testing */
    cs2d_ui_box_entry_t  box_cache[CS2D_UI_MAX_BOX_CACHE];
    int                  box_cache_count;

    /* Emitted Render Commands */
    cs2d_ui_render_cmd_t render_cmds[CS2D_UI_MAX_RENDER_CMDS];
    int                  render_cmd_count;
} cs2d_ui_context_t;

/* Deterministic FNV-1a ID hashing (like CLAY_ID / CLAY_IDI) */
cs2d_ui_id_t cs2d_ui_hash_id(const char *label, uint32_t index);

#define CS2D_UI_ID(label)       cs2d_ui_hash_id((label), 0)
#define CS2D_UI_IDI(label, idx) cs2d_ui_hash_id((label), (uint32_t)(idx))

/* Scoped C99 Declarative Element Macro (like CLAY() {}) */
#define CS2D_UI_ELEMENT(ctx, elem_id, ...) \
    for (int _cs2d_ui_i = (cs2d_ui_open_element((ctx), (elem_id), &(cs2d_ui_element_decl_t)__VA_ARGS__), 1); \
         _cs2d_ui_i; \
         _cs2d_ui_i = (cs2d_ui_close_element(ctx), 0))

/* Core Lifecycle & Layout Solver */
void cs2d_ui_init(cs2d_ui_context_t *ctx);
void cs2d_ui_begin_layout(
    cs2d_ui_context_t *ctx,
    int screen_w, int screen_h,
    float mouse_x, float mouse_y,
    bool mouse_down,
    int wheel_delta
);
void cs2d_ui_feed_text(cs2d_ui_context_t *ctx, const char *text);
void cs2d_ui_feed_key(cs2d_ui_context_t *ctx, int sdl_key);

void cs2d_ui_open_element(cs2d_ui_context_t *ctx, cs2d_ui_id_t id, const cs2d_ui_element_decl_t *decl);
void cs2d_ui_close_element(cs2d_ui_context_t *ctx);
void cs2d_ui_text(cs2d_ui_context_t *ctx, const char *text, cs2d_font_id_t font_id, cs2d_ui_color_t color);
void cs2d_ui_emit_line(cs2d_ui_context_t *ctx, float x1, float y1, float x2, float y2, cs2d_ui_color_t color);

void cs2d_ui_end_layout(cs2d_ui_context_t *ctx);
void cs2d_ui_render(const cs2d_ui_context_t *ctx);

/* Hit-Testing & Geometry Queries */
cs2d_ui_rect_t cs2d_ui_get_box(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id);
bool           cs2d_ui_hovered(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id);
bool           cs2d_ui_clicked(const cs2d_ui_context_t *ctx, cs2d_ui_id_t id);

/* ============================================================================
 * Authentic CS2D Theme Widgets (Built on the Declarative Layout Engine)
 * ============================================================================ */

/* Opens a floating CS2D window with title bar and 'X' close button */
bool cs2d_ui_window_begin(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *title,
    float x, float y, float w, float h,
    bool *is_open
);
void cs2d_ui_window_end(cs2d_ui_context_t *ctx);

/* Horizontal CS2D Tab Bar (e.g. Server | Map | Settings | Bots | Mods) */
int cs2d_ui_tab_bar(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char **tab_labels,
    int tab_count,
    int *active_tab
);

/* CS2D Button (e.g. Start, Cancel, Okay, Help) */
bool cs2d_ui_button(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    float width,
    float height
);

/* CS2D Checkbox ([X] Label) */
bool cs2d_ui_checkbox(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    bool *checked
);

/* CS2D Radio Button ((o) Label) */
bool cs2d_ui_radio(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *label,
    int value,
    int *current_value
);

/* CS2D Dropdown / Cycle Selector ([ Value           |+| ]) */
bool cs2d_ui_select(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char **options,
    int option_count,
    int *selected_index,
    float width
);

/* CS2D Single-Line Textbox with focus, editing, and blinking cursor */
bool cs2d_ui_textbox(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    char *buffer,
    size_t buffer_size,
    int *cursor_pos,
    float width,
    bool password_mode
);

/* CS2D Gradient Section Header inside lists (e.g. "DE - Bomb Defuse", "Communication") */
void cs2d_ui_section_header(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    const char *title
);

#endif /* CS2D_UI_H */
