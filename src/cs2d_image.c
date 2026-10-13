/*
 * Portable asset decoding — the replacement for the GDI+ (gdiplus.dll) image path.
 *
 * Evidence for the behaviour this has to match is in docs/agents/KNOWLEDGE.md "Web/Wasm
 * portability": the original client decoded every asset with 19 functions of gdiplus.dll
 * (GdipCreateBitmapFromFile, GdipCreateBitmapFromStream for .ico, GdipBitmapGetPixel, ...).
 * Nothing Windows-specific is left here: the vendored public-domain stb_image does the decoding,
 * and only the file I/O is libc.
 *
 * Two properties of the old path are load-bearing and are reproduced exactly:
 *   - the format is sniffed from the bytes, not the extension (gfx/hud_symbols.bmp is a PNG,
 *     gfx/lighting/gfx_shadowmap.png is a 4-bit palette image);
 *   - the pixels are RGBA (bytes R,G,B,A, top row first, never premultiplied) and
 *     has_embedded_alpha is "min alpha < 255 && max alpha > 0", which is what the renderer's
 *     mask modes (cs2d_render.c apply_cs2d_mask_mode) switch on.
 */

#include "cs2d_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_image is a vendored third-party single-header library; keep its warnings out of our build */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
/*
 * stb_image keeps a few of its settings in thread-local storage (__thread on GCC), but this client is
 * single-threaded (there is no SDL_CreateThread / pthread_create / CreateThread anywhere in src/) and
 * never calls stbi_set_*_thread, so the local variants buy nothing. They do cost something on MinGW:
 * GCC implements __thread through emutls, whose runtime is built on the winpthreads API, so the linker
 * records an import of libwinpthread-1.dll (pthread_once, pthread_mutex_*, pthread_key_*,
 * pthread_getspecific/setspecific) that does not ship next to the executables — the exe then dies with
 * "libwinpthread-1.dll was not found" (0xC0000135) before main() runs. With STBI_NO_THREAD_LOCALS the
 * settings fall back to plain statics, with identical behaviour for us.
 */
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint8_t *read_file_bytes(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }

    *out_size = (size_t)size;
    return buf;
}

/*
 * Adopts an stb buffer. With 4 requested channels stb hands back R,G,B,A bytes, which is exactly
 * the uint32 layout the renderer uses on a little-endian host.
 */
static bool adopt(cs2d_image_t *out, unsigned char *pixels, int w, int h)
{
    out->width  = w;
    out->height = h;
    out->rgba   = (uint32_t *)pixels;

    uint8_t min_a = 255, max_a = 0;
    for (int i = 0; i < w * h; i++) {
        uint8_t a = (uint8_t)((out->rgba[i] >> 24) & 0xFF);
        if (a < min_a) min_a = a;
        if (a > max_a) max_a = a;
    }
    out->has_embedded_alpha = (min_a < 255 && max_a > 0);
    return true;
}

bool cs2d_image_load(cs2d_image_t *out, const char *path)
{
    if (!out || !path) return false;
    memset(out, 0, sizeof(*out));

    int w = 0, h = 0, channels = 0;
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    return adopt(out, pixels, w, h);
}

/*
 * A .ico frame is either a PNG blob or a DIB: a BITMAPINFOHEADER whose biHeight counts the XOR
 * bitmap plus the 1-bit AND mask. Prefixing the DIB with a BITMAPFILEHEADER (and halving biHeight,
 * so the mask rows are not decoded as pixels) turns it back into an ordinary BMP.
 */
