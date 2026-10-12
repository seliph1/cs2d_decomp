/*
 * Scoreboard (Thud_scoreboard in CS2D.exe): window, team headers, columns, player rows and the spectator list.
 *
 * Settings are the shipped defaults: pings, assists, flags, MVPs, deaths, scores and avatars on (0x140a14fd0..
 * 0x140a14fe8 = 1); money, K / D, shortcuts, ids, ranks and OS off (0x140dde208, 0x140ddf1d8..0x140ddf1ec in .bss);
 * no voice mute. Strings are sys/language entries (g_lang[N] + k = entry (N * 36 + k + 28) / 8).
 */
#include "cs2d_scoreboard.h"
#include "cs2d_render.h"
#include <stdio.h>
#include <string.h>

#define SB_FONT       CS2D_FONT_MENU  /* g_font_normal, 15 px */
#define SB_FONT_SMALL CS2D_FONT_SMALL /* PTR_bbNullObject_140a1ccb8 (HUD small font); assumed */

static cs2d_texture_t s_tex_gui_c;     /* gfx/gui_c.bmp: 4 corners of 16x16, maskMode 3 (0x140a0dea0) */
static cs2d_texture_t s_tex_avatar;    /* incbindata/avatar.png: player without avatar (0x140a0dfc0) */
static cs2d_texture_t s_tex_icons;     /* gfx/gui_icons.bmp: 24 icons of 16x16, maskMode 3 (0x140a0de88) */
static cs2d_texture_t s_tex_icons_new; /* incbindata/gui_icons_new.bmp: 24 icons, mask (255,0,255) (0x140a0dee8) */

/* Current Max2D color, like SetColor / SetAlpha: drawTxt and drawPlayer read it back with GetColor */
static int   s_r = 255, s_g = 255, s_b = 255;
static float s_a = 1.0f;

static void set_color(int r, int g, int b) { s_r = r; s_g = g; s_b = b; }

static int clamp255(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (int)v); }

static void fill_rect(float x, float y, float w, float h)
{
    cs2d_render_rect(x, y, w, h, s_r / 255.0f, s_g / 255.0f, s_b / 255.0f, s_a, true);
}

/* Thud_scoreboard.drawTxt 0x140106510: black shadow at +1, squeezed horizontally to maxw; returns the x scale */
static float draw_txt(cs2d_font_id_t f, const char *text, int x, int y, int maxw, float sx)
{
    float w = cs2d_render_text_width_ex(f, text);
    float k = (w <= (float)maxw) ? 1.0f : (float)maxw / w;
    float scale = sx * k;
    cs2d_render_text_scaled_ex(f, (float)(x + 1), (float)(y + 1), text, scale, 0, 0, 0, s_a);
    cs2d_render_text_scaled_ex(f, (float)x, (float)y, text, scale, s_r / 255.0f, s_g / 255.0f, s_b / 255.0f, s_a);
    return k;
}

static int text_w(cs2d_font_id_t f, const char *text)
{
    return (int)cs2d_render_text_width_ex(f, text);
}

static bool mouse_in(const cs2d_scoreboard_view_t *v, float x, float y, float w, float h)
{
    return v->mouse_x >= x && v->mouse_y >= y && v->mouse_x <= x + w && v->mouse_y <= y + h;
}

/* Image drawn with its top-left corner at (x, y), size s x s */
static void draw_icon(const cs2d_texture_t *tex, int cell, int frame, float x, float y, float s, int blend)
{
    if (!tex || !tex->id) return;
    cs2d_render_sprite_frame(tex, cell, cell, frame, x + s * 0.5f, y + s * 0.5f, s, s, 0.0f,
                             s_r / 255.0f, s_g / 255.0f, s_b / 255.0f, s_a, blend);
}

