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
