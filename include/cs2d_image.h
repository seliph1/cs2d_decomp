#ifndef CS2D_IMAGE_H
#define CS2D_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Portable asset decoding for the renderer.
 *
 * This replaces the GDI+ (gdiplus.dll) image path the renderer used before. The real CS2D.exe
 * decoded its graphics through GDI+ (GdipCreateBitmapFromFile / GdipBitmapGetPixel), which has two
 * consequences our decoder reproduces:
 *
 *   - the container is sniffed from the bytes, never from the extension: the shipped
 *     gfx/hud_symbols.bmp is in fact a PNG, and gfx/lighting/gfx_shadowmap.png is a 4-bit palette;
 *   - the decoded pixels are one uint32 per pixel, memory bytes R,G,B,A, top row first and never
 *     premultiplied, which is the layout the GL uploader expects.
 *
 * Decoding itself is the vendored public-domain stb_image (third_party/stb_image.h), named in
 * docs/agents/KNOWLEDGE.md "Web/Wasm portability" as the portable replacement for this path.
 */
typedef struct {
    int       width;
    int       height;
    uint32_t *rgba;                /* width * height pixels, bytes R,G,B,A (0xAABBGGRR on x86) */
    bool      has_embedded_alpha;  /* the source carried a real alpha channel: min < 255 && max > 0 */
} cs2d_image_t;

/* BMP / PNG / JPEG / TGA / GIF / PSD / HDR / PNM, chosen by content (see above). */
bool cs2d_image_load(cs2d_image_t *out, const char *path);

/*
 * Largest frame of a Windows .ico container (cs2d.ico, the main menu logo), decoded either from
 * its embedded PNG or from its DIB payload. GdipCreateBitmapFromFile picked a 16x16 / 32x32 frame
 * instead of the 256x256 one, so this walks ICONDIR itself.
 */
bool cs2d_image_load_ico(cs2d_image_t *out, const char *path);

void cs2d_image_free(cs2d_image_t *img);

#endif /* CS2D_IMAGE_H */