/*
 * Thud_scoreboard.draw 0x140251220 + calculatePossibleSlotCount 0x14005d480. The HUD font is 13 px (below 15), so
 * the sizes use the 15 px font with scale 1.1538461 and a 12 px small font. Window: when the screen width is in the
 * resolution table (widths 0x140bd7860, heights 0x140bd78a0, built at 0x1403086f9) at index i > 1, the scoreboard
 * keeps the size of entry 1 (i = 2, 3), of 0x140bc8a40[i - 4] (i = 4..11) or of i, minus 20, centered when it fits;
 * otherwise (and with the size setting 0x140ddf1e0 = 0) it fills the screen minus 10 px on every side.
 */
void cs2d_scoreboard_layout(int screen_w, int screen_h, cs2d_scoreboard_layout_t *L)
{
    static const int res_w[14] = { 640, 850, 800, 1060, 1024, 1280, 1280, 1360, 1366, 1440, 1600, 1920, 2560, 3840 };
    static const int res_h[14] = { 480, 480, 600, 600, 768, 720, 960, 768, 768, 900, 900, 1080, 1440, 2160 };
    static const int remap[8]  = { 3, 3, 3, 3, 3, 3, 9, 10 };

    memset(L, 0, sizeof(*L));
    L->font_h = 15;
    L->font_h_small = 12;
    L->scale = 1.1538461f;
    float fh = 15.0f;
    L->value_w  = (int)(L->scale * 42.0f);
    L->team_h   = (int)(L->scale * 10.0f + fh);
    L->team_pad = (int)(L->scale + L->scale);
    L->slot_h   = (int)(L->scale * 6.0f + fh);
    L->icon     = (float)(L->slot_h - 4);

    L->rw = screen_w - 20; L->rh = screen_h - 20; L->rx = 10; L->ry = 10;
    for (int i = 2; i < 14; i++) {
        if (res_w[i] != screen_w) continue;
        int e = (i < 4) ? 1 : (i < 12 ? remap[i - 4] : i);
        if (screen_w >= res_w[e]) { L->rw = res_w[e] - 20; L->rx = (screen_w - L->rw) >> 1; }
        if (screen_h >= res_h[e]) { L->rh = res_h[e] - 20; L->ry = (screen_h - L->rh) >> 1; }
        break;
    }

    float rows = (float)((L->rh - (int)((float)L->team_h + (float)L->team_pad + L->scale * 8.0f + fh +
                                        (float)L->font_h_small + 7.0f)) -
                         (int)(L->scale + L->scale + (float)L->font_h_small + 5.0f)) /
                 (L->scale * 6.0f + fh);
    int n = (int)rows;
    L->slots  = ((float)n < rows) ? n + 1 : n;
    L->half_w = (L->rw - 32) / 2 - 4; /* centerPad 4 */
}

int cs2d_scoreboard_sortval(const cs2d_player_t *p)
{
    return p->score * 500000 + p->assists * 5000 + p->mvp * 500 - p->deaths;
}

/* drawWithTeams / drawWithoutTeams: repeatedly take the untaken player with the highest sortval (strictly higher
 * replaces, so ties keep the Tpl.list order; ours is the id order) */
int cs2d_scoreboard_order(const cs2d_player_t *players, int *out_ids)
{
    bool taken[CS2D_MAX_PLAYERS + 1] = { false };
    int n = 0;
    for (;;) {
        int best = 0;
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            const cs2d_player_t *p = &players[i];
            if (p->id == 0 || p->team <= CS2D_TEAM_SPEC || taken[i]) continue;
            if (best == 0 || cs2d_scoreboard_sortval(&players[best]) < cs2d_scoreboard_sortval(p)) best = i;
        }
        if (best == 0) return n;
        taken[best] = true;
        out_ids[n++] = best;
    }
}

/* Tpl.updateAll: scoreIncTime / assistsIncTime += time factor every frame; deadTime counts while dead, 0 alive */
void cs2d_scoreboard_tick(cs2d_player_t *players, float delta_time)
{
    float f = delta_time * 60.0f;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        cs2d_player_t *p = &players[i];
        if (p->id == 0) continue;
        p->score_inc_time   += f;
        p->assists_inc_time += f;
        if (p->alive && p->health > 0) p->dead_time = 0.0f;
        else p->dead_time += f;
    }
}

