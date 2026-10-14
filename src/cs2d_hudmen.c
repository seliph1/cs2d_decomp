#include "cs2d_hudmen.h"
#include "cs2d_weapons.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Tgui_hudmen (CS2D.exe): hud menu widgets and the Buy menu. Sources: docs/dump/cs2d/gui/Tgui_hudmen.c
 * (update 0x14002eb80, button 0x140129620, box 0x14012aaf0, moneyinfo 0x140108010, drawBuySectionPreview
 * 0x140111b10, weaponinfo 0x14013bfd0, buyWpnButton 0x140202670, draw 0x14021f6d0 menus 0x14..0x1a); g_lang
 * strings are sys/language (EN) entry (N * 36 + k + 28) / 8 of g_lang[N].scales + data_start + k.
 */

static cs2d_texture_t s_tex_shade;                      /* gfx/gui_shade.bmp (0x140a0de78) */
static cs2d_texture_t s_tex_k[CS2D_MAX_WEAPONS];        /* gfx/weapons/<name>_k.bmp (0x1409f3d18), top-left handle */
static cs2d_texture_t s_tex_m[CS2D_MAX_WEAPONS];        /* gfx/weapons/<name>_m.bmp (0x1409f3d20), midhandled */
static cs2d_texture_t s_tex_d[CS2D_MAX_WEAPONS];        /* gfx/weapons/<name>_d.bmp (0x1409f3d28), midhandled */

/* Items listed by the Buy menu: only their images are loaded */
static const uint8_t s_buy_items[] = {
    1, 2, 3, 4, 5, 6, 10, 11, 20, 21, 22, 23, 24, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    51, 52, 53, 54, 56, 57, 58, 59, 61, 62
};

/* Image name of an item (0x140220000 init loop): the name without spaces and dashes, in lower case */
static void item_slug(uint8_t id, char *out, size_t n)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get(id);
    size_t j = 0;
    for (const char *s = (w && w->name) ? w->name : ""; *s && j + 1 < n; s++) {
        if (*s != ' ' && *s != '-') out[j++] = (char)tolower((unsigned char)*s);
    }
    out[j] = '\0';
}

static void load_if_present(cs2d_texture_t *tex, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fclose(f);
    cs2d_texture_load_ex(tex, path, 2); /* TResGfx maskMode 2 (magenta) for _k, _m and _d */
}

void cs2d_hudmen_load(void)
{
    cs2d_texture_load_ex(&s_tex_shade, "gfx/gui_shade.bmp", 3);
    for (size_t i = 0; i < sizeof(s_buy_items); i++) {
        uint8_t id = s_buy_items[i];
        char slug[48], path[96];
        item_slug(id, slug, sizeof(slug));
        snprintf(path, sizeof(path), "gfx/weapons/%s_k.bmp", slug);
        load_if_present(&s_tex_k[id], path);
        snprintf(path, sizeof(path), "gfx/weapons/%s_m.bmp", slug);
        load_if_present(&s_tex_m[id], path);
        snprintf(path, sizeof(path), "gfx/weapons/%s_d.bmp", slug);
        load_if_present(&s_tex_d[id], path);
    }
}

void cs2d_hudmen_unload(void)
{
    cs2d_texture_free(&s_tex_shade);
    for (int i = 0; i < CS2D_MAX_WEAPONS; i++) {
        cs2d_texture_free(&s_tex_k[i]);
        cs2d_texture_free(&s_tex_m[i]);
        cs2d_texture_free(&s_tex_d[i]);
    }
}

void cs2d_hudmen_origin(int screen_w, int screen_h, float *wx, float *wy)
{
    *wx = (float)((screen_w - CS2D_HUDMEN_WIN_W) / 2);
    *wy = (float)((screen_h - CS2D_HUDMEN_WIN_H) / 2);
}

void cs2d_hudmen_window(float wx, float wy, const char *title)
{
    /* Window frame and title like the other Tgui windows (cs2d_ui_window_begin colors) */
    cs2d_render_rect(wx, wy, (float)CS2D_HUDMEN_WIN_W, (float)CS2D_HUDMEN_WIN_H, 58.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f,
                     228.0f / 255.0f, true);
    cs2d_render_rect(wx, wy, (float)CS2D_HUDMEN_WIN_W, (float)CS2D_HUDMEN_WIN_H, 96.0f / 255.0f, 96.0f / 255.0f, 96.0f / 255.0f,
                     245.0f / 255.0f, false);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 11.0f, wy + 4.0f, title, 1.0f, 1.0f, 1.0f, 1.0f);
}

static bool inside(float mx, float my, float x, float y, float w, float h)
{
    return mx >= x && my >= y && mx <= x + w && my <= y + h;
}

/* Image drawn with its top-left corner at (x, y) */
static void draw_image_tl(const cs2d_texture_t *t, float x, float y, float s, float c, float a, int blend)
{
    cs2d_render_sprite_frame(t, t->width, t->height, 0, x + t->width * s * 0.5f, y + t->height * s * 0.5f,
                             t->width * s, t->height * s, 0.0f, c, c, c, a, blend);
}

/* Midhandled image (MIDHANDLEINT for _m: the handle is width / 2, height / 2 in whole pixels) */
static void draw_image_mid(const cs2d_texture_t *t, float x, float y, float s, float c, float a)
{
    float hx = (float)(t->width / 2) * s, hy = (float)(t->height / 2) * s;
    draw_image_tl(t, x - hx, y - hy, s, c, a, 0);
}

bool cs2d_hudmen_button(float mx, float my, float x, float y, float w, int key, const char *label,
                        const char *right, bool enabled, const cs2d_texture_t *img)
{
    bool hover = enabled && key > -2 && inside(mx, my, x, y, w, 25.0f);
    /* Black inside at 0.3; frame 200 hovered / 150 / 50 disabled */
    float fc = (hover ? 200.0f : (enabled ? 150.0f : 50.0f)) / 255.0f;
    cs2d_render_rect(x + 1.0f, y + 1.0f, w - 2.0f, 23.0f, 0.0f, 0.0f, 0.0f, 0.3f, true);
    cs2d_render_rect(x + 1.0f, y, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x, y + 1.0f, 1.0f, 23.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + 24.0f, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + w - 1.0f, y + 1.0f, 1.0f, 23.0f, fc, fc, fc, 1.0f, true);
    /* gui_shade stretched over the inside, additive 0.3 hovered / 0.1 */
    if (s_tex_shade.id) {
        cs2d_render_sprite_frame(&s_tex_shade, 10, 10, 0, x + 1.0f + (w - 2.0f) * 0.5f, y + 6.0f, w - 2.0f, 10.0f,
                                 0.0f, 1.0f, 1.0f, 1.0f, hover ? 0.3f : 0.1f, 4);
    }
    /* Text white hovered / 200 / 100 disabled, centered on y + 12 */
    float tc = hover ? 1.0f : (enabled ? 200.0f : 100.0f) / 255.0f;
    float tw, th;
    cs2d_render_text_measure(CS2D_FONT_REGULAR, label, &tw, &th);
    float ty = y + 12.0f - (float)((int)th / 2);
    float lx = x + (key >= 0 ? 20.0f : 5.0f);
    if (key >= 0) {
        char k[12];
        snprintf(k, sizeof(k), "%d", key);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 5.0f, ty, k, tc, tc, tc, 0.4f);
    }
    if (img && img->id && img->width > 0 && img->height > 0) {
        /* Scaled to fit 46 x 22 (never enlarged), additive at 0.75, centered on x + 42; the label moves right */
        float s = fminf(fminf(22.0f / (float)img->height, 46.0f / (float)img->width), 1.0f);
        draw_image_tl(img, x + 42.0f - img->width * 0.5f * s, y + 12.0f - img->height * 0.5f * s, s, tc, 0.75f, 4);
        lx = x + (key >= 0 ? 70.0f : 55.0f);
    }
    cs2d_render_text_ex(CS2D_FONT_REGULAR, lx, ty, label, tc, tc, tc, 1.0f);
    if (right && right[0]) {
        float rw, rh;
        cs2d_render_text_measure(CS2D_FONT_REGULAR, right, &rw, &rh);
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + w - 5.0f - rw, ty, right, tc, tc, tc, 0.4f);
    }
    return hover;
}

