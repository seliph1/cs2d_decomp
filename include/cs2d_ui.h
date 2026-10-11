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
    bool                      draw_selection;   /* Text selection highlight, drawn under the children */
    float                     selection_x0;     /* Offsets from rect.x (clamped to the element) */
    float                     selection_x1;
    cs2d_ui_color_t           selection_color;
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

#define CS2D_UI_MAX_KEY_EVENTS   32
#define CS2D_UI_MAX_FOCUSABLE    32
#define CS2D_UI_EDIT_MAX_TEXT    256 /* Longest buffer the edit state can snapshot / measure */
#define CS2D_UI_UNDO_DEPTH       32

/* Modifier bits stored with each queued key (taken from SDL_GetModState when fed) */
#define CS2D_UI_MOD_SHIFT 0x1u
#define CS2D_UI_MOD_CTRL  0x2u
#define CS2D_UI_MOD_ALT   0x4u

typedef struct {
    int      key;  /* SDL keycode */
    uint32_t mods; /* CS2D_UI_MOD_* */
} cs2d_ui_key_event_t;

typedef struct {
    char text[CS2D_UI_EDIT_MAX_TEXT];
    int  cursor;
    int  anchor;
} cs2d_ui_undo_entry_t;

/*
 * Editing state of the focused textbox (only one textbox is focused at a time).
 * Positions are byte offsets into the UTF-8 buffer, always on codepoint boundaries.
 */
typedef struct {
    cs2d_ui_id_t         id;              /* Textbox this state belongs to (0 = none) */
    int                  anchor;          /* Selection anchor; selection is [min(anchor,cursor), max) */
    float                scroll_x;        /* Horizontal scroll of the text inside the box */
    bool                 dragging;        /* Mouse selection in progress */
    uint32_t             activity_ms;     /* Last edit/move; keeps the caret solid while typing */

    /* Caret x positions for every byte offset of the displayed text (recomputed when it changes) */
    char                 measured[CS2D_UI_EDIT_MAX_TEXT];
    bool                 measured_password;
    float                caret_x[CS2D_UI_EDIT_MAX_TEXT + 1];

    /* Undo / redo snapshots; entries [0, undo_pos) are undo, [undo_pos, undo_count) are redo */
    cs2d_ui_undo_entry_t undo[CS2D_UI_UNDO_DEPTH];
    int                  undo_count;
    int                  undo_pos;
    int                  last_edit_kind;  /* Consecutive edits of the same kind merge into one undo step */
    uint32_t             last_edit_ms;
} cs2d_ui_edit_state_t;

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
    int                  click_count;    /* 1 = single, 2 = double, 3 = triple click (set on mouse_pressed) */
    uint32_t             last_click_ms;
    float                last_click_x;
    float                last_click_y;

    cs2d_ui_id_t         hot_id;
    cs2d_ui_id_t         active_id;
    cs2d_ui_id_t         focused_id;

    /* Input queues consumed by the focused widget during the next layout pass */
    char                 text_input[128];
    cs2d_ui_key_event_t  key_events[CS2D_UI_MAX_KEY_EVENTS];
    int                  key_event_count;

    /* Tab / Shift+Tab focus traversal across textboxes, in build order */
    cs2d_ui_id_t         focus_order[CS2D_UI_MAX_FOCUSABLE];
    int                  focus_order_count;
    int                  focus_move;       /* +1 / -1 requested by the focused textbox this frame */
    bool                 select_all_on_focus;

    cs2d_ui_edit_state_t edit;

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
/* Queue text (UTF-8, from SDL_EVENT_TEXT_INPUT) and key presses (with the current modifiers) */
void cs2d_ui_feed_text(cs2d_ui_context_t *ctx, const char *text);
void cs2d_ui_feed_key(cs2d_ui_context_t *ctx, int sdl_key);

/* Drop queued input; call on frames where no UI is laid out so keys don't pile up */
void cs2d_ui_clear_input(cs2d_ui_context_t *ctx);

/* Forget selection, scroll and undo history of the focused textbox (e.g. after replacing its text) */
void cs2d_ui_edit_reset(cs2d_ui_context_t *ctx);

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

/* Textbox behaviour flags for cs2d_ui_textbox_ex */
#define CS2D_UI_TEXTBOX_PASSWORD 0x1u /* Shows '*' per character; copy / cut are disabled */
#define CS2D_UI_TEXTBOX_NUMERIC  0x2u /* Accepts digits only (typed or pasted) */
#define CS2D_UI_TEXTBOX_READONLY 0x4u /* Selectable and copyable, but not editable */

/*
 * CS2D Single-Line Textbox. Returns true on the frame Enter is pressed while focused.
 *   Mouse:     click to place the caret, drag / Shift+click to select,
 *              double-click selects a word, triple-click selects everything
 *   Movement:  Left/Right, Home/End, Ctrl+Left/Right by word; add Shift to select
 *   Editing:   Backspace/Delete, Ctrl+Backspace/Delete by word, Ctrl+A select all
 *   Clipboard: Ctrl+C / Ctrl+Insert copy, Ctrl+X / Shift+Delete cut, Ctrl+V / Shift+Insert paste
 *   History:   Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo
 *   Focus:     Tab / Shift+Tab moves to the next / previous textbox
 * Long text scrolls horizontally to keep the caret visible.
 */
bool cs2d_ui_textbox_ex(
    cs2d_ui_context_t *ctx,
    cs2d_ui_id_t id,
    char *buffer,
    size_t buffer_size,
    int *cursor_pos,
    float width,
    uint32_t flags
);

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