typedef struct {
    const cs2d_scoreboard_view_t   *v;
    const cs2d_scoreboard_layout_t *L;
    int  yp;
    int  t, ct, spec;
    int  hover;          /* hoverPl: first row under the mouse */
    int  screen_w;
} sb_ctx_t;

/* drawBase 0x140250170: black 0.55 window with round corners, server name, ticks and map name */
static void draw_base(sb_ctx_t *c)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    const cs2d_scoreboard_view_t *v = c->v;
    if (!s_tex_gui_c.id) cs2d_texture_load_grid(&s_tex_gui_c, "gfx/gui_c.bmp", 3, 16, 16);
    set_color(0, 0, 0);
    s_a = 0.55f;
    draw_icon(&s_tex_gui_c, 16, 0, (float)L->rx, (float)L->ry, 16, 0);
    draw_icon(&s_tex_gui_c, 16, 1, (float)(L->rx + L->rw - 16), (float)L->ry, 16, 0);
    fill_rect((float)(L->rx + 16), (float)L->ry, (float)(L->rw - 32), 16.0f);
    fill_rect((float)L->rx, (float)(L->ry + 16), (float)L->rw, (float)(L->rh - 32));
    draw_icon(&s_tex_gui_c, 16, 2, (float)L->rx, (float)(L->ry + L->rh - 16), 16, 0);
    draw_icon(&s_tex_gui_c, 16, 3, (float)(L->rx + L->rw - 16), (float)(L->ry + L->rh - 16), 16, 0);
    fill_rect((float)(L->rx + 16), (float)(L->ry + L->rh - 16), (float)(L->rw - 32), 16.0f);

    c->yp += 5;
    s_a = 1.0f;
    set_color(150, 255, 0);
    const char *map = v->map_name ? v->map_name : "";
    draw_txt(SB_FONT, map, L->rx + L->rw - 16 - text_w(SB_FONT, map), c->yp, 100000, 1.0f);
    set_color(255, 220, 0);
    const char *srv = v->server_name ? v->server_name : "";
    draw_txt(SB_FONT, srv, L->rx + 16, c->yp, L->rw - 32, 1.0f);
    /* Server ticks of [247] (DAT_140ddf3e0): red below 20, yellow below 30, else gray. The "Ticks" (0946) tooltip
     * on hover is not drawn. */
    if (v->ticks >= 0) {
        if (v->ticks < 20) set_color(255, 0, 0);
        else if (v->ticks < 30) set_color(255, 255, 0);
        else set_color(180, 180, 180);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", v->ticks);
        draw_txt(SB_FONT_SMALL, buf, text_w(SB_FONT, srv) + 21 + L->rx, c->yp + 2, L->rw - 32, 1.0f);
    }
    c->yp = (int)((float)c->yp + (float)L->font_h + L->scale * 8.0f);
}

/* drawTeamHeader 0x140124cd0: a 0.2 bar in the team color; name, team score and "n Players" in white */
static void draw_team_header(sb_ctx_t *c, int x, int y, int w, const char *name, int count, const char *score)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    s_a = 0.2f;
    fill_rect((float)x, (float)y, (float)w, (float)L->team_h);
    s_a = 1.0f;
    set_color(255, 255, 255);
    int pad = (L->team_h - L->font_h) / 2;
    float nw = (float)text_w(SB_FONT, name), lim = (float)(w - 4) * 0.75f;
    float sx = 1.0f;
    if (nw > lim) { sx = lim / nw; nw *= sx; }
    draw_txt(SB_FONT, name, x + 4, y + pad, 100000, sx);
    draw_txt(SB_FONT, score, x + w - text_w(SB_FONT, score) - 4, y + pad, 100000, 1.0f);
    s_a = 0.7f;
    char buf[48];
    if (count == 1) snprintf(buf, sizeof(buf), "1 Player");            /* "1 " + 0036 */
    else snprintf(buf, sizeof(buf), "%d Players", count);              /* n + " " + 0201 */
    draw_txt(SB_FONT_SMALL, buf, (int)(L->scale * 5.0f + (float)(x + 4) + nw),
             y + L->team_h - L->font_h_small - pad, 100000, 1.0f);
    s_a = 1.0f;
}