bool cs2d_hudmen_fbutton(float mx, float my, float x, float y, float w, const char *label, bool enabled)
{
    bool hover = enabled && inside(mx, my, x, y, w, 25.0f);
    float fc = (hover ? 200.0f : (enabled ? 150.0f : 50.0f)) / 255.0f;
    cs2d_render_rect(x + 1.0f, y + 1.0f, w - 2.0f, 23.0f, 0.0f, 0.0f, 0.0f, 0.3f, true);
    cs2d_render_rect(x + 1.0f, y, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x, y + 1.0f, 1.0f, 23.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + 24.0f, w - 2.0f, 1.0f, fc, fc, fc, 1.0f, true);
    cs2d_render_rect(x + w - 1.0f, y + 1.0f, 1.0f, 23.0f, fc, fc, fc, 1.0f, true);
    if (s_tex_shade.id) {
        cs2d_render_sprite_frame(&s_tex_shade, 10, 10, 0, x + 1.0f + (w - 2.0f) * 0.5f, y + 6.0f, w - 2.0f, 10.0f,
                                 0.0f, 1.0f, 1.0f, 1.0f, hover ? 0.3f : 0.1f, 4);
    }
    float tc = hover ? 1.0f : (enabled ? 200.0f : 100.0f) / 255.0f;
    float tw, th;
    cs2d_render_text_measure(CS2D_FONT_REGULAR, label, &tw, &th);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, (float)((int)(x + w * 0.5f) - (int)tw / 2), y + 12.0f - (float)((int)th / 2),
                        label, tc, tc, tc, 1.0f);
    return hover;
}

void cs2d_hudmen_box(float x, float y, float w, float h)
{
    float c = 150.0f / 255.0f;
    cs2d_render_rect(x, y, w, 1.0f, c, c, c, 1.0f, true);
    cs2d_render_rect(x, y + 1.0f, 1.0f, h - 2.0f, c, c, c, 1.0f, true);
    cs2d_render_rect(x, y + h - 1.0f, w, 1.0f, c, c, c, 1.0f, true);
    cs2d_render_rect(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, c, c, c, 1.0f, true);
    cs2d_render_rect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 0.0f, 0.0f, 0.0f, 0.3f, true);
}

/* ---------------------------------------------------------------------------
 * Buy menu
 * --------------------------------------------------------------------------- */

#define BTN_X 16
#define BTN_W 240

static int add_btn(cs2d_hudmen_btn_t *out, int n, int max, int x, int y, int key, const char *label, int item,
                   int img, cs2d_buy_act_t act, int menu, bool enabled)
{
    if (n >= max) return n;
    out[n] = (cs2d_hudmen_btn_t){ x, y, BTN_W, key, label, item, img, act, menu, enabled };
    return n + 1;
}

/* buyWpnButton (0x140202670): the item's name, its _k image and "<price> $" */
static int add_item(cs2d_hudmen_btn_t *out, int n, int max, int y, int key, int item, bool enabled)
{
    return add_btn(out, n, max, BTN_X, y, key, NULL, item, item, CS2D_BUY_ACT_ITEM, 0, enabled);
}

int cs2d_buymenu_layout(int menu, const cs2d_buyctx_t *ctx, cs2d_hudmen_btn_t *out, int max)
{
    bool t = ctx->team == 1; /* Tpl +0x230 == 1 */
    int n = 0;
    switch (menu) {
        case CS2D_BUYMENU_MAIN:
            /* Section buttons show the _k image of the T or CT standard weapon (0x1409f3d18 [2] / [1] ...) */
            n = add_btn(out, n, max, BTN_X,  30, 1, "Handgun",         0, t ? 2 : 1,   CS2D_BUY_ACT_MENU, 21, true); /* 0309 */
            n = add_btn(out, n, max, BTN_X,  60, 2, "Shotgun",         0, 10,          CS2D_BUY_ACT_MENU, 22, true); /* 0310 */
            n = add_btn(out, n, max, BTN_X,  90, 3, "Sub Machine Gun", 0, 20,          CS2D_BUY_ACT_MENU, 23, true); /* 0311 */
            n = add_btn(out, n, max, BTN_X, 120, 4, "Rifle",           0, t ? 30 : 32, CS2D_BUY_ACT_MENU, 24, true); /* 0312 */
            n = add_btn(out, n, max, BTN_X, 150, 5, "Machine Gun",     0, 40,          CS2D_BUY_ACT_MENU, 25, true); /* 0313 */
            n = add_item(out, n, max, 210, 6, CS2D_WEP_PRIMARYAMMO, true);
            n = add_item(out, n, max, 240, 7, CS2D_WEP_SECONDARYAMMO, true);
            n = add_btn(out, n, max, BTN_X, 270, 8, "Equipment",       0, 58,          CS2D_BUY_ACT_MENU, 26, true); /* 0316 */
            n = add_btn(out, n, max, BTN_X, CS2D_HUDMEN_WIN_H - 46, 0, "Close", 0, 0, CS2D_BUY_ACT_CLOSE, 0, true);       /* 0031 */
            n = add_btn(out, n, max, CS2D_HUDMEN_WIN_W - 256, CS2D_HUDMEN_WIN_H - 76, -1, "Auto-Buy", 0, 0,
                        CS2D_BUY_ACT_AUTOBUY, 0, true);                                                               /* 0318 */
            n = add_btn(out, n, max, CS2D_HUDMEN_WIN_W - 256, CS2D_HUDMEN_WIN_H - 46, -1, "Re-Buy Previous", 0, 0,
                        CS2D_BUY_ACT_REBUY, 0, true);                                                                 /* 0317 */
            return n;
        case CS2D_BUYMENU_HANDGUN:
            for (int i = 0; i < 6; i++) n = add_item(out, n, max, 30 + 30 * i, i + 1, i + 1, true);
            break;
        case CS2D_BUYMENU_SHOTGUN:
            n = add_item(out, n, max, 30, 1, CS2D_WEP_M3, true);
            n = add_item(out, n, max, 60, 2, CS2D_WEP_XM1014, true);
            break;
        case CS2D_BUYMENU_SMG:
            /* Key 1 is the Mac 10 for Terrorists, the MP5 for the others; the TMP stays on key 2 for both */
            n = add_item(out, n, max,  30, 1, t ? CS2D_WEP_MAC10 : CS2D_WEP_MP5, true);
            n = add_item(out, n, max,  60, 2, CS2D_WEP_TMP, true);
            n = add_item(out, n, max,  90, 3, CS2D_WEP_P90, true);
            n = add_item(out, n, max, 120, 4, CS2D_WEP_UMP45, true);
            break;
        case CS2D_BUYMENU_RIFLE:
            n = add_item(out, n, max,  30, 1, t ? CS2D_WEP_GALIL : CS2D_WEP_FAMAS, true);
            n = add_item(out, n, max,  60, 2, t ? CS2D_WEP_AK47 : CS2D_WEP_M4A1, true);
            n = add_item(out, n, max,  90, 3, t ? CS2D_WEP_SG552 : CS2D_WEP_AUG, true);
            n = add_item(out, n, max, 150, 4, CS2D_WEP_SCOUT, true);
            n = add_item(out, n, max, 180, 5, CS2D_WEP_AWP, true);
            n = add_item(out, n, max, 210, 6, CS2D_WEP_G3SG1, true);
            n = add_item(out, n, max, 240, 7, CS2D_WEP_SG550, true);
            break;
        case CS2D_BUYMENU_MG:
            n = add_item(out, n, max, 30, 1, CS2D_WEP_M249, true);
            break;
        case CS2D_BUYMENU_EQUIPMENT:
            n = add_item(out, n, max,  30, 1, CS2D_WEP_KEVLAR, true);
            n = add_item(out, n, max,  60, 2, CS2D_WEP_KEVLARHELM, true);
            n = add_item(out, n, max,  90, 3, CS2D_WEP_FLASHBANG, true);
            n = add_item(out, n, max, 120, 4, CS2D_WEP_HE, true);
            n = add_item(out, n, max, 150, 5, CS2D_WEP_SMOKE, true);
            /* Defuse Kit: maps with bomb spots (DAT_140ddf078), CT only; Tactical Shield: CT only */
            n = add_item(out, n, max, 180, 6, CS2D_WEP_DEFUSEKIT, ctx->bomb_spots && ctx->team >= 2);
            n = add_item(out, n, max, 210, 7, CS2D_WEP_NIGHTVISION, true);
            n = add_item(out, n, max, 240, 8, CS2D_WEP_SHIELD, ctx->team > 1);
            n = add_item(out, n, max, 270, 9, CS2D_WEP_FLARE, true);
            break;
        default:
            return 0;
    }
    /* Section pages: Back (no key, 0032) and Close (0031) */
    n = add_btn(out, n, max, BTN_X, CS2D_HUDMEN_WIN_H - 76, -1, "Back",  0, 0, CS2D_BUY_ACT_MENU, CS2D_BUYMENU_MAIN, true);
    n = add_btn(out, n, max, BTN_X, CS2D_HUDMEN_WIN_H - 46,  0, "Close", 0, 0, CS2D_BUY_ACT_CLOSE, 0, true);
    return n;
}

