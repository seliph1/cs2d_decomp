#ifndef CS2D_HUDMEN_H
#define CS2D_HUDMEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/* ---------------------------------------------------------------------------
 * Lua server menu: menu 120 (0x78) (FUN_14015c320, opcode 235 CS2D_NET_LUAMENU)
 * Format: "Title[@b|@i],Btn1|Right1,(Btn2|Right2),..."
 * --------------------------------------------------------------------------- */
#define CS2D_LUAMENU_MAX_ITEMS 9

typedef struct {
    char label[64];
    char right[64];
    bool enabled;
} cs2d_luamenu_item_t;

typedef struct {
    bool                open;
    int                 mode;       /* 0 = default left (x=20, w=272, h=330), 1 = big center (w=600, h=330), 2 = hide (x=10000) */
    char                title[64];  /* Window title */
    cs2d_luamenu_item_t items[CS2D_LUAMENU_MAX_ITEMS]; /* 1..9 (index 0..8) */
} cs2d_luamenu_t;

/* Parses raw Lua menu string (from opcode 235) into cs2d_luamenu_t */
void cs2d_luamenu_parse(cs2d_luamenu_t *menu, const char *raw);

/* Renders the Lua menu (if open) */
void cs2d_luamenu_render(const cs2d_luamenu_t *menu, float mx, float my, int screen_w, int screen_h);

/* Handles mouse click; returns 0..9 selection (0 = Cancel / Close, 1..9 = button, -1 = no hit) */
int cs2d_luamenu_click(cs2d_luamenu_t *menu, float mx, float my, int screen_w, int screen_h);

/* Handles digit key 0..9; returns 0..9 selection, or -1 if not handled / disabled */
int cs2d_luamenu_key(cs2d_luamenu_t *menu, int digit);

/* ---------------------------------------------------------------------------
 * Ingame menu (Tgui_hudmen.draw menu 2, opened by Esc) and vote screen (menu 100)
 * --------------------------------------------------------------------------- */
typedef enum {
    CS2D_GM_NONE = 0,
    CS2D_GM_BRIEFING,     /* 1 Show Briefing */
    CS2D_GM_TEAM,         /* 2 Change Team (menu 10) */
    CS2D_GM_LEADERBOARD,  /* 3 Leaderboard ([230,0]) */
    CS2D_GM_VOTE,         /* 4 Vote (menu 100) */
    CS2D_GM_OPTIONS,      /* 5 Options */
    CS2D_GM_DISCONNECT,   /* 9 Disconnect / Quit */
    CS2D_GM_CLOSE,        /* 0 Close */
    CS2D_GM_SERVERINFO,   /* Server Info (the F1 window) */
    CS2D_GM_FAVORITE      /* Favorite row: toggles the server in sys/favorites.lst */
} cs2d_gm_act_t;

/* What the right-hand info box shows ([252,6] settings, Tgui_hudmen.draw 0x14021f6d0 menu 2) */
typedef struct {
    const char *server_name;
    const char *address;     /* "ip:port" */
    const char *map;
    int         os;          /* 1 Windows, 2 MacOS, 3 Linux */
    int         ticks;
    int         players, bots, max_players;
    int         gamemode;
    bool        ff, forcelight, recoil, offscreen, physics, lua;
    int         voicechat, fow;
    int         lagcomp, lag_ticks;
    bool        leaderboard; /* Button 3 enabled */
    bool        favorite;
} cs2d_gamemenu_info_t;

void          cs2d_gamemenu_render(const cs2d_gamemenu_info_t *info, float mx, float my, float wx, float wy);
cs2d_gm_act_t cs2d_gamemenu_click(const cs2d_gamemenu_info_t *info, float mx, float my, float wx, float wy);
cs2d_gm_act_t cs2d_gamemenu_key(const cs2d_gamemenu_info_t *info, int digit);

/* Player value of the info box: "N/max" plus " (B Bot)" / " (B Bots)" */
void cs2d_gamemenu_players_text(int players, int bots, int max_players, char *out, size_t n);
/* "200ms (62 ticks)": lagcomp * 100 ms and 1000 / ticks */
void cs2d_gamemenu_lag_text(int lagcomp, int lag_ticks, char *out, size_t n);

#define CS2D_VOTE_LIST_MAX 64

typedef struct {
    char label[64];
    int  value;       /* Player id or map cycle index */
    int  votes;
    bool current;     /* The current map: (150,255,0) */
    bool mark;        /* Yellow '<': the local player / the current map */
} cs2d_vote_entry_t;

typedef struct {
    cs2d_vote_entry_t e[CS2D_VOTE_LIST_MAX];
    int               n;
} cs2d_vote_list_t;

/* Tgui_hudmen.votesetup (0x140087a50): sorted by the key "99 - votes" + label, i.e. most votes first */
void cs2d_vote_list_sort(cs2d_vote_list_t *l);
/* getVoteBrightness (0x140064830): 0 -> 125, 1 -> 200, 2 -> 220, 3 -> 240, more -> 255 */
int  cs2d_vote_brightness(int votes);

/* Vote screen; click returns 1 = kick row (*value = player), 2 = map row (*value = index), 0 = Close, -1 none */
void cs2d_votemenu_render(const cs2d_vote_list_t *kick, const cs2d_vote_list_t *maps, float mx, float my,
                          float wx, float wy);
int  cs2d_votemenu_click(const cs2d_vote_list_t *kick, const cs2d_vote_list_t *maps, float mx, float my,
                         float wx, float wy, int *value);

/* Leaderboard formats: FUN_140199930 ("42 s", "3 m 5 s", "8 h 4 m", "29 d 15 h") and FUN_1402e2ef0 (UTC
 * "YYYY/MM/DD HH:MM") */
void cs2d_format_playtime(int seconds, char *out, size_t n);
void cs2d_format_timestamp(int32_t unix_time, char *out, size_t n);

#endif /* CS2D_HUDMEN_H */