/* drawTeamColumns 0x140124ff0: a 1 px line under the titles; values from the right: Latency (0202), Deaths
 * (0750), Assists (0929), Score (0748), each value_w wide + 2; Player (0036) on the left */
static void draw_team_columns(sb_ctx_t *c, int x, int y, int w)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    static const char *const titles[4] = { "Latency", "Deaths", "Assists", "Score" };
    s_a = 1.0f;
    fill_rect((float)x, (float)(L->font_h_small + y + 1), (float)w, 1.0f);
    if (c->screen_w < 700) y -= 2;
    for (int i = 0; i < 4; i++) {
        w -= L->value_w + 2;
        draw_txt(SB_FONT_SMALL, titles[i], x + w, y, L->value_w, 1.0f);
    }
    draw_txt(SB_FONT_SMALL, "Player", x, y, w, 1.0f);
}

/* Value color flash: from white (or green / yellow) back to the row color over 97.5 frames */
static void flash_color(sb_ctx_t *c, int r, int g, int b, float t)
{
    float f = t * 0.010256411f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float w = (1.0f - f) * 255.0f;
    /* team game: (r f, g f + w, b f), green; without teams also r + w, yellow */
    set_color(clamp255((float)r * f + (c->v->teams ? 0.0f : w)), clamp255((float)g * f + w), clamp255((float)b * f));
}

