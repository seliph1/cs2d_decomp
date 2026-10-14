#include "cs2d_hudtxt.h"
#include "cs2d_render.h"
#include <string.h>
#include <math.h>

/*
 * Lua HUD texts. Sources (CS2D.exe): receive [234] FUN_1400a3530, tweens Ttween.receive 0x1400b4420 /
 * Ttween.addtween 0x1400a14c0 / Ttween.update 0x14027fc50 / Ttween.freehudtext 0x140097be0, colour tag builder
 * FUN_14027fb80, draw FUN_14024f5a0 (called from the game HUD 0x1402a0000 after the radar / HUD, before the chat)
 * and the inline tag drawer FUN_14024cc00.
 *
 * mp_hudscale transform (FUN_140291080 / FUN_14024f5a0 / 1402b0000.c:6760-6785):
 * modes 0 (1:1 pixels), 1 (850x480 zoom/aspect), 2 (640x480 stretch), 3 (640x480 centered);
 * font sharpness at screen_h > 589 under modes 1 and 2.
 */

#define TAG_COLOR 0x00A9 /* "©RRRGGGBBB" */
#define TAG_IMAGE 0x00AE /* "®gfx/path.ext" */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t  rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }

static void recount(cs2d_hudtxt_t *h)
{
    h->last = 0;
    h->count = 0;
    for (int i = 0; i < CS2D_HUDTXT_MAX; i++) {
        if (h->line[i].len > 0) {
            h->last = i;
            h->count++;
        }
    }
}

/* Ttween.freehudtext: drop every animation of text id */
static void free_tweens(cs2d_hudtxt_t *h, int id)
{
    for (int i = 0; i < CS2D_HUDTXT_TWEENS; i++) {
        if (h->tween[i].used && h->tween[i].id == id) h->tween[i].used = false;
    }
}

void cs2d_hudtxt_reset(cs2d_hudtxt_t *h)
{
    memset(h, 0, sizeof(*h));
}

/* bbStringFind(s, ch) */
static int find16(const uint16_t *s, int len, uint16_t ch, int from)
{
    for (int i = from; i < len; i++) {
        if (s[i] == ch) return i;
    }
    return -1;
}

/* bbStringFindLast(s, ch) (FUN_1408c3b70) */
static int find_last16(const uint16_t *s, int len, uint16_t ch)
{
    for (int i = len - 1; i >= 0; i--) {
        if (s[i] == ch) return i;
    }
    return -1;
}