void cs2d_buymenu_title(int menu, char *out, size_t n)
{
    static const char *const sections[] = { "Handgun", "Shotgun", "Sub Machine Gun", "Rifle", "Machine Gun",
                                            "Equipment" };
    if (menu > CS2D_BUYMENU_MAIN && menu <= CS2D_BUYMENU_EQUIPMENT) {
        snprintf(out, n, "Buy - %s", sections[menu - CS2D_BUYMENU_HANDGUN]);
    } else {
        snprintf(out, n, "Buy");
    }
}

int cs2d_buymenu_preview(int key, int team, int *items, int max, int *radius)
{
    static const int handgun[] = { 1, 2, 3, 4, 5, 6 }, shotgun[] = { 10, 11 };
    static const int smg_t[] = { 23, 20, 22, 24 }, smg_ct[] = { 20, 21, 22, 24 };
    static const int rifle_t[] = { 38, 30, 31, 34, 35, 36, 37 }, rifle_ct[] = { 39, 32, 33, 34, 35, 36, 37 };
    static const int mg[] = { 40 }, ammo1[] = { 61 }, ammo2[] = { 62 };
    static const int equip[] = { 57, 58, 52, 51, 56, 53, 59, 41, 54 };
    const int *src = NULL;
    int n = 0, r = 0;
    switch (key) {
        case 1: src = handgun; n = 6; r = 35; break;
        case 2: src = shotgun; n = 2; r = 30; break;
        case 3: src = team == 1 ? smg_t : smg_ct; n = 4; r = 40; break;
        case 4: src = team == 1 ? rifle_t : rifle_ct; n = 7; r = 55; break;
        case 5: src = mg; n = 1; break;
        case 6: src = ammo1; n = 1; break;
        case 7: src = ammo2; n = 1; break;
        case 8: src = equip; n = 9; r = 55; break;
        default: break;
    }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) items[i] = src[i];
    if (radius) *radius = r;
    return n;
}

/* Client stats from init_weapons (CS2D.exe 0x14023xxxx, read with a symbolic pass over the stores):
 * damage 0x1409f62a0, clip 0x140a017a8, reserve 0x140a017b0, fire delay 0x1409f2600, range 0x1409f62a8,
 * accuracy 0x1409f5d18, weight 0x1409efd18 (0 when not set), zoom damage 0x1409efd10 / 0x1409efd08.
 * Range, accuracy and weight differ from the dedicated's weapon table. */
typedef struct { uint8_t id; int16_t dmg, clip, ammo, rate, range, acc; float weight; int16_t z1, z2; } buy_stats_t;
static const buy_stats_t s_stats[] = {
    {  1, 24,  12, 100,  9, 300,  0,  0.5f,  0,   0 }, {  2, 21,  20, 120,  9, 250,  0,  0.5f,  0,   0 },
    {  3, 34,   7,  35, 13, 300,  0,  0.5f,  0,   0 }, {  4, 22,  13,  53,  9, 300,  0,  0.5f,  0,   0 },
    {  5, 22,  15, 120,  8, 300,  0,  0.5f,  0,   0 }, {  6, 18,  20, 100,  5, 300,  0,  0.5f,  0,   0 },
    { 10, 26,   8,  32, 25,  50, 10,  0.0f,  0,   0 }, { 11, 14,   7,  32,  7,  30, 13,  0.0f,  0,   0 },
    { 20, 13,  30, 120,  3, 300,  2,  0.0f,  0,   0 }, { 21,  9,  30, 120,  2, 100,  4, 0.35f,  0,   0 },
    { 22, 11,  50, 100,  2, 300,  3,  0.0f,  0,   0 }, { 23,  9,  30,  90,  2, 100,  4, 0.35f,  0,   0 },
    { 24, 15,  25,  90,  4, 300,  3,  0.0f,  0,   0 }, { 30, 22,  30,  90,  3, 300,  3,  0.0f,  0,   0 },
    { 31, 24,  30,  90,  4, 325,  0,  0.0f, 35,   0 }, { 32, 22,  30,  90,  3, 300,  2,  0.0f,  0,   0 },
    { 33, 24,  30,  90,  4, 325,  0,  0.0f, 35,   0 }, { 34, 45,  10,  60, 20, 350,  0,  0.1f, 50, 100 },
    { 35, 50,  10,  30, 65, 400,  0, -0.5f, 65, 150 }, { 36, 32,  20,  60,  6, 350,  1, -0.3f, 37,  45 },
    { 37, 30,  30,  90,  6, 350,  1, -0.3f, 35,  40 }, { 38, 13,  35,  90,  3, 300,  1,  0.0f,  0,   0 },
    { 39, 14,  25,  90,  3, 300,  1,  0.0f,  0,   0 }, { 40, 15, 100, 200,  2, 300,  8, -0.5f,  0,   0 },
};

static const buy_stats_t *find_stats(int id)
{
    for (size_t i = 0; i < sizeof(s_stats) / sizeof(s_stats[0]); i++) {
        if (s_stats[i].id == id) return &s_stats[i];
    }
    return NULL;
}

static int add_line(cs2d_buyinfo_line_t *out, int n, int max, int y, const char *label, const char *value)
{
    if (n >= max) return n;
    out[n].y = y;
    snprintf(out[n].label, sizeof(out[n].label), "%s", label);
    snprintf(out[n].value, sizeof(out[n].value), "%s", value ? value : "");
    out[n].red = false;
    return n + 1;
}