/* drawPlayer 0x14012e4b0 */
static void draw_player(sb_ctx_t *c, const cs2d_player_t *p, int x, int y, int w, int h, int odd)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    const cs2d_scoreboard_view_t *v = c->v;
    int r = s_r, g = s_g, b = s_b;
    bool dead = p->health < 1 || !p->alive;
    bool local = p->id == v->local_id;

    s_a = odd ? 0.1f : 0.03f;
    if (local) {
        r = r + 50 > 255 ? 255 : r + 50;
        g = g + 50 > 255 ? 255 : g + 50;
        b = b + 50 > 255 ? 255 : b + 50;
        set_color(r, g, b);
        s_a = 0.3f;
    }
    fill_rect((float)x, (float)y, (float)w, (float)h);

    int name_w = (int)((float)(w - 2) + (-2.0f - L->icon)) - 4 * (L->value_w + 2); /* 4 value columns */
    int x2 = x + 2;
    /* Hovered row: 0.35 underline under the name (gray when dead) */
    if (c->hover == 0 && mouse_in(v, (float)x, (float)y, (float)w, (float)h)) {
        c->hover = p->id;
        s_a = 0.35f;
        int ul_w = name_w - h, ul_x = x2 + h; /* avatars */
        int tw = text_w(SB_FONT, p->name);
        if (tw <= ul_w) ul_w = tw;
        if (dead) set_color(110, 110, 110);
        else set_color(r, g, b);
        fill_rect((float)ul_x, (float)(L->font_h + y + 2), (float)ul_w, 1.0f);
    }
    int ty = y + 2, isz = h - 4, off = 0;

    /* Avatar (Tpl.drawAvatarRect): incbindata/avatar.png without a Steam / U.S.G.N. avatar */
    if (!s_tex_avatar.id) cs2d_texture_load_ex(&s_tex_avatar, "incbindata/avatar.png", 0);
    float a0 = s_a;
    s_a = 1.0f;
    set_color(255, 255, 255);
    if (s_tex_avatar.id) cs2d_render_sprite(&s_tex_avatar, (float)x2 + isz * 0.5f, (float)ty + isz * 0.5f,
                                            (float)isz, (float)isz, 0.0f, 1, 1, 1, 1);
    set_color(r, g, b);
    s_a = a0;
    off = h;
    /* Country flags (Tpl.drawFlag) need a U.S.G.N. / Steam country: never drawn */

    /* Status icon right of the name column (hud_symbols at icon size, additive) */
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    const cs2d_texture_t *sym = bank ? &bank->tex_hud_symbols : NULL;
    int sym_cell = (sym && sym->height >= 64) ? 64 : 32;
    int ix = (int)((float)(name_w + x) + L->icon + 2.0f);
    float icon_x = -2.0f - L->icon + (float)ix;
    const cs2d_player_t *me = (v->local_id >= 1 && v->local_id <= CS2D_MAX_PLAYERS) ? &v->players[v->local_id] : NULL;
    s_a = 1.0f;
    if (dead) {
        /* Frame 12, yellow fading to (200,200,200) over 130 frames after death */
        if (p->dead_time >= 130.0f) {
            set_color(200, 200, 200);
        } else {
            float f = p->dead_time * 0.0076923077f;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            int c2 = clamp255((1.0f - f) * 255.0f + f * 200.0f);
            set_color(c2, c2, clamp255(f * 200.0f));
        }
        draw_icon(sym, sym_cell, 12, icon_x, (float)(y + 3), L->icon, 4);
    } else {
        bool has_bomb = false, has_flag = false;
        for (int i = 0; i < CS2D_SLOT_COUNT; i++) {
            if (p->inventory[i] == CS2D_WEP_BOMB) has_bomb = true;
            if (p->inventory[i] == CS2D_WEP_REDFLAG || p->inventory[i] == CS2D_WEP_BLUEFLAG) has_flag = true;
        }
        if (p->team == CS2D_TEAM_VIP) {
            /* The VIP, seen by CTs and the VIP */
            if (me && me->team >= CS2D_TEAM_CT) { set_color(255, 255, 0); draw_icon(sym, sym_cell, 1, icon_x, (float)(y + 1), L->icon, 4); }
        } else if (has_bomb) {
            if (me && me->team == CS2D_TEAM_T) { set_color(255, 255, 0); draw_icon(sym, sym_cell, 6, icon_x, (float)(y + 3), L->icon, 4); }
        } else if (has_flag) {
            /* gfx/hud_radar_icons.png frame 2, midhandled (0x140a0de00) */
            set_color(255, 255, 0);
            float hs = L->icon * 0.5f;
            if (bank) draw_icon(&bank->tex_hud_radar_icons, 32, 2, ((float)ix - 2.0f) + (hs - L->icon) - hs,
                                (float)(y + 3) + hs - hs, L->icon, 4);
        } else if (p->has_defuse_kit && me && me->team >= CS2D_TEAM_CT) {
            set_color(r, g, b);
            draw_icon(sym, sym_cell, 3, icon_x, (float)(y + 3), L->icon, 4);
        }
    }

    /* Name: gray (110) at 0.8 when dead */
    if (dead) { r = g = b = 110; s_a = 0.8f; }
    else s_a = 1.0f;
    set_color(r, g, b);
    int reserve = 2;
    if (p->mvp > 0) reserve = (int)((float)isz * 1.6f);
    bool local_dead = !me || me->health < 1 || !me->alive;
    if (local_dead && p->id == v->spec_target) reserve += h - 8;
    int nx = x2 + off + 2;
    float nsx = draw_txt(SB_FONT, p->name, nx, ty, (name_w - (off + 2)) - reserve, 1.0f);

    char buf[32];
    char name_sp[CS2D_MAX_NAME_LEN + 2];
    snprintf(name_sp, sizeof(name_sp), "%s ", p->name);
    int nspw = text_w(SB_FONT, name_sp);
    float mvp_w = 0.0f;
    if (p->mvp > 0) {
        /* MVP star: gui_icons frame 19 with the count, shadow black 0.45 at +1 */
        if (!s_tex_icons.id) cs2d_texture_load_grid(&s_tex_icons, "gfx/gui_icons.bmp", 3, 16, 16);
        float ssz = (float)(h - 8);
        int sx = (int)((float)nspw * nsx) + nx;
        snprintf(buf, sizeof(buf), "%d", p->mvp);
        set_color(0, 0, 0);
        s_a = 0.45f;
        draw_icon(&s_tex_icons, 16, 19, (float)(off + 5 + x + (int)((float)nspw * nsx)), (float)(y + 5), ssz, 0);
        cs2d_render_text_ex(SB_FONT_SMALL, (float)(h + sx - 7), (float)(y + 3), buf, 0, 0, 0, s_a);
        s_a = 1.0f;
        set_color(255, 200, 0);
        draw_icon(&s_tex_icons, 16, 19, (float)sx, (float)(y + 4), ssz, 0);
        cs2d_render_text_ex(SB_FONT_SMALL, (float)(h + sx - 8), (float)ty, buf, 1.0f, 200 / 255.0f, 0, 1.0f);
        set_color(r, g, b);
        mvp_w = (float)h;
    }
    /* The followed player while the local one is dead: an eye (incbindata/gui_icons_new.bmp frame 10) */
    if (local_dead && p->id == v->spec_target && v->spec_mode == 2) {
        if (!s_tex_icons_new.id) cs2d_texture_load_grid(&s_tex_icons_new, "incbindata/gui_icons_new.bmp", 2, 16, 16);
        set_color(200, 200, 200);
        draw_icon(&s_tex_icons_new, 16, 10, (float)((int)((float)nspw * nsx + mvp_w) + off + 5 + x), (float)(y + 5),
                  (float)(h - 8), 0);
        set_color(r, g, b);
    }

    /* Latency: offline the local player is "Server" (0281) and bots "Bot" (0229); online 0xffff / 0xfffe */
    w -= L->value_w + 2;
    const char *ptxt = NULL;
    bool numeric = false;
    if (!v->host) {
        if (p->ping < 30001) numeric = true;
        else if (p->ping == 0xffff) ptxt = "Server";
        else if (p->ping == 0xfffe) ptxt = "Bot";
    } else if (local) {
        ptxt = "Server";
    } else if (p->bot) {
        ptxt = "Bot";
    } else {
        numeric = true;
    }
    if (numeric) {
        int pr = 255, pg = 0;
        if ((float)p->ping <= 499.99997f) {
            float f = (float)p->ping * 0.002f;
            pr = (int)(f * 510.0f); if (pr > 255) pr = 255;
            pg = (int)((1.0f - f) * 510.0f); if (pg > 255) pg = 255;
        }
        set_color(pr, pg, 0);
        snprintf(buf, sizeof(buf), "%d", p->ping);
        draw_txt(SB_FONT, buf, x + w, ty, L->value_w, 1.0f);
    } else if (ptxt) {
        draw_txt(SB_FONT, ptxt, x + w, ty, L->value_w, 1.0f);
    }
    set_color(r, g, b);

    /* Deaths: white fading back to the row color for 130 frames after death */
    w -= L->value_w + 2;
    if (dead && p->dead_time < 130.0f) {
        float f = p->dead_time * 0.0076923077f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        float wv = (1.0f - f) * 255.0f;
        set_color(clamp255((float)r * f + wv), clamp255((float)g * f + wv), clamp255((float)b * f));
    }
    snprintf(buf, sizeof(buf), "%d", p->deaths);
    draw_txt(SB_FONT, buf, x + w, ty, L->value_w, 1.0f);
    set_color(r, g, b);

    /* Assists and score flash for 97.5 frames after a change */
    w -= L->value_w + 2;
    if (p->assists_inc_time < 97.5f) flash_color(c, r, g, b, p->assists_inc_time);
    snprintf(buf, sizeof(buf), "%d", p->assists);
    draw_txt(SB_FONT, buf, x + w, ty, L->value_w, 1.0f);
    set_color(r, g, b);

    if (p->score_inc_time < 97.5f) flash_color(c, r, g, b, p->score_inc_time);
    snprintf(buf, sizeof(buf), "%d", p->score);
    draw_txt(SB_FONT, buf, x - L->value_w - 2 + w, ty, L->value_w, 1.0f);
    set_color(r, g, b);
}

