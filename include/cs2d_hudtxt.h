#ifndef CS2D_HUDTXT_H
#define CS2D_HUDTXT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Lua HUD texts (hudtxt / hudtxt2 / hudtxtclear / hudtxtmove / hudtxtalphafade / hudtxtcolorfade).
 * Client state: the parallel arrays 0x140a15460 (text), 0x140a15458 (x), 0x140a15450 (y), 0x140a15448 (align),
 * 0x140a15440 (valign), 0x140a15438 (alpha), 0x140a15430 (inline tags: FindLast("©") > 0 or "®" present), 0x140a15428 (size),
 * 200 entries; DAT_140ddf2e8 = highest used id, DAT_140ddf2ec = used count.
 * Receive: [234] FUN_1400a3530; animations: [54] Ttween.receive 0x1400b4420 / Ttween.update 0x14027fc50;
 * draw: FUN_14024f5a0 (inline tags FUN_14024cc00).
 */
#define CS2D_HUDTXT_MAX      200
#define CS2D_HUDTXT_TEXT_LEN 512  /* UTF-16 code units kept per text */
#define CS2D_HUDTXT_TWEENS   64

typedef struct {
    uint16_t text[CS2D_HUDTXT_TEXT_LEN]; /* UTF-16, may start with the colour tag U+00A9 "RRRGGGBBB" */
    int      len;
    int      x, y;
    int      align, valign; /* 0 left/top, 1 center, 2 right/bottom */
    int      size;          /* font size in px (lua_font.SetFont clamps it to 10..100) */
    float    alpha;
    bool     tags;          /* U+00A9 after the first char or any U+00AE: drawn by FUN_14024cc00 */
} cs2d_hudtxt_line_t;

/* Ttween with hudtext <= 0: mode 0 move, 4 alpha fade, 5 colour fade */
typedef struct {
    bool     used;
    uint8_t  mode;
    int      id;
    uint32_t start_ms;
    int      duration;
    float    x[2], y[2], alpha[2];
    uint8_t  r[2], g[2], b[2];
} cs2d_hudtxt_tween_t;

typedef struct {
    cs2d_hudtxt_line_t  line[CS2D_HUDTXT_MAX];
    cs2d_hudtxt_tween_t tween[CS2D_HUDTXT_TWEENS];
    int                 last;  /* DAT_140ddf2e8 */
    int                 count; /* DAT_140ddf2ec */
} cs2d_hudtxt_t;

void cs2d_hudtxt_reset(cs2d_hudtxt_t *h);

/* [234, ...] at buf[*pos]; advances *pos past the message. False if it is truncated */
bool cs2d_hudtxt_on_message(cs2d_hudtxt_t *h, const uint8_t *buf, int len, int *pos);

/* [54, ...] at buf[*pos] (Ttween.receive): hud text tweens are applied, Tdo tweens only skipped */
bool cs2d_hudtxt_on_tween(cs2d_hudtxt_t *h, const uint8_t *buf, int len, int *pos, uint32_t now_ms);

/* Ttween.update for the hud text tweens */
void cs2d_hudtxt_update(cs2d_hudtxt_t *h, uint32_t now_ms);

/* Transformation result computed for a HUD text or HUD image under mp_hudscale */
typedef struct {
    float x;           /* transformed base x */
    float y;           /* transformed base y */
    float scale_x;     /* horizontal render scale */
    float scale_y;     /* vertical render scale */
    int   font_size;   /* raster font size (10..100) */
    float f_scale;     /* scale factor (fVar9) used for alignment */
} cs2d_hudscale_calc_t;

/* Computes HUD scaling transform (CS2D.exe FUN_140291080 / FUN_14024f5a0 / 1402b0000.c:6760-6785) */
void cs2d_hudscale_compute(int hudscale, int screen_w, int screen_h, int in_x, int in_y, int in_size,
                           cs2d_hudscale_calc_t *out);

/* FUN_14024f5a0: Lua HUD texts with mp_hudscale transform (0 pixel, 1 850x480, 2 640x480 stretched, 3 640x480 centered) */
void cs2d_hudtxt_render(const cs2d_hudtxt_t *h, int screen_w, int screen_h, int hudscale);

#endif /* CS2D_HUDTXT_H */