int cs2d_buymenu_info(int item, int money, cs2d_buyinfo_line_t *out, int max)
{
    const cs2d_weapon_def_t *w = cs2d_weapon_get((uint8_t)item);
    if (!w) return 0;
    char v[64], v2[64];
    int n = add_line(out, 0, max, 220, "Name:", w->name);                    /* 0199 */
    snprintf(v, sizeof(v), "%d $", w->price);
    n = add_line(out, n, max, 240, "Price:", v);                             /* 0242 */
    if (n > 0 && money < w->price) out[n - 1].red = true;                    /* (255, 0, 0) */

    const buy_stats_t *s = find_stats(item);
    int slot = cs2d_weapon_hud_slot((uint8_t)item);
    if ((slot == 1 || slot == 2) && item != CS2D_WEP_SHIELD && s) {
        snprintf(v, sizeof(v), "%d", s->dmg);
        n = add_line(out, n, max, 260, "Damage:", v);                        /* 0243 */
        snprintf(v, sizeof(v), "%d|%d", s->clip, s->ammo);
        n = add_line(out, n, max, 280, "Ammo:", v);                          /* 0244 */
        snprintf(v, sizeof(v), "%d rounds/min", s->rate > 0 ? 60000 / (s->rate * 40) : 0);
        n = add_line(out, n, max, 300, "Rate of Fire:", v);                  /* 0245, 0246 */
        const char *range = s->range < 21 ? "Very Short" : s->range < 101 ? "Short" : s->range < 301 ? "Medium" : "Far";
        snprintf(v, sizeof(v), "%s (%d)", range, s->range);
        n = add_line(out, n, max, 320, "Range:", v);                         /* 0247, 0248..0251 */
        n = add_line(out, n, max, 340, "Weight:",                            /* 0252 */
                     s->weight == 0.0f ? "Normal" : s->weight < 0.0f ? "Heavy" : "Light"); /* 0253, 0876, 0254 */
        static const char *const acc[] = { "Perfect", "Good", "Average", "Bad", "Very Bad" }; /* 0256..0260 */
        int a = s->acc == 0 ? 0 : s->acc == 1 ? 1 : s->acc < 4 ? 2 : s->acc < 6 ? 3 : 4;
        snprintf(v, sizeof(v), "%s (%d Variation)", acc[a], s->acc);
        n = add_line(out, n, max, 360, "Accuracy:", v);                      /* 0255 */
        switch (item) {
            case 1: case 32: n = add_line(out, n, max, 380, "Silencer", NULL); break;   /* 0261 */
            case 2: case 39: n = add_line(out, n, max, 380, "Burst-Mode", NULL); break; /* 0263 */
            case 10: case 11:
                snprintf(v, sizeof(v), "5 bullets per shot (damage up to %d)", s->dmg * 5); /* 0262 */
                n = add_line(out, n, max, 380, v, NULL);
                break;
            case 31: case 33:
                snprintf(v, sizeof(v), "1x Zoom (inc. damage to %d)", s->z1); /* 0264 */
                n = add_line(out, n, max, 380, v, NULL);
                break;
            case 34: case 35: case 36: case 37:
                snprintf(v2, sizeof(v2), "2x Zoom (inc. damage to %d / %d)", s->z1, s->z2); /* 0265 */
                n = add_line(out, n, max, 380, v2, NULL);
                break;
            default: break;
        }
        return n;
    }
    switch (item) {
        case CS2D_WEP_SHIELD:
            n = add_line(out, n, max, 360, "Additional protection for you and mates", NULL); /* 0275 */
            n = add_line(out, n, max, 380, "(used as primary weapon)", NULL);                /* 0276 */
            break;
        case CS2D_WEP_HE:        n = add_line(out, n, max, 380, "Causes damage", NULL); break;          /* 0269 */
        case CS2D_WEP_FLASHBANG: n = add_line(out, n, max, 380, "Blinds for some seconds", NULL); break; /* 0268 */
        case CS2D_WEP_SMOKE:     n = add_line(out, n, max, 380, "Releases smoke", NULL); break;         /* 0270 */
        case CS2D_WEP_FLARE:
            n = add_line(out, n, max, 360, "Removes fog of war", NULL);           /* 0277 */
            n = add_line(out, n, max, 380, "(in a limited area and time)", NULL); /* 0278 */
            break;
        case CS2D_WEP_DEFUSEKIT:
            n = add_line(out, n, max, 360, "Increases defuse speed", NULL);        /* 0271 */
            n = add_line(out, n, max, 380, "(used automatically when defusing)", NULL); /* 0272 */
            break;
        case CS2D_WEP_KEVLAR: case CS2D_WEP_KEVLARHELM: {
            int ap = item == CS2D_WEP_KEVLAR ? 65 : 100;
            snprintf(v, sizeof(v), "%d", ap);
            n = add_line(out, n, max, 260, "Armor:", v);                          /* 0266 */
            snprintf(v2, sizeof(v2), "Costs 10 $ per armor point. Max %d AP", ap); /* 0267 */
            n = add_line(out, n, max, 380, v2, NULL);
            break;
        }
        case CS2D_WEP_NIGHTVISION:
            n = add_line(out, n, max, 360, "Allows better sight in darkness", NULL); /* 0273 */
            n = add_line(out, n, max, 380, "(toggle with 'N')", NULL);              /* 0274 */
            break;
        default: break;
    }
    return n;
}

/* Tgui_hudmen.button runs on a click over it or on its number key (DAT_140ddf340 == key) */
static cs2d_buy_act_t apply(cs2d_buymenu_t *m, const cs2d_hudmen_btn_t *b, int *item)
{
    switch (b->act) {
        case CS2D_BUY_ACT_ITEM:  *item = b->item; break;   /* The menu stays open (FUN_1401fff70) */
        case CS2D_BUY_ACT_MENU:  m->menu = b->menu; break;
        case CS2D_BUY_ACT_CLOSE:
        case CS2D_BUY_ACT_AUTOBUY:
        case CS2D_BUY_ACT_REBUY: m->menu = 0; break;
        default: break;
    }
    return b->act;
}

cs2d_buy_act_t cs2d_buymenu_click(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, float mx, float my,
                                  int screen_w, int screen_h, int *item)
{
    cs2d_hudmen_btn_t btns[16];
    int n = cs2d_buymenu_layout(m->menu, ctx, btns, 16);
    float wx, wy;
    cs2d_hudmen_origin(screen_w, screen_h, &wx, &wy);
    *item = 0;
    for (int i = 0; i < n; i++) {
        const cs2d_hudmen_btn_t *b = &btns[i];
        if (b->enabled && inside(mx, my, wx + b->x, wy + b->y, (float)b->w, 25.0f)) return apply(m, b, item);
    }
    return CS2D_BUY_ACT_NONE;
}

cs2d_buy_act_t cs2d_buymenu_key(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, int digit, int *item)
{
    cs2d_hudmen_btn_t btns[16];
    int n = cs2d_buymenu_layout(m->menu, ctx, btns, 16);
    *item = 0;
    for (int i = 0; i < n; i++) {
        if (btns[i].enabled && btns[i].key == digit) return apply(m, &btns[i], item);
    }
    return CS2D_BUY_ACT_NONE;
}

/* drawBuySectionPreview (0x140111b10): the section's _m images on a circle around window + (430, 180),
 * a shadow at +2 (black, 0.3), turning 3 degrees per 60 Hz frame */
static void render_preview(const cs2d_buymenu_t *m, int key, int team, float wx, float wy)
{
    int items[16], radius = 0;
    int n = cs2d_buymenu_preview(key, team, items, 16, &radius);
    for (int i = 0; i < n; i++) {
        const cs2d_texture_t *t = &s_tex_m[items[i]];
        if (!t->id) continue;
        int x = (int)wx + 430, y = (int)wy + 180;
        if (n != 1) {
            double a = (double)(m->rot + (float)((360 / n) * i)) * 0.017453292519943295;
            x = (int)((double)x + cos(a) * radius);
            y = (int)((double)y + sin(a) * radius);
        }
        draw_image_mid(t, (float)x + 2.0f, (float)y + 2.0f, 1.0f, 0.0f, 0.3f);
        draw_image_mid(t, (float)x, (float)y, 1.0f, 1.0f, 1.0f);
    }
}