/* drawWithTeams 0x140130450: CTs on the left, Terrorists on the right; a column longer than the window continues
 * at the bottom of the other one */
static void draw_with_teams(sb_ctx_t *c, const int *order, int n)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    const cs2d_scoreboard_view_t *v = c->v;
    int lx = L->rx + 16, rx = L->rx + L->rw - 16 - L->half_w;
    char sc[16];

    set_color(50, 150, 255);
    snprintf(sc, sizeof(sc), "%d", v->score_ct);
    draw_team_header(c, lx, c->yp, L->half_w, v->zombies ? "Survivors" : "Counter-Terrorist Forces", c->ct, sc);
    set_color(50, 150, 255);
    draw_team_columns(c, lx, L->team_h + c->yp + L->team_pad, L->half_w);

    int tr = v->zombies ? 0 : 255, tg = v->zombies ? 255 : 25;
    set_color(tr, tg, 0);
    snprintf(sc, sizeof(sc), "%d", v->score_t);
    draw_team_header(c, rx, c->yp, L->half_w, v->zombies ? "Zombies" : "Terrorist Forces", c->t, sc);
    set_color(tr, tg, 0);
    draw_team_columns(c, rx, L->team_h + c->yp + L->team_pad, L->half_w);
    c->yp = L->team_pad + L->team_h + c->yp + 2 + L->font_h_small;

    int it = 0, ict = 0;
    for (int k = 0; k < n; k++) {
        const cs2d_player_t *p = &v->players[order[k]];
        if (p->team == CS2D_TEAM_T) {
            set_color(tr, tg, 0);
            if (it < L->slots) {
                draw_player(c, p, rx, L->slot_h * it + c->yp, L->half_w, L->slot_h, it & 1);
            } else {
                int row = (L->slots - c->t) + it;
                draw_player(c, p, lx, row * L->slot_h + c->yp, L->half_w, L->slot_h, ~row & 1);
            }
            it++;
        } else {
            set_color(50, 150, 255);
            if (ict < L->slots) {
                draw_player(c, p, lx, ict * L->slot_h + c->yp, L->half_w, L->slot_h, ~ict & 1);
            } else {
                int row = (L->slots - c->ct) + ict;
                draw_player(c, p, rx, row * L->slot_h + c->yp, L->half_w, L->slot_h, row % 2 == 1);
            }
            ict++;
        }
    }
}

