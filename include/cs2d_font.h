#ifndef CS2D_FONT_H
#define CS2D_FONT_H

/*
 * TrueType text engine, GL-free: measurement and coverage rasterization of one line of text.
 * cs2d_render.c owns the texture cache on top of it.
 *
 * Two backends draw the same fonts (gfx/fonts/liberationsans.ttf 13 / 10 / 13-bold / 15 px and
 * sourcesans.ttf 18 px, plus Liberation Sans at any CS2D_FONT_SIZED size):
 *   - GDI+ (Windows only): PrivateFontCollection, TextRenderingHintAntiAlias (4), GenericTypographic
 *     with MeasureTrailingSpaces | NoWrap | NoClip. This is what the desktop build has always used.
 *   - stb_truetype (portable; the only one on the web): the same fonts, unhinted, sized so that the
 *     em square is the pixel size (GDI+ UnitPixel), widths summed from the hmtx advances.
 */

#include <stdbool.h>
#include <stdint.h>
#include "cs2d_render.h"

typedef enum {
    CS2D_FONT_BACKEND_GDIPLUS = 0,
    CS2D_FONT_BACKEND_STB     = 1
} cs2d_font_backend_t;

/* Backend used by cs2d_font_measure / _raster (default: GDI+ on Windows, stb elsewhere); false if unavailable */
bool cs2d_font_set_backend(cs2d_font_backend_t backend);
cs2d_font_backend_t cs2d_font_get_backend(void);

/* Loads the fonts of the current backend on first use; false when they could not be opened */
bool cs2d_font_ready(void);
bool cs2d_font_backend_ready(cs2d_font_backend_t backend);

/* Bounding box of text[0..len) (len < 0: up to the NUL), unrounded, as GdipMeasureString reports it */
bool cs2d_font_measure(cs2d_font_id_t font_id, const char *text, int len, float *out_w, float *out_h);

/*
 * Rasterizes the text into cov (w * h bytes, row-major, cleared by the caller) with the top-left of
 * the layout box at (0, 0): 0 = background, 255 = fully covered. No gamma applied.
 */
bool cs2d_font_raster(cs2d_font_id_t font_id, const char *text, uint8_t *cov, int w, int h);

/* The same two calls on an explicit backend (the GDI+ vs stb comparison runs both side by side) */
bool cs2d_font_measure_with(cs2d_font_backend_t backend, cs2d_font_id_t font_id, const char *text, int len,
                            float *out_w, float *out_h);
bool cs2d_font_raster_with(cs2d_font_backend_t backend, cs2d_font_id_t font_id, const char *text,
                           uint8_t *cov, int w, int h);

/*
 * Text bytes to code points: strict UTF-8 first, and when that fails the bytes are Windows-1252
 * (what MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS) then CP_ACP does on a Western system).
 * Returns the number of code points written (no terminator).
 */
int cs2d_font_decode(const char *text, int len, uint32_t *out, int max_out);

#endif