/* weaponinfo (0x14013bfd0): images, then the description box (275, 215) 305 x 190 */
static void render_info(int item, int money, float wx, float wy)
{
    const cs2d_texture_t *m = &s_tex_m[item], *big = m->id ? m : &s_tex_d[item];
    if (big->id) {
        if (m->id) {
            draw_image_mid(m, wx + 432.0f, wy + 182.0f, 1.0f, 0.0f, 0.3f);
            draw_image_mid(m, wx + 430.0f, wy + 180.0f, 1.0f, 1.0f, 1.0f);
        }
        /* The same image 3x larger at (430, 110), its shadow offset 3..6 px by size */
        float f = 4.0f;
        if (big->width <= 100 && big->height <= 35) f = 6.0f;
        if (big->width > 150 || big->height > 55) f = 3.0f;
        draw_image_mid(big, wx + 430.0f + f, wy + 110.0f + f, 3.0f, 0.0f, 0.3f);
        draw_image_mid(big, wx + 430.0f, wy + 110.0f, 3.0f, 1.0f, 1.0f);
    }
    const cs2d_texture_t *k = &s_tex_k[item];
    if (k->id && k->height > 0) {
        float s = fminf(32.0f / (float)k->height, 1.0f);
        draw_image_tl(k, wx + 285.0f, wy + 35.0f, s, 100.0f / 255.0f, 1.0f, 4);
    }
    cs2d_hudmen_box(wx + 275.0f, wy + 215.0f, 305.0f, 190.0f);
    cs2d_buyinfo_line_t lines[12];
    int n = cs2d_buymenu_info(item, money, lines, 12);
    float c = 150.0f / 255.0f;
    for (int i = 0; i < n; i++) {
        float r = lines[i].red ? 1.0f : c, g = lines[i].red ? 0.0f : c;
        cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 280.0f, wy + (float)lines[i].y, lines[i].label, r, g, g, 1.0f);
        if (lines[i].value[0]) {
            float tw, th;
            cs2d_render_text_measure(CS2D_FONT_REGULAR, lines[i].value, &tw, &th);
            cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 575.0f - (float)(int)tw, wy + (float)lines[i].y, lines[i].value,
                                r, g, g, 1.0f);
        }
    }
}

void cs2d_buymenu_render(cs2d_buymenu_t *m, const cs2d_buyctx_t *ctx, float mx, float my,
                         int screen_w, int screen_h)
{
    if (m->menu < CS2D_BUYMENU_MAIN || m->menu > CS2D_BUYMENU_EQUIPMENT) return;
    float wx, wy;
    cs2d_hudmen_origin(screen_w, screen_h, &wx, &wy);
    char title[48];
    cs2d_buymenu_title(m->menu, title, sizeof(title));
    cs2d_hudmen_window(wx, wy, title);

    cs2d_hudmen_btn_t btns[16];
    int n = cs2d_buymenu_layout(m->menu, ctx, btns, 16), overn = 0;
    for (int i = 0; i < n; i++) {
        const cs2d_hudmen_btn_t *b = &btns[i];
        const cs2d_weapon_def_t *w = b->item ? cs2d_weapon_get((uint8_t)b->item) : NULL;
        char price[16] = "";
        if (b->act == CS2D_BUY_ACT_ITEM && w) snprintf(price, sizeof(price), "%d $", w->price);
        const cs2d_texture_t *img = (b->img > 0 && s_tex_k[b->img].id) ? &s_tex_k[b->img] : NULL;
        const char *label = b->label ? b->label : (w ? w->name : "");
        if (cs2d_hudmen_button(mx, my, wx + b->x, wy + b->y, (float)b->w, b->key, label, price, b->enabled, img)) {
            overn = b->key; /* Tgui_hudmen.overn */
        }
    }
    if (m->menu == CS2D_BUYMENU_MAIN) {
        if (overn > 0) render_preview(m, overn, ctx->team, wx, wy);
    } else if (overn > 0) {
        for (int i = 0; i < n; i++) {
            if (btns[i].key == overn && btns[i].item) render_info(btns[i].item, ctx->money, wx, wy);
        }
    }

    /* moneyinfo (0x140108010): box (514, 30) 70 x 25, "<money> $" right aligned at 579 in (200) */
    cs2d_hudmen_box(wx + 514.0f, wy + 30.0f, 70.0f, 25.0f);
    char money[24];
    snprintf(money, sizeof(money), "%d $", ctx->money);
    float tw, th, c = 200.0f / 255.0f;
    cs2d_render_text_measure(CS2D_FONT_REGULAR, money, &tw, &th);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 579.0f - (float)(int)tw, wy + 33.0f, money, c, c, c, 1.0f);
}

/* ---------------------------------------------------------------------------
 * Lua server menu: menu 120 (0x78) (FUN_14015c320, Tgui_hudmen.update / draw)
 * --------------------------------------------------------------------------- */

/* Helper to unescape "\," -> "," and "\\)" -> "\)" if any, and "\(" -> "(" */
static void unescape_inplace(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' && (r[1] == '(' || r[1] == ')' || r[1] == '|' || r[1] == ',')) {
            r++;
        }
        *w++ = *r++;
    }
    *w = '\0';
}

void cs2d_luamenu_parse(cs2d_luamenu_t *menu, const char *raw)
{
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    if (!raw || !raw[0]) {
        menu->open = false;
        return;
    }

    /* Split raw string by unescaped ',' */
    char buf[1024];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[16];
    int ntokens = 0;
    char *p = buf;
    tokens[ntokens++] = p;

    while (*p && ntokens < 16) {
        if (*p == '\\' && p[1] != '\0') {
            p += 2; /* Skip escaped char */
            continue;
        }
        if (*p == ',') {
            *p = '\0';
            tokens[ntokens++] = p + 1;
        }
        p++;
    }

    if (ntokens < 1) {
        menu->open = false;
        return;
    }

    /* Token 0: Title */
    char title_buf[64];
    strncpy(title_buf, tokens[0], sizeof(title_buf) - 1);
    title_buf[sizeof(title_buf) - 1] = '\0';
    unescape_inplace(title_buf);

    size_t tlen = strlen(title_buf);
    int mode = 0;
    if (tlen >= 2 && title_buf[tlen - 2] == '@') {
        char tag = title_buf[tlen - 1];
        if (tag == 'b' || tag == 'B') {
            mode = 1;
            title_buf[tlen - 2] = '\0';
        } else if (tag == 'i' || tag == 'I') {
            mode = 2;
            title_buf[tlen - 2] = '\0';
        }
    }
    menu->mode = mode;
    strncpy(menu->title, title_buf, sizeof(menu->title) - 1);

    /* Items 1..9 (tokens 1..ntokens-1) */
    for (int i = 1; i < ntokens && i <= CS2D_LUAMENU_MAX_ITEMS; i++) {
        cs2d_luamenu_item_t *item = &menu->items[i - 1];
        char *tok = tokens[i];
        if (!tok || !tok[0]) continue;

        /* Check for '|' separator */
        char *bar = NULL;
        for (char *c = tok; *c; c++) {
            if (*c == '\\' && c[1] != '\0') {
                c++;
                continue;
            }
            if (*c == '|') {
                bar = c;
                break;
            }
        }

        char left_str[64] = "";
        char right_str[64] = "";
        if (bar) {
            *bar = '\0';
            strncpy(left_str, tok, sizeof(left_str) - 1);
            strncpy(right_str, bar + 1, sizeof(right_str) - 1);
        } else {
            strncpy(left_str, tok, sizeof(left_str) - 1);
        }

        /* Check disabled: starts with '(' */
        bool enabled = true;
        char *l = left_str;
        while (*l == ' ') l++; /* skip spaces if any */

        if (l[0] == '(') {
            enabled = false;
            l++;
            /* If ends with ')', strip it */
            size_t rlen = strlen(right_str);
            if (rlen > 0 && right_str[rlen - 1] == ')') {
                right_str[rlen - 1] = '\0';
            } else {
                size_t llen = strlen(l);
                if (llen > 0 && l[llen - 1] == ')') {
                    l[llen - 1] = '\0';
                }
            }
        }

        unescape_inplace(l);
        unescape_inplace(right_str);

        strncpy(item->label, l, sizeof(item->label) - 1);
        strncpy(item->right, right_str, sizeof(item->right) - 1);
        item->enabled = enabled;
    }

    menu->open = true;
}