/* drawWithoutTeams 0x1401309d0: one green "Players" header; the first half of the players on the left */
static void draw_without_teams(sb_ctx_t *c, const int *order, int n)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    const cs2d_scoreboard_view_t *v = c->v;
    int lx = L->rx + 16, rx = L->rx + L->rw - 16 - L->half_w;
    set_color(0, 255, 0);
    draw_team_header(c, lx, c->yp, L->rw - 32, "Players", c->t + c->ct, "");
    c->yp += L->team_pad + L->team_h;
    set_color(0, 255, 0);
    draw_team_columns(c, lx, c->yp, L->half_w);
    draw_team_columns(c, rx, c->yp, L->half_w);
    c->yp += L->font_h_small + 2;

    float half = (float)(c->t + c->ct) * 0.5f;
    int left_n = ((float)(int)half < half) ? (int)half + 1 : (int)half;
    int li = 0, ri = 0;
    for (int k = 0; k < n; k++) {
        set_color(0, 255, 0);
        const cs2d_player_t *p = &v->players[order[k]];
        if (k < left_n) {
            draw_player(c, p, lx, L->slot_h * li + c->yp, L->half_w, L->slot_h, ~li & 1);
            li++;
        } else {
            draw_player(c, p, rx, L->slot_h * ri + c->yp, L->half_w, L->slot_h, ri & 1);
            ri++;
        }
    }
}

