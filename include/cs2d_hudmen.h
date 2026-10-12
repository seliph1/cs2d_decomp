#ifndef CS2D_HUDMEN_H
#define CS2D_HUDMEN_H

#include <stdbool.h>
#include <stddef.h>
#include "cs2d_render.h"

/*
 * Tgui_hudmen (CS2D.exe, docs/dump/cs2d/gui/Tgui_hudmen.c): the hud menu window (600 x 440, centered,
 * Tgui_hudmen.update 0x14002eb80) and its widgets, plus the Buy menu (menus 20..26).
 */

#define CS2D_HUDMEN_WIN_W 600
#define CS2D_HUDMEN_WIN_H 440

/* Loads gfx/gui_shade.bmp and the weapon images of the Buy menu (gfx/weapons/<name>_k.bmp, _m.bmp, _d.bmp) */
void cs2d_hudmen_load(void);
void cs2d_hudmen_unload(void);

/* Top-left corner of the hud menu window */
void cs2d_hudmen_origin(int screen_w, int screen_h, float *wx, float *wy);

/* The window frame and its title */
void cs2d_hudmen_window(float wx, float wy, const char *title);

/*
 * Tgui_hudmen.button (0x140129620), 25 px high: key >= 0 draws the key number, -1 none. img (may be NULL) is
 * scaled to fit 46 x 22 around x + 42. Returns true while the pointer is over an enabled button.
 */
bool cs2d_hudmen_button(float mx, float my, float x, float y, float w, int key, const char *label,
                        const char *right, bool enabled, const cs2d_texture_t *img);

/* Tgui_hudmen.fbutton (0x14012a2c0): the same frame with a centered label and no key */
bool cs2d_hudmen_fbutton(float mx, float my, float x, float y, float w, const char *label, bool enabled);

/* Tgui_hudmen.box (0x14012aaf0): a 1 px frame (150) around black at alpha 0.3 */
void cs2d_hudmen_box(float x, float y, float w, float h);

/* ---------------------------------------------------------------------------
 * Buy menu: menu 20 ("buy" command) lists the sections, 21..26 their items ("buyequip" opens 26)
 * --------------------------------------------------------------------------- */
enum {
    CS2D_BUYMENU_MAIN      = 20,
    CS2D_BUYMENU_HANDGUN   = 21,
    CS2D_BUYMENU_SHOTGUN   = 22,
    CS2D_BUYMENU_SMG       = 23,
    CS2D_BUYMENU_RIFLE     = 24,
    CS2D_BUYMENU_MG        = 25,
    CS2D_BUYMENU_EQUIPMENT = 26
};

typedef enum {
    CS2D_BUY_ACT_NONE = 0,
    CS2D_BUY_ACT_ITEM,     /* buy `item` ([23, item, 0]) */
    CS2D_BUY_ACT_MENU,     /* open `menu` */
    CS2D_BUY_ACT_CLOSE,
    CS2D_BUY_ACT_AUTOBUY,  /* runs the "autobuy" command, then closes */
    CS2D_BUY_ACT_REBUY     /* runs the "rebuy" command, then closes */
} cs2d_buy_act_t;

typedef struct {
    int            team;       /* Local player's team (1 T, 2 CT) */
    int            money;
    bool           bomb_spots; /* The map has Info_BombSpot entities (DAT_140ddf078 > 0) */
} cs2d_buyctx_t;

typedef struct {
    int            x, y, w;    /* Relative to the window */
    int            key;        /* 0..9, -1 = no key */
    const char    *label;      /* NULL: the item's name */
    int            item;       /* Item bought or described (0: none) */
    int            img;        /* Item whose _k.bmp is drawn on the button (0: none) */
    cs2d_buy_act_t act;
    int            menu;       /* Menu opened by CS2D_BUY_ACT_MENU */
    bool           enabled;
} cs2d_hudmen_btn_t;

typedef struct {
    int            menu;       /* Tgui_hudmen.menu: 0 closed, 20..26 */
    float          rot;        /* Tgui_hudmen.rot: section preview rotation (degrees) */
} cs2d_buymenu_t;

/* Buttons of a Buy menu page (Tgui_hudmen.draw 0x14021f6d0); returns the count */
int cs2d_buymenu_layout(int menu, const cs2d_buyctx_t *ctx, cs2d_hudmen_btn_t *out, int max);

/* Window title: "Buy" (0308), "Buy - <section>" for 21..26 */
void cs2d_buymenu_title(int menu, char *out, size_t n);

/* Items circling the preview of a section (drawBuySectionPreview 0x140111b10) and the circle radius */
int cs2d_buymenu_preview(int key, int team, int *items, int max, int *radius);

/* One line of the item description (Tgui_hudmen.weaponinfo 0x14013bfd0), at window + (280, y) */
typedef struct {
    int            y;
    char           label[64];  /* Left text */
    char           value[64];  /* Right aligned at window x + 575 (empty: none) */
    bool           red;        /* Price above the player's money */
} cs2d_buyinfo_line_t;

int cs2d_buymenu_info(int item, int money, cs2d_buyinfo_line_t *out, int max);

/* Mouse click / number key: navigation and closing are applied to `m`; *item gets the item to buy */
cs2d_buy_act_t cs2d_buymenu_click(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, float mx, float my,
                                  int screen_w, int screen_h, int *item);
cs2d_buy_act_t cs2d_buymenu_key(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, int digit, int *item);

void cs2d_buymenu_render(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, float mx, float my,
                         int screen_w, int screen_h);

#endif /* CS2D_HUDMEN_H */