static void get_luamenu_geometry(const cs2d_luamenu_t *menu, int screen_w, int screen_h,
                                 float *wx, float *wy, float *ww, float *wh, float *btn_w)
{
    /* Tgui_hudmen.update (0x14002ec04): every menu starts 600 x 440 (0x1b8), centred vertically; menu 120 only
     * changes x and width */
    *wh = 440.0f;
    *wy = (float)((screen_h - 440) / 2);
    if (menu->mode == 1) {
        /* Big centered */
        *ww = 600.0f;
        *wx = (float)((screen_w - 600) / 2);
        *btn_w = 568.0f; /* 0x238 */
    } else if (menu->mode == 2) {
        /* Hidden off-screen */
        *ww = 272.0f;
        *wx = 10000.0f;
        *btn_w = 240.0f; /* 0xf0 */
    } else {
        /* Default left */
        *ww = 272.0f; /* 0x110 */
        *wx = 20.0f;
        *btn_w = 240.0f; /* 0xf0 */
    }
}

void cs2d_luamenu_render(const cs2d_luamenu_t *menu, float mx, float my, int screen_w, int screen_h)
{
    if (!menu || !menu->open || menu->mode == 2) return;

    float wx, wy, ww, wh, btn_w;
    get_luamenu_geometry(menu, screen_w, screen_h, &wx, &wy, &ww, &wh, &btn_w);

    /* Window frame with custom dimensions */
    cs2d_render_rect(wx, wy, ww, wh, 58.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 228.0f / 255.0f, true);
    cs2d_render_rect(wx, wy, ww, wh, 96.0f / 255.0f, 96.0f / 255.0f, 96.0f / 255.0f, 245.0f / 255.0f, false);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 11.0f, wy + 4.0f, menu->title, 1.0f, 1.0f, 1.0f, 1.0f);

    /* Close 'X' button in top-right header */
    float close_x = wx + ww - 19.0f;
    float close_y = wy + 4.0f;
    bool close_hov = inside(mx, my, close_x, close_y, 14.0f, 14.0f);
    if (close_hov) {
        cs2d_render_rect(close_x, close_y, 14.0f, 14.0f, 110.0f / 255.0f, 45.0f / 255.0f, 45.0f / 255.0f, 220.0f / 255.0f, true);
    }
    float cross_c = close_hov ? 1.0f : (185.0f / 255.0f);
    float ins = 3.0f;
    cs2d_render_line(close_x + ins, close_y + ins, close_x + 14.0f - ins - 1.0f, close_y + 14.0f - ins - 1.0f,
                     cross_c, cross_c, cross_c, 1.0f);
    cs2d_render_line(close_x + 14.0f - ins - 1.0f, close_y + ins, close_x + ins, close_y + 14.0f - ins - 1.0f,
                     cross_c, cross_c, cross_c, 1.0f);

    /* Buttons 1..9 (y = wy + 30, step 30) */
    for (int i = 0; i < CS2D_LUAMENU_MAX_ITEMS; i++) {
        const cs2d_luamenu_item_t *item = &menu->items[i];
        if (!item->label[0]) continue;
        float by = wy + 30.0f + (float)i * 30.0f;
        cs2d_hudmen_button(mx, my, wx + 16.0f, by, btn_w, i + 1, item->label, item->right, item->enabled, NULL);
    }

    /* Button 0 (Cancel) at bottom (y = wy + wh - 46 = wy + 394) */
    float cancel_y = wy + wh - 46.0f;
    cs2d_hudmen_button(mx, my, wx + 16.0f, cancel_y, 240.0f, 0, "Cancel", "", true, NULL);
}

int cs2d_luamenu_click(cs2d_luamenu_t *menu, float mx, float my, int screen_w, int screen_h)
{
    if (!menu || !menu->open || menu->mode == 2) return -1;

    float wx, wy, ww, wh, btn_w;
    get_luamenu_geometry(menu, screen_w, screen_h, &wx, &wy, &ww, &wh, &btn_w);

    /* Check close 'X' button */
    float close_x = wx + ww - 19.0f;
    float close_y = wy + 4.0f;
    if (inside(mx, my, close_x, close_y, 14.0f, 14.0f)) {
        menu->open = false;
        return 0;
    }

    /* Check buttons 1..9 */
    for (int i = 0; i < CS2D_LUAMENU_MAX_ITEMS; i++) {
        const cs2d_luamenu_item_t *item = &menu->items[i];
        if (!item->label[0] || !item->enabled) continue;
        float by = wy + 30.0f + (float)i * 30.0f;
        if (inside(mx, my, wx + 16.0f, by, btn_w, 25.0f)) {
            menu->open = false;
            return i + 1;
        }
    }

    /* Check Cancel button (key 0) */
    float cancel_y = wy + wh - 46.0f;
    if (inside(mx, my, wx + 16.0f, cancel_y, 240.0f, 25.0f)) {
        menu->open = false;
        return 0;
    }

    return -1;
}

int cs2d_luamenu_key(cs2d_luamenu_t *menu, int digit)
{
    if (!menu || !menu->open) return -1;

    if (digit == 0) {
        menu->open = false;
        return 0;
    }
    if (digit >= 1 && digit <= CS2D_LUAMENU_MAX_ITEMS) {
        const cs2d_luamenu_item_t *item = &menu->items[digit - 1];
        if (item->label[0] && item->enabled) {
            menu->open = false;
            return digit;
        }
    }
    return -1;
}


/* ---------------------------------------------------------------------------
 * Ingame menu (menu 2) and vote screen (menu 100), Tgui_hudmen.draw 0x14021f6d0
 * --------------------------------------------------------------------------- */

static cs2d_texture_t s_tex_icons; /* gfx/gui_icons.bmp: 24 icons of 16x16, maskMode 3 (0x140a0de88) */

typedef struct {
    int           x, y, w, key; /* y < 0: from the window bottom (440 + y) */
    const char   *label;
    cs2d_gm_act_t act;
} gm_btn_t;

/* Online client layout (DAT_140ddf400 == 0). Buttons 7 / 8 (lang 0279 / 0163) need DAT_140ddf0d4 == 1 and are
 * not shown; F12 "Back to Editor" only after a test run from the editor */
static const gm_btn_t s_gm_btns[] = {
    { 16,   63, 240, 1,  "Show Briefing",     CS2D_GM_BRIEFING },     /* lang 0232 */
    { 16,   93, 240, 2,  "Change Team",       CS2D_GM_TEAM },         /* 0234 */
    { 16,  123, 240, 3,  "Leaderboard",       CS2D_GM_LEADERBOARD },
    { 16,  153, 240, 4,  "Vote",              CS2D_GM_VOTE },         /* 0235 */
    { 16,  183, 240, 5,  "Options",           CS2D_GM_OPTIONS },      /* 0014 */
    { 16, -106, 240, 9,  "Disconnect / Quit", CS2D_GM_DISCONNECT },   /* 0280, h - 0x6a */
    { 16,  -46, 240, 0,  "Close",             CS2D_GM_CLOSE },        /* 0031, h - 0x2e */
    { 275, -46, 305, -1, "Server Info",       CS2D_GM_SERVERINFO }    /* 0728, (0x113, h - 0x2e) w 0x131 */
};
#define GM_BTN_COUNT ((int)(sizeof(s_gm_btns) / sizeof(s_gm_btns[0])))