/* FUN_1401066d0 (drawSpectators): "Spectators (n): " then the names separated by ", " (80), wrapped to the
 * window width and anchored to its bottom; names alternate 110 / 150, white when hovered */
static void draw_spectators(sb_ctx_t *c)
{
    const cs2d_scoreboard_layout_t *L = c->L;
    const cs2d_scoreboard_view_t *v = c->v;
    char label[48];
    snprintf(label, sizeof(label), "Spectators (%d): ", c->spec); /* 0791 + " (" + n + "): " */
    int lw = text_w(SB_FONT_SMALL, label), sepw = text_w(SB_FONT_SMALL, ", "), avail = L->rw - 32;

    int lines = 1, run = lw, k = 0;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &v->players[i];
        if (p->id == 0 || p->team != CS2D_TEAM_SPEC) continue;
        int nw = text_w(SB_FONT_SMALL, p->name) + (++k < c->spec ? sepw : 0);
        run += nw;
        if (avail < run) { lines++; run = nw; }
    }
    int lh = (int)((float)L->font_h_small + L->scale + L->scale);
    int y = (L->rh + L->ry - 5) - lines * lh;
    set_color(110, 110, 110);
    s_a = 1.0f;
    draw_txt(SB_FONT_SMALL, label, L->rx + 16, y, 100000, 1.0f);

    run = lw;
    k = 0;
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &v->players[i];
        if (p->id == 0 || p->team != CS2D_TEAM_SPEC) continue;
        k++;
        int nw = text_w(SB_FONT_SMALL, p->name) + (k < c->spec ? sepw : 0);
        int start = run;
        run += nw;
        if (avail < run) { start = 0; y += lh; run = nw; }
        if ((k & 1) == 0) set_color(110, 110, 110);
        else set_color(150, 150, 150);
        int nx = start + 16 + L->rx;
        if (c->hover == 0 && mouse_in(c->v, (float)nx, (float)y, (float)nw, (float)lh)) {
            c->hover = p->id;
            set_color(255, 255, 255);
        }
        draw_txt(SB_FONT_SMALL, p->name, nx, y, 100000, 1.0f);
        if (k < c->spec) {
            set_color(80, 80, 80);
            draw_txt(SB_FONT_SMALL, ", ", nx + nw - sepw, y, 100000, 1.0f);
        }
    }
}

/*
 * Thud_scoreboard.draw. Not reconstructed: the right-click player context menu (Thud_contextmenu), left-clicking a
 * row to follow that player as a spectator, the competitive mode line, the voice and chat mute icons.
 */
void cs2d_scoreboard_render(const cs2d_scoreboard_view_t *v, int screen_w, int screen_h)
{
    cs2d_scoreboard_layout_t L;
    cs2d_scoreboard_layout(screen_w, screen_h, &L);
    sb_ctx_t c = { .v = v, .L = &L, .yp = L.ry, .screen_w = screen_w };
    for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
        const cs2d_player_t *p = &v->players[i];
        if (p->id == 0) continue;
        if (p->team == CS2D_TEAM_T) c.t++;                 /* Tpl::count_t */
        else if (p->team >= CS2D_TEAM_CT) c.ct++;          /* Tpl::count_ct */
        else c.spec++;
    }
    draw_base(&c);
    int order[CS2D_MAX_PLAYERS];
    int n = cs2d_scoreboard_order(v->players, order);
    if (v->teams) draw_with_teams(&c, order, n);
    else draw_without_teams(&c, order, n);
    if (c.spec > 0) draw_spectators(&c);
    set_color(255, 255, 255);
    s_a = 1.0f;
}