static int digits3(const uint16_t *s)
{
    int v = 0;
    for (int i = 0; i < 3; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

bool cs2d_hudtxt_on_message(cs2d_hudtxt_t *h, const uint8_t *buf, int len, int *pos)
{
    /* [234, id u8]: 255 clears every text; else [234, id, chars u16, chars * u16, x u16, y u16, align u8,
       valign u8, size u8], x / y biased by 0x4000 */
    int p = *pos + 1;
    if (p + 1 > len) return false;
    int id = buf[p++];
    if (id == 255) {
        for (int i = 0; i < CS2D_HUDTXT_MAX; i++) {
            h->line[i].len = 0;
            free_tweens(h, i);
        }
        h->last = 0;
        h->count = 0;
        *pos = p;
        return true;
    }
    if (p + 2 > len) return false;
    int chars = rd16(buf + p);
    p += 2;
    if (p + chars * 2 + 7 > len) return false;
    const uint8_t *txt = buf + p;
    p += chars * 2;
    int x = (int)rd16(buf + p) - 0x4000;
    int y = (int)rd16(buf + p + 2) - 0x4000;
    int align = buf[p + 4], valign = buf[p + 5], size = buf[p + 6];
    p += 7;
    if (id < CS2D_HUDTXT_MAX) {
        cs2d_hudtxt_line_t *l = &h->line[id];
        int n = chars < CS2D_HUDTXT_TEXT_LEN ? chars : CS2D_HUDTXT_TEXT_LEN;
        for (int i = 0; i < n; i++) l->text[i] = rd16(txt + i * 2);
        l->len = n;
        l->x = x;
        l->y = y;
        l->align = align;
        l->valign = valign;
        l->size = size;
        l->alpha = 1.0f;
        l->tags = find_last16(l->text, n, TAG_COLOR) > 0 || find_last16(l->text, n, TAG_IMAGE) >= 0;
    }
    free_tweens(h, id);
    recount(h);
    *pos = p;
    return true;
}

/* Ttween.addtween for a hud text: replaces the animation of the same kind (modes 0 / 4 / 5 never conflict) */
static cs2d_hudtxt_tween_t *add_tween(cs2d_hudtxt_t *h, int id, uint8_t mode)
{
    cs2d_hudtxt_tween_t *slot = NULL;
    for (int i = 0; i < CS2D_HUDTXT_TWEENS; i++) {
        cs2d_hudtxt_tween_t *t = &h->tween[i];
        if (t->used && t->id == id && t->mode == mode) t->used = false;
        if (!t->used && !slot) slot = t;
    }
    if (slot) memset(slot, 0, sizeof(*slot));
    return slot;
}

bool cs2d_hudtxt_on_tween(cs2d_hudtxt_t *h, const uint8_t *buf, int len, int *pos, uint32_t now_ms)
{
    /* [54, mode u8, target u16, ...]: target 1..200 is hud text 200 - target, above 200 the Tdo 200 + id */
    static const int body[9] = { 8, 10, 6, 4, 5, 7, 12, 5, 5 };
    int p = *pos;
    if (p + 4 > len) return false;
    uint8_t mode = buf[p + 1];
    int target = rd16(buf + p + 2);
    int n = mode < 9 ? body[mode] : 0;
    if (p + 4 + n > len) return false;
    const uint8_t *d = buf + p + 4;
    *pos = p + 4 + n;

    if ((unsigned)(target - 1) >= CS2D_HUDTXT_MAX) return true;
    if (mode != 0 && mode != 4 && mode != 5) return true; /* Ttween.update drops other modes on a hud text */
    int id = 200 - target;
    cs2d_hudtxt_line_t *l = &h->line[id];
    cs2d_hudtxt_tween_t *t = add_tween(h, id, mode);
    if (!t) return true;
    t->used = true;
    t->mode = mode;
    t->id = id;
    t->start_ms = now_ms;
    t->duration = rd32(d);

    /* Start values from the text: position, alpha and the leading colour tag (else 255, 220, 0) */
    uint8_t r = 255, g = 220, b = 0;
    if (l->len > 0 && l->text[0] == TAG_COLOR && l->len >= 10) {
        r = (uint8_t)digits3(l->text + 1);
        g = (uint8_t)digits3(l->text + 4);
        b = (uint8_t)digits3(l->text + 7);
    }
    if (mode == 0) {
        t->x[0] = (float)l->x;
        t->y[0] = (float)l->y;
        t->x[1] = (float)((int)rd16(d + 4) - 0x8000);
        t->y[1] = (float)((int)rd16(d + 6) - 0x8000);
    } else if (mode == 4) {
        t->alpha[0] = l->alpha;
        t->alpha[1] = (float)d[4] * 0.003921569f;
    } else {
        t->r[0] = r;
        t->g[0] = g;
        t->b[0] = b;
        t->r[1] = d[4];
        t->g[1] = d[5];
        t->b[1] = d[6];
    }
    return true;
}

/* FUN_14027fb80: "©RRRGGGBBB" with zero padded components */
static void put_color_tag(uint16_t *dst, int r, int g, int b)
{
    int v[3] = { r, g, b };
    dst[0] = TAG_COLOR;
    for (int i = 0; i < 3; i++) {
        int c = v[i] < 0 ? 0 : v[i] > 999 ? 999 : v[i];
        dst[1 + i * 3] = (uint16_t)('0' + c / 100);
        dst[2 + i * 3] = (uint16_t)('0' + c / 10 % 10);
        dst[3 + i * 3] = (uint16_t)('0' + c % 10);
    }
}

void cs2d_hudtxt_update(cs2d_hudtxt_t *h, uint32_t now_ms)
{
    for (int i = 0; i < CS2D_HUDTXT_TWEENS; i++) {
        cs2d_hudtxt_tween_t *t = &h->tween[i];
        if (!t->used) continue;
        int el = (int)(now_ms - t->start_ms);
        float f = t->duration > 0 ? (float)el / (float)t->duration : 1.0f;
        if (f >= 1.0f) f = 1.0f;
        cs2d_hudtxt_line_t *l = &h->line[t->id];
        if (t->mode == 0) {
            l->x = (int)((t->x[1] - t->x[0]) * f + t->x[0]);
            l->y = (int)((t->y[1] - t->y[0]) * f + t->y[0]);
        } else if (t->mode == 4) {
            l->alpha = (1.0f - f) * t->alpha[0] + f * t->alpha[1];
        } else if (l->len > 0) {
            /* Strip the leading colour tag, then prepend the interpolated one */
            if (l->text[0] == TAG_COLOR) {
                int rest = l->len > 10 ? l->len - 10 : 0;
                memmove(l->text, l->text + 10, (size_t)rest * 2);
                l->len = rest;
            }
            int keep = l->len < CS2D_HUDTXT_TEXT_LEN - 10 ? l->len : CS2D_HUDTXT_TEXT_LEN - 10;
            memmove(l->text + 10, l->text, (size_t)keep * 2);
            float k = 1.0f - f;
            put_color_tag(l->text, (int)((float)t->r[0] * k + (float)t->r[1] * f),
                          (int)((float)t->g[0] * k + (float)t->g[1] * f),
                          (int)((float)t->b[0] * k + (float)t->b[1] * f));
            l->len = keep + 10;
        }
        if (el > t->duration) t->used = false;
    }
}

/* ---- Drawing ---- */

static int utf16_to_utf8(const uint16_t *s, int n, char *out, int cap)
{
    int o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x80) {
            if (o + 1 >= cap) break;
            out[o++] = (char)c;
        } else if (c < 0x800) {
            if (o + 2 >= cap) break;
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            if (o + 3 >= cap) break;
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            if (o + 4 >= cap) break;
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o] = '\0';
    return o;
}

void cs2d_hudscale_compute(int hudscale, int screen_w, int screen_h, int in_x, int in_y, int in_size,
                           cs2d_hudscale_calc_t *out)
{
    if (!out) return;
    if (screen_w < 1) screen_w = 850;
    if (screen_h < 1) screen_h = 480;

    /* g_zoom and letterbox offsets (CS2D.exe 1402b0000.c:6760-6785) */
    float f_h = (float)screen_h * 0.0020833334f; /* screen_h / 480.0f */
    float f_w = (float)screen_w / 850.0f;
    float g_zoom = (f_w <= f_h) ? f_w : f_h;
    float off_x = 0.0f;
    float off_y = 0.0f;
    if (f_h <= f_w) {
        if ((float)screen_w / (float)screen_h > 1.78f) {
            off_x = fabsf(850.0f * g_zoom - (float)screen_w) * 0.5f;
        }
    } else {
        off_y = fabsf(g_zoom * 480.0f - (float)screen_h) * 0.5f;
    }

    /* Font sharpness & raster font size selection (CS2D.exe FUN_14024f5a0 / 140240000.c:8645-8656) */
    float f_scale = 1.0f;
    int font_size = in_size;
    if ((hudscale == 1 || hudscale == 2) && (screen_h > 589)) {
        float scaled_sz = (float)font_size * g_zoom;
        if (scaled_sz >= 100.0f) {
            f_scale = (float)font_size * 0.01f;
            font_size = 100;
        } else {
            font_size = (int)scaled_sz;
            f_scale = 1.0f / g_zoom;
        }
    }
    if (font_size < 10) font_size = 10;
    if (font_size > 100) font_size = 100;

    out->font_size = font_size;
    out->f_scale = f_scale;

    switch (hudscale) {
    case 1:
        /* Mode 1: 850x480 virtual resolution scaled by g_zoom */
        out->x = (float)in_x * g_zoom + off_x;
        out->y = (float)in_y * g_zoom + off_y;
        out->scale_x = f_scale * g_zoom;
        out->scale_y = f_scale * g_zoom;
        break;

    case 2:
        /* Mode 2: 640x480 virtual resolution stretched across viewport (FUN_140291080) */
        out->x = (float)in_x * ((float)screen_w / 640.0f);
        out->y = (float)in_y * ((float)screen_h / 480.0f);
        out->scale_x = f_scale * ((float)screen_w / 640.0f);
        out->scale_y = f_scale * ((float)screen_h / 480.0f);
        break;

    case 3: {
        /* Mode 3: 640x480 virtual resolution centered unscaled (FUN_140291080:695) */
        float center_x = (float)((int)(((float)screen_w - 640.0f) * 0.5f));
        float center_y = (float)((int)(((float)screen_h - 480.0f) * 0.5f));
        out->x = (float)in_x + center_x;
        out->y = (float)in_y + center_y;
        out->scale_x = 1.0f;
        out->scale_y = 1.0f;
        break;
    }

    case 0:
    default:
        /* Mode 0: 1:1 screen pixels */
        out->x = (float)in_x;
        out->y = (float)in_y;
        out->scale_x = 1.0f;
        out->scale_y = 1.0f;
        break;
    }
}

/* DrawText with the black drop shadow at (+scale_x, +scale_y); returns the advance in font units */
static float draw_piece(cs2d_font_id_t font, const uint16_t *s, int n, float x, float y,
                        float scale_x, float scale_y, int r, int g, int b, float a, bool draw)
{
    if (n <= 0) return 0.0f;
    char u8[CS2D_HUDTXT_TEXT_LEN * 3 + 1];
    utf16_to_utf8(s, n, u8, (int)sizeof(u8));
    if (draw) {
        cs2d_render_text_transform_ex(font, x + scale_x, y + scale_y, u8, scale_x, scale_y, 0.0f, 0.0f, 0.0f, a);
        cs2d_render_text_transform_ex(font, x, y, u8, scale_x, scale_y, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, a);
    }
    return cs2d_render_text_width_ex(font, u8);
}

/*
 * FUN_14024cc00: the text split at "©"; each part starts with RRRGGGBBB (a text not starting with "©" gets
 * 255220000) and may hold "®path.ext" images drawn TextHeight high. draw == false only measures the width
 * (FUN_14024c8b0 for the alignment).
 */
static float draw_tagged(cs2d_font_id_t font, const cs2d_hudtxt_line_t *l, float x, float y, float line_h,
                         float scale_x, float scale_y, bool draw)
{
    uint16_t s[CS2D_HUDTXT_TEXT_LEN + 1];
    int n = 0;
    if (l->text[0] != TAG_COLOR) s[n++] = TAG_COLOR;
    for (int i = 0; i < l->len && n < CS2D_HUDTXT_TEXT_LEN; i++) s[n++] = l->text[i];
    float x0 = x;
    int start = 0;
    while (start < n) {
        int end = find16(s, n, TAG_COLOR, start + 1);
        if (end < 0) end = n;
        const uint16_t *seg = s + start + 1;
        int seg_len = end - start - 1;
        start = end;
        if (seg_len <= 8) continue;
        int r = digits3(seg), g = digits3(seg + 3), b = digits3(seg + 6);
        const uint16_t *p = seg + 9;
        int pl = seg_len - 9;
        for (int guard = 0; guard < 40; guard++) {
            int k = find16(p, pl, TAG_IMAGE, 0);
            if (k < 0) break;
            x += draw_piece(font, p, k, x, y, scale_x, scale_y, r, g, b, l->alpha, draw) * scale_x;
            p += k + 1;
            pl -= k + 1;
            int dot = find16(p, pl, '.', 0);
            if (dot < 1 || pl < 4 || dot + 4 > pl) {
                /* No "name.ext" after the tag: the original drops one more char */
                if (pl > 0) {
                    p++;
                    pl--;
                }
                continue;
            }
            char path[256];
            utf16_to_utf8(p, dot + 4, path, (int)sizeof(path));
            const cs2d_texture_t *tex = cs2d_gfx_get_sprite(cs2d_render_get_gfx_bank(), path, false);
            if (tex && tex->id && tex->height > 0) {
                float w = (line_h * (float)tex->width / (float)tex->height) * scale_x;
                float h = line_h * scale_y;
                if (draw) cs2d_render_sprite(tex, x + w * 0.5f, y + h * 0.5f, w, h, 0.0f, 1.0f, 1.0f, 1.0f, l->alpha);
                x += w;
                p += dot + 4;
                pl -= dot + 4;
            }
        }
        x += draw_piece(font, p, pl, x, y, scale_x, scale_y, r, g, b, l->alpha, draw) * scale_x;
    }
    return x - x0;
}

void cs2d_hudtxt_render(const cs2d_hudtxt_t *h, int screen_w, int screen_h, int hudscale)
{
    if (h->count == 0) return;
    for (int i = 0; i <= h->last; i++) {
        const cs2d_hudtxt_line_t *l = &h->line[i];
        if (l->len <= 0) continue;

        cs2d_hudscale_calc_t tr;
        cs2d_hudscale_compute(hudscale, screen_w, screen_h, l->x, l->y, l->size, &tr);

        cs2d_font_id_t font = CS2D_FONT_SIZED(tr.font_size);
        float line_h = cs2d_render_font_line_height(font);
        float x = tr.x, y = tr.y;

        if (l->tags) {
            /* FUN_14024cc00: float alignment, valign by (int)(TextHeight / 2) */
            float total_w = draw_tagged(font, l, 0.0f, 0.0f, line_h, tr.scale_x, tr.scale_y, false);
            if (l->align == 1) x -= 0.5f * total_w;
            else if (l->align == 2) x -= total_w;
            if (l->valign == 1) y -= (float)((int)line_h / 2) * tr.scale_y;
            else if (l->valign == 2) y -= line_h * tr.scale_y;
            draw_tagged(font, l, x, y, line_h, tr.scale_x, tr.scale_y, true);
            continue;
        }

        const uint16_t *s = l->text;
        int n = l->len;
        int r = 255, g = 220, b = 0;
        if (s[0] == TAG_COLOR) {
            if (n >= 10) {
                r = digits3(s + 1);
                g = digits3(s + 4);
                b = digits3(s + 7);
            }
            s += n >= 10 ? 10 : n;
            n -= n >= 10 ? 10 : n;
        }

        float w = draw_piece(font, s, n, 0.0f, 0.0f, tr.scale_x, tr.scale_y, 0, 0, 0, 0.0f, false) * tr.scale_x;
        if (l->align == 1) x -= (float)((int)w / 2);
        else if (l->align == 2) x -= (float)((int)w);
        if (l->valign == 1) y -= (float)((int)(line_h * tr.scale_y) / 2);
        else if (l->valign == 2) y -= (float)((int)(line_h * tr.scale_y));

        draw_piece(font, s, n, x, y, tr.scale_x, tr.scale_y, r, g, b, l->alpha, true);
    }
}