static float gm_btn_y(const gm_btn_t *b)
{
    return (float)(b->y < 0 ? CS2D_HUDMEN_WIN_H + b->y : b->y);
}

static bool gm_btn_enabled(const cs2d_gamemenu_info_t *info, const gm_btn_t *b)
{
    return b->act != CS2D_GM_LEADERBOARD || info->leaderboard;
}

void cs2d_gamemenu_players_text(int players, int bots, int max_players, char *out, size_t n)
{
    if (bots > 0) {
        snprintf(out, n, "%d/%d (%d %s)", players, max_players, bots, bots == 1 ? "Bot" : "Bots");
    } else {
        snprintf(out, n, "%d/%d", players, max_players);
    }
}

void cs2d_gamemenu_lag_text(int lagcomp, int lag_ticks, char *out, size_t n)
{
    int ms = lagcomp * 100;
    if (ms < 1) ms = 1;
    int ticks = lag_ticks > 0 ? (int)(1000.0f / (float)lag_ticks) : 0;
    snprintf(out, n, "%dms (%d ticks)", ms, ticks);
}

/* One info row: label "<text>:" at x 280 in 150, the value right-aligned on x 575 */
static void gm_row(float wx, float wy, int y, const char *label, const char *value, int r, int g, int b)
{
    char l[64];
    snprintf(l, sizeof(l), "%s:", label);
    float c = 150.0f / 255.0f;
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 280.0f, wy + (float)y, l, c, c, c, 1.0f);
    float tw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, value);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 575.0f - floorf(tw), wy + (float)y, value, r / 255.0f, g / 255.0f,
                        b / 255.0f, 1.0f);
}

/* On (0,255,0) / Off (255,0,0) (lang 0209 / 0210) */
static void gm_onoff(float wx, float wy, int y, const char *label, bool on)
{
    gm_row(wx, wy, y, label, on ? "On" : "Off", on ? 0 : 255, on ? 255 : 0, 0);
}

void cs2d_gamemenu_render(const cs2d_gamemenu_info_t *info, float mx, float my, float wx, float wy)
{
    cs2d_hudmen_window(wx, wy, "Menu"); /* lang 0231 */
    for (int i = 0; i < GM_BTN_COUNT; i++) {
        const gm_btn_t *b = &s_gm_btns[i];
        cs2d_hudmen_button(mx, my, wx + (float)b->x, wy + gm_btn_y(b), (float)b->w, b->key, b->label, NULL,
                           gm_btn_enabled(info, b), NULL);
    }

    cs2d_hudmen_box(wx + 275.0f, wy + 63.0f, 305.0f, 326.0f);
    /* Server row in the normal font at full brightness, the others in g_font_medium at 150 */
    cs2d_render_text_ex(CS2D_FONT_MENU, wx + 280.0f, wy + 63.0f, "Server:", 1.0f, 1.0f, 1.0f, 1.0f); /* 0281 */
    float tw = cs2d_render_text_width_ex(CS2D_FONT_MENU, info->server_name);
    cs2d_render_text_ex(CS2D_FONT_MENU, wx + 575.0f - floorf(tw), wy + 63.0f, info->server_name, 1.0f, 1.0f, 1.0f, 1.0f);

    static const char *os_names[] = { "Unknown", "Windows", "MacOS", "Linux" };
    char buf[64];
    gm_row(wx, wy, 83, "Address", info->address, 150, 150, 150);                                   /* 0282 */
    gm_row(wx, wy, 100, "Operating System", os_names[(info->os >= 1 && info->os <= 3) ? info->os : 0],
           150, 150, 150);                                                                         /* 0945 */
    snprintf(buf, sizeof(buf), "%d", info->ticks);
    gm_row(wx, wy, 117, "Ticks", buf, 150, 150, 150);                                              /* 0946 */
    gm_row(wx, wy, 142, "Map", info->map, 150, 255, 0);                                            /* 0200 */
    cs2d_gamemenu_players_text(info->players, info->bots, info->max_players, buf, sizeof(buf));
    bool full = info->players >= info->max_players;
    gm_row(wx, wy, 159, "Players", buf, full ? 255 : 150, 150, full ? 150 : 255);                 /* 0201 */
    /* Game mode names: the original's array (0x140a1ccf8) is filled at runtime; "DM" seen on a screenshot */
    static const char *modes[] = { "Normal", "DM", "TDM", "Construction", "Zombies" };
    gm_row(wx, wy, 176, "Game Mode", modes[(info->gamemode >= 0 && info->gamemode <= 4) ? info->gamemode : 0],
           255, 150, 150);                                                                         /* 0145 */
    gm_onoff(wx, wy, 201, "Friendly Fire", info->ff);                                              /* 0140 */
    gm_onoff(wx, wy, 218, "Force Lighting", info->forcelight);                                     /* 0890 */
    /* sv_voicechat 1..3: names from 0x140a14ed0 (runtime array; only "For All" seen on a screenshot) */
    static const char *voice[] = { "Off", "For All", "Team", "Team" };
    if (info->voicechat >= 1 && info->voicechat <= 3) gm_row(wx, wy, 235, "Voice Chat", voice[info->voicechat], 0, 255, 0);
    else gm_row(wx, wy, 235, "Voice Chat", "Off", 255, 0, 0);                                      /* 0930 */
    /* sv_fow: lang 0883 "Off|Hide characters only|Hide characters and effects|Hide everything" */
    static const char *fow[] = { "Off", "Hide characters only", "Hide characters and effects", "Hide everything" };
    if (info->fow >= 1 && info->fow <= 3) gm_row(wx, wy, 252, "Fog of War", fow[info->fow], 0, 255, 0);
    else gm_row(wx, wy, 252, "Fog of War", "Off", 255, 0, 0);                                      /* 0139 */
    gm_onoff(wx, wy, 269, "Recoil influences accuracy", info->recoil);                             /* 0900 */
    gm_onoff(wx, wy, 286, "Off-Screen Damage", info->offscreen);                                   /* 0959 */
    gm_onoff(wx, wy, 303, "Physics (experimental)", info->physics);                                /* 0961 */
    gm_onoff(wx, wy, 320, "Lua Scripts", info->lua);                                               /* 0947 */
    cs2d_gamemenu_lag_text(info->lagcomp, info->lag_ticks, buf, sizeof(buf));
    gm_row(wx, wy, 337, "Lag Compensation", buf, 0, 255, 0);                                       /* 0943 */

    /* Favorite row at y 0x53 + 0x117 (online only): text 150 (200 hovered), star icon at x 0x22f */
    float fy = wy + 362.0f;
    bool hot = mx >= wx + 280.0f && mx <= wx + 575.0f && my >= fy && my < fy + 20.0f;
    float c = (hot ? 200.0f : 150.0f) / 255.0f;
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 280.0f, fy, "Favorite:", c, c, c, 1.0f);           /* 0283 */
    const char *ft = info->favorite ? "Favorite! (click to remove)" : "No favorite (click to add)"; /* 0285 / 0284 */
    tw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, ft);
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 557.0f - floorf(tw), fy, ft, c, c, c, 1.0f);
    if (!s_tex_icons.id) cs2d_texture_load_grid(&s_tex_icons, "gfx/gui_icons.bmp", 3, 16, 16);
    if (s_tex_icons.id) {
        /* Favorite: SetColor(255, 255, 0); else the text color */
        float sc = info->favorite ? 1.0f : c, sb = info->favorite ? 0.0f : c;
        cs2d_render_sprite_frame(&s_tex_icons, 16, 16, 19, wx + 567.0f, fy + 8.0f, 16.0f, 16.0f, 0.0f,
                                 sc, sc, sb, 1.0f, 0);
    }
}