static unsigned char *ico_dib_to_bmp(const uint8_t *dib, uint32_t dib_len, uint32_t *out_len)
{
    if (dib_len < 40) return NULL;

    uint32_t hsz = rd32le(dib + 0);
    if (hsz < 40 || hsz > dib_len) return NULL;

    int32_t  bm_h  = (int32_t)rd32le(dib + 8);
    int32_t  bm_hh = (bm_h < 0) ? -bm_h : bm_h;
    uint16_t bpp   = rd16le(dib + 14);

    if (bm_hh < 2 || (bm_hh % 2) != 0) return NULL;                 /* XOR bitmap + AND mask */
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return NULL;

    uint32_t palette_bytes = (bpp <= 8) ? ((uint32_t)1u << bpp) * 4u : 0u;
    uint32_t pixel_offset  = 14u + hsz + palette_bytes;

    uint8_t *bmp = (uint8_t *)malloc(14u + dib_len);
    if (!bmp) return NULL;

    bmp[0] = 'B';
    bmp[1] = 'M';
    wr32le(bmp + 2, 14u + dib_len);   /* bfSize */
    wr16le(bmp + 6, 0);               /* bfReserved1 */
    wr16le(bmp + 8, 0);               /* bfReserved2 */
    wr32le(bmp + 10, pixel_offset);   /* bfOffBits */
    memcpy(bmp + 14, dib, dib_len);
    wr32le(bmp + 14 + 8, (uint32_t)(bm_hh / 2));

    *out_len = 14u + dib_len;
    return bmp;
}

bool cs2d_image_load_ico(cs2d_image_t *out, const char *path)
{
    if (!out || !path) return false;
    memset(out, 0, sizeof(*out));

    size_t size = 0;
    uint8_t *data = read_file_bytes(path, &size);
    if (!data) return false;

    /* ICONDIR: reserved(2)=0, type(2)=1 (icon), count(2); then 16-byte ICONDIRENTRY records */
    if (size < 22 || rd16le(data + 0) != 0 || rd16le(data + 2) != 1) {
        free(data);
        return false;
    }
    uint16_t count = rd16le(data + 4);

    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint32_t last_area = 0xFFFFFFFFu;

    /* Frames are tried largest-area first; one that fails to decode falls through to the next */
    for (;;) {
        int      best      = -1;
        uint32_t best_area = 0;

        for (uint16_t i = 0; i < count; i++) {
            size_t rec = 6u + (size_t)i * 16u;
            if (rec + 16u > size) break;
            uint32_t ew  = data[rec + 0] ? data[rec + 0] : 256u;
            uint32_t eh  = data[rec + 1] ? data[rec + 1] : 256u;
            uint32_t len = rd32le(data + rec + 8);
            uint32_t off = rd32le(data + rec + 12);
            if (!len || (uint64_t)off + len > size) continue;

            uint32_t area = ew * eh;
            if (area >= last_area || area <= best_area) continue;
            best_area = area;
            best      = i;
        }

        if (best < 0) break;
        last_area = best_area;

        size_t         rec   = 6u + (size_t)best * 16u;
        uint32_t       len   = rd32le(data + rec + 8);
        uint32_t       off   = rd32le(data + rec + 12);
        const uint8_t *frame = data + off;

        int            w = 0, h = 0, channels = 0;
        unsigned char *pixels = NULL;

        if (len >= 8 && memcmp(frame, png_sig, 8) == 0) {
            pixels = stbi_load_from_memory(frame, (int)len, &w, &h, &channels, 4);
        } else {
            uint32_t bmp_len  = 0;
            uint8_t *bmp_file = ico_dib_to_bmp(frame, len, &bmp_len);
            if (bmp_file) {
                pixels = stbi_load_from_memory(bmp_file, (int)bmp_len, &w, &h, &channels, 4);
                free(bmp_file);
            }
        }

        if (pixels && w > 0 && h > 0) {
            free(data);
            return adopt(out, pixels, w, h);
        }
        if (pixels) stbi_image_free(pixels);
    }

    free(data);
    return false;
}

void cs2d_image_free(cs2d_image_t *img)
{
    if (!img) return;
    if (img->rgba) {
        stbi_image_free(img->rgba);
        img->rgba = NULL;
    }
    img->width  = 0;
    img->height = 0;
    img->has_embedded_alpha = false;
}