cs2d_gm_act_t cs2d_gamemenu_click(const cs2d_gamemenu_info_t *info, float mx, float my, float wx, float wy)
{
    for (int i = 0; i < GM_BTN_COUNT; i++) {
        const gm_btn_t *b = &s_gm_btns[i];
        if (gm_btn_enabled(info, b) && inside(mx, my, wx + (float)b->x, wy + gm_btn_y(b), (float)b->w, 25.0f)) {
            return b->act;
        }
    }
    if (mx >= wx + 280.0f && mx <= wx + 575.0f && my >= wy + 362.0f && my < wy + 382.0f) return CS2D_GM_FAVORITE;
    return CS2D_GM_NONE;
}

cs2d_gm_act_t cs2d_gamemenu_key(const cs2d_gamemenu_info_t *info, int digit)
{
    for (int i = 0; i < GM_BTN_COUNT; i++) {
        const gm_btn_t *b = &s_gm_btns[i];
        if (b->key == digit && gm_btn_enabled(info, b)) return b->act;
    }
    return CS2D_GM_NONE;
}

int cs2d_vote_brightness(int votes)
{
    if (votes < 1) return 125;
    if (votes == 1) return 200;
    if (votes == 2) return 220;
    if (votes == 3) return 240;
    return 255;
}

void cs2d_vote_list_sort(cs2d_vote_list_t *l)
{
    /* Insertion sort on (votes desc, label asc): the BlitzMax list sorts "99 - votes" (2 digits) + label */
    for (int i = 1; i < l->n; i++) {
        cs2d_vote_entry_t t = l->e[i];
        int j = i - 1;
        while (j >= 0 && (l->e[j].votes < t.votes || (l->e[j].votes == t.votes && strcmp(l->e[j].label, t.label) > 0))) {
            l->e[j + 1] = l->e[j];
            j--;
        }
        l->e[j + 1] = t;
    }
}

/* Tgui_slist (16,50) / (305,50), 279 x 330 (0x1401a6800); rows 16 px (calibrated on a screenshot) */
#define VOTE_LIST_Y   50.0f
#define VOTE_LIST_W  279.0f
#define VOTE_LIST_H  330.0f
#define VOTE_ROW_H    16.0f

static int vote_rows(void)
{
    return (int)((VOTE_LIST_H - 4.0f) / VOTE_ROW_H);
}

/* Count at +5 in getVoteBrightness, name at +30 (players 150, maps by votes, the current map (150,255,0)),
 * '<' 5 px from the right in yellow */
static void vote_list_render(const cs2d_vote_list_t *l, bool players, float x, float y, float mx, float my)
{
    cs2d_hudmen_box(x, y, VOTE_LIST_W - 16.0f, VOTE_LIST_H);
    cs2d_hudmen_box(x + VOTE_LIST_W - 16.0f, y, 16.0f, VOTE_LIST_H); /* scrollbar track */
    for (int i = 0; i < l->n && i < vote_rows(); i++) {
        const cs2d_vote_entry_t *e = &l->e[i];
        float ry = y + 2.0f + VOTE_ROW_H * (float)i;
        if (inside(mx, my, x, ry, VOTE_LIST_W - 16.0f, VOTE_ROW_H)) {
            cs2d_render_rect(x + 1.0f, ry, VOTE_LIST_W - 18.0f, VOTE_ROW_H, 1.0f, 1.0f, 1.0f, 0.1f, true);
        }
        char cnt[12];
        snprintf(cnt, sizeof(cnt), "%d", e->votes);
        float b = (float)cs2d_vote_brightness(e->votes) / 255.0f;
        cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 5.0f, ry, cnt, b, b, b, 1.0f);
        if (players) {
            float c = 150.0f / 255.0f;
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 30.0f, ry, e->label, c, c, c, 1.0f);
        } else if (e->current) {
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 30.0f, ry, e->label, 150.0f / 255.0f, 1.0f, 0.0f, 1.0f);
        } else {
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x + 30.0f, ry, e->label, b, b, b, 1.0f);
        }
        if (e->mark) {
            float mw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, "<");
            cs2d_render_text_ex(CS2D_FONT_REGULAR, x + VOTE_LIST_W - 21.0f - mw, ry, "<", 1.0f, 1.0f, 0.0f, 1.0f);
        }
    }
}

void cs2d_votemenu_render(const cs2d_vote_list_t *kick, const cs2d_vote_list_t *maps, float mx, float my,
                          float wx, float wy)
{
    cs2d_hudmen_window(wx, wy, "Vote"); /* lang 0235 */
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 16.0f, wy + 30.0f, "Vote to kick a Player", 1.0f, 1.0f, 1.0f, 1.0f); /* 0236 */
    cs2d_render_text_ex(CS2D_FONT_REGULAR, wx + 26.0f + VOTE_LIST_W, wy + 30.0f, "Vote a Map", 1.0f, 1.0f, 1.0f, 1.0f); /* 0239 */
    vote_list_render(kick, true, wx + 16.0f, wy + VOTE_LIST_Y, mx, my);
    vote_list_render(maps, false, wx + 305.0f, wy + VOTE_LIST_Y, mx, my);
    cs2d_hudmen_button(mx, my, wx + 16.0f, wy + CS2D_HUDMEN_WIN_H - 46.0f, 240.0f, 0, "Close", NULL, true, NULL);
}

static int vote_list_hit(const cs2d_vote_list_t *l, float x, float y, float mx, float my)
{
    for (int i = 0; i < l->n && i < vote_rows(); i++) {
        if (inside(mx, my, x, y + 2.0f + VOTE_ROW_H * (float)i, VOTE_LIST_W - 16.0f, VOTE_ROW_H)) return i;
    }
    return -1;
}

int cs2d_votemenu_click(const cs2d_vote_list_t *kick, const cs2d_vote_list_t *maps, float mx, float my,
                        float wx, float wy, int *value)
{
    int i = vote_list_hit(kick, wx + 16.0f, wy + VOTE_LIST_Y, mx, my);
    if (i >= 0) {
        *value = kick->e[i].value;
        return 1;
    }
    i = vote_list_hit(maps, wx + 305.0f, wy + VOTE_LIST_Y, mx, my);
    if (i >= 0) {
        *value = maps->e[i].value;
        return 2;
    }
    if (inside(mx, my, wx + 16.0f, wy + CS2D_HUDMEN_WIN_H - 46.0f, 240.0f, 25.0f)) return 0;
    return -1;
}

void cs2d_format_playtime(int s, char *out, size_t n)
{
    if (s < 60) {
        snprintf(out, n, "%d s", s);
    } else if (s < 3600) {
        snprintf(out, n, "%d m %d s", s / 60, s % 60);
    } else if (s < 86400) {
        snprintf(out, n, "%d h %d m", s / 3600, s / 60 - (s / 3600) * 60);
    } else {
        snprintf(out, n, "%d d %d h", s / 86400, s / 3600 - (s / 86400) * 24);
    }
}

void cs2d_format_timestamp(int32_t t, char *out, size_t n)
{
    /* FUN_1402e2ef0: days since 1970 to a civil date (UTC), then hours and minutes */
    int days = t / 86400, rem = t % 86400;
    int z = days + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp < 10 ? mp + 3 : mp - 9;
    int y = yoe + era * 400 + (m <= 2);
    snprintf(out, n, "%04d/%02d/%02d %02d:%02d", y, m, d, rem / 3600, (rem / 60) % 60);
}
