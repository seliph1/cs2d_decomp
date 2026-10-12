#include "cs2d_render.h"
#include "cs2d_dynobj.h"
#include "cs2d_weapons.h"
#include "cs2d_map.h"
#include "cs2d_fx.h"
#include "cs2d_player.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static SDL_GLContext   g_gl_ctx = NULL;
static int             g_viewport_w = 850;
static int             g_viewport_h = 480;
static cs2d_gfx_bank_t g_gfx_bank;

/* Minimal GDI+ Flat API bindings for loading PNG/BMP tilesets & sprites without external dependencies */
typedef int GpStatus;
typedef void GpBitmap;
typedef void GpImage;
typedef uint32_t ARGB;

typedef struct {
    uint32_t GdiplusVersion;
    void    *DebugEventCallback;
    BOOL     SuppressBackgroundThread;
    BOOL     SuppressExternalCodecs;
} GdiplusStartupInput;

typedef GpStatus (WINAPI *PFN_GdiplusStartup)(ULONG_PTR *token, const GdiplusStartupInput *input, void *output);
typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromFile)(const WCHAR *filename, GpBitmap **bitmap);
typedef GpStatus (WINAPI *PFN_GdipGetImageWidth)(GpImage *image, UINT *width);
typedef GpStatus (WINAPI *PFN_GdipGetImageHeight)(GpImage *image, UINT *height);
typedef GpStatus (WINAPI *PFN_GdipBitmapGetPixel)(GpBitmap *bitmap, INT x, INT y, ARGB *color);
typedef GpStatus (WINAPI *PFN_GdipDisposeImage)(GpImage *image);

static HMODULE                      g_gdiplus_mod = NULL;
static ULONG_PTR                    g_gdiplus_token = 0;
static PFN_GdiplusStartup           pGdiplusStartup = NULL;
static PFN_GdipCreateBitmapFromFile pGdipCreateBitmapFromFile = NULL;
static PFN_GdipGetImageWidth        pGdipGetImageWidth = NULL;
static PFN_GdipGetImageHeight       pGdipGetImageHeight = NULL;
static PFN_GdipBitmapGetPixel       pGdipBitmapGetPixel = NULL;
static PFN_GdipDisposeImage         pGdipDisposeImage = NULL;

static bool init_gdiplus_once(void)
{
    if (g_gdiplus_mod && pGdipCreateBitmapFromFile) return true;
    g_gdiplus_mod = LoadLibraryA("gdiplus.dll");
    if (!g_gdiplus_mod) return false;

    pGdiplusStartup           = (PFN_GdiplusStartup)(void *)GetProcAddress(g_gdiplus_mod, "GdiplusStartup");
    pGdipCreateBitmapFromFile = (PFN_GdipCreateBitmapFromFile)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateBitmapFromFile");
    pGdipGetImageWidth        = (PFN_GdipGetImageWidth)(void *)GetProcAddress(g_gdiplus_mod, "GdipGetImageWidth");
    pGdipGetImageHeight       = (PFN_GdipGetImageHeight)(void *)GetProcAddress(g_gdiplus_mod, "GdipGetImageHeight");
    pGdipBitmapGetPixel       = (PFN_GdipBitmapGetPixel)(void *)GetProcAddress(g_gdiplus_mod, "GdipBitmapGetPixel");
    pGdipDisposeImage         = (PFN_GdipDisposeImage)(void *)GetProcAddress(g_gdiplus_mod, "GdipDisposeImage");

    if (!pGdiplusStartup || !pGdipCreateBitmapFromFile || !pGdipGetImageWidth ||
        !pGdipGetImageHeight || !pGdipBitmapGetPixel || !pGdipDisposeImage) {
        return false;
    }

    GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
    if (pGdiplusStartup(&g_gdiplus_token, &input, NULL) != 0) {
        return false;
    }
    return true;
}

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

/*
 * Reconstructed from CS2D.exe maskMode jump table (0x1401ad6a9 & 0x140062db0):
 *   mask_mode == 0: Opaque / embedded alpha only
 *   mask_mode == 1: Black (0,0,0) color key (hud_nums.bmp, hud_symbols.bmp, hud_radar.bmp)
 *   mask_mode == 2: Magenta (255,0,255) color key or embedded 32-bit alpha (players, weapons, pointer.bmp)
 *   mask_mode == 3: Grayscale luminance-to-alpha mask (0.21*R + 0.72*G + 0.07*B -> A, RGB = 255,255,255)
 *                   (shadowmap.bmp, decals.bmp, flare1.bmp, flare3.bmp, fragments.bmp, blood.bmp)
 */
static void apply_cs2d_mask_mode(uint32_t *rgba, int total_px, bool has_embedded_alpha, int mask_mode)
{
    for (int i = 0; i < total_px; i++) {
        uint32_t px = rgba[i];
        uint8_t r = (uint8_t)(px & 0xFF);
        uint8_t g = (uint8_t)((px >> 8) & 0xFF);
        uint8_t b = (uint8_t)((px >> 16) & 0xFF);
        uint8_t a = (uint8_t)((px >> 24) & 0xFF);

        if (mask_mode == 3) {
            /* Exact luminance-to-alpha formula from 0x140062f40..0x140062fc4 in CS2D.exe */
            if (!has_embedded_alpha) {
                int lum = (int)(0.21f * (float)r + 0.72f * (float)g + 0.07f * (float)b);
                if (lum < 0)   lum = 0;
                if (lum > 255) lum = 255;
                a = (uint8_t)lum;
            }
            r = 255;
            g = 255;
            b = 255;
        } else if (mask_mode == 1) {
            if (r >= 250 && g <= 5 && b >= 250) {
                a = 0;
            } else if (!has_embedded_alpha) {
                if (r <= 4 && g <= 4 && b <= 4) {
                    a = 0;
                } else {
                    a = 255;
                }
            }
        } else if (mask_mode == 2 || mask_mode == 4) {
            if (r >= 250 && g <= 5 && b >= 250) {
                a = 0;
            } else if (!has_embedded_alpha) {
                a = 255;
            }
        } else {
            if (!has_embedded_alpha) {
                a = 255;
            }
        }

        rgba[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }
}

/*
 * 2-pass RGB neighbor dilation into transparent pixels (alpha == 0).
 * Prevents magenta (255,0,255) or black (0,0,0) color fringes when rotated
 * or scaled sprites are sampled with GL_LINEAR bilinear filtering.
 */
static void dilate_transparent_rgb(uint32_t *rgba, int w, int h, int cell_w, int cell_h)
{
    if (!rgba || w <= 0 || h <= 0) return;

    int cw = (cell_w > 0) ? cell_w : w;
    int ch = (cell_h > 0) ? cell_h : h;

    uint32_t *tmp = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!tmp) return;

    uint8_t *valid = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (!valid) {
        free(tmp);
        return;
    }

    for (int i = 0; i < w * h; i++) {
        valid[i] = ((rgba[i] >> 24) & 0xFF) > 0 ? 1 : 0;
    }

    for (int pass = 0; pass < 2; pass++) {
        memcpy(tmp, rgba, (size_t)w * (size_t)h * 4);
        for (int y = 0; y < h; y++) {
            int cy = y / ch;
            for (int x = 0; x < w; x++) {
                int idx = y * w + x;
                if (valid[idx]) continue;

                int cx = x / cw;
                int sum_r = 0, sum_g = 0, sum_b = 0, cnt = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    int ny = y + dy;
                    if (ny < 0 || ny >= h || (ny / ch) != cy) continue;
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        if (nx < 0 || nx >= w || (nx / cw) != cx) continue;
                        int nidx = ny * w + nx;
                        if (valid[nidx] == 1) {
                            uint32_t npx = rgba[nidx];
                            sum_r += (int)(npx & 0xFF);
                            sum_g += (int)((npx >> 8) & 0xFF);
                            sum_b += (int)((npx >> 16) & 0xFF);
                            cnt++;
                        }
                    }
                }
                if (cnt > 0) {
                    uint8_t nr = (uint8_t)(sum_r / cnt);
                    uint8_t ng = (uint8_t)(sum_g / cnt);
                    uint8_t nb = (uint8_t)(sum_b / cnt);
                    tmp[idx] = ((uint32_t)nb << 16) | ((uint32_t)ng << 8) | (uint32_t)nr; /* alpha remains 0 */
                    valid[idx] = 2;
                }
            }
        }
        memcpy(rgba, tmp, (size_t)w * (size_t)h * 4);
        for (int i = 0; i < w * h; i++) {
            if (valid[i] == 2) valid[i] = 1;
        }
    }

    free(valid);
    free(tmp);
}

/*
 * Alpha-weighted 2x2 box downsampler for generating clean mipmap levels
 * without dark/transparent border bleeding.
 */
static uint32_t *downsample_rgba_2x(const uint32_t *src, int src_w, int src_h, int *out_w, int *out_h)
{
    int dst_w = (src_w > 1) ? (src_w / 2) : 1;
    int dst_h = (src_h > 1) ? (src_h / 2) : 1;
    uint32_t *dst = (uint32_t *)malloc((size_t)dst_w * (size_t)dst_h * 4);
    if (!dst) return NULL;

    for (int y = 0; y < dst_h; y++) {
        int sy0 = y * 2;
        int sy1 = (sy0 + 1 < src_h) ? (sy0 + 1) : sy0;
        for (int x = 0; x < dst_w; x++) {
            int sx0 = x * 2;
            int sx1 = (sx0 + 1 < src_w) ? (sx0 + 1) : sx0;

            uint32_t p[4] = {
                src[sy0 * src_w + sx0],
                src[sy0 * src_w + sx1],
                src[sy1 * src_w + sx0],
                src[sy1 * src_w + sx1]
            };

            uint32_t sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
            uint32_t ur = 0, ug = 0, ub = 0;
            for (int k = 0; k < 4; k++) {
                uint32_t r = p[k] & 0xFF;
                uint32_t g = (p[k] >> 8) & 0xFF;
                uint32_t b = (p[k] >> 16) & 0xFF;
                uint32_t a = (p[k] >> 24) & 0xFF;
                sum_a += a;
                sum_r += r * a;
                sum_g += g * a;
                sum_b += b * a;
                ur += r;
                ug += g;
                ub += b;
            }

            uint8_t da = (uint8_t)((sum_a + 2) / 4);
            uint8_t dr, dg, db;
            if (sum_a > 0) {
                dr = (uint8_t)((sum_r + sum_a / 2) / sum_a);
                dg = (uint8_t)((sum_g + sum_a / 2) / sum_a);
                db = (uint8_t)((sum_b + sum_a / 2) / sum_a);
            } else {
                dr = (uint8_t)((ur + 2) / 4);
                dg = (uint8_t)((ug + 2) / 4);
                db = (uint8_t)((ub + 2) / 4);
            }
            dst[y * dst_w + x] = ((uint32_t)da << 24) | ((uint32_t)db << 16) | ((uint32_t)dg << 8) | (uint32_t)dr;
        }
    }

    *out_w = dst_w;
    *out_h = dst_h;
    return dst;
}

/*
 * Uploads processed RGBA image to OpenGL.
 * When cell_w > 0 && cell_h > 0 on a multi-cell sheet (tilesets, shadowmap, player skins,
 * legs, HUD digits/symbols, pointer, etc.), automatically builds a Padded Cell Atlas
 * with PAD = 4 extruded border pixels around every cell to completely eliminate
 * tileset & spritesheet bleeding under GL_LINEAR filtering.
 */
static void upload_rgba_to_texture(
    cs2d_texture_t *tex,
    uint32_t *rgba,
    int w, int h,
    bool has_embedded_alpha,
    int mask_mode,
    int cell_w, int cell_h
)
{
    apply_cs2d_mask_mode(rgba, w * h, has_embedded_alpha, mask_mode);
    dilate_transparent_rgb(rgba, w, h, cell_w, cell_h);

    int pad = 0;
    int gpu_w = w;
    int gpu_h = h;
    uint32_t *upload_buf = rgba;

    if (cell_w > 0 && cell_h > 0 && w >= cell_w && h >= cell_h) {
        int cols = w / cell_w;
        int rows = h / cell_h;
        if (cols >= 1 && rows >= 1 && (cols > 1 || rows > 1)) {
            pad = 4;
            int stride_x = cell_w + pad * 2;
            int stride_y = cell_h + pad * 2;
            gpu_w = cols * stride_x;
            gpu_h = rows * stride_y;
            uint32_t *atlas = (uint32_t *)calloc((size_t)gpu_w * (size_t)gpu_h, sizeof(uint32_t));
            if (atlas) {
                for (int r = 0; r < rows; r++) {
                    for (int c = 0; c < cols; c++) {
                        int src_ox = c * cell_w;
                        int src_oy = r * cell_h;
                        int dst_ox = c * stride_x + pad;
                        int dst_oy = r * stride_y + pad;

                        for (int dy = -pad; dy < cell_h + pad; dy++) {
                            int sy = (dy < 0) ? 0 : ((dy >= cell_h) ? (cell_h - 1) : dy);
                            uint32_t *dst_row = &atlas[(dst_oy + dy) * gpu_w + dst_ox];
                            const uint32_t *src_row = &rgba[(src_oy + sy) * w + src_ox];
                            for (int dx = -pad; dx < cell_w + pad; dx++) {
                                int sx = (dx < 0) ? 0 : ((dx >= cell_w) ? (cell_w - 1) : dx);
                                dst_row[dx] = src_row[sx];
                            }
                        }
                    }
                }
                upload_buf = atlas;
            } else {
                pad = 0;
                gpu_w = w;
                gpu_h = h;
            }
        }
    }

    GLuint tid = 0;
    if (g_gl_ctx != NULL) {
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* Use 2-level alpha-weighted mipmaps for smooth downscaling & rotated anti-aliasing */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)gpu_w, (GLsizei)gpu_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, upload_buf);

        int m1_w = 0, m1_h = 0;
        uint32_t *mip1 = (gpu_w >= 4 && gpu_h >= 4) ? downsample_rgba_2x(upload_buf, gpu_w, gpu_h, &m1_w, &m1_h) : NULL;
        if (mip1) {
            glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, (GLsizei)m1_w, (GLsizei)m1_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip1);
            int m2_w = 0, m2_h = 0;
            uint32_t *mip2 = (m1_w >= 4 && m1_h >= 4) ? downsample_rgba_2x(mip1, m1_w, m1_h, &m2_w, &m2_h) : NULL;
            if (mip2) {
                glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, (GLsizei)m2_w, (GLsizei)m2_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip2);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
                free(mip2);
            } else {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            free(mip1);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (upload_buf != rgba) {
        free(upload_buf);
    }

    tex->id     = tid;
    tex->width  = w;
    tex->height = h;
    tex->gpu_w  = gpu_w;
    tex->gpu_h  = gpu_h;
    tex->cell_w = (pad > 0) ? cell_w : 0;
    tex->cell_h = (pad > 0) ? cell_h : 0;
    tex->pad    = pad;
}

/* Converts a decoded GDI+ bitmap to RGBA, uploads it and disposes the bitmap */
static bool upload_gdiplus_bitmap(cs2d_texture_t *tex, GpBitmap *bmp, int mask_mode, int cell_w, int cell_h)
{
    UINT w = 0, h = 0;
    pGdipGetImageWidth((GpImage *)bmp, &w);
    pGdipGetImageHeight((GpImage *)bmp, &h);
    if (w == 0 || h == 0) {
        pGdipDisposeImage((GpImage *)bmp);
        return false;
    }

    uint32_t *rgba = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        pGdipDisposeImage((GpImage *)bmp);
        return false;
    }

    uint8_t min_a = 255, max_a = 0;
    for (UINT y = 0; y < h; y++) {
        for (UINT x = 0; x < w; x++) {
            ARGB argb = 0;
            pGdipBitmapGetPixel(bmp, (INT)x, (INT)y, &argb);
            uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
            uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
            uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
            uint8_t b = (uint8_t)(argb & 0xFF);
            if (a < min_a) min_a = a;
            if (a > max_a) max_a = a;
            rgba[y * w + x] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }
    }

    pGdipDisposeImage((GpImage *)bmp);

    bool has_embedded_alpha = (min_a < 255 && max_a > 0);
    upload_rgba_to_texture(tex, rgba, (int)w, (int)h, has_embedded_alpha, mask_mode, cell_w, cell_h);
    free(rgba);
    return true;
}

static bool load_image_via_gdiplus(cs2d_texture_t *tex, const char *filepath, int mask_mode, int cell_w, int cell_h)
{
    if (!init_gdiplus_once()) return false;

    WCHAR wpath[MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH) == 0) {
        return false;
    }

    GpBitmap *bmp = NULL;
    if (pGdipCreateBitmapFromFile(wpath, &bmp) != 0 || !bmp) {
        return false;
    }
    return upload_gdiplus_bitmap(tex, bmp, mask_mode, cell_w, cell_h);
}

typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromStream)(IStream *stream, GpBitmap **bitmap);
typedef IStream *(WINAPI *PFN_SHCreateMemStream)(const BYTE *data, UINT size);

/*
 * Loads the largest frame of a .ico file (e.g. the 256x256 PNG frame of cs2d.ico used as the
 * main menu logo). GdipCreateBitmapFromFile would pick one of the small frames instead.
 */
static bool load_ico_largest_frame(cs2d_texture_t *tex, const char *filepath, int mask_mode)
{
    if (!init_gdiplus_once()) return false;

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (size > 6) ? (uint8_t *)malloc((size_t)size) : NULL;
    bool ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) {
        free(data);
        return false;
    }

    /* ICONDIR: reserved(2) type(2) count(2), then 16-byte ICONDIRENTRY records (0 means 256 px) */
    uint16_t count = (uint16_t)(data[4] | (data[5] << 8));
    uint32_t best_px = 0, best_off = 0, best_len = 0;
    for (uint16_t i = 0; i < count && 6u + ((uint32_t)i + 1u) * 16u <= (uint32_t)size; i++) {
        const uint8_t *e = data + 6 + i * 16;
        uint32_t ew  = e[0] ? e[0] : 256;
        uint32_t len = (uint32_t)e[8]  | ((uint32_t)e[9]  << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t off = (uint32_t)e[12] | ((uint32_t)e[13] << 8) | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        if (ew > best_px && off + len <= (uint32_t)size) {
            best_px  = ew;
            best_off = off;
            best_len = len;
        }
    }

    /* Only PNG-compressed frames are decoded from memory; others fall back to GDI+'s own choice */
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    HMODULE shlwapi = LoadLibraryA("shlwapi.dll");
    PFN_SHCreateMemStream pSHCreateMemStream = shlwapi
        ? (PFN_SHCreateMemStream)(void *)GetProcAddress(shlwapi, "SHCreateMemStream") : NULL;
    PFN_GdipCreateBitmapFromStream pGdipCreateBitmapFromStream =
        (PFN_GdipCreateBitmapFromStream)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateBitmapFromStream");

    ok = false;
    if (best_len >= 8 && memcmp(data + best_off, png_sig, 8) == 0 &&
        pSHCreateMemStream && pGdipCreateBitmapFromStream) {
        IStream *stream = pSHCreateMemStream(data + best_off, best_len);
        GpBitmap *bmp = NULL;
        if (stream && pGdipCreateBitmapFromStream(stream, &bmp) == 0 && bmp) {
            ok = upload_gdiplus_bitmap(tex, bmp, mask_mode, 0, 0);
        }
        if (stream) stream->lpVtbl->Release(stream);
    }
    free(data);
    return ok ? true : load_image_via_gdiplus(tex, filepath, mask_mode, 0, 0);
}

bool cs2d_render_init(SDL_Window *window, int width, int height)
{
    g_viewport_w = width;
    g_viewport_h = height;

    if (!window) return false;

    g_gl_ctx = SDL_GL_CreateContext(window);
    if (!g_gl_ctx) {
        return false;
    }

    SDL_GL_MakeCurrent(window, g_gl_ctx);
    SDL_GL_SetSwapInterval(1);

    cs2d_render_resize(width, height);

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    /* Automatically initialize the global CS2D graphics bank now that GL context is active */
    g_gfx_bank.initialized = false;
    cs2d_gfx_bank_init(&g_gfx_bank);

    return true;
}

void cs2d_render_shutdown(SDL_Window *window)
{
    (void)window;
    cs2d_gfx_bank_free(&g_gfx_bank);
    if (g_gl_ctx) {
        SDL_GL_DestroyContext(g_gl_ctx);
        g_gl_ctx = NULL;
    }
}

cs2d_gfx_bank_t *cs2d_render_get_gfx_bank(void)
{
    if ((!g_gfx_bank.initialized || (g_gl_ctx != NULL && g_gfx_bank.tex_ct[0].id == 0)) && g_gl_ctx != NULL) {
        g_gfx_bank.initialized = false;
        cs2d_gfx_bank_init(&g_gfx_bank);
    }
    return &g_gfx_bank;
}

void cs2d_render_resize(int width, int height)
{
    g_viewport_w = width;
    g_viewport_h = height;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)width, (double)height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void cs2d_render_begin_frame(void)
{
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
}

void cs2d_render_end_frame(SDL_Window *window)
{
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

bool cs2d_texture_load_grid(cs2d_texture_t *tex, const char *filepath, int mask_mode, int cell_w, int cell_h)
{
    if (!tex || !filepath) return false;
    memset(tex, 0, sizeof(*tex));

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    uint8_t header[54];
    size_t nread = fread(header, 1, 54, f);
    if (nread >= 6 && header[0] == 0 && header[1] == 0 && header[2] == 1 && header[3] == 0) {
        fclose(f);
        return load_ico_largest_frame(tex, filepath, mask_mode);
    }
    if (nread != 54 || header[0] != 'B' || header[1] != 'M') {
        fclose(f);
        /* Not a raw BMP (e.g. PNG tileset or gfx/hud_symbols.bmp): load via GDI+ */
        return load_image_via_gdiplus(tex, filepath, mask_mode, cell_w, cell_h);
    }

    int width = *(int32_t *)&header[18];
    int height = *(int32_t *)&header[22];
    int bpp = *(int16_t *)&header[28];
    uint32_t data_offset = *(uint32_t *)&header[10];

    if (width <= 0 || height == 0 || (bpp != 24 && bpp != 32)) {
        fclose(f);
        return load_image_via_gdiplus(tex, filepath, mask_mode, cell_w, cell_h);
    }

    bool flip_y = (height > 0);
    if (height < 0) height = -height;

    fseek(f, data_offset, SEEK_SET);

    int row_stride = ((width * (bpp / 8) + 3) / 4) * 4;
    uint8_t *raw_row = (uint8_t *)malloc(row_stride);
    uint32_t *rgba_pixels = (uint32_t *)malloc((size_t)width * (size_t)height * 4);

    if (!raw_row || !rgba_pixels) {
        free(raw_row);
        free(rgba_pixels);
        fclose(f);
        return false;
    }

    uint8_t min_a = 255, max_a = 0;

    for (int y = 0; y < height; y++) {
        if (fread(raw_row, 1, row_stride, f) != (size_t)row_stride) break;
        int dst_y = flip_y ? (height - 1 - y) : y;

        for (int x = 0; x < width; x++) {
            uint8_t b = 0, g = 0, r = 0, a = 255;
            if (bpp == 24) {
                b = raw_row[x * 3 + 0];
                g = raw_row[x * 3 + 1];
                r = raw_row[x * 3 + 2];
            } else {
                b = raw_row[x * 4 + 0];
                g = raw_row[x * 4 + 1];
                r = raw_row[x * 4 + 2];
                a = raw_row[x * 4 + 3];
                if (a < min_a) min_a = a;
                if (a > max_a) max_a = a;
            }
            rgba_pixels[dst_y * width + x] = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
        }
    }

    free(raw_row);
    fclose(f);

    /*
     * In CS2D, 32-bit BMPs (ct1..4.bmp, t1..4.bmp, legs.bmp, weapons/<name>.bmp, equipment.bmp)
     * store an embedded per-pixel 8-bit alpha channel (min_a == 0, max_a == 255), where
     * black boots/gun barrels have RGB=(0,0,0) and A=255. Preserve embedded alpha when present!
     */
    bool has_embedded_alpha = (bpp == 32 && min_a < 255 && max_a > 0);
    upload_rgba_to_texture(tex, rgba_pixels, width, height, has_embedded_alpha, mask_mode, cell_w, cell_h);
    free(rgba_pixels);
    return true;
}

bool cs2d_texture_load_ex(cs2d_texture_t *tex, const char *filepath, int mask_mode)
{
    return cs2d_texture_load_grid(tex, filepath, mask_mode, 0, 0);
}

bool cs2d_texture_load_bmp(cs2d_texture_t *tex, const char *filepath, bool color_key_black)
{
    return cs2d_texture_load_ex(tex, filepath, color_key_black ? 3 : 2);
}

/*
 * Reconstructed from FUN_140113d50 (lines 236-263 in cs2d_dedicated.exe):
 * Resolves gfx/tiles/<tileset_name> (trying exact name, .bmp, and .png)
 * and builds a 32x32 padded cell atlas to prevent tileset edge bleeding.
 */
bool cs2d_texture_load_tileset(cs2d_texture_t *tex, const char *tileset_name)
{
    if (!tex || !tileset_name || tileset_name[0] == '\0') return false;

    char path[256];
    snprintf(path, sizeof(path), "gfx/tiles/%s", tileset_name);
    if (cs2d_texture_load_grid(tex, path, 2, 32, 32)) return true;

    snprintf(path, sizeof(path), "gfx/tiles/%s.bmp", tileset_name);
    if (cs2d_texture_load_grid(tex, path, 2, 32, 32)) return true;

    snprintf(path, sizeof(path), "gfx/tiles/%s.png", tileset_name);
    if (cs2d_texture_load_grid(tex, path, 2, 32, 32)) return true;

    return false;
}

void cs2d_texture_free(cs2d_texture_t *tex)
{
    if (tex && tex->id) {
        GLuint tid = tex->id;
        glDeleteTextures(1, &tid);
        tex->id = 0;
    }
}

/* Helper to derive gfx/weapons/<slug>.bmp from weapon ID & name (reconstructed from 0x1401d9210) */
static void weapon_id_to_slug(uint8_t id, const char *name, char *out_slug, size_t max_len)
{
    if (id == 57 || id == 58 || id == 80) {
        snprintf(out_slug, max_len, "armor");
        return;
    }
    if (id == 64) {
        snprintf(out_slug, max_len, "medikit");
        return;
    }
    if (!name || !name[0]) {
        out_slug[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j + 1 < max_len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c)) {
            out_slug[j++] = (char)tolower(c);
        }
    }
    out_slug[j] = '\0';
}

bool cs2d_gfx_bank_init(cs2d_gfx_bank_t *bank)
{
    if (!bank) return false;
    if (bank->initialized && (g_gl_ctx == NULL || bank->tex_ct[0].id != 0)) return true;
    memset(bank, 0, sizeof(*bank));

    cs2d_weapons_init();

    /* 1. Player Skins (64x96 -> 6 frames of 32x32, maskMode = 2, padded 32x32 cell atlas) */
    for (int i = 0; i < 4; i++) {
        char path[64];
        snprintf(path, sizeof(path), "gfx/player/t%d.bmp", i + 1);
        cs2d_texture_load_grid(&bank->tex_t[i], path, 2, 32, 32);
        snprintf(path, sizeof(path), "gfx/player/ct%d.bmp", i + 1);
        cs2d_texture_load_grid(&bank->tex_ct[i], path, 2, 32, 32);
    }
    cs2d_texture_load_grid(&bank->tex_vip,       "gfx/player/vip.bmp",      2, 32, 32);
    cs2d_texture_load_grid(&bank->tex_zombie,    "gfx/player/zombie.bmp",   2, 32, 32);
    cs2d_texture_load_grid(&bank->tex_legs,      "gfx/player/legs.bmp",     2, 32, 32);
    cs2d_texture_load_grid(&bank->tex_hostages,  "gfx/player/hostages.bmp", 2, 32, 32);
    cs2d_texture_load_grid(&bank->tex_equipment, "gfx/equipment.bmp",       2, 32, 32);

    /* 2. Weapons (held .bmp maskMode=2, dropped _d.bmp maskMode=2, killfeed _k.bmp maskMode=3) */
    for (int id = 1; id < CS2D_MAX_WEAPONS; id++) {
        const cs2d_weapon_def_t *def = cs2d_weapon_get((uint8_t)id);
        if (!def || !def->name) continue;

        char slug[64];
        weapon_id_to_slug((uint8_t)id, def->name, slug, sizeof(slug));
        if (!slug[0]) continue;

        char path[128];
        snprintf(path, sizeof(path), "gfx/weapons/%s.bmp", slug);
        cs2d_texture_load_ex(&bank->tex_weapons[id], path, 2);

        snprintf(path, sizeof(path), "gfx/weapons/%s_d.bmp", slug);
        if (!cs2d_texture_load_ex(&bank->tex_weapons_d[id], path, 2)) {
            /* Fallback to held sprite if _d.bmp does not exist (e.g. flags, medikit, ammo) */
            snprintf(path, sizeof(path), "gfx/weapons/%s.bmp", slug);
            cs2d_texture_load_ex(&bank->tex_weapons_d[id], path, 2);
        }

        snprintf(path, sizeof(path), "gfx/weapons/%s_k.bmp", slug);
        cs2d_texture_load_ex(&bank->tex_weapons_k[id], path, 3);
    }

    cs2d_texture_load_ex(&bank->tex_reload,   "gfx/weapons/reload.bmp",   2);
    cs2d_texture_load_ex(&bank->tex_silencer, "gfx/weapons/silencer.bmp", 2);
    cs2d_texture_load_ex(&bank->tex_m4a1s,    "gfx/weapons/m4a1s.bmp",    2);
    cs2d_texture_load_ex(&bank->tex_redflag,  "gfx/weapons/redflag.bmp",  2);
    cs2d_texture_load_ex(&bank->tex_blueflag, "gfx/weapons/blueflag.bmp", 2);
    cs2d_texture_load_ex(&bank->tex_noflag,   "gfx/weapons/noflag.bmp",   2);

    /* 3. Map & Entity Sprites (including embedded IncBin assets extracted from CS2D.exe) */
    cs2d_texture_load_grid(&bank->tex_shadowmap, "gfx/shadowmap.bmp",       3, 32, 32);
    cs2d_texture_load_grid(&bank->tex_decals,    "gfx/decals.bmp",          3, 32, 32);
    cs2d_texture_load_grid(&bank->tex_buildings, "gfx/buildings.png",       2, 32, 32);
    cs2d_texture_load_ex(&bank->tex_turret[0], "gfx/weapons/turret.bmp",  2);
    cs2d_texture_load_ex(&bank->tex_turret[1], "gfx/weapons/turret2.bmp", 2);
    cs2d_texture_load_ex(&bank->tex_turret[2], "gfx/weapons/turret3.bmp", 2);
    cs2d_texture_load_ex(&bank->tex_buildhelper, "gfx/hud_buildhelper.png", 1);
    if (!cs2d_texture_load_ex(&bank->tex_palms, "incbindata/palmleave.png", 2)) {
        if (!cs2d_texture_load_ex(&bank->tex_palms, "gfx/incbindata/palmleave.png", 2)) {
            cs2d_texture_load_ex(&bank->tex_palms, "gfx/sprites/palm.png", 2);
        }
    }
    if (!cs2d_texture_load_ex(&bank->tex_tree, "incbindata/treeleaves.png", 2)) {
        if (!cs2d_texture_load_ex(&bank->tex_tree, "gfx/incbindata/treeleaves.png", 2)) {
            cs2d_texture_load_ex(&bank->tex_tree, "gfx/sprites/tree01.bmp", 3);
        }
    }
    if (!cs2d_texture_load_ex(&bank->tex_leaf, "incbindata/shadow.bmp", 3)) {
        cs2d_texture_load_ex(&bank->tex_leaf, "gfx/shadow.bmp", 3);
    }
    if (!cs2d_texture_load_grid(&bank->tex_lamp1, "incbindata/objects.bmp", 2, 32, 32)) {
        if (!cs2d_texture_load_grid(&bank->tex_lamp1, "gfx/incbindata/objects.bmp", 2, 32, 32)) {
            cs2d_texture_load_ex(&bank->tex_lamp1, "gfx/sprites/lamp1.bmp", 2);
        }
    }
    cs2d_texture_load_ex(&bank->tex_lamp2,     "gfx/sprites/lamp2.bmp",    2);
    cs2d_texture_load_ex(&bank->tex_flare1,    "gfx/sprites/flare1.bmp",   3);
    cs2d_texture_load_ex(&bank->tex_flare3,    "gfx/sprites/flare3.bmp",   3);
    cs2d_texture_load_grid(&bank->tex_fragments, "gfx/fragments.bmp",     3, 32, 32);
    cs2d_texture_load_grid(&bank->tex_explosion, "gfx/explosion.png",     1, 64, 64);
    if (!cs2d_texture_load_ex(&bank->tex_flare_x, "incbindata/flare_x.png", 1)) {
        cs2d_texture_load_ex(&bank->tex_flare_x, "gfx/incbindata/flare_x.png", 1);
    }
    if (!cs2d_texture_load_ex(&bank->tex_glowdot, "incbindata/glowdot.bmp", 3)) {
        cs2d_texture_load_ex(&bank->tex_glowdot, "gfx/incbindata/glowdot.bmp", 3);
    }
    if (!cs2d_texture_load_grid(&bank->tex_shells, "gfx/shells.bmp", 2, 8, 8)) {
        cs2d_texture_load_grid(&bank->tex_shells, "gfx/shells.png", 2, 8, 8);
    }
    cs2d_texture_load_ex(&bank->tex_knifeslash, "gfx/knifeslash.bmp",      3);
    cs2d_texture_load_ex(&bank->tex_bspot_a,   "gfx/sprites/bspot_a.png",  2);
    cs2d_texture_load_ex(&bank->tex_bspot_b,   "gfx/sprites/bspot_b.png",  2);

    cs2d_texture_load_grid(&bank->tex_npc[0], "gfx/npc/zombie.bmp",     2, 32, 32);
    cs2d_texture_load_ex(&bank->tex_npc[1],   "gfx/npc/headcrab.bmp",   2);
    cs2d_texture_load_ex(&bank->tex_npc[2],   "gfx/npc/snark.bmp",      2);
    cs2d_texture_load_ex(&bank->tex_npc[3],   "gfx/npc/vortigaunt.bmp", 2);
    cs2d_texture_load_grid(&bank->tex_npc[4], "gfx/npc/soldier.bmp",    2, 32, 32);

    /* 4. HUD & Crosshair Assets (exact maskMode and padded cell grids) */
    cs2d_texture_load_grid(&bank->tex_hud_nums,        "gfx/hud_nums.bmp",           1, 48, 66);
    cs2d_texture_load_grid(&bank->tex_hud_symbols,     "gfx/hud_symbols.bmp",        1, 64, 64);
    cs2d_texture_load_ex(&bank->tex_hud_radar,         "gfx/hud_radar.bmp",          1);
    cs2d_texture_load_ex(&bank->tex_hud_radar_adv,     "gfx/hud_radar_advanced.png", 2);
    cs2d_texture_load_ex(&bank->tex_hud_radar_ring,    "gfx/hud_radar_ring.png",     2);
    cs2d_texture_load_grid(&bank->tex_hud_radar_icons, "gfx/hud_radar_icons.png",    2, 32, 32);
    cs2d_texture_load_ex(&bank->tex_hud_slot,          "gfx/hud_slot.bmp",           0);
    cs2d_texture_load_grid(&bank->tex_hud_slotheads,   "gfx/hud_slotheads.bmp",      1, 10, 10);
    cs2d_texture_load_grid(&bank->tex_pointer,         "gfx/pointer.bmp",            2, 46, 46);

    bank->initialized = (g_gl_ctx != NULL);
    return true;
}

void cs2d_gfx_bank_free(cs2d_gfx_bank_t *bank)
{
    if (!bank) return;

    for (int i = 0; i < 4; i++) {
        cs2d_texture_free(&bank->tex_t[i]);
        cs2d_texture_free(&bank->tex_ct[i]);
    }
    cs2d_texture_free(&bank->tex_vip);
    cs2d_texture_free(&bank->tex_zombie);
    cs2d_texture_free(&bank->tex_legs);
    cs2d_texture_free(&bank->tex_hostages);
    cs2d_texture_free(&bank->tex_equipment);

    for (int i = 0; i < CS2D_MAX_WEAPONS; i++) {
        cs2d_texture_free(&bank->tex_weapons[i]);
        cs2d_texture_free(&bank->tex_weapons_d[i]);
        cs2d_texture_free(&bank->tex_weapons_k[i]);
    }
    cs2d_texture_free(&bank->tex_reload);
    cs2d_texture_free(&bank->tex_silencer);
    cs2d_texture_free(&bank->tex_m4a1s);
    cs2d_texture_free(&bank->tex_redflag);
    cs2d_texture_free(&bank->tex_blueflag);
    cs2d_texture_free(&bank->tex_noflag);

    cs2d_texture_free(&bank->tex_shadowmap);
    cs2d_texture_free(&bank->tex_decals);
    cs2d_texture_free(&bank->tex_buildings);
    for (int i = 0; i < 3; i++) cs2d_texture_free(&bank->tex_turret[i]);
    cs2d_texture_free(&bank->tex_buildhelper);
    cs2d_texture_free(&bank->tex_palms);
    cs2d_texture_free(&bank->tex_tree);
    cs2d_texture_free(&bank->tex_leaf);
    cs2d_texture_free(&bank->tex_lamp1);
    cs2d_texture_free(&bank->tex_lamp2);
    cs2d_texture_free(&bank->tex_flare1);
    cs2d_texture_free(&bank->tex_flare3);
    cs2d_texture_free(&bank->tex_fragments);
    cs2d_texture_free(&bank->tex_explosion);
    cs2d_texture_free(&bank->tex_flare_x);
    cs2d_texture_free(&bank->tex_glowdot);
    cs2d_texture_free(&bank->tex_shells);
    cs2d_texture_free(&bank->tex_knifeslash);
    cs2d_texture_free(&bank->tex_bspot_a);
    cs2d_texture_free(&bank->tex_bspot_b);
    for (int i = 0; i < 5; i++) {
        cs2d_texture_free(&bank->tex_npc[i]);
    }

    cs2d_texture_free(&bank->tex_hud_nums);
    cs2d_texture_free(&bank->tex_hud_symbols);
    cs2d_texture_free(&bank->tex_hud_radar);
    cs2d_texture_free(&bank->tex_hud_radar_adv);
    cs2d_texture_free(&bank->tex_hud_radar_ring);
    cs2d_texture_free(&bank->tex_hud_radar_icons);
    cs2d_texture_free(&bank->tex_hud_slot);
    cs2d_texture_free(&bank->tex_hud_slotheads);
    cs2d_texture_free(&bank->tex_pointer);
    cs2d_texture_free(&bank->tex_minimap);

    for (int i = 0; i < bank->sprite_cache_count; i++) {
        cs2d_texture_free(&bank->sprite_cache[i].tex);
    }
    memset(bank, 0, sizeof(*bank));
}

const cs2d_texture_t *cs2d_gfx_get_sprite(cs2d_gfx_bank_t *bank, const char *rel_path, bool black_mask)
{
    if (!bank || !rel_path || !rel_path[0]) return NULL;

    int mask_mode = black_mask ? 3 : 1;
    for (int i = 0; i < bank->sprite_cache_count; i++) {
        if (_stricmp(bank->sprite_cache[i].path, rel_path) == 0 &&
            bank->sprite_cache[i].mask_mode == mask_mode) {
            return bank->sprite_cache[i].tex.id ? &bank->sprite_cache[i].tex : NULL;
        }
    }

    if (bank->sprite_cache_count >= CS2D_MAX_SPRITE_CACHE) return NULL;

    cs2d_sprite_cache_entry_t *entry = &bank->sprite_cache[bank->sprite_cache_count++];
    snprintf(entry->path, sizeof(entry->path), "%s", rel_path);
    entry->mask_mode = mask_mode;

    if (!cs2d_texture_load_ex(&entry->tex, rel_path, mask_mode)) {
        char alt[160];
        snprintf(alt, sizeof(alt), "gfx/sprites/%s", rel_path);
        cs2d_texture_load_ex(&entry->tex, alt, mask_mode);
    }

    return entry->tex.id ? &entry->tex : NULL;
}

void cs2d_render_rect(float x, float y, float w, float h, float r, float g, float b, float a, bool fill)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(fill ? GL_QUADS : GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
    glEnd();
}

void cs2d_render_line(float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
    glEnd();
}

void cs2d_render_circle(float cx, float cy, float radius, float r, float g, float b, float a, bool fill)
{
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(fill ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (fill) glVertex2f(cx, cy);
    int segments = 24;
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * 3.1415926f * (float)i / (float)segments;
        glVertex2f(cx + cosf(theta) * radius, cy + sinf(theta) * radius);
    }
    glEnd();
}

void cs2d_render_sprite_frame(
    const cs2d_texture_t *tex,
    int cell_w, int cell_h, int frame,
    float x, float y,
    float draw_w, float draw_h,
    float angle_deg,
    float r, float g, float b, float a,
    int blend_mode
)
{
    if (!tex || !tex->id || tex->width <= 0 || tex->height <= 0) {
        return;
    }

    if (cell_w <= 0) cell_w = tex->width;
    if (cell_h <= 0) cell_h = tex->height;

    int cols = tex->width / cell_w;
    if (cols < 1) cols = 1;
    if (frame < 0) frame = 0;

    int col = frame % cols;
    int row = frame / cols;

    float u0, v0, u1, v1;
    if (tex->pad > 0 && tex->cell_w == cell_w && tex->cell_h == cell_h && tex->gpu_w > 0 && tex->gpu_h > 0) {
        int stride_x = cell_w + tex->pad * 2;
        int stride_y = cell_h + tex->pad * 2;
        u0 = (float)(col * stride_x + tex->pad) / (float)tex->gpu_w;
        v0 = (float)(row * stride_y + tex->pad) / (float)tex->gpu_h;
        u1 = (float)(col * stride_x + tex->pad + cell_w) / (float)tex->gpu_w;
        v1 = (float)(row * stride_y + tex->pad + cell_h) / (float)tex->gpu_h;
    } else {
        u0 = (float)(col * cell_w) / (float)tex->width;
        v0 = (float)(row * cell_h) / (float)tex->height;
        u1 = (float)((col + 1) * cell_w) / (float)tex->width;
        v1 = (float)((row + 1) * cell_h) / (float)tex->height;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex->id);

    /* CS2D Blend Modes: 3/4 = Light/Additive, 5/6 = Shade, 0 = Alpha */
    if (blend_mode == 3 || blend_mode == 4) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glColor4f(r, g, b, a);

    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    if (angle_deg != 0.0f) {
        glRotatef(angle_deg, 0.0f, 0.0f, 1.0f);
    }

    float half_w = draw_w * 0.5f;
    float half_h = draw_h * 0.5f;

    glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(-half_w, -half_h);
        glTexCoord2f(u1, v0); glVertex2f( half_w, -half_h);
        glTexCoord2f(u1, v1); glVertex2f( half_w,  half_h);
        glTexCoord2f(u0, v1); glVertex2f(-half_w,  half_h);
    glEnd();

    glPopMatrix();

    if (blend_mode == 3 || blend_mode == 4) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void cs2d_render_sprite(
    const cs2d_texture_t *tex,
    float x, float y,
    float w, float h,
    float angle_deg,
    float r, float g, float b, float a
)
{
    if (!tex || !tex->id) {
        cs2d_render_rect(x - w * 0.5f, y - h * 0.5f, w, h, r, g, b, a, true);
        return;
    }
    cs2d_render_sprite_frame(tex, tex->width, tex->height, 0, x, y, w, h, angle_deg, r, g, b, a, 0);
}

/*
 * Reconstructed from Tpl.getFrame (FUN_140002e20..0x140002fc5 in CS2D.exe):
 * Selects which of the 6 cells (32x32, 2 cols x 3 rows) in gfx/player/ct1..4.bmp or t1..4.bmp to draw:
 *   Frame 0/1: Knife / Grenade / Bomb / Unarmed / Reloading stance (0=right-handed, 1=left-handed)
 *   Frame 2:   Claw / Zombie dual-hand stance (weapon 78)
 *   Frame 3:   Pistol / Two-handed centered stance (weapons 1..9 or melee slash)
 *   Frame 4/5: Rifle / Shotgun / SMG / Heavy two-handed weapon stance (4=right-handed, 5=left-handed)
 */
int cs2d_player_get_frame(const cs2d_player_t *player, bool lefthanded)
{
    int lh = lefthanded ? 1 : 0;
    if (!player) return lh;

    uint8_t w = player->current_weapon;
    if (w == 0) return lh;

    int frame = lh;
    if (w <= 9) {
        frame = 3; /* Pistols: USP, Glock, Deagle, P228, Elite, Five-Seven */
    } else if (w == 78) {
        frame = 2; /* Claw */
    } else if (w > 49 && w <= 89) {
        frame = lh; /* Knife (50), Grenades (51..54), Bomb (55), Defuse (56), Machete (69), Wrench (74), Mine (77), Chainsaw (85) */
    } else {
        frame = 4 + lh; /* Shotguns (10..11), SMGs (20..24), Rifles (30..39), M249 (40), Shield (41), Laser (45), RPG (47..49), Portal (90..91) */
    }

    if (player->is_reloading || player->reload_timer > 0.0f) {
        frame = lh;
    }

    /* Bomb planting animation (0x140002ee4..0x140002fc4): alternates frames 3 and 4 */
    if (w == CS2D_WEP_BOMB && player->is_planting) {
        int step = (int)(player->plant_progress * 6.0f);
        frame = (step & 1) ? 4 : 3;
    }

    return frame;
}

/*
 * Reconstructed from Tpl.draw (FUN_140121160..0x140122b20 in CS2D.exe):
 *   1. Visual recoil backward kickback offset: sx -= sin(rad)*recoil, sy += cos(rad)*recoil (0x14012131c..0x1401213cc)
 *   2. Walking legs (gfx/player/legs.bmp, 8 frames of 32x32) offset 5.0px behind torso (0x140122a26..0x140122af8)
 *   3. Player torso skin (ct1..4.bmp, t1..4.bmp, vip.bmp, 6 frames of 32x32) rotated by draw_angle = angle + 6*slash_timer (0x140121463)
 *   4. Special armor overlay (Tpl.getArmorImage 0x1400320a0) if armor in 201..205 (0x140121596)
 *   5. Held weapon sprite (gfx/weapons/<wep>.bmp) along draw_angle at 16.5px (guns, 0x14012168e),
 *      13.5px (melee/bomb, 0x1401229b5), or 12.5px (grenades, 0x1401227ab) + silencer overlay (0x1401217e4)
 *   6. Equipment overlay (gfx/equipment.bmp, 3 frames of 32x32: 0=bomb/defuse at angle-10, 1=NVG, 2=gasmask) (0x1401218b0..0x140121f19)
 */
void cs2d_render_player(
    const cs2d_player_t  *player,
    const cs2d_texture_t *skin_tex,
    const cs2d_texture_t *wep_tex,
    float cam_x, float cam_y
)
{
    if (!player || !player->alive) return;

    cam_x = roundf(cam_x);
    cam_y = roundf(cam_y);

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();

    float rad = player->angle * (3.14159265f / 180.0f);
    float sin_a = sinf(rad);
    float cos_a = cosf(rad);

    /* Rendered torso & held weapon angle: angle + 6.0f * slash_timer during melee swing
     * (Tpl.updateAll 0x14021d68a..0x14021d6d2, Tpl.draw 0x14012146b & 0x14012299b) */
    float draw_ang = player->angle + ((player->slash_timer > 0.0f) ? (6.0f * player->slash_timer) : 0.0f);
    float draw_rad = draw_ang * (3.14159265f / 180.0f);
    float sin_d = sinf(draw_rad);
    float cos_d = cosf(draw_rad);

    /* Recoil visual backward kickback (0x14012131c..0x1401213cc) */
    float recoil_px = (player->recoil > 0.0f) ? player->recoil : 0.0f;
    float sx = (player->pos.x - cam_x) - sin_a * recoil_px;
    float sy = (player->pos.y - cam_y) + cos_a * recoil_px;

    /* 0. Player Ground Drop Shadow (gfx/shadow.bmp, maskMode=3) */
    if (bank && bank->tex_leaf.id) {
        cs2d_render_sprite_frame(&bank->tex_leaf, bank->tex_leaf.width, bank->tex_leaf.height, 0,
                                 sx + 2.0f, sy + 2.0f, 32.0f, 32.0f, 0.0f,
                                 0.0f, 0.0f, 0.0f, 0.35f, 0);
    }

    /* 1. Walking Legs (gfx/player/legs.bmp: 128x64 -> 8 frames of 32x32, offset 5px behind player) */
    bool moving = (player->feet_timer > 0.0f) ||
                  (fabsf(player->vel.x) > 1.0f || fabsf(player->vel.y) > 1.0f);
    if (moving && bank && bank->tex_legs.id) {
        int leg_frame = ((int)player->feet_frame) & 7;
        float leg_ang = (player->feet_timer > 0.0f) ? player->feet_angle : player->angle;
        float lx = sx - sin_a * 5.0f;
        float ly = sy + cos_a * 5.0f;
        cs2d_render_sprite_frame(&bank->tex_legs, 32, 32, leg_frame,
                                 lx, ly, 32.0f, 32.0f, leg_ang,
                                 1.0f, 1.0f, 1.0f, 1.0f, 0);
    }

    /* 2. Resolve Player Skin Texture (ct1..4, t1..4, vip) */
    const cs2d_texture_t *active_skin = skin_tex;
    uint8_t look_idx = player->look & 3;
    if (bank && bank->initialized) {
        if (player->is_vip || player->team == CS2D_TEAM_VIP) {
            if (bank->tex_vip.id) active_skin = &bank->tex_vip;
        } else if (player->team == CS2D_TEAM_CT) {
            if (bank->tex_ct[look_idx].id) active_skin = &bank->tex_ct[look_idx];
        } else if (player->team == CS2D_TEAM_T) {
            if (bank->tex_t[look_idx].id) active_skin = &bank->tex_t[look_idx];
        }
    }

    int body_frame = cs2d_player_get_frame(player, false);

    if (active_skin && active_skin->id) {
        /* Slice exact 32x32 frame from 64x96 player sheet (2 cols x 3 rows) */
        int cell_w = (active_skin->width >= 64) ? (active_skin->width / 2) : active_skin->width;
        int cell_h = (active_skin->height >= 96) ? (active_skin->height / 3) : active_skin->height;
        cs2d_render_sprite_frame(active_skin, cell_w, cell_h, body_frame,
                                 sx, sy, 32.0f, 32.0f, draw_ang,
                                 1.0f, 1.0f, 1.0f, 1.0f, 0);
    }

    /* 3. Special Armor Overlay (Tpl.getArmorImage 0x1400320a0: armor 201..205 -> weapon IDs 79..83) */
    if (bank && player->armor >= 201 && player->armor <= 205) {
        int arm_wep_id = 79 + (player->armor - 201);
        const cs2d_texture_t *arm_tex = &bank->tex_weapons[arm_wep_id];
        if (arm_tex->id) {
            cs2d_render_sprite(arm_tex, sx, sy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    /* 4. Held Weapon in Hands (0x1401215ee..0x1401218a0 & 0x140122791..0x140122a20) */
    const cs2d_texture_t *active_wep = wep_tex;
    if (bank && player->current_weapon > 0 && player->current_weapon < CS2D_MAX_WEAPONS) {
        if (bank->tex_weapons[player->current_weapon].id) {
            active_wep = &bank->tex_weapons[player->current_weapon];
        }
    }

    if ((player->is_reloading || player->reload_timer > 0.0f) && bank && bank->tex_reload.id) {
        /* Reloading sprite at 13.5px in front of player (0x140122983) */
        float wx = sx + sin_d * 13.5f;
        float wy = sy - cos_d * 13.5f;
        cs2d_render_sprite(&bank->tex_reload, wx, wy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
    } else if (active_wep && active_wep->id) {
        /* Held weapon distance along draw_angle: 16.5px for firearms (0x14012168e),
         * 12.5px for grenades (0x1401227ab), 13.5px for melee/bomb (0x1401229b5) */
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get(player->current_weapon);
        float wep_dist = 16.5f;
        if (wdef) {
            if (wdef->slot == CS2D_SLOT_GRENADE) {
                wep_dist = 12.5f;
            } else if (wdef->slot == CS2D_SLOT_MELEE || wdef->slot == CS2D_SLOT_BOMB) {
                wep_dist = 13.5f;
            }
        }
        float wx = sx + sin_d * wep_dist;
        float wy = sy - cos_d * wep_dist;
        cs2d_render_sprite(active_wep, wx, wy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);

        /* Silencer attachment overlay for USP (1) and M4A1 (32) when zoom_mode == 3 (0x1401217e4) */
        if (bank && player->zoom_mode == 3) {
            if (player->current_weapon == CS2D_WEP_USP && bank->tex_silencer.id) {
                cs2d_render_sprite(&bank->tex_silencer, wx, wy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
            } else if (player->current_weapon == CS2D_WEP_M4A1 && bank->tex_m4a1s.id) {
                cs2d_render_sprite(&bank->tex_m4a1s, wx, wy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
    }

    /* 5. Equipment Overlays (gfx/equipment.bmp: 96x32 -> 3 frames of 32x32) */
    if (bank && bank->tex_equipment.id) {
        /* Frame 0: Bomb pack or Defuse kit on back at angle - 10 deg (0x140121b48) */
        if (player->inventory[CS2D_SLOT_BOMB] == CS2D_WEP_BOMB || player->has_defuse_kit) {
            cs2d_render_sprite_frame(&bank->tex_equipment, 32, 32, 0,
                                     sx, sy, 32.0f, 32.0f, player->angle - 10.0f,
                                     1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
        /* Frame 1: Nightvision goggles */
        if (player->has_nightvision) {
            cs2d_render_sprite_frame(&bank->tex_equipment, 32, 32, 1,
                                     sx, sy, 32.0f, 32.0f, player->angle,
                                     1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
        /* Frame 2: Gasmask (0x140121e88) */
        if (player->has_gasmask) {
            cs2d_render_sprite_frame(&bank->tex_equipment, 32, 32, 2,
                                     sx, sy, 32.0f, 32.0f, player->angle,
                                     1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
    }
}

/*
 * Draws a single 32x32 tile frame from the padded tileset atlas using
 * exact axis-aligned vertex positions and rotated corner UVs (0x140010450 & 0x14013fd30).
 */
static void draw_tile_frame(
    const cs2d_texture_t *tileset_tex,
    int tile_src, int cols,
    uint8_t frame_idx, uint8_t mod,
    float rx, float ry,
    float alpha,
    const uint8_t *rgb    /* Tile color (modifier 0x80) or NULL for white */
)
{
    int col = (int)frame_idx % cols;
    int row = (int)frame_idx / cols;

    float u0, v0, u1, v1;
    if (tileset_tex->pad > 0 && tileset_tex->cell_w == tile_src && tileset_tex->cell_h == tile_src &&
        tileset_tex->gpu_w > 0 && tileset_tex->gpu_h > 0) {
        int stride_x = tile_src + tileset_tex->pad * 2;
        int stride_y = tile_src + tileset_tex->pad * 2;
        u0 = (float)(col * stride_x + tileset_tex->pad) / (float)tileset_tex->gpu_w;
        v0 = (float)(row * stride_y + tileset_tex->pad) / (float)tileset_tex->gpu_h;
        u1 = (float)(col * stride_x + tileset_tex->pad + tile_src) / (float)tileset_tex->gpu_w;
        v1 = (float)(row * stride_y + tileset_tex->pad + tile_src) / (float)tileset_tex->gpu_h;
    } else {
        u0 = (float)(col * tile_src) / (float)tileset_tex->width;
        v0 = (float)(row * tile_src) / (float)tileset_tex->height;
        u1 = (float)((col + 1) * tile_src) / (float)tileset_tex->width;
        v1 = (float)((row + 1) * tile_src) / (float)tileset_tex->height;
    }

    /* TileProp.Get (0x140010450): rotation in bits 0-1, color (r, g, b) x brightness b * 0.1 (b = 0: 100 %) */
    uint8_t rot_mode = mod & 3;
    uint8_t bmod = (mod >> 2) & 0x0F;
    float shade = (bmod > 0) ? (float)bmod * 0.1f : 1.0f;
    float cr = rgb ? (float)rgb[0] / 255.0f : 1.0f;
    float cg = rgb ? (float)rgb[1] / 255.0f : 1.0f;
    float cb = rgb ? (float)rgb[2] / 255.0f : 1.0f;

    /* Corner UVs for [TL, TR, BR, BL] rotated clockwise by 0, 90, 180, or 270 degrees */
    float tu[4], tv[4];
    if (rot_mode == 1) {
        /* 90 deg clockwise */
        tu[0] = u0; tv[0] = v1;
        tu[1] = u0; tv[1] = v0;
        tu[2] = u1; tv[2] = v0;
        tu[3] = u1; tv[3] = v1;
    } else if (rot_mode == 2) {
        /* 180 deg */
        tu[0] = u1; tv[0] = v1;
        tu[1] = u0; tv[1] = v1;
        tu[2] = u0; tv[2] = v0;
        tu[3] = u1; tv[3] = v0;
    } else if (rot_mode == 3) {
        /* 270 deg clockwise */
        tu[0] = u1; tv[0] = v0;
        tu[1] = u1; tv[1] = v1;
        tu[2] = u0; tv[2] = v1;
        tu[3] = u0; tv[3] = v0;
    } else {
        /* 0 deg */
        tu[0] = u0; tv[0] = v0;
        tu[1] = u1; tv[1] = v0;
        tu[2] = u1; tv[2] = v1;
        tu[3] = u0; tv[3] = v1;
    }

    float x1 = rx + (float)CS2D_TILE_SIZE;
    float y1 = ry + (float)CS2D_TILE_SIZE;

    glColor4f(cr * shade, cg * shade, cb * shade, alpha);
    glBegin(GL_QUADS);
        glTexCoord2f(tu[0], tv[0]); glVertex2f(rx, ry);
        glTexCoord2f(tu[1], tv[1]); glVertex2f(x1, ry);
        glTexCoord2f(tu[2], tv[2]); glVertex2f(x1, y1);
        glTexCoord2f(tu[3], tv[3]); glVertex2f(rx, y1);
    glEnd();
}

void cs2d_render_tile_frame(const cs2d_texture_t *tileset_tex, uint8_t frame, uint8_t mod, const uint8_t *rgb,
                            float x, float y, float size, float tint, float alpha)
{
    if (!tileset_tex || tileset_tex->id == 0 || tileset_tex->width < CS2D_TILE_SIZE) return;
    int cols = tileset_tex->width / CS2D_TILE_SIZE;
    uint8_t color[3];
    for (int i = 0; i < 3; i++) color[i] = (uint8_t)((float)(rgb ? rgb[i] : 255) * tint);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tileset_tex->id);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(size / (float)CS2D_TILE_SIZE, size / (float)CS2D_TILE_SIZE, 1.0f);
    draw_tile_frame(tileset_tex, CS2D_TILE_SIZE, cols, frame, mod, 0.0f, 0.0f, alpha, color);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

/* Cached unified sun-angle & multi-layer room lighting/shadow texture (reconstructed from 0x140260330 & 0x14025e420) */
static cs2d_texture_t s_room_shadow_tex = {0};
static uint32_t       s_room_shadow_map_w = 0;
static uint32_t       s_room_shadow_map_h = 0;
static uint32_t       s_room_shadow_solid_version = 0; /* Rebuild when a DynWall / Breakable toggles */
static bool           s_shadows_enabled = true;

/*
 * Reconstructed from 0x14025e420 (cast_shadow_volume) + lighting/gfx_penumbra.png & lighting/gfx_blur.png:
 * Rasterizes the 6-vertex extruded shadow volume of rectangle [x0..x1, y0..y1] swept by (dx, dy)
 * at scale = 16 texels per 32x32 tile (2.0 world pixels per texel), with a soft penumbra ramp.
 */
static void cast_shadow_rect(
    uint8_t *alpha_buf,
    int tex_w, int tex_h,
    float x0, float y0, float x1, float y1,
    float dx, float dy,
    uint8_t target_alpha
)
{
    const float inv_scale = 2.0f; /* 32.0f / 16.0f = 2 world pixels per texel */

    float min_x = (dx >= 0.0f ? x0 : x0 + dx);
    float max_x = (dx >= 0.0f ? x1 + dx : x1);
    float min_y = (dy >= 0.0f ? y0 : y0 + dy);
    float max_y = (dy >= 0.0f ? y1 + dy : y1);

    int sx0 = (int)(min_x / inv_scale);
    int sy0 = (int)(min_y / inv_scale);
    int sx1 = (int)(max_x / inv_scale) + 1;
    int sy1 = (int)(max_y / inv_scale) + 1;

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > tex_w) sx1 = tex_w;
    if (sy1 > tex_h) sy1 = tex_h;

    for (int sy = sy0; sy < sy1; sy++) {
        float py = ((float)sy + 0.5f) * inv_scale;
        float ty_min, ty_max;
        if (dy > 0.0001f) {
            ty_min = (py - y1) / dy;
            ty_max = (py - y0) / dy;
        } else if (dy < -0.0001f) {
            ty_min = (py - y0) / dy;
            ty_max = (py - y1) / dy;
        } else {
            ty_min = (py >= y0 && py <= y1) ? 0.0f : 2.0f;
            ty_max = (py >= y0 && py <= y1) ? 1.0f : -1.0f;
        }

        for (int sx = sx0; sx < sx1; sx++) {
            float px = ((float)sx + 0.5f) * inv_scale;
            float tx_min, tx_max;
            if (dx > 0.0001f) {
                tx_min = (px - x1) / dx;
                tx_max = (px - x0) / dx;
            } else if (dx < -0.0001f) {
                tx_min = (px - x0) / dx;
                tx_max = (px - x1) / dx;
            } else {
                tx_min = (px >= x0 && px <= x1) ? 0.0f : 2.0f;
                tx_max = (px >= x0 && px <= x1) ? 1.0f : -1.0f;
            }

            float t_lo = 0.0f;
            if (tx_min > t_lo) t_lo = tx_min;
            if (ty_min > t_lo) t_lo = ty_min;

            float t_hi = 1.0f;
            if (tx_max < t_hi) t_hi = tx_max;
            if (ty_max < t_hi) t_hi = ty_max;

            if (t_lo <= t_hi) {
                /*
                 * Reconstructed from 0x14025e420 (cast_shadow_volume):
                 * Extruded 6-vertex shadow polygons have flat uniform alpha (target_alpha).
                 * Soft penumbra at the outer boundary is provided by GL_LINEAR filtering at 2px/texel.
                 */
                if (target_alpha > alpha_buf[sy * tex_w + sx]) {
                    alpha_buf[sy * tex_w + sx] = target_alpha;
                }
            }
        }
    }
}

/* Clears shadow alpha to 0 inside world rectangle [x0..x1, y0..y1] */
static void clear_shadow_rect(
    uint8_t *alpha_buf,
    int tex_w, int tex_h,
    float x0, float y0, float x1, float y1
)
{
    const float inv_scale = 2.0f;
    int sx0 = (int)(x0 / inv_scale);
    int sy0 = (int)(y0 / inv_scale);
    int sx1 = (int)(x1 / inv_scale);
    int sy1 = (int)(y1 / inv_scale);

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > tex_w) sx1 = tex_w;
    if (sy1 > tex_h) sy1 = tex_h;

    for (int sy = sy0; sy < sy1; sy++) {
        memset(&alpha_buf[sy * tex_w + sx0], 0, (size_t)(sx1 - sx0));
    }
}

/*
 * Reconstructed from 0x14010bb08..0x14010e37f (lighting/gfx_oclussionmap.png):
 * Accumulates a soft (~10px) ambient occlusion halo around one Obstacle/Wall tile into occl_buf.
 * Halos are merged with max() rather than summed, so adjacent solid tiles form one continuous
 * halo instead of double-darkening the texels near their shared vertices.
 */
static void accumulate_tile_occlusion(
    uint8_t *occl_buf,
    int tex_w, int tex_h,
    float x0, float y0, float x1, float y1,
    float halo_px,
    uint8_t max_add
)
{
    const float inv_scale = 2.0f;
    int sx0 = (int)((x0 - halo_px) / inv_scale);
    int sy0 = (int)((y0 - halo_px) / inv_scale);
    int sx1 = (int)((x1 + halo_px) / inv_scale) + 1;
    int sy1 = (int)((y1 + halo_px) / inv_scale) + 1;

    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > tex_w) sx1 = tex_w;
    if (sy1 > tex_h) sy1 = tex_h;

    for (int sy = sy0; sy < sy1; sy++) {
        float py = ((float)sy + 0.5f) * inv_scale;
        float dy = 0.0f;
        if (py < y0) dy = y0 - py;
        else if (py > y1) dy = py - y1;

        for (int sx = sx0; sx < sx1; sx++) {
            float px = ((float)sx + 0.5f) * inv_scale;
            float dx = 0.0f;
            if (px < x0) dx = x0 - px;
            else if (px > x1) dx = px - x1;

            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 0.0f && dist < halo_px) {
                float f = 1.0f - (dist / halo_px);
                uint8_t add = (uint8_t)((float)max_add * f * f);
                if (add > occl_buf[sy * tex_w + sx]) {
                    occl_buf[sy * tex_w + sx] = add;
                }
            }
        }
    }
}

/*
 * Applies the merged occlusion halo (0x14010bb08..0x14010e37f) onto alpha_buf:
 * only inside existing shadow regions (alpha_buf > 0) and only onto floor tiles,
 * so sunlit outer wall edges and Wall/Obstacle tops stay un-darkened.
 */
static void apply_shadow_masked_occlusion(
    const cs2d_map_t *map,
    const uint8_t *eff_modes,
    uint8_t *alpha_buf,
    const uint8_t *occl_buf,
    int tex_w, int tex_h,
    uint8_t cap_alpha
)
{
    const float inv_scale = 2.0f;
    for (int sy = 0; sy < tex_h; sy++) {
        int ty = (int)(((float)sy + 0.5f) * inv_scale / (float)CS2D_TILE_SIZE);
        if ((uint32_t)ty >= map->height) continue;

        for (int sx = 0; sx < tex_w; sx++) {
            size_t idx = (size_t)sy * (size_t)tex_w + (size_t)sx;
            uint8_t add = occl_buf[idx];
            uint8_t cur = alpha_buf[idx];
            if (add == 0 || cur == 0) continue; /* Only darken inside existing shadow regions */

            int tx = (int)(((float)sx + 0.5f) * inv_scale / (float)CS2D_TILE_SIZE);
            if ((uint32_t)tx >= map->width) continue;

            /* 0x14010bb8c: Only apply occlusion onto floor tiles (skip Wall and Obstacle tops) */
            uint8_t m = eff_modes[(uint32_t)ty * map->width + (uint32_t)tx];
            if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW ||
                m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) {
                continue;
            }

            int val = (int)cur + (int)add;
            if (val > (int)cap_alpha) val = (int)cap_alpha;
            if (val > (int)cur) alpha_buf[idx] = (uint8_t)val;
        }
    }
}

/*
 * Reconstructed from 0x1401413b0..0x14014276d, 0x140260330..0x1402626d0, and 0x1401da4a6..0x1401da82d in CS2D.exe:
 * Builds the complete map shadow & lighting texture using:
 *   - Effective tile mode table (0x140a100a0): base tile mode overridden by active Func_DynWall
 *     (ints[1]: 0=Wall, 1=Obstacle, 2=Wall w/o Shdw, 3=Obstacle w/o Shdw, 4=Floor+Tile Behavior)
 *     and Env_Breakable (ints[6]: 0/1=Wall, 2=No shadow, 3=Obstacle)
 *   - Map sun angle (0x1401da4a6): if map->daylight_time > 999, (daylight_time - 1000) % 360, else 225.0f
 *   - Extrusion math (0x1402603a9..0x14026071c):
 *       s_factor = fabsf(sinf((sun_angle - 180.0f) * PI/180)) * 0.5f
 *       dz = -(h / 2) - h * s_factor
 *       dx = dz * cosf(sun_angle * PI/180), dy = dz * sinf(sun_angle * PI/180)
 *   - Step 1: Effective Obstacle tiles (CS2D_TILE_OBSTACLE) cast shadows, then Obstacle/Obstacle-NoShadow self-clear
 *   - Step 2: Effective Wall tiles (CS2D_TILE_WALL) cast shadows
 *   - Step 3: Env_Room Layer 0 (int_settings[9] == 0) casts shadows, followed by floor occlusion
 *             (lighting/gfx_oclussionmap.png) and effective Wall/Wall-NoShadow self-clear
 *   - Step 4: Env_Room Layers 1..5 (int_settings[9] == 1..5) cast rooftop shadows, then self-clear
 *   - Step 5: Env_Light (type 33) radial light subtraction (lighting/gfx_light.png)
 */
static void ensure_room_shadow_texture(const cs2d_map_t *map)
{
    if (!map || !map->tiles || map->width == 0 || map->height == 0 || !g_gl_ctx) return;
    if (s_room_shadow_tex.id != 0 &&
        s_room_shadow_map_w == map->width &&
        s_room_shadow_map_h == map->height &&
        s_room_shadow_solid_version == map->entity_solid_version) {
        return;
    }
    s_room_shadow_solid_version = map->entity_solid_version;

    if (s_room_shadow_tex.id != 0) {
        cs2d_texture_free(&s_room_shadow_tex);
    }

    /* 16 texels per 32x32 tile (2 world pixels per texel) */
    const int scale = 16;
    int tex_w = (int)map->width * scale;
    int tex_h = (int)map->height * scale;
    uint8_t *alpha_buf = (uint8_t *)calloc((size_t)tex_w * (size_t)tex_h, 1);
    if (!alpha_buf) return;

    size_t map_tiles = (size_t)map->width * (size_t)map->height;
    uint8_t  *eff_modes   = (uint8_t *)malloc(map_tiles);
    uint16_t *eff_heights = (uint16_t *)calloc(map_tiles, sizeof(uint16_t));
    if (!eff_modes || !eff_heights) {
        free(eff_modes);
        free(eff_heights);
        free(alpha_buf);
        return;
    }

    /* Base tile modes and custom heights (0x1401414b4..0x1401415c0) */
    for (size_t idx = 0; idx < map_tiles; idx++) {
        uint8_t t = map->tiles[idx];
        eff_modes[idx] = map->tile_modes[t];
        if (map->save_tile_heights != 0 && map->tile_heights[t] > 0) {
            eff_heights[idx] = map->tile_heights[t];
        }
    }

    /* Override effective tile modes with active Func_DynWall and Env_Breakable (0x140141cb4..0x14014276d) */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
        if (e->x < 0 || e->y < 0 || (uint32_t)e->x >= map->width || (uint32_t)e->y >= map->height) continue;
        size_t idx = (size_t)e->y * (size_t)map->width + (size_t)e->x;
        int tile_f = e->int_settings[0];
        if (e->type == CS2D_ENT_FUNC_DYNWALL) {
            switch (e->int_settings[1]) {
                case 0:  eff_modes[idx] = CS2D_TILE_WALL; break;
                case 1:  eff_modes[idx] = CS2D_TILE_OBSTACLE; break;
                case 2:  eff_modes[idx] = CS2D_TILE_WALL_NOSHADOW; break;
                case 3:  eff_modes[idx] = CS2D_TILE_OBSTACLE_NOSHADOW; break;
                default:
                    eff_modes[idx] = (tile_f >= 0 && tile_f <= (int)map->tile_count)
                                     ? map->tile_modes[tile_f] : CS2D_TILE_FLOOR_NOSOUND;
                    break;
            }
            eff_heights[idx] = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                               ? map->tile_heights[tile_f] : 0;
        } else if (e->type == CS2D_ENT_ENV_BREAKABLE) {
            if (e->int_settings[6] == 2) {
                eff_modes[idx] = CS2D_TILE_FLOOR_NOSOUND;
            } else if (e->int_settings[6] == 3) {
                eff_modes[idx] = CS2D_TILE_OBSTACLE;
            } else {
                eff_modes[idx] = CS2D_TILE_WALL;
            }
            eff_heights[idx] = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                               ? map->tile_heights[tile_f] : 0;
        }
    }

    /* Solid buildings (cs2d_dynobj_casts_shadow) shadow like a 32 px wall tile */
    if (map->object_shadow) {
        for (size_t idx = 0; idx < map_tiles; idx++) {
            if (!map->object_shadow[idx]) continue;
            eff_modes[idx] = CS2D_TILE_WALL;
            eff_heights[idx] = 0;
        }
    }

    const uint8_t shadow_alpha = 126; /* Base shadow darkness matching CS2D original (255 -> 128 on sandstone) */

    /* Reconstructed from 0x1401da4a6..0x1401da82d: custom angle stored as 1000 + deg (or already decoded 0..359) */
    float sun_angle = 225.0f;
    if (map->daylight_time > 999) {
        sun_angle = (float)((map->daylight_time - 1000) % 360);
    } else if (map->daylight_time >= 0 && map->daylight_time < 360) {
        sun_angle = (float)map->daylight_time;
    }
    float rad_sun  = sun_angle * (3.14159265f / 180.0f);
    float rad_180  = (sun_angle - 180.0f) * (3.14159265f / 180.0f);
    float s_factor = fabsf(sinf(rad_180)) * 0.5f;
    float cos_sun  = cosf(rad_sun);
    float sin_sun  = sinf(rad_sun);

    /* Step 1: Effective Obstacle Tiles (CS2D_TILE_OBSTACLE = 2) cast shadows, then clear own 32x32 tops */
    for (uint32_t ty = 0; ty < map->height; ty++) {
        for (uint32_t tx = 0; tx < map->width; tx++) {
            size_t idx = (size_t)ty * (size_t)map->width + (size_t)tx;
            if (eff_modes[idx] == CS2D_TILE_OBSTACLE) {
                int h = (eff_heights[idx] > 0) ? (int)eff_heights[idx] : 16;
                float dz = (float)(-(h / 2)) - (float)h * s_factor;
                float dx = dz * cos_sun;
                float dy = dz * sin_sun;
                float x0 = (float)(tx * CS2D_TILE_SIZE);
                float y0 = (float)(ty * CS2D_TILE_SIZE);
                cast_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, dx, dy, shadow_alpha);
            }
        }
    }
    for (uint32_t ty = 0; ty < map->height; ty++) {
        for (uint32_t tx = 0; tx < map->width; tx++) {
            uint8_t m = eff_modes[(size_t)ty * (size_t)map->width + (size_t)tx];
            if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) {
                float x0 = (float)(tx * CS2D_TILE_SIZE);
                float y0 = (float)(ty * CS2D_TILE_SIZE);
                clear_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f);
            }
        }
    }

    /* Step 2: Effective Wall Tiles (CS2D_TILE_WALL = 1, including Wall-mode Func_DynWall & Env_Breakable) cast shadows */
    for (uint32_t ty = 0; ty < map->height; ty++) {
        for (uint32_t tx = 0; tx < map->width; tx++) {
            size_t idx = (size_t)ty * (size_t)map->width + (size_t)tx;
            if (eff_modes[idx] == CS2D_TILE_WALL) {
                int h = (eff_heights[idx] > 0) ? (int)eff_heights[idx] : 32;
                float dz = (float)(-(h / 2)) - (float)h * s_factor;
                float dx = dz * cos_sun;
                float dy = dz * sin_sun;
                float x0 = (float)(tx * CS2D_TILE_SIZE);
                float y0 = (float)(ty * CS2D_TILE_SIZE);
                cast_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, dx, dy, shadow_alpha);
            }
        }
    }

    /* Step 3: Env_Room Layer 0 (0x140261153..0x140261438), then occlusion & clear Wall tiles */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        if (ent->type != CS2D_ENT_ENV_ROOM && ent->type != CS2D_ENT_ENV_CUBE3D && ent->type != CS2D_ENT_ENV_BUILDING) {
            continue;
        }
        int w_tiles = ent->int_settings[0];
        int h_tiles = ent->int_settings[1];
        int z_px    = ent->int_settings[2];
        if (w_tiles <= 0 || h_tiles <= 0 || z_px <= 0) continue;
        if (ent->type == CS2D_ENT_ENV_ROOM && ent->int_settings[7] == 0) continue;
        if (ent->int_settings[9] != 0) continue;

        int x_off = ent->str_settings[0][0] ? atoi(ent->str_settings[0]) : 0;
        int y_off = ent->str_settings[1][0] ? atoi(ent->str_settings[1]) : 0;
        float x0 = (float)(ent->x * CS2D_TILE_SIZE + x_off);
        float y0 = (float)(ent->y * CS2D_TILE_SIZE + y_off);
        float x1 = x0 + (float)(w_tiles * CS2D_TILE_SIZE);
        float y1 = y0 + (float)(h_tiles * CS2D_TILE_SIZE);

        float dz = (float)(-(z_px / 2)) - (float)z_px * s_factor;
        float dx = dz * cos_sun;
        float dy = dz * sin_sun;
        cast_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x1, y1, dx, dy, shadow_alpha);
    }

    /* Apply floor contact occlusion (lighting/gfx_oclussionmap.png) around Obstacles and Walls in shadow */
    uint8_t *occl_buf = (uint8_t *)calloc((size_t)tex_w * (size_t)tex_h, 1);
    if (occl_buf) {
        for (uint32_t ty = 0; ty < map->height; ty++) {
            for (uint32_t tx = 0; tx < map->width; tx++) {
                uint8_t m = eff_modes[(size_t)ty * (size_t)map->width + (size_t)tx];
                if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW ||
                    m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW) {
                    float x0 = (float)(tx * CS2D_TILE_SIZE);
                    float y0 = (float)(ty * CS2D_TILE_SIZE);
                    accumulate_tile_occlusion(occl_buf, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, 10.0f, 72);
                }
            }
        }
        apply_shadow_masked_occlusion(map, eff_modes, alpha_buf, occl_buf, tex_w, tex_h, 200);
        free(occl_buf);
    }

    /* Clear Wall tiles (0x1402619d6..0x140261b11) so wall tops remain completely un-darkened */
    for (uint32_t ty = 0; ty < map->height; ty++) {
        for (uint32_t tx = 0; tx < map->width; tx++) {
            uint8_t m = eff_modes[(size_t)ty * (size_t)map->width + (size_t)tx];
            if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW) {
                float x0 = (float)(tx * CS2D_TILE_SIZE);
                float y0 = (float)(ty * CS2D_TILE_SIZE);
                clear_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f);
            }
        }
    }

    free(eff_modes);
    free(eff_heights);

    /*
     * Step 4: Env_Room Layers 1..5 (0x1402620be..0x140262608) for multi-level roofs.
     * Note: CS2D.exe does NOT clear CS2D_TILE_WALL after Layers 1..5 so higher roofs
     * cast shadows across lower ground walls and lower roofs.
     */
    for (int layer = 1; layer <= 5; layer++) {
        /* Pass A (0x1402620d8..0x1402623a1): Cast shadow for all Env_Room on this layer */
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *ent = &map->entities[i];
            if (ent->type != CS2D_ENT_ENV_ROOM && ent->type != CS2D_ENT_ENV_CUBE3D) continue;
            if (ent->int_settings[7] == 0 || ent->int_settings[9] != layer) continue;

            int w_tiles = ent->int_settings[0];
            int h_tiles = ent->int_settings[1];
            int z_px    = ent->int_settings[2];
            if (w_tiles <= 0 || h_tiles <= 0 || z_px <= 0) continue;

            int x_off = ent->str_settings[0][0] ? atoi(ent->str_settings[0]) : 0;
            int y_off = ent->str_settings[1][0] ? atoi(ent->str_settings[1]) : 0;
            float x0 = (float)(ent->x * CS2D_TILE_SIZE + x_off);
            float y0 = (float)(ent->y * CS2D_TILE_SIZE + y_off);
            float x1 = x0 + (float)(w_tiles * CS2D_TILE_SIZE);
            float y1 = y0 + (float)(h_tiles * CS2D_TILE_SIZE);

            float dz = (float)(-(z_px / 2)) - (float)z_px * s_factor;
            float dx = dz * cos_sun;
            float dy = dz * sin_sun;
            cast_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x1, y1, dx, dy, shadow_alpha);
        }

        /* Pass B (0x1402623b0..0x1402625f5): White-fill (clear) interior of Env_Room on this layer */
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *ent = &map->entities[i];
            if (ent->type != CS2D_ENT_ENV_ROOM && ent->type != CS2D_ENT_ENV_CUBE3D) continue;
            if (ent->int_settings[7] == 0 || ent->int_settings[9] != layer) continue;

            int w_tiles = ent->int_settings[0];
            int h_tiles = ent->int_settings[1];
            if (w_tiles <= 0 || h_tiles <= 0) continue;

            int x_off = ent->str_settings[0][0] ? atoi(ent->str_settings[0]) : 0;
            int y_off = ent->str_settings[1][0] ? atoi(ent->str_settings[1]) : 0;
            float x0 = (float)(ent->x * CS2D_TILE_SIZE + x_off);
            float y0 = (float)(ent->y * CS2D_TILE_SIZE + y_off);
            float x1 = x0 + (float)(w_tiles * CS2D_TILE_SIZE);
            float y1 = y0 + (float)(h_tiles * CS2D_TILE_SIZE);

            clear_shadow_rect(alpha_buf, tex_w, tex_h, x0, y0, x1, y1);
        }
    }

    /* Step 5: Env_Light (type 33, 0x1401d71e0 & 0x140047f50) radial light subtraction (lighting/gfx_light.png) */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        if (ent->type != CS2D_ENT_ENV_LIGHT) continue;

        int x_off = ent->str_settings[1][0] ? atoi(ent->str_settings[1]) : 0;
        int y_off = ent->str_settings[2][0] ? atoi(ent->str_settings[2]) : 0;
        float lx = (float)(ent->x * CS2D_TILE_SIZE + 16 + x_off);
        float ly = (float)(ent->y * CS2D_TILE_SIZE + 16 + y_off);
        float radius = (float)(ent->int_settings[0] > 0 ? ent->int_settings[0] : 128) * 0.85f;
        float strength = (float)(ent->int_settings[1] + ent->int_settings[2] + ent->int_settings[3]) / (3.0f * 255.0f);
        if (strength <= 0.0f) strength = 0.5f;

        const float inv_scale = 2.0f;
        int sx0 = (int)((lx - radius) / inv_scale);
        int sy0 = (int)((ly - radius) / inv_scale);
        int sx1 = (int)((lx + radius) / inv_scale) + 1;
        int sy1 = (int)((ly + radius) / inv_scale) + 1;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > tex_w) sx1 = tex_w;
        if (sy1 > tex_h) sy1 = tex_h;

        for (int sy = sy0; sy < sy1; sy++) {
            float py = ((float)sy + 0.5f) * inv_scale;
            for (int sx = sx0; sx < sx1; sx++) {
                float px = ((float)sx + 0.5f) * inv_scale;
                float dist = sqrtf((px - lx) * (px - lx) + (py - ly) * (py - ly));
                if (dist < radius) {
                    float falloff = 1.0f - (dist / radius);
                    float smooth = falloff * (2.0f - falloff);
                    int sub = (int)((float)shadow_alpha * strength * smooth * 1.6f);
                    int cur = (int)alpha_buf[sy * tex_w + sx];
                    alpha_buf[sy * tex_w + sx] = (uint8_t)((cur > sub) ? (cur - sub) : 0);
                }
            }
        }
    }

    uint8_t *rgba = (uint8_t *)calloc((size_t)tex_w * (size_t)tex_h * 4, 1);
    if (!rgba) {
        free(alpha_buf);
        return;
    }
    for (size_t idx = 0; idx < (size_t)tex_w * (size_t)tex_h; idx++) {
        rgba[idx * 4 + 3] = alpha_buf[idx];
    }
    free(alpha_buf);

    GLuint gl_id = 0;
    glGenTextures(1, &gl_id);
    glBindTexture(GL_TEXTURE_2D, gl_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(rgba);
    s_room_shadow_tex.id     = gl_id;
    s_room_shadow_tex.width  = tex_w;
    s_room_shadow_tex.height = tex_h;
    s_room_shadow_map_w      = map->width;
    s_room_shadow_map_h      = map->height;
}

/* Helper to render an Env_Sprite (type 22) entity (reconstructed from Te.draw 0x1402721f0..0x1402724ef) */
static void render_env_sprite_entity(
    cs2d_gfx_bank_t *bank,
    const cs2d_entity_t *ent,
    float cam_x, float cam_y,
    uint32_t tick_ms
)
{
    if (!bank || !ent->str_settings[0][0]) return;

    int blend_mode = ent->int_settings[9];
    bool black_mask = (blend_mode == 3 || blend_mode == 4 ||
                       strstr(ent->str_settings[0], "flare") != NULL ||
                       strstr(ent->str_settings[0], "light") != NULL);

    const cs2d_texture_t *tex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], black_mask);
    if (!tex || !tex->id) return;

    float w = (ent->int_settings[0] > 0) ? (float)ent->int_settings[0] : (float)tex->width;
    float h = (ent->int_settings[1] > 0) ? (float)ent->int_settings[1] : (float)tex->height;

    /* Te.draw 0x140272254..0x1402722c5: (x*32) - cam_x + x_off + (w/2) */
    float sx = (float)(ent->x * CS2D_TILE_SIZE + ent->int_settings[2]) + w * 0.5f - cam_x;
    float sy = (float)(ent->y * CS2D_TILE_SIZE + ent->int_settings[3]) + h * 0.5f - cam_y;

    /* 0x1402724e8: xorps xmm0, [0x140bd69d0]; call SetRotation (-rot) */
    float rot = -(float)ent->int_settings[4];
    if (ent->str_settings[3][0]) {
        float rot_speed = (float)atof(ent->str_settings[3]);
        rot -= rot_speed * ((float)tick_ms * 0.05f);
    }

    float alpha = ent->str_settings[1][0] ? (float)atof(ent->str_settings[1]) : 1.0f;
    if (alpha <= 0.0f) alpha = 1.0f;
    if (alpha > 1.0f)  alpha = 1.0f;

    /* 0x14027252e: SetColor(ints[5], ints[6], ints[7]) directly */
    float r = (float)ent->int_settings[5] / 255.0f;
    float g = (float)ent->int_settings[6] / 255.0f;
    float b = (float)ent->int_settings[7] / 255.0f;

    cs2d_render_sprite_frame(tex, tex->width, tex->height, 0, sx, sy, w, h, rot, r, g, b, alpha, blend_mode);
}

/*
 * FUN_140271b60: a flag image (midhandle) waving at (x, y). The pole (3 px viewport) is drawn still, then nine
 * 4 px wide viewports every 2 px redraw the image shifted by 2 sin((ms >> 2) + 30 i) - 2 sin((ms >> 2) + 30)
 * in gray |sin(...)| * 55 + 200 at alpha 0.8.
 */
static void render_waving_flag(const cs2d_texture_t *t, float x, float y, uint32_t tick_ms)
{
    float hx = (float)t->width * 0.5f, hy = (float)t->height * 0.5f;
    int x0 = (int)(x - hx), y0 = (int)(y - hy);
    int ms = (int)tick_ms;
    cs2d_render_scissor_begin(x0, y0, 3, 100);
    cs2d_render_sprite_frame(t, t->width, t->height, 0, x, y, (float)t->width, (float)t->height, 0.0f, 1, 1, 1, 1, 0);
    double base = sin((double)((ms >> 2) + 30) * 0.017453292519943295);
    for (int i = 1; i < 10; i++) {
        double w = sin((double)((ms >> 2) + i * 30) * 0.017453292519943295);
        float c = (float)(fabs(w) * 55.0 + 200.0) / 255.0f;
        cs2d_render_scissor_begin(x0 - 1 + i * 2, y0, 4, 100);
        cs2d_render_sprite_frame(t, t->width, t->height, 0, x, (float)(w + w + (float)(base * -2.0) + y),
                                 (float)t->width, (float)t->height, 0.0f, c, c, c, 0.8f, 0);
    }
    cs2d_render_scissor_end();
}

void cs2d_render_map_ex(
    const cs2d_map_t     *map,
    const cs2d_texture_t *tileset_tex,
    float cam_x, float cam_y,
    int screen_w, int screen_h
)
{
    if (!map || !map->tiles) return;

    /* Snap camera to integer pixels matching CS2D.exe 0x140ddf06c / 0x140ddf068 */
    cam_x = roundf(cam_x);
    cam_y = roundf(cam_y);

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    uint32_t tick_ms = GetTickCount();

    int start_tx = (int)(cam_x / CS2D_TILE_SIZE) - 1;
    int end_tx   = (int)((cam_x + screen_w) / CS2D_TILE_SIZE) + 1;
    int start_ty = (int)(cam_y / CS2D_TILE_SIZE) - 1;
    int end_ty   = (int)((cam_y + screen_h) / CS2D_TILE_SIZE) + 1;

    if (start_tx < 0) start_tx = 0;
    if (start_ty < 0) start_ty = 0;
    if ((uint32_t)end_tx > map->width) end_tx = (int)map->width;
    if ((uint32_t)end_ty > map->height) end_ty = (int)map->height;

    bool has_tileset = (tileset_tex && tileset_tex->id != 0 && tileset_tex->width >= 32);
    int tile_src = (map->use_64x64_tiles == 1) ? 64 : 32;
    int cols = has_tileset ? (tileset_tex->width / tile_src) : 1;
    if (cols < 1) cols = 1;

    /* Build tile animation remap table */
    uint8_t anim_remap[256];
    for (int i = 0; i < 256; i++) anim_remap[i] = (uint8_t)i;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->type == CS2D_ENT_INFO_ANIMATION) {
            int start_f = e->int_settings[0];
            int end_f   = e->int_settings[1];
            int speed   = e->int_settings[2] > 0 ? e->int_settings[2] : 200;
            if (start_f >= 0 && end_f > start_f && end_f < 256) {
                int span = (end_f - start_f) + 1;
                anim_remap[start_f] = (uint8_t)(start_f + ((tick_ms / (uint32_t)speed) % (uint32_t)span));
            }
        }
    }

    /* Fill map background color (map->bg_r, map->bg_g, map->bg_b) behind transparent tiles */
    if (map->bg_r != 0 || map->bg_g != 0 || map->bg_b != 0) {
        cs2d_render_rect(0.0f, 0.0f, (float)screen_w, (float)screen_h,
                         (float)map->bg_r / 255.0f,
                         (float)map->bg_g / 255.0f,
                         (float)map->bg_b / 255.0f,
                         1.0f, true);
    }

    if (has_tileset) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tileset_tex->id);
    }

    /* Pass 1: Render base tiles and tile overlays (per cs2d_spec_map_format.txt section 4) */
    for (int ty = start_ty; ty < end_ty; ty++) {
        for (int tx = start_tx; tx < end_tx; tx++) {
            uint32_t idx = (uint32_t)(ty * map->width + tx);
            uint8_t tile_idx = anim_remap[map->tiles[idx]];
            uint8_t mod      = map->modifiers ? map->modifiers[idx] : 0;

            float rx = (float)(tx * CS2D_TILE_SIZE) - cam_x;
            float ry = (float)(ty * CS2D_TILE_SIZE) - cam_y;

            if (has_tileset) {
                const uint8_t *rgb = ((mod & 0xc0) == 0x80 && map->tile_data) ? &map->tile_data[idx * 4] : NULL;
                draw_tile_frame(tileset_tex, tile_src, cols, tile_idx, mod, rx, ry, 1.0f, rgb);
            }
        }
    }

    /* Pass 1b: Render Env_Breakable (type 25) and Func_DynWall (type 71) using map tileset (Te.draw 0x140272808 & 0x140273858) */
    if (has_tileset) {
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *ent = &map->entities[i];
            if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
            if (ent->type == CS2D_ENT_ENV_BREAKABLE || ent->type == CS2D_ENT_FUNC_DYNWALL) {
                int tile_f = ent->int_settings[0];
                if (tile_f >= 0 && tile_f <= (int)map->tile_count) {
                    float rx = (float)(ent->x * CS2D_TILE_SIZE) - cam_x;
                    float ry = (float)(ent->y * CS2D_TILE_SIZE) - cam_y;
                    float trans = ent->str_settings[0][0] ? (float)atof(ent->str_settings[0]) : 0.0f;
                    /* prison_funst3r's glass (strs[0] = -0.4) shows the floor through it in CS2D.exe:
                     * the sign is ignored */
                    float alpha = 1.0f - fabsf(trans);
                    if (alpha < 0.1f) alpha = 1.0f;
                    if (alpha > 1.0f) alpha = 1.0f;
                    draw_tile_frame(tileset_tex, tile_src, cols, anim_remap[(uint8_t)tile_f], 0, rx, ry, alpha, NULL);
                }
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    /* Pass 3: Ground Entities & Actors (below players) */
    int bombspot_idx = 0;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        /* Hidden / open / broken by a trigger; a CTF flag base keeps its glow while the flag is away */
        if (map->entity_state[i] && ent->type != CS2D_ENT_INFO_CTF_FLAG) continue;
        float ex = (float)(ent->x * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_x;
        float ey = (float)(ent->y * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_y;

        switch (ent->type) {
            case CS2D_ENT_INFO_BOMBSPOT: {
                /* Draw authentic bombspot A / B decal on floor */
                const cs2d_texture_t *btex = (bombspot_idx == 0) ?
                    (bank ? &bank->tex_bspot_a : NULL) : (bank ? &bank->tex_bspot_b : NULL);
                bombspot_idx++;
                if (btex && btex->id) {
                    cs2d_render_sprite(btex, ex, ey, (float)btex->width, (float)btex->height, 0.0f, 1.0f, 1.0f, 1.0f, 0.85f);
                }
                break;
            }
            case CS2D_ENT_ENV_DECAL: {
                /* Env_Decal (24): ints[0]=frame (0..71) in gfx/decals.bmp, ints[1..3]=r,g,b, ints[4]=rot, strs[0]=alpha */
                if (bank && bank->tex_decals.id) {
                    int frame = ent->int_settings[0];
                    float r = (float)ent->int_settings[1] / 255.0f;
                    float g = (float)ent->int_settings[2] / 255.0f;
                    float b = (float)ent->int_settings[3] / 255.0f;
                    float a = ent->str_settings[0][0] ? (float)atof(ent->str_settings[0]) : 0.9f;
                    float rot = (float)ent->int_settings[4];
                    cs2d_render_sprite_frame(&bank->tex_decals, 32, 32, frame, ex, ey, 32.0f, 32.0f, rot, r, g, b, a, 0);
                }
                break;
            }
            case CS2D_ENT_ENV_ITEM: {
                /* Env_Item (21): ints[0]=weapon/item ID -> drawn with gfx/weapons/<wep>_d.bmp (Titem.draw 0x140147b80) */
                int wep_id = ent->int_settings[0];
                if (bank && wep_id > 0 && wep_id < CS2D_MAX_WEAPONS && bank->tex_weapons_d[wep_id].id) {
                    const cs2d_texture_t *dtex = &bank->tex_weapons_d[wep_id];
                    float dw = (dtex->width > 64) ? (float)(dtex->width / 2) : (float)dtex->width;
                    float dh = (dtex->height > 32) ? (float)(dtex->height / 2) : (float)dtex->height;
                    cs2d_render_sprite(dtex, ex, ey, dw, dh, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
                }
                break;
            }
            case CS2D_ENT_INFO_CTF_FLAG: {
                /*
                 * Info_CTF_Flag (15), Te.draw case 0xf: while the flag is home (state 0) the waving flag
                 * (item 70 red for ints[0] = 0, 71 blue for 1) stands at (x * 32 + 26, y * 32); then the team
                 * glow (flare1, additive, rotation ms / 15) at the tile center, at the alpha the flag left
                 * (0.8) or 1 without it.
                 */
                if (bank) {
                    bool is_red = (ent->int_settings[0] == 0);
                    bool home = (map->entity_state[i] == 0);
                    const cs2d_texture_t *ftex = is_red ? &bank->tex_redflag : &bank->tex_blueflag;
                    if (home && ftex->id) render_waving_flag(ftex, ex + 10.0f, ey - 16.0f, tick_ms);
                    if (bank->tex_flare1.id) {
                        cs2d_render_sprite_frame(&bank->tex_flare1, 32, 32, 0, ex, ey, 32.0f, 32.0f,
                                                 (float)(tick_ms / 15), is_red ? 1.0f : 0.0f, 0.0f,
                                                 is_red ? 0.0f : 1.0f, home ? 0.8f : 1.0f, 4);
                    }
                }
                break;
            }
            case CS2D_ENT_INFO_DOM_POINT: {
                /* Info_Dom_Point (17): ints[0]=0 (neutral), 1 (T), 2 (CT) (Te.draw 0x140274bb0) */
                if (bank) {
                    const cs2d_texture_t *dtex = &bank->tex_noflag;
                    float dx_off = 1.0f;
                    if (ent->int_settings[0] == 1) { dtex = &bank->tex_redflag; dx_off = 10.0f; }
                    else if (ent->int_settings[0] == 2) { dtex = &bank->tex_blueflag; dx_off = 10.0f; }
                    if (dtex->id) {
                        cs2d_render_sprite(dtex, ex + dx_off, ey - 16.0f,
                                           (float)dtex->width, (float)dtex->height,
                                           0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
                    }
                }
                break;
            }
            case CS2D_ENT_INFO_HOSTAGE: {
                /* Info_Hostage (3): ints[0]=rot, ints[1]=look -> legs.bmp + hostages.bmp (Thostage.draw 0x140147090) */
                if (bank && bank->tex_hostages.id) {
                    float rot = (float)ent->int_settings[0];
                    int look = ent->int_settings[1] & 7;
                    if (bank->tex_legs.id) {
                        cs2d_render_sprite_frame(&bank->tex_legs, 32, 32, 0, ex, ey, 32.0f, 32.0f, rot, 1.0f, 1.0f, 1.0f, 1.0f, 0);
                    }
                    cs2d_render_sprite_frame(&bank->tex_hostages, 32, 32, look, ex, ey, 32.0f, 32.0f, rot, 1.0f, 1.0f, 1.0f, 1.0f, 0);
                }
                break;
            }
            case CS2D_ENT_ENV_NPC: {
                /* Env_NPC (31): ints[0]=rot, ints[1]=NPC type (1=Zombie, 2=Headcrab, 3=Snark, 4=Vortigaunt, 5=Soldier) */
                int ntype = ent->int_settings[1] - 1;
                if (ntype < 0 || ntype >= 5) ntype = 0;
                if (bank && bank->tex_npc[ntype].id) {
                    const cs2d_texture_t *ntex = &bank->tex_npc[ntype];
                    int cw = (ntex->width >= 64) ? 32 : ntex->width;
                    int ch = (ntex->height >= 64) ? 32 : ntex->height;
                    cs2d_render_sprite_frame(ntex, cw, ch, 0, ex, ey, 32.0f, 32.0f, (float)ent->int_settings[0], 1.0f, 1.0f, 1.0f, 1.0f, 0);
                }
                break;
            }
            case CS2D_ENT_ENV_SPRITE: {
                /* Ground pass Env_Sprite (when strs[4] == "1", i.e. covered by player) */
                if (atoi(ent->str_settings[4]) == 1) {
                    render_env_sprite_entity(bank, ent, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_IMAGE: {
                /* Ground pass Env_Image (28) when ints[2] == 0 (floor image) */
                if (ent->int_settings[2] == 0 && bank && ent->str_settings[0][0]) {
                    const cs2d_texture_t *itex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], false);
                    if (itex && itex->id) {
                        float ix = (float)(ent->x * CS2D_TILE_SIZE + ent->int_settings[0]) + itex->width * 0.5f - cam_x;
                        float iy = (float)(ent->y * CS2D_TILE_SIZE + ent->int_settings[1]) + itex->height * 0.5f - cam_y;
                        cs2d_render_sprite(itex, ix, iy, (float)itex->width, (float)itex->height, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

/*
 * Reconstructed from 0x140262750..0x140262885 & Te.draw(1) (0x140272938..0x1402782a4):
 *   1. Non-additive rooftop sprites (dustroof.png, roof_edge1.png, dome.png, Env_Image)
 *   2. Unified Sun-Angle & Multi-Layer Room Shadow/Lighting Map (s_room_shadow_tex)
 *   3. Overhead Env_Object entities (Palm trees, Leafy trees, Lamps) & additive light flares
 */
void cs2d_render_map_overhead(
    const cs2d_map_t *map,
    float cam_x, float cam_y,
    int screen_w, int screen_h
)
{
    if (!map) return;
    (void)screen_w;
    (void)screen_h;

    cam_x = roundf(cam_x);
    cam_y = roundf(cam_y);

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank) return;

    uint32_t tick_ms = GetTickCount();

    /* Pass 1: Non-additive rooftop sprites & overhead images (shaded by Layer 1..5 rooftop shadows) */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
        switch (ent->type) {
            case CS2D_ENT_ENV_SPRITE: {
                if (atoi(ent->str_settings[4]) == 0 &&
                    ent->int_settings[9] != 3 && ent->int_settings[9] != 4) {
                    render_env_sprite_entity(bank, ent, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_IMAGE: {
                if (ent->int_settings[2] != 0 && ent->str_settings[0][0]) {
                    const cs2d_texture_t *itex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], false);
                    if (itex && itex->id) {
                        float ix = (float)(ent->x * CS2D_TILE_SIZE + ent->int_settings[0]) + itex->width * 0.5f - cam_x;
                        float iy = (float)(ent->y * CS2D_TILE_SIZE + ent->int_settings[1]) + itex->height * 0.5f - cam_y;
                        cs2d_render_sprite(itex, ix, iy, (float)itex->width, (float)itex->height, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    /* Pass 2: Unified Sun-Angle & Multi-Layer Room Shadow/Lighting Overlay (0x140262750..0x14026286f) */
    if (s_shadows_enabled) {
        ensure_room_shadow_texture(map);
        if (s_room_shadow_tex.id != 0) {
            float map_pw = (float)(map->width * CS2D_TILE_SIZE);
            float map_ph = (float)(map->height * CS2D_TILE_SIZE);
            cs2d_render_sprite(&s_room_shadow_tex,
                               map_pw * 0.5f - cam_x,
                               map_ph * 0.5f - cam_y,
                               map_pw, map_ph,
                               0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    /* Pass 3: Te.draw(1) Overhead Env_Object (trees, palms, lamps) & additive light flares (0x140262875) */
    float sun_angle = (map->daylight_time > 999)
                      ? (float)((map->daylight_time - 1000) % 360) : 225.0f;
    float sun_rad   = (sun_angle - 180.0f) * (3.14159265f / 180.0f);
    float sh_dx     = sinf(sun_rad) * 4.0f;
    float sh_dy     = cosf(sun_rad) * 4.0f;

    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
        float ex = (float)(ent->x * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_x;
        float ey = (float)(ent->y * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_y;

        switch (ent->type) {
            case CS2D_ENT_ENV_SPRITE: {
                if (atoi(ent->str_settings[4]) == 0 &&
                    (ent->int_settings[9] == 3 || ent->int_settings[9] == 4)) {
                    render_env_sprite_entity(bank, ent, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_OBJECT: {
                int obj_type = ent->int_settings[0];
                float r = (float)ent->int_settings[1] / 255.0f;
                float g = (float)ent->int_settings[2] / 255.0f;
                float b = (float)ent->int_settings[3] / 255.0f;
                if (ent->int_settings[1] == 0 && ent->int_settings[2] == 0 && ent->int_settings[3] == 0) {
                    r = 1.0f; g = 1.0f; b = 1.0f;
                }
                float scale = ent->str_settings[0][0] ? (float)atof(ent->str_settings[0]) : 1.0f;
                float alpha = ent->str_settings[1][0] ? (float)atof(ent->str_settings[1]) : 1.0f;
                if (scale <= 0.0f) scale = 1.0f;
                if (alpha <= 0.0f) alpha = 1.0f;
                float rot = (float)ent->int_settings[4];
                float phase = (float)tick_ms * 0.002f + (float)(ent->x * 13 + ent->y * 7);

                if (obj_type == 0) {
                    /*
                     * Palm tree (0x14027779c..0x140277e8b):
                     *   - 8 fronds of incbindata/palmleave.png (30x70, handle at (15.0, 2.0) per 0x14022ebd0)
                     *   - Scale factor = scale * 0.8f (0x140bd6a74 = 0.8f)
                     *   - Stem at (15, 2) is pinned to (ex, ey), so quad center is at (-sin(rad)*d, +cos(rad)*d)
                     *     where d = (fh * 0.5f) - 2.0f * pscale = 33.0f * pscale
                     */
                    if (bank->tex_palms.id) {
                        float pscale = scale * 0.8f;
                        float fw = (float)bank->tex_palms.width * pscale;
                        float fh = (float)bank->tex_palms.height * pscale;
                        float d  = (fh * 0.5f) - 2.0f * pscale;

                        /* Pass 1: 8 frond drop shadows (0x140277cf8..0x140277e8b) */
                        for (int k = 1; k <= 8; k++) {
                            float sway = ((k % 3) == 0 ? sinf(phase + (float)(k * 20) * 0.017453f)
                                                       : cosf(phase + (float)(k * 20) * 0.017453f)) * 2.5f;
                            float ang = (float)(k * 45) + rot + sway;
                            float rad = ang * (3.14159265f / 180.0f);
                            float sx = (ex + sh_dx) - sinf(rad) * d;
                            float sy = (ey + sh_dy) + cosf(rad) * d;
                            cs2d_render_sprite_frame(&bank->tex_palms, bank->tex_palms.width, bank->tex_palms.height, 0,
                                                     sx, sy, fw, fh, ang,
                                                     0.0f, 0.0f, 0.0f, 0.25f * alpha, 0);
                        }

                        /* Pass 2: 8 palm fronds radiating outward from stem at (ex, ey) (0x14027780e..0x140277984) */
                        for (int k = 1; k <= 8; k++) {
                            float sway = ((k % 3) == 0 ? sinf(phase + (float)(k * 20) * 0.017453f)
                                                       : cosf(phase + (float)(k * 20) * 0.017453f)) * 2.5f;
                            float ang = (float)(k * 45) + rot + sway;
                            float rad = ang * (3.14159265f / 180.0f);
                            float fx = ex - sinf(rad) * d;
                            float fy = ey + cosf(rad) * d;
                            cs2d_render_sprite_frame(&bank->tex_palms, bank->tex_palms.width, bank->tex_palms.height, 0,
                                                     fx, fy, fw, fh, ang,
                                                     r, g, b, alpha, 0);
                        }
                    }
                } else if (obj_type == 1) {
                    /*
                     * Leafy Tree (0x140275f35..0x140276228 & 0x140277ebb..0x1402782a4):
                     *   - 7 outer clumps of incbindata/treeleaves.png (40x40, MidHandle) in a circle of
                     *     radius 30.0f * scale (0x140bd6a98 = 30.0f) at angles k*51 + rot
                     *   - Plus 1 center clump at (ex, ey) scaled by 1.2f * scale (0x140bd6f48 = 1.2f)
                     */
                    if (bank->tex_tree.id) {
                        float cw = (float)bank->tex_tree.width * scale;
                        float ch = (float)bank->tex_tree.height * scale;
                        float rad_dist = 30.0f * scale;

                        /* Pass 1: 7 outer clump drop shadows (0x140277fae..0x140278290) */
                        for (int k = 1; k <= 7; k++) {
                            float sway = ((k % 3) == 0) ? sinf(phase + (float)(k * 50) * 0.017453f)
                                                        : cosf(phase + (float)(k * 50) * 0.017453f);
                            float ang_pos = (float)(k * 51) + rot + sway * 2.0f;
                            float ang_rot = rot + sway * 5.0f;
                            float rad_pos = ang_pos * (3.14159265f / 180.0f);
                            float sx = (ex + sh_dx) + sinf(rad_pos) * rad_dist;
                            float sy = (ey + sh_dy) - cosf(rad_pos) * rad_dist;
                            cs2d_render_sprite_frame(&bank->tex_tree, bank->tex_tree.width, bank->tex_tree.height, 0,
                                                     sx, sy, cw, ch, ang_rot,
                                                     0.0f, 0.0f, 0.0f, 0.25f * alpha, 0);
                        }

                        /* Pass 2: 7 outer foliage clumps (0x140276037..0x140276228) */
                        for (int k = 1; k <= 7; k++) {
                            float sway = ((k % 3) == 0) ? sinf(phase + (float)(k * 50) * 0.017453f)
                                                        : cosf(phase + (float)(k * 50) * 0.017453f);
                            float ang_pos = (float)(k * 51) + rot + sway * 2.0f;
                            float ang_rot = rot + sway * 5.0f;
                            float rad_pos = ang_pos * (3.14159265f / 180.0f);
                            float cx = ex + sinf(rad_pos) * rad_dist;
                            float cy = ey - cosf(rad_pos) * rad_dist;
                            cs2d_render_sprite_frame(&bank->tex_tree, bank->tex_tree.width, bank->tex_tree.height, 0,
                                                     cx, cy, cw, ch, ang_rot,
                                                     r, g, b, alpha, 0);
                        }

                        /* Pass 3: 8th center foliage clump at 1.2x scale (0x140277ebb..0x140277fa9) */
                        float ang_center = rot + sinf(phase) * 5.0f;
                        cs2d_render_sprite_frame(&bank->tex_tree, bank->tex_tree.width, bank->tex_tree.height, 0,
                                                 ex, ey, cw * 1.2f, ch * 1.2f, ang_center,
                                                 r, g, b, alpha, 0);
                    }
                } else if (obj_type >= 2) {
                    /* Objects (0x140275f26 & 0x14027622d): incbindata/objects.bmp (32x32 cells, frame = obj_type - 2) */
                    if (bank->tex_lamp1.id) {
                        int frame = obj_type - 2;
                        int cell_w = (bank->tex_lamp1.width >= 64) ? 32 : bank->tex_lamp1.width;
                        int cell_h = (bank->tex_lamp1.height >= 32) ? 32 : bank->tex_lamp1.height;
                        cs2d_render_sprite_frame(&bank->tex_lamp1, cell_w, cell_h, frame,
                                                 ex, ey, 32.0f * scale, 32.0f * scale,
                                                 rot, 1.0f, 1.0f, 1.0f, alpha, 0);
                    }
                    if (obj_type == 4 && bank->tex_flare1.id) {
                        cs2d_render_sprite_frame(&bank->tex_flare1, bank->tex_flare1.width, bank->tex_flare1.height, 0,
                                                 ex, ey, 96.0f * scale, 96.0f * scale,
                                                 0.0f, r, g, b, alpha * 0.45f, 3);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void cs2d_render_map(
    const cs2d_map_t *map,
    float cam_x, float cam_y,
    int screen_w, int screen_h
)
{
    cs2d_render_map_ex(map, NULL, cam_x, cam_y, screen_w, screen_h);
}

/*
 * Generates a 128x128 circular minimap texture centered around the local player
 * to render inside gfx/hud_radar_advanced.png + gfx/hud_radar_ring.png.
 */
static int s_minimap_last_ptx = -9999;
static int s_minimap_last_pty = -9999;

void cs2d_render_invalidate_shadows(void)
{
    if (s_room_shadow_tex.id != 0) {
        cs2d_texture_free(&s_room_shadow_tex);
    }
    s_room_shadow_map_w = 0;
    s_room_shadow_map_h = 0;
    s_minimap_last_ptx  = -9999;
    s_minimap_last_pty  = -9999;
}

void cs2d_render_set_shadows_enabled(bool enabled)
{
    s_shadows_enabled = enabled;
}

bool cs2d_render_get_shadows_enabled(void)
{
    return s_shadows_enabled;
}

static void ensure_minimap_texture(cs2d_gfx_bank_t *bank, const cs2d_map_t *map, const cs2d_player_t *player)
{
    if (!bank || !map || !map->tiles || map->width == 0 || map->height == 0 || !g_gl_ctx) return;

    int ptx = player ? (int)(player->pos.x / CS2D_TILE_SIZE) : (int)(map->width / 2);
    int pty = player ? (int)(player->pos.y / CS2D_TILE_SIZE) : (int)(map->height / 2);

    if (bank->tex_minimap.id != 0 &&
        abs(ptx - s_minimap_last_ptx) <= 1 &&
        abs(pty - s_minimap_last_pty) <= 1) {
        return;
    }
    s_minimap_last_ptx = ptx;
    s_minimap_last_pty = pty;

    uint8_t rgba[128 * 128 * 4];
    memset(rgba, 0, sizeof(rgba));

    float center_tx = player ? (player->pos.x / (float)CS2D_TILE_SIZE) : ((float)map->width * 0.5f);
    float center_ty = player ? (player->pos.y / (float)CS2D_TILE_SIZE) : ((float)map->height * 0.5f);
    /* Local radar view spans 36x36 tiles across 128x128 radar pixels (~3.55 radar px per tile) */
    float tiles_per_px = 36.0f / 128.0f;

    for (int py = 0; py < 128; py++) {
        for (int px = 0; px < 128; px++) {
            float dx = (float)px - 63.5f;
            float dy = (float)py - 63.5f;
            if (dx * dx + dy * dy > 56.5f * 56.5f) {
                continue;
            }

            int tx = (int)floorf(center_tx + dx * tiles_per_px);
            int ty = (int)floorf(center_ty + dy * tiles_per_px);
            if (tx < 0 || ty < 0 || (uint32_t)tx >= map->width || (uint32_t)ty >= map->height) {
                rgba[(py * 128 + px) * 4 + 0] = 32;
                rgba[(py * 128 + px) * 4 + 1] = 28;
                rgba[(py * 128 + px) * 4 + 2] = 22;
                rgba[(py * 128 + px) * 4 + 3] = 160;
                continue;
            }

            uint32_t idx = (uint32_t)ty * map->width + (uint32_t)tx;
            uint8_t t = map->tiles[idx];
            uint8_t m = map->tile_modes[t];

            uint8_t r = 198, g = 172, b = 122, a = 195;
            if (t == 0) {
                r = 45;  g = 36;  b = 26;  a = 175;
            } else if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW) {
                r = 148; g = 114; b = 76;  a = 215;
            } else if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) {
                r = 168; g = 134; b = 92;  a = 205;
            } else {
                int v = (t * 7) & 15;
                r = (uint8_t)(194 + v);
                g = (uint8_t)(168 + v);
                b = (uint8_t)(118 + v);
                a = 190;
            }

            rgba[(py * 128 + px) * 4 + 0] = r;
            rgba[(py * 128 + px) * 4 + 1] = g;
            rgba[(py * 128 + px) * 4 + 2] = b;
            rgba[(py * 128 + px) * 4 + 3] = a;
        }
    }

    if (bank->tex_minimap.id == 0) {
        GLuint gl_id = 0;
        glGenTextures(1, &gl_id);
        bank->tex_minimap.id     = gl_id;
        bank->tex_minimap.width  = 128;
        bank->tex_minimap.height = 128;
    }

    glBindTexture(GL_TEXTURE_2D, bank->tex_minimap.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/*
 * Reconstructed from FUN_140113000..0x140113289 in CS2D.exe:
 * Draws integer digits using gfx/hud_nums.bmp (592x66 -> 12 cells of 48x66: '0'..'9', ':', '|').
 * Each digit advances X by 25px (drawn at 24x33, additive blended).
 */
void cs2d_render_hud_number(
    const cs2d_texture_t *hud_nums_tex,
    int value,
    float x, float y,
    float r, float g, float b, float a
)
{
    if (!hud_nums_tex || !hud_nums_tex->id) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);

    int cell_w = (hud_nums_tex->height >= 60) ? 48 : 24;
    int cell_h = (hud_nums_tex->height >= 60) ? 66 : 33;

    float cur_x = x;
    for (int i = 0; buf[i] != '\0'; i++) {
        int frame = 0;
        if (buf[i] >= '0' && buf[i] <= '9') {
            frame = buf[i] - '0';
        } else if (buf[i] == ':') {
            frame = 10;
        } else if (buf[i] == '-' || buf[i] == '|') {
            frame = 11;
        } else {
            continue;
        }
        cs2d_render_sprite_frame(hud_nums_tex, cell_w, cell_h, frame,
                                 cur_x + 12.0f, y + 16.0f, 24.0f, 33.0f,
                                 0.0f, r, g, b, a, 3);
        cur_x += 25.0f;
    }
}

static void render_hud_time(
    const cs2d_texture_t *hud_nums_tex,
    int total_seconds,
    float x, float y,
    float r, float g, float b, float a
)
{
    if (!hud_nums_tex || !hud_nums_tex->id) return;
    if (total_seconds < 0) total_seconds = 0;

    int mins = total_seconds / 60;
    int secs = total_seconds % 60;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);

    int cell_w = (hud_nums_tex->height >= 60) ? 48 : 24;
    int cell_h = (hud_nums_tex->height >= 60) ? 66 : 33;

    float cur_x = x;
    for (int i = 0; buf[i] != '\0'; i++) {
        int frame = (buf[i] == ':') ? 10 : (buf[i] - '0');
        cs2d_render_sprite_frame(hud_nums_tex, cell_w, cell_h, frame,
                                 cur_x + 12.0f, y + 16.0f, 24.0f, 33.0f,
                                 0.0f, r, g, b, a, 3);
        cur_x += (buf[i] == ':') ? 16.0f : 24.0f;
    }
}

/*
 * Reconstructed from CS2D HUD renderer (0x1402931cc..0x14029ccde in CS2D.exe):
 *   - Top-Left: Circular Minimap Radar (gfx/hud_radar_advanced.png + minimap + gfx/hud_radar_ring.png + gfx/hud_radar_icons.png)
 *   - Left Side: Equipment icons (C4 Bomb frame 6, Defuse Kit frame 3, Buyzone frame 4)
 *   - Bottom-Left: Health symbol (frame 0) + Health number, Armor symbol (frame 1) + Armor number
 *   - Bottom-Center: Timer symbol (frame 2) + M:SS round clock
 *   - Bottom-Right: Money symbol (frame 7 '$') + Money number, Clip | Reserve ammo
 */
void cs2d_render_hud(
    const cs2d_player_t *player,
    const cs2d_game_t   *game,
    int screen_w, int screen_h
)
{
    if (!player) return;

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    bool has_hud_gfx = (bank && bank->tex_hud_nums.id && bank->tex_hud_symbols.id);
    if (!has_hud_gfx) return;

    int sym_cell = (bank->tex_hud_symbols.height >= 64) ? 64 : 32;
    int num_cw   = (bank->tex_hud_nums.height >= 60) ? 48 : 24;
    int num_ch   = (bank->tex_hud_nums.height >= 60) ? 66 : 33;

    /* 1. Top-Left Circular Minimap Radar at (66, 66), size 128x128 (0x1402931cc) */
    float rcx = 66.0f;
    float rcy = 66.0f;
    if (game && game->map.tiles) {
        ensure_minimap_texture(bank, &game->map, player);
    }
    if (bank->tex_hud_radar_adv.id) {
        cs2d_render_sprite_frame(&bank->tex_hud_radar_adv,
                                 bank->tex_hud_radar_adv.width, bank->tex_hud_radar_adv.height, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.65f, 0);
    }
    if (bank->tex_minimap.id) {
        cs2d_render_sprite_frame(&bank->tex_minimap, 128, 128, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.80f, 0);
    }
    if (bank->tex_hud_radar_ring.id) {
        cs2d_render_sprite_frame(&bank->tex_hud_radar_ring,
                                 bank->tex_hud_radar_ring.width, bank->tex_hud_radar_ring.height, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.85f, 0);
    } else if (bank->tex_hud_radar.id) {
        cs2d_render_sprite_frame(&bank->tex_hud_radar,
                                 bank->tex_hud_radar.width, bank->tex_hud_radar.height, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 0.4f, 0.9f, 0.4f, 0.65f, 0);
    }

    /* Radar Icons (Info_RadarIcon type 81 -> int_settings[0] is direct frame index: 8='A', 9='B') */
    if (game && game->map.width > 0 && game->map.height > 0 && bank->tex_hud_radar_icons.id) {
        float px_per_tile = 128.0f / 36.0f;
        float p_tx = player->pos.x / (float)CS2D_TILE_SIZE;
        float p_ty = player->pos.y / (float)CS2D_TILE_SIZE;

        for (uint32_t i = 0; i < game->map.entity_count; i++) {
            const cs2d_entity_t *ent = &game->map.entities[i];
            if (ent->type == CS2D_ENT_INFO_RADARICON) {
                int icon_f = ent->int_settings[0];
                float dx = ((float)ent->x - p_tx) * px_per_tile;
                float dy = ((float)ent->y - p_ty) * px_per_tile;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > 52.0f) {
                    dx *= 52.0f / dist;
                    dy *= 52.0f / dist;
                }
                float ir = (ent->int_settings[1] || ent->int_settings[2] || ent->int_settings[3])
                           ? ((float)ent->int_settings[1] / 255.0f) : 1.0f;
                float ig = (ent->int_settings[1] || ent->int_settings[2] || ent->int_settings[3])
                           ? ((float)ent->int_settings[2] / 255.0f) : 0.75f;
                float ib = (ent->int_settings[1] || ent->int_settings[2] || ent->int_settings[3])
                           ? ((float)ent->int_settings[3] / 255.0f) : 0.1f;
                cs2d_render_sprite_frame(&bank->tex_hud_radar_icons, 32, 32, icon_f,
                                         rcx + dx, rcy + dy, 22.0f, 22.0f, 0.0f,
                                         ir, ig, ib, 0.90f, 0);
            }
        }
        /* Local player radar blip at center of radar */
        cs2d_render_sprite_frame(&bank->tex_hud_radar_icons, 32, 32, 0,
                                 rcx, rcy, 12.0f, 12.0f, 0.0f,
                                 1.0f, 1.0f, 1.0f, 0.90f, 0);
    }

    float hud_y = (float)(screen_h - 36);
    /* Classic CS2D warm amber/gold HUD tint (or red when HP <= 25) */
    float hr = 1.0f;
    float hg = (player->health <= 25) ? 0.2f : 0.78f;
    float hb = (player->health <= 25) ? 0.1f : 0.0f;
    float ha = 0.52f;

    /* 2. Bottom-Left: Health (symbol 0) & Armor (symbol 1) */
    cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 0,
                             20.0f, hud_y + 16.0f, 32.0f, 32.0f, 0.0f, hr, hg, hb, ha, 3);
    cs2d_render_hud_number(&bank->tex_hud_nums, player->health, 40.0f, hud_y, hr, hg, hb, ha);

    cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 1,
                             152.0f, hud_y + 16.0f, 32.0f, 32.0f, 0.0f, 1.0f, 0.78f, 0.0f, ha, 3);
    int disp_armor = (player->armor > 200) ? 100 : player->armor;
    cs2d_render_hud_number(&bank->tex_hud_nums, disp_armor, 172.0f, hud_y, 1.0f, 0.78f, 0.0f, ha);

    /* 3. Bottom-Center: Round Timer (symbol 2) + M:SS */
    if (game) {
        float timer_x = (float)(screen_w / 2) - 46.0f;
        float tr = 1.0f;
        float tg = game->bomb_planted ? 0.2f : 0.78f;
        float tb = game->bomb_planted ? 0.1f : 0.0f;
        int sec_left = (int)(game->bomb_planted ? game->c4_time : game->round_time);

        cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 2,
                                 timer_x, hud_y + 16.0f, 32.0f, 32.0f, 0.0f, tr, tg, tb, ha, 3);
        render_hud_time(&bank->tex_hud_nums, sec_left, timer_x + 20.0f, hud_y, tr, tg, tb, ha);
    }

    /* 4. Bottom-Right: Money (symbol 7 '$') above Ammo (Clip | Reserve) */
    float money_y = (float)(screen_h - 72);
    float money_x = (float)(screen_w - 150);
    cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 7,
                             money_x, money_y + 16.0f, 32.0f, 32.0f, 0.0f, 1.0f, 0.78f, 0.0f, ha, 3);
    cs2d_render_hud_number(&bank->tex_hud_nums, player->money, money_x + 20.0f, money_y, 1.0f, 0.78f, 0.0f, ha);

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if (wep && wep->clip_size > 0) {
        int clip = player->clip_ammo[player->current_weapon];
        int res  = player->reserve_ammo[player->current_weapon];
        cs2d_render_hud_number(&bank->tex_hud_nums, clip, (float)(screen_w - 165), hud_y, 1.0f, 0.78f, 0.0f, ha);
        /* Frame 11 ('|') ammo separator from gfx/hud_nums.bmp */
        cs2d_render_sprite_frame(&bank->tex_hud_nums, num_cw, num_ch, 11,
                                 (float)(screen_w - 94), hud_y + 16.0f, 24.0f, 33.0f,
                                 0.0f, 1.0f, 0.78f, 0.0f, ha, 3);
        cs2d_render_hud_number(&bank->tex_hud_nums, res,  (float)(screen_w - 82),  hud_y, 1.0f, 0.78f, 0.0f, ha);
    }

    /* 5. Left Status Icons: Buyzone (frame 4), Defuse Kit (frame 3), C4 Bomb (frame 6) */
    float icon_y = 156.0f;
    if (game) {
        int ptx = (int)(player->pos.x / CS2D_TILE_SIZE);
        int pty = (int)(player->pos.y / CS2D_TILE_SIZE);
        cs2d_entity_type_t spawn_type = (player->team == CS2D_TEAM_T) ? CS2D_ENT_INFO_T : CS2D_ENT_INFO_CT;
        if (cs2d_map_in_zone(&game->map, spawn_type, ptx, pty)) {
            cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 4,
                                     24.0f, icon_y, 32.0f, 32.0f, 0.0f, 0.2f, 0.95f, 0.2f, 0.65f, 3);
            icon_y += 36.0f;
        }
        if (cs2d_map_in_zone(&game->map, CS2D_ENT_INFO_BOMBSPOT, ptx, pty) &&
            player->inventory[CS2D_SLOT_BOMB] == CS2D_WEP_BOMB) {
            cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 5,
                                     24.0f, icon_y, 32.0f, 32.0f, 0.0f, 1.0f, 0.2f, 0.1f, 0.75f, 3);
            icon_y += 36.0f;
        }
    }
    if (player->has_defuse_kit) {
        cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 3,
                                 24.0f, icon_y, 32.0f, 32.0f, 0.0f, 0.2f, 0.95f, 0.2f, 0.65f, 3);
        icon_y += 36.0f;
    }
    if (player->inventory[CS2D_SLOT_BOMB] == CS2D_WEP_BOMB) {
        cs2d_render_sprite_frame(&bank->tex_hud_symbols, sym_cell, sym_cell, 6,
                                 24.0f, icon_y, 32.0f, 32.0f, 0.0f, 0.2f, 0.95f, 0.2f, 0.65f, 3);
    }
}

/* Item image of a weapon selection row, midhandled at (x, y): a black shadow at alpha 0.4 2 s lower right, then the
 * image; images wider than 64 px are drawn at half size */
static void weapon_select_icon(const cs2d_texture_t *tex, float x, float y, float s, float rot)
{
    if (!tex->id) return;
    float sc = (tex->width > 64) ? s * 0.5f : s;
    float w = (float)tex->width * sc, h = (float)tex->height * sc;
    cs2d_render_sprite_frame(tex, 0, 0, 0, x + 2.0f * s, y + 2.0f * s, w, h, rot, 0.0f, 0.0f, 0.0f, 0.4f, 0);
    cs2d_render_sprite_frame(tex, 0, 0, 0, x, y, w, h, rot, 1.0f, 1.0f, 1.0f, 1.0f, 0);
}

/* Text with a black shadow at alpha 0.8 1 px lower right; (255,255,0) when highlighted, else (220,180,0) */
static void weapon_select_text(float x, float y, const char *text, float scale_x, bool hl)
{
    cs2d_render_text_scaled_ex(CS2D_FONT_REGULAR, x + 1.0f, y + 1.0f, text, scale_x, 0.0f, 0.0f, 0.0f, 0.8f);
    cs2d_render_text_scaled_ex(CS2D_FONT_REGULAR, x, y, text, scale_x,
                               hl ? 1.0f : 220.0f / 255.0f, hl ? 1.0f : 180.0f / 255.0f, 0.0f, 1.0f);
}

/*
 * Reconstructed from the end of FUN_140295190 (the HUD), drawn while DAT_140ddf338 (slot) > 0. s is the HUD font
 * height / 13 (DAT_140a1ccc8 * 0.07692308): 13 px at 850x480, so 1. Slots 1..9 go left to right from x0 at y 3 s:
 *  - open slot: its number (gfx/hud_slotheads.bmp frame slot - 1) additive (200,200,0), then one gfx/hud_slot.bmp
 *    row per item from y 15 s every 46 s, additive (200,200,0) highlighted / (100,50,0). A row has the turning item
 *    image (_d.bmp) at +21 s, its name at +45 s (+43 s in slots 4 and 6) squeezed to 95 s, and at +18 s
 *    "clip | reserve" (slots 1, 2; not the Tactical Shield nor weapons without ammo) or "count x" (slots 4, 6).
 *    The melee icon turns the other way around a 5 s circle. Slots 7..9 rows have no content. The column is 155 s.
 *  - other slots: a 10 s square per owned item every 12 s under the number, additive (100,50,0); the number turns
 *    (100,0,0) when the slot is empty. The column is 15 s.
 * Not done: "clip | INF" (sys/language 0762) under infinite ammo (0x140a14e38[1]) and "0 x - Remote" (0763, slot 6).
 */
void cs2d_render_weapon_select(const cs2d_player_t *player, const struct cs2d_invsel_s *sel, float x0)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!player || !sel || sel->slot <= 0 || !bank) return;

    const float s = 1.0f;
    float x = x0, head_y = (float)(int)(s * 3.0f);
    for (int slot = 1; slot < 10; slot++) {
        if (slot == sel->slot) {
            cs2d_render_sprite_frame(&bank->tex_hud_slotheads, 10, 10, slot - 1, x + 5.0f * s, head_y + 5.0f * s,
                                     10.0f * s, 10.0f * s, 0.0f, 200.0f / 255.0f, 200.0f / 255.0f, 0.0f, 1.0f, 4);
            float y = (float)(int)(s * 15.0f);
            int n = cs2d_invsel_count(player, slot);
            for (int i = 0; i < n; i++, y += (float)(int)(s * 46.0f)) {
                uint8_t w = cs2d_invsel_item(player, slot, i);
                bool hl = (i == sel->index);
                float c = (hl ? 200.0f : 100.0f) / 255.0f;
                cs2d_render_sprite_frame(&bank->tex_hud_slot, 0, 0, 0, x + 75.0f * s, y + 20.0f * s, 150.0f * s,
                                         40.0f * s, 0.0f, c, hl ? c : 50.0f / 255.0f, 0.0f, 1.0f, 4);
                if (slot > 6) continue;

                const cs2d_texture_t *img = &bank->tex_weapons_d[w];
                float rot = sel->rot[w];
                if (slot == 3) {
                    float r = -rot * 0.017453292f;
                    weapon_select_icon(img, x + (21.0f + sinf(r) * 5.0f) * s, y + (21.0f - cosf(r) * 5.0f) * s, s, -rot);
                } else {
                    weapon_select_icon(img, x + (float)(int)(s * 21.0f), y + (float)(int)(s * 21.0f), s, rot);
                }

                const cs2d_weapon_def_t *def = cs2d_weapon_get(w);
                const char *name = (def && def->name) ? def->name : "";
                float tw = cs2d_render_text_width_ex(CS2D_FONT_REGULAR, name);
                float sx = (tw > s * 95.0f) ? (s * 95.0f) / tw : 1.0f;
                float tx = x + (float)(int)(s * ((slot == 4 || slot == 6) ? 43.0f : 45.0f));
                weapon_select_text(tx, y + head_y, name, sx, hl);

                char line[32] = "";
                if ((slot == 1 || slot == 2) && w != CS2D_WEP_SHIELD && cs2d_weapon_hud_ammo(w)) {
                    snprintf(line, sizeof(line), "%d | %d", player->clip_ammo[w], player->reserve_ammo[w]);
                } else if (slot == 4 || (slot == 6 && player->clip_ammo[w] > 0)) {
                    snprintf(line, sizeof(line), "%d x", player->clip_ammo[w]);
                }
                if (line[0]) weapon_select_text(tx, y + (float)(int)(s * 18.0f), line, 1.0f, hl);
            }
            x += (float)(int)(s * 155.0f);
        } else {
            float y = head_y;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            for (int k = 0; k < CS2D_SLOT_COUNT; k++) {
                uint8_t w = player->inventory[k];
                if (w == CS2D_WEP_NONE || cs2d_weapon_hud_slot(w) != slot) continue;
                y += (float)(int)(s * 12.0f);
                cs2d_render_rect(x, y, (float)(int)(s * 10.0f), (float)(int)(s * 10.0f),
                                 100.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f, true);
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            float g = (y == head_y) ? 0.0f : 50.0f / 255.0f;
            cs2d_render_sprite_frame(&bank->tex_hud_slotheads, 10, 10, slot - 1, x + 5.0f * s, head_y + 5.0f * s,
                                     10.0f * s, 10.0f * s, 0.0f, 100.0f / 255.0f, g, 0.0f, 1.0f, 4);
            x += (float)(int)(s * 15.0f);
        }
    }
}

void cs2d_render_crosshair(float mx, float my)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (bank && bank->tex_pointer.id) {
        /* gfx/pointer.bmp: 92x92 -> 2x2 grid of 46x46 cells (frame 0 = green crosshair scaled to 20x20) */
        cs2d_render_sprite_frame(&bank->tex_pointer, 46, 46, 0,
                                 mx, my, 20.0f, 20.0f, 0.0f,
                                 1.0f, 1.0f, 1.0f, 0.95f, 0);
    }
}

/*
 * High-Quality CS2D TrueType Font Engine (gfx/fonts/liberationsans.ttf & sourcesans.ttf)
 * Uses GDI+ PrivateFontCollection + TextRenderingHintAntiAlias (unhinted 256-level grayscale
 * to avoid broken TrueType GridFit bytecode instructions in liberationsans.ttf) with a
 * FreeType-style stem-darkening gamma curve (pow(a, 0.62)) and a 512-entry GPU String Texture Cache.
 */
typedef void GpGraphics;
typedef void GpFontCollection;
typedef void GpFontFamily;
typedef void GpFont;
typedef void GpStringFormat;
typedef void GpBrush;
typedef void GpSolidFill;

typedef struct {
    float X;
    float Y;
    float Width;
    float Height;
} GpRectF;

typedef GpStatus (WINAPI *PFN_GdipCreateBitmapFromScan0)(INT width, INT height, INT stride, INT format, BYTE *scan0, GpBitmap **bitmap);
typedef GpStatus (WINAPI *PFN_GdipGetImageGraphicsContext)(GpImage *image, GpGraphics **graphics);
typedef GpStatus (WINAPI *PFN_GdipDeleteGraphics)(GpGraphics *graphics);
typedef GpStatus (WINAPI *PFN_GdipGraphicsClear)(GpGraphics *graphics, ARGB color);
typedef GpStatus (WINAPI *PFN_GdipSetTextRenderingHint)(GpGraphics *graphics, INT mode);
typedef GpStatus (WINAPI *PFN_GdipNewPrivateFontCollection)(GpFontCollection **fontCollection);
typedef GpStatus (WINAPI *PFN_GdipPrivateAddFontFile)(GpFontCollection *fontCollection, const WCHAR *filename);
typedef GpStatus (WINAPI *PFN_GdipCreateFontFamilyFromName)(const WCHAR *name, GpFontCollection *fontCollection, GpFontFamily **fontFamily);
typedef GpStatus (WINAPI *PFN_GdipCreateFont)(const GpFontFamily *fontFamily, float emSize, INT style, INT unit, GpFont **font);
typedef GpStatus (WINAPI *PFN_GdipDeleteFont)(GpFont *font);
typedef GpStatus (WINAPI *PFN_GdipCreateSolidFill)(ARGB color, GpSolidFill **brush);
typedef GpStatus (WINAPI *PFN_GdipDeleteBrush)(GpBrush *brush);
typedef GpStatus (WINAPI *PFN_GdipStringFormatGetGenericTypographic)(GpStringFormat **format);
typedef GpStatus (WINAPI *PFN_GdipCloneStringFormat)(const GpStringFormat *format, GpStringFormat **newFormat);
typedef GpStatus (WINAPI *PFN_GdipSetStringFormatFlags)(GpStringFormat *format, INT flags);
typedef GpStatus (WINAPI *PFN_GdipDeleteStringFormat)(GpStringFormat *format);
typedef GpStatus (WINAPI *PFN_GdipDrawString)(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, const GpBrush *brush);
typedef GpStatus (WINAPI *PFN_GdipMeasureString)(GpGraphics *graphics, const WCHAR *string, INT length, const GpFont *font, const GpRectF *layoutRect, const GpStringFormat *stringFormat, GpRectF *boundingBox, INT *codepointsFitted, INT *linesFilled);

static PFN_GdipCreateBitmapFromScan0             pGdipCreateBitmapFromScan0 = NULL;
static PFN_GdipGetImageGraphicsContext           pGdipGetImageGraphicsContext = NULL;
static PFN_GdipDeleteGraphics                    pGdipDeleteGraphics = NULL;
static PFN_GdipGraphicsClear                     pGdipGraphicsClear = NULL;
static PFN_GdipSetTextRenderingHint              pGdipSetTextRenderingHint = NULL;
static PFN_GdipNewPrivateFontCollection          pGdipNewPrivateFontCollection = NULL;
static PFN_GdipPrivateAddFontFile                pGdipPrivateAddFontFile = NULL;
static PFN_GdipCreateFontFamilyFromName          pGdipCreateFontFamilyFromName = NULL;
static PFN_GdipCreateFont                        pGdipCreateFont = NULL;
static PFN_GdipDeleteFont                        pGdipDeleteFont = NULL;
static PFN_GdipCreateSolidFill                   pGdipCreateSolidFill = NULL;
static PFN_GdipDeleteBrush                       pGdipDeleteBrush = NULL;
static PFN_GdipStringFormatGetGenericTypographic pGdipStringFormatGetGenericTypographic = NULL;
static PFN_GdipCloneStringFormat                 pGdipCloneStringFormat = NULL;
static PFN_GdipSetStringFormatFlags              pGdipSetStringFormatFlags = NULL;
static PFN_GdipDeleteStringFormat                pGdipDeleteStringFormat = NULL;
static PFN_GdipDrawString                        pGdipDrawString = NULL;
static PFN_GdipMeasureString                     pGdipMeasureString = NULL;

#define CS2D_TEXT_SCRATCH_W  1024
#define CS2D_TEXT_SCRATCH_H  48
#define CS2D_TEXT_CACHE_SIZE 512

typedef struct {
    bool           used;
    uint32_t       hash;
    cs2d_font_id_t font_id;
    char           text[192];
    GLuint         tex_id;
    int            tex_w;
    int            tex_h;
    float          width;
    float          height;
    uint32_t       last_used;
} cs2d_text_cache_entry_t;

static bool                    s_fonts_loaded = false;
static GpFontCollection       *s_font_collection = NULL;
static GpFont                 *s_gp_fonts[CS2D_FONT_COUNT] = {0};
static float                   s_font_line_h[CS2D_FONT_COUNT] = { 15.0f, 10.0f, 15.0f, 22.0f, 18.0f };
static GpStringFormat         *s_str_format = NULL;
static GpSolidFill            *s_white_brush = NULL;
static uint32_t               *s_scratch_pixels = NULL;
static GpBitmap               *s_scratch_bitmap = NULL;
static GpGraphics             *s_scratch_gfx = NULL;
static uint8_t                 s_gamma_lut[256];
static cs2d_text_cache_entry_t s_text_cache[CS2D_TEXT_CACHE_SIZE];
static uint32_t                s_text_cache_tick = 0;

static void ensure_ui_fonts(void)
{
    if (s_fonts_loaded || !g_gl_ctx) return;
    if (!init_gdiplus_once() || !g_gdiplus_mod) return;

    pGdipCreateBitmapFromScan0             = (PFN_GdipCreateBitmapFromScan0)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateBitmapFromScan0");
    pGdipGetImageGraphicsContext           = (PFN_GdipGetImageGraphicsContext)(void *)GetProcAddress(g_gdiplus_mod, "GdipGetImageGraphicsContext");
    pGdipDeleteGraphics                    = (PFN_GdipDeleteGraphics)(void *)GetProcAddress(g_gdiplus_mod, "GdipDeleteGraphics");
    pGdipGraphicsClear                     = (PFN_GdipGraphicsClear)(void *)GetProcAddress(g_gdiplus_mod, "GdipGraphicsClear");
    pGdipSetTextRenderingHint              = (PFN_GdipSetTextRenderingHint)(void *)GetProcAddress(g_gdiplus_mod, "GdipSetTextRenderingHint");
    pGdipNewPrivateFontCollection          = (PFN_GdipNewPrivateFontCollection)(void *)GetProcAddress(g_gdiplus_mod, "GdipNewPrivateFontCollection");
    pGdipPrivateAddFontFile                = (PFN_GdipPrivateAddFontFile)(void *)GetProcAddress(g_gdiplus_mod, "GdipPrivateAddFontFile");
    pGdipCreateFontFamilyFromName          = (PFN_GdipCreateFontFamilyFromName)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateFontFamilyFromName");
    pGdipCreateFont                        = (PFN_GdipCreateFont)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateFont");
    pGdipDeleteFont                        = (PFN_GdipDeleteFont)(void *)GetProcAddress(g_gdiplus_mod, "GdipDeleteFont");
    pGdipCreateSolidFill                   = (PFN_GdipCreateSolidFill)(void *)GetProcAddress(g_gdiplus_mod, "GdipCreateSolidFill");
    pGdipDeleteBrush                       = (PFN_GdipDeleteBrush)(void *)GetProcAddress(g_gdiplus_mod, "GdipDeleteBrush");
    pGdipStringFormatGetGenericTypographic = (PFN_GdipStringFormatGetGenericTypographic)(void *)GetProcAddress(g_gdiplus_mod, "GdipStringFormatGetGenericTypographic");
    pGdipCloneStringFormat                 = (PFN_GdipCloneStringFormat)(void *)GetProcAddress(g_gdiplus_mod, "GdipCloneStringFormat");
    pGdipSetStringFormatFlags              = (PFN_GdipSetStringFormatFlags)(void *)GetProcAddress(g_gdiplus_mod, "GdipSetStringFormatFlags");
    pGdipDeleteStringFormat                = (PFN_GdipDeleteStringFormat)(void *)GetProcAddress(g_gdiplus_mod, "GdipDeleteStringFormat");
    pGdipDrawString                        = (PFN_GdipDrawString)(void *)GetProcAddress(g_gdiplus_mod, "GdipDrawString");
    pGdipMeasureString                     = (PFN_GdipMeasureString)(void *)GetProcAddress(g_gdiplus_mod, "GdipMeasureString");

    if (!pGdipCreateBitmapFromScan0 || !pGdipGetImageGraphicsContext || !pGdipSetTextRenderingHint ||
        !pGdipNewPrivateFontCollection || !pGdipPrivateAddFontFile || !pGdipCreateFontFamilyFromName ||
        !pGdipCreateFont || !pGdipCreateSolidFill || !pGdipDrawString || !pGdipMeasureString) {
        return;
    }

    /* Precompute FreeType-style stem-darkening gamma curve (pow(a, 0.78)) */
    s_gamma_lut[0] = 0;
    for (int i = 1; i < 256; i++) {
        float norm = (float)i / 255.0f;
        int   val  = (int)(powf(norm, 0.78f) * 255.0f + 0.5f);
        if (val > 255) val = 255;
        s_gamma_lut[i] = (uint8_t)val;
    }

    s_scratch_pixels = (uint32_t *)calloc((size_t)CS2D_TEXT_SCRATCH_W * (size_t)CS2D_TEXT_SCRATCH_H, 4);
    /* PixelFormat32bppARGB = 0x26200A */
    pGdipCreateBitmapFromScan0(CS2D_TEXT_SCRATCH_W, CS2D_TEXT_SCRATCH_H,
                               CS2D_TEXT_SCRATCH_W * 4, 0x26200A,
                               (BYTE *)s_scratch_pixels, &s_scratch_bitmap);
    if (s_scratch_bitmap) {
        pGdipGetImageGraphicsContext((GpImage *)s_scratch_bitmap, &s_scratch_gfx);
        if (s_scratch_gfx) {
            /* TextRenderingHintAntiAlias = 4 (smooth 256-level grayscale without broken GridFit bytecode) */
            pGdipSetTextRenderingHint(s_scratch_gfx, 4);
        }
    }

    pGdipCreateSolidFill(0xFFFFFFFFu, &s_white_brush);

    const GpStringFormat *generic_typo = NULL;
    if (pGdipStringFormatGetGenericTypographic &&
        pGdipStringFormatGetGenericTypographic((GpStringFormat **)&generic_typo) == 0 &&
        generic_typo && pGdipCloneStringFormat) {
        pGdipCloneStringFormat(generic_typo, &s_str_format);
        if (s_str_format && pGdipSetStringFormatFlags) {
            /* StringFormatFlagsMeasureTrailingSpaces (0x0800) | StringFormatFlagsNoWrap (0x1000) | StringFormatFlagsNoClip (0x4000) */
            pGdipSetStringFormatFlags(s_str_format, 0x0800 | 0x1000 | 0x4000);
        }
    }

    pGdipNewPrivateFontCollection(&s_font_collection);
    if (s_font_collection) {
        WCHAR wpath1[MAX_PATH] = {0};
        WCHAR wpath2[MAX_PATH] = {0};
        GetFullPathNameW(L"gfx\\fonts\\liberationsans.ttf", MAX_PATH, wpath1, NULL);
        GetFullPathNameW(L"gfx\\fonts\\sourcesans.ttf",     MAX_PATH, wpath2, NULL);
        pGdipPrivateAddFontFile(s_font_collection, wpath1);
        pGdipPrivateAddFontFile(s_font_collection, wpath2);
    }

    GpFontFamily *fam_lib = NULL;
    GpFontFamily *fam_src = NULL;
    if (s_font_collection) {
        pGdipCreateFontFamilyFromName(L"Liberation Sans",       s_font_collection, &fam_lib);
        pGdipCreateFontFamilyFromName(L"Source Sans Pro Black", s_font_collection, &fam_src);
    }
    if (!fam_lib) {
        pGdipCreateFontFamilyFromName(L"Segoe UI", NULL, &fam_lib);
    }
    if (!fam_lib) {
        pGdipCreateFontFamilyFromName(L"Arial", NULL, &fam_lib);
    }
    if (!fam_src) {
        fam_src = fam_lib;
    }

    /* UnitPixel = 2; FontStyleRegular = 0, FontStyleBold = 1 */
    if (fam_lib) {
        pGdipCreateFont(fam_lib, 13.0f, 0, 2, &s_gp_fonts[CS2D_FONT_REGULAR]);
        pGdipCreateFont(fam_lib, 10.0f, 0, 2, &s_gp_fonts[CS2D_FONT_SMALL]);
        pGdipCreateFont(fam_lib, 13.0f, 1, 2, &s_gp_fonts[CS2D_FONT_BOLD]);
        pGdipCreateFont(fam_lib, 15.0f, 0, 2, &s_gp_fonts[CS2D_FONT_MENU]);
    }
    if (fam_src) {
        pGdipCreateFont(fam_src, 18.0f, 0, 2, &s_gp_fonts[CS2D_FONT_TITLE]);
    }

    memset(s_text_cache, 0, sizeof(s_text_cache));
    s_fonts_loaded = true;
}

static uint32_t hash_font_string(cs2d_font_id_t font_id, const char *text)
{
    uint32_t h = 2166136261u ^ (uint32_t)font_id;
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        h ^= (uint32_t)*p;
        h *= 16777619u;
    }
    return h;
}

static cs2d_text_cache_entry_t *get_or_create_text_entry(cs2d_font_id_t font_id, const char *text)
{
    if (!text || !text[0]) return NULL;
    ensure_ui_fonts();
    if (!s_fonts_loaded || !s_scratch_gfx) return NULL;
    if (font_id < 0 || font_id >= CS2D_FONT_COUNT) font_id = CS2D_FONT_REGULAR;

    GpFont *gp_font = s_gp_fonts[font_id];
    if (!gp_font) gp_font = s_gp_fonts[CS2D_FONT_REGULAR];
    if (!gp_font) return NULL;

    uint32_t h = hash_font_string(font_id, text);
    s_text_cache_tick++;

    /* 1. Fast path: check cache hit */
    int empty_slot  = -1;
    int oldest_slot = 0;
    uint32_t oldest_tick = 0xFFFFFFFFu;

    for (int i = 0; i < CS2D_TEXT_CACHE_SIZE; i++) {
        cs2d_text_cache_entry_t *e = &s_text_cache[i];
        if (!e->used) {
            if (empty_slot < 0) empty_slot = i;
            continue;
        }
        if (e->hash == h && e->font_id == font_id && strncmp(e->text, text, sizeof(e->text) - 1) == 0) {
            e->last_used = s_text_cache_tick;
            return e;
        }
        if (e->last_used < oldest_tick) {
            oldest_tick = e->last_used;
            oldest_slot = i;
        }
    }

    int slot = (empty_slot >= 0) ? empty_slot : oldest_slot;
    cs2d_text_cache_entry_t *entry = &s_text_cache[slot];
    if (entry->used && entry->tex_id != 0) {
        glDeleteTextures(1, &entry->tex_id);
        entry->tex_id = 0;
    }

    memset(entry, 0, sizeof(*entry));
    entry->used      = true;
    entry->hash      = h;
    entry->font_id   = font_id;
    entry->last_used = s_text_cache_tick;
    strncpy(entry->text, text, sizeof(entry->text) - 1);
    entry->text[sizeof(entry->text) - 1] = '\0';

    /* Convert UTF-8 (or ANSI fallback) to WCHAR */
    WCHAR wtext[192] = {0};
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, entry->text, -1, wtext, 191);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, entry->text, -1, wtext, 191);
    }
    if (wlen > 0) wlen--; /* exclude NUL terminator */
    if (wlen <= 0) return entry;

    GpRectF layout_rect = { 0.0f, 0.0f, (float)(CS2D_TEXT_SCRATCH_W - 4), (float)CS2D_TEXT_SCRATCH_H };
    GpRectF bbox        = { 0.0f, 0.0f, 0.0f, 0.0f };
    pGdipMeasureString(s_scratch_gfx, wtext, wlen, gp_font, &layout_rect, s_str_format, &bbox, NULL, NULL);

    int tw = (int)ceilf(bbox.Width) + 2;
    int th = (int)ceilf(bbox.Height) + 2;
    if (tw < 2) tw = 2;
    if (th < 2) th = 2;
    if (tw > CS2D_TEXT_SCRATCH_W) tw = CS2D_TEXT_SCRATCH_W;
    if (th > CS2D_TEXT_SCRATCH_H) th = CS2D_TEXT_SCRATCH_H;

    entry->width  = ceilf(bbox.Width);
    entry->height = ceilf(bbox.Height);
    entry->tex_w  = tw;
    entry->tex_h  = th;

    /* Clear active region to opaque black (0xFF000000) so GDI+ blends white coverage directly into R/G/B */
    for (int y = 0; y < th; y++) {
        uint32_t *row = &s_scratch_pixels[y * CS2D_TEXT_SCRATCH_W];
        for (int x = 0; x < tw; x++) {
            row[x] = 0xFF000000u;
        }
    }

    GpRectF draw_rect = { 0.0f, 0.0f, (float)tw, (float)th };
    pGdipDrawString(s_scratch_gfx, wtext, wlen, gp_font, &draw_rect, s_str_format, (GpBrush *)s_white_brush);

    uint8_t *rgba = (uint8_t *)malloc((size_t)tw * (size_t)th * 4);
    if (rgba) {
        for (int y = 0; y < th; y++) {
            const uint32_t *src_row = &s_scratch_pixels[y * CS2D_TEXT_SCRATCH_W];
            uint8_t        *dst_row = &rgba[y * tw * 4];
            for (int x = 0; x < tw; x++) {
                uint32_t px = src_row[x];
                uint8_t cov = (uint8_t)((px >> 16) & 0xFF);
                dst_row[x * 4 + 0] = 255;
                dst_row[x * 4 + 1] = 255;
                dst_row[x * 4 + 2] = 255;
                dst_row[x * 4 + 3] = s_gamma_lut[cov];
            }
        }

        GLuint gl_id = 0;
        glGenTextures(1, &gl_id);
        glBindTexture(GL_TEXTURE_2D, gl_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glBindTexture(GL_TEXTURE_2D, 0);
        free(rgba);

        entry->tex_id = gl_id;
    }

    return entry;
}

void cs2d_render_text_ex(cs2d_font_id_t font_id, float x, float y, const char *text, float r, float g, float b, float a)
{
    cs2d_render_text_scaled_ex(font_id, x, y, text, 1.0f, r, g, b, a);
}

void cs2d_render_text_scaled_ex(cs2d_font_id_t font_id, float x, float y, const char *text, float scale_x,
                                float r, float g, float b, float a)
{
    cs2d_text_cache_entry_t *e = get_or_create_text_entry(font_id, text);
    if (!e || e->tex_id == 0) return;

    float rx = floorf(x);
    float ry = floorf(y);
    float rw = (float)e->tex_w * scale_x;
    float rh = (float)e->tex_h;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, e->tex_id);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(rx,      ry);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(rx + rw, ry);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(rx + rw, ry + rh);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(rx,      ry + rh);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void cs2d_render_text_measure(cs2d_font_id_t font_id, const char *text, float *out_w, float *out_h)
{
    if (out_w) *out_w = 0.0f;
    if (out_h) *out_h = cs2d_render_font_line_height(font_id);
    if (!text || !text[0]) return;

    cs2d_text_cache_entry_t *e = get_or_create_text_entry(font_id, text);
    if (e) {
        if (out_w) *out_w = e->width;
        if (out_h) *out_h = e->height;
    }
}

float cs2d_render_text_width_ex(cs2d_font_id_t font_id, const char *text)
{
    float w = 0.0f;
    cs2d_render_text_measure(font_id, text, &w, NULL);
    return w;
}

float cs2d_render_text_prefix_width(cs2d_font_id_t font_id, const char *text, int len)
{
    if (!text || len <= 0) return 0.0f;
    ensure_ui_fonts();
    if (!s_fonts_loaded || !s_scratch_gfx) return 0.0f;
    if (font_id < 0 || font_id >= CS2D_FONT_COUNT) font_id = CS2D_FONT_REGULAR;
    GpFont *gp_font = s_gp_fonts[font_id] ? s_gp_fonts[font_id] : s_gp_fonts[CS2D_FONT_REGULAR];
    if (!gp_font) return 0.0f;

    WCHAR wtext[256];
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, len, wtext, 255);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, text, len, wtext, 255);
    }
    if (wlen <= 0) return 0.0f;

    GpRectF layout_rect = { 0.0f, 0.0f, (float)(CS2D_TEXT_SCRATCH_W - 4), (float)CS2D_TEXT_SCRATCH_H };
    GpRectF bbox        = { 0.0f, 0.0f, 0.0f, 0.0f };
    pGdipMeasureString(s_scratch_gfx, wtext, wlen, gp_font, &layout_rect, s_str_format, &bbox, NULL, NULL);
    return bbox.Width;
}

float cs2d_render_font_line_height(cs2d_font_id_t font_id)
{
    if (font_id < 0 || font_id >= CS2D_FONT_COUNT) return 15.0f;
    return s_font_line_h[font_id];
}

void cs2d_render_text(float x, float y, const char *text, float r, float g, float b, float a)
{
    cs2d_render_text_ex(CS2D_FONT_REGULAR, x, y, text, r, g, b, a);
}

void cs2d_render_text_small(float x, float y, const char *text, float r, float g, float b, float a)
{
    cs2d_render_text_ex(CS2D_FONT_SMALL, x, y, text, r, g, b, a);
}

float cs2d_render_text_width(const char *text)
{
    return cs2d_render_text_width_ex(CS2D_FONT_REGULAR, text);
}

void cs2d_render_gradient_rect(
    float x, float y, float w, float h,
    float r_top, float g_top, float b_top, float a_top,
    float r_bot, float g_bot, float b_bot, float a_bot
)
{
    glDisable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(r_top, g_top, b_top, a_top);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glColor4f(r_bot, g_bot, b_bot, a_bot);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

void cs2d_render_scissor_begin(int x, int y, int w, int h)
{
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, g_viewport_h - (y + h), w, h);
}

void cs2d_render_scissor_end(void)
{
    glDisable(GL_SCISSOR_TEST);
}



/* Breakable effects (cs2d_fx): tile quarters, material fragments and explosions, all fading out */
/* ---------------------------------------------------------------------------
 * Dynamic objects (Tdo.draw_ground 0x140284a40, draw_obstacle 0x14010e5b0, draw_wall 0x14028f520)
 * --------------------------------------------------------------------------- */

/*
 * Tdo.setteamcolor (0x14005ec20): in team game modes T red, CT blue, neutral yellow; otherwise the local
 * player's own objects yellow and everybody else's green.
 */
static void dynobj_team_color(const cs2d_dynobj_t *o, bool team_game, int local_id, float rgb[3])
{
    if (team_game) {
        if (o->team == 1) { rgb[0] = 1.0f; rgb[1] = 0.0f; rgb[2] = 0.0f; return; }
        if (o->team == 2) { rgb[0] = 0.0f; rgb[1] = 0.0f; rgb[2] = 1.0f; return; }
        if (o->team != 0) { rgb[0] = rgb[1] = rgb[2] = 1.0f; return; } /* Color left unchanged */
    } else if (o->player == 0 || o->player != local_id) {
        rgb[0] = 0.0f; rgb[1] = 1.0f; rgb[2] = 0.0f;
        return;
    }
    rgb[0] = 1.0f; rgb[1] = 1.0f; rgb[2] = 0.0f;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* gfx/buildings.png frame (midhandle) at the tile center */
static void dynobj_frame(const cs2d_gfx_bank_t *bank, float cx, float cy, int frame, float rot,
                         float r, float g, float b, float a, int blend)
{
    cs2d_render_sprite_frame(&bank->tex_buildings, 32, 32, frame, cx, cy, 32.0f, 32.0f, rot, r, g, b, a, blend);
}

/* gfx/sprites/flare1.bmp (32x32, midhandle) */
static void dynobj_flare(const cs2d_gfx_bank_t *bank, float cx, float cy, float rot, float scale,
                         const float rgb[3], float a)
{
    cs2d_render_sprite_frame(&bank->tex_flare1, 32, 32, 0, cx, cy, 32.0f * scale, 32.0f * scale, rot,
                             rgb[0], rgb[1], rgb[2], a, 4);
}

/* incbindata/shadow.bmp in black (Max2D alpha blend) */
static void dynobj_shadow(const cs2d_gfx_bank_t *bank, float cx, float cy, float scale, float a)
{
    cs2d_render_sprite_frame(&bank->tex_leaf, 32, 32, 0, cx, cy, 32.0f * scale, 32.0f * scale, 0.0f,
                             0.0f, 0.0f, 0.0f, a, 0);
}

void cs2d_render_dynobjs(const cs2d_dynobj_world_t *w, cs2d_dynobj_layer_t layer, float cam_x, float cam_y,
                         int screen_w, int screen_h, bool team_game, int local_id)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!w || !bank || !bank->tex_buildings.id) return;
    int ms = (int)GetTickCount();
    for (int i = 0; i < CS2D_DYNOBJ_MAX; i++) {
        const cs2d_dynobj_t *o = &w->objs[i];
        if (!o->active) continue;
        /* Culling: (int)(x - camx) + 31 < 850 + 32, same for y (0x372 / 0x200 on the 850x480 screen) */
        int sx = (int)(o->x - cam_x), sy = (int)(o->y - cam_y);
        if ((unsigned)(sx + 31) >= (unsigned)(screen_w + 32) || (unsigned)(sy + 31) >= (unsigned)(screen_h + 32)) continue;
        float cx = o->x + 16.0f - cam_x, cy = o->y + 16.0f - cam_y;
        float tc[3];
        dynobj_team_color(o, team_game, local_id, tc);
        /* Pulse used by the gate field, teleporters and mines: sin((ms >> 3) + x + y) */
        double pulse = sin(((double)(ms >> 3) + o->x + o->y) * 0.017453292519943295);

        if (layer == CS2D_DO_LAYER_OBSTACLE) {
            if (o->typ == CS2D_DO_BARRICADE) dynobj_frame(bank, cx, cy, 0, 0.0f, 1, 1, 1, 1, 0);
            continue;
        }
        if (layer == CS2D_DO_LAYER_GROUND) {
            switch (o->typ) {
                case CS2D_DO_BARBED_WIRE: {
                    /* Rotated by rot + 90; a hit ([36,id,5]) wobbles it for 60 frames */
                    float r = o->rot + 90.0f;
                    if (o->mode > 0) r += (float)((double)o->mode * 0.3333333333333333 * sin((double)(o->mode * 40) * 0.017453292519943295));
                    dynobj_frame(bank, cx, cy, 1, r, 1, 1, 1, 1, 0);
                    break;
                }
                case CS2D_DO_GATE_FIELD:
                    dynobj_shadow(bank, cx, cy, 2.0f, 0.4f);
                    dynobj_frame(bank, cx, cy, 5, 0.0f, tc[0], tc[1], tc[2], (float)(fabs(pulse * 0.7) + 0.3), 4);
                    break;
                case CS2D_DO_SITE:
                    dynobj_frame(bank, cx, cy, 7, 0.0f, 1, 1, 1, 1, 0);
                    break;
                case CS2D_DO_TELE_ENTRANCE:
                    dynobj_shadow(bank, cx, cy, 2.3f, 0.5f);
                    dynobj_frame(bank, cx, cy, 8, 0.0f, tc[0], tc[1], tc[2], 1.0f, 4);
                    dynobj_frame(bank, cx, cy, 8, 0.0f, tc[0], tc[1], tc[2], clamp01((float)pulse), 4);
                    if (o->mode == 1 && bank->tex_flare1.id) {
                        /* Charged: four flares circling at 20 px */
                        float sc = (float)((fabs(sin((double)(ms / 30) * 0.017453292519943295)) + 1.0) * 0.5);
                        for (int k = 90; k != 450; k += 90) {
                            double a = (double)(ms / 10 + k) * 0.017453292519943295;
                            dynobj_flare(bank, cx + (float)(sin(a) * 20.0), cy - (float)(cos(a) * 20.0), 0.0f, sc, tc, 1.0f);
                        }
                    }
                    break;
                case CS2D_DO_TELE_EXIT:
                    dynobj_shadow(bank, cx, cy, 2.3f, 0.5f);
                    dynobj_frame(bank, cx, cy, 9, 0.0f, tc[0], tc[1], tc[2], (float)(fabs(pulse * 0.3) + 0.7), 4);
                    break;
                case CS2D_DO_MINE:
                    if (!bank->tex_flare1.id) break;
                    dynobj_shadow(bank, cx, cy, 0.5f, 0.4f);
                    dynobj_flare(bank, cx, cy, (float)ms * 0.1f, 0.3f, tc,
                                 clamp01((float)sin(((double)ms * 0.1 + o->x + o->y) * 0.017453292519943295)));
                    break;
                default:
                    break;
            }
            continue;
        }

        /* Wall layer */
        switch (o->typ) {
            case CS2D_DO_WALL_1: case CS2D_DO_WALL_2: case CS2D_DO_WALL_3:
                dynobj_frame(bank, cx, cy, o->typ - 1, 0.0f, 1, 1, 1, 1, 0);
                break;
            case CS2D_DO_DISPENSER: /* FUN_14028b980 */
                dynobj_frame(bank, cx, cy, 6, 0.0f, 1, 1, 1, 1, 0);
                if (bank->tex_flare1.id) dynobj_flare(bank, cx, cy, (float)(ms / 10), 2.0f, tc, 1.0f);
                break;
            case CS2D_DO_TURRET: case CS2D_DO_DUAL_TURRET: case CS2D_DO_TRIPLE_TURRET: { /* FUN_14028c630 */
                dynobj_frame(bank, cx, cy, 6, 0.0f, 1, 1, 1, 1, 0);
                if (bank->tex_flare1.id) dynobj_flare(bank, cx, cy, (float)(ms / 10), 1.0f, tc, 1.0f);
                const cs2d_texture_t *head = &bank->tex_turret[o->typ == CS2D_DO_TURRET ? 0 : (o->typ == CS2D_DO_DUAL_TURRET ? 1 : 2)];
                if (head->id) {
                    cs2d_render_sprite_frame(head, head->width, head->height, 0, cx, cy, (float)head->width,
                                             (float)head->height, o->rot, 1, 1, 1, 1, 0);
                }
                break;
            }
            case CS2D_DO_SUPPLY: case CS2D_DO_SUPER_SUPPLY: /* FUN_14028dba0 */
                dynobj_frame(bank, cx, cy, 6, 0.0f, 1, 1, 1, 1, 0);
                if (bank->tex_flare1.id) {
                    float a = (float)fabs(sin(((double)ms * 0.1 + o->x + o->y) * 0.017453292519943295));
                    dynobj_flare(bank, cx, cy, (float)ms * 0.06666667f, 0.6f, tc, a);
                }
                break;
            default:
                break;
        }
    }
}

void cs2d_render_build_helper(int ptx, int pty, int tx, int ty, bool ok, float cam_x, float cam_y)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank || !bank->tex_buildhelper.id) return;
    const cs2d_texture_t *t = &bank->tex_buildhelper;
    /* Additive, alpha sin((ms >> 3) deg) (SetAlpha with a raw sine), top-left at tile * 32 - 25.
     * Floored at 0.35 (our choice) so the marker never disappears while the raw sine is <= 0. */
    int ms = (int)GetTickCount();
    float a = 0.35f + 0.65f * clamp01((float)sin((double)(ms >> 3) * 0.017453292519943295));
    float w = (float)t->width, h = (float)t->height;
    float px = (float)(ptx * 32 - (int)cam_x - 25), py = (float)(pty * 32 - (int)cam_y - 25);
    cs2d_render_sprite_frame(t, t->width, t->height, 0, px + w * 0.5f, py + h * 0.5f, w, h, 0.0f, 1, 1, 0, a, 4);
    float bx = (float)(tx * 32 - (int)cam_x - 25), by = (float)(ty * 32 - (int)cam_y - 25);
    cs2d_render_sprite_frame(t, t->width, t->height, 0, bx + w * 0.5f, by + h * 0.5f, w, h, 0.0f,
                             ok ? 0.0f : 1.0f, ok ? 1.0f : 0.0f, 0.0f, a, 4);
}

void cs2d_render_fx(const struct cs2d_fx_s *fx, const cs2d_map_t *map, const cs2d_texture_t *tileset_tex, float cam_x, float cam_y)
{
    if (!fx) return;
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    cam_x = roundf(cam_x);
    cam_y = roundf(cam_y);

    /* Cases on the floor (and in flight) */
    for (int i = 0; bank && i < CS2D_FX_MAX_SHELLS; i++) {
        const cs2d_fx_particle_t *c = &fx->shells[i];
        if (c->kind != CS2D_FX_SHELL) continue;
        cs2d_render_sprite_frame(&bank->tex_shells, 8, 8, c->frame, c->x - cam_x, c->y - cam_y, 8.0f, 8.0f,
                                 c->rot, 1.0f, 1.0f, 1.0f, 1.0f, 0);
    }
    bool has_tileset = (map && tileset_tex && tileset_tex->id != 0 && tileset_tex->width >= 32);
    int tile_src = (map && map->use_64x64_tiles == 1) ? 64 : 32;
    int cols = has_tileset ? (tileset_tex->width / tile_src) : 1;
    if (cols < 1) cols = 1;

    for (int i = 0; i < fx->count; i++) {
        const cs2d_fx_particle_t *p = &fx->p[i];
        float t = p->max_life > 0.0f ? p->life / p->max_life : 0.0f; /* 1 -> 0 */
        float sx = p->x - cam_x, sy = p->y - cam_y;

        if (p->kind == CS2D_FX_TILE_PART && has_tileset) {
            /* UV quarter of the tile frame (same layout as draw_tile_frame) */
            int col = p->frame % cols, row = p->frame / cols;
            float half = (float)tile_src * 0.5f;
            float ox = (p->quarter & 1) ? half : 0.0f, oy = (p->quarter & 2) ? half : 0.0f;
            float bx, by, gw, gh;
            if (tileset_tex->pad > 0 && tileset_tex->cell_w == tile_src && tileset_tex->gpu_w > 0 && tileset_tex->gpu_h > 0) {
                bx = (float)(col * (tile_src + tileset_tex->pad * 2) + tileset_tex->pad);
                by = (float)(row * (tile_src + tileset_tex->pad * 2) + tileset_tex->pad);
                gw = (float)tileset_tex->gpu_w;
                gh = (float)tileset_tex->gpu_h;
            } else {
                bx = (float)(col * tile_src);
                by = (float)(row * tile_src);
                gw = (float)tileset_tex->width;
                gh = (float)tileset_tex->height;
            }
            float u0 = (bx + ox) / gw, v0 = (by + oy) / gh;
            float u1 = (bx + ox + half) / gw, v1 = (by + oy + half) / gh;
            float hs = (p->size + (1.0f - t) * 4.0f) * 0.5f; /* Grows while it fades */

            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tileset_tex->id);
            glColor4f(1.0f, 1.0f, 1.0f, t);
            glPushMatrix();
            glTranslatef(sx, sy, 0.0f);
            glRotatef(p->rot, 0.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
                glTexCoord2f(u0, v0); glVertex2f(-hs, -hs);
                glTexCoord2f(u1, v0); glVertex2f( hs, -hs);
                glTexCoord2f(u1, v1); glVertex2f( hs,  hs);
                glTexCoord2f(u0, v1); glVertex2f(-hs,  hs);
            glEnd();
            glPopMatrix();
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        } else if (p->kind == CS2D_FX_FRAGMENT && bank) {
            cs2d_render_sprite_frame(&bank->tex_fragments, 32, 32, p->frame, sx, sy, p->size, p->size,
                                     p->rot, p->r, p->g, p->b, t, 0);
        } else if (p->kind == CS2D_FX_MUZZLE && bank) {
            /* Yellow light around the shooter, then the flash stretched along the aim */
            cs2d_render_sprite_frame(&bank->tex_flare3, 0, 0, 0, p->x2 - cam_x, p->y2 - cam_y, 320.0f, 320.0f,
                                     0.0f, 1.0f, 0.85f, 0.35f, 0.6f * t, 3);
            cs2d_render_sprite_frame(&bank->tex_flare_x, 0, 0, 0, sx, sy, p->size * 1.7f, p->size * 2.9f,
                                     p->rot, 1.0f, 1.0f, 1.0f, t, 3);
        } else if (p->kind == CS2D_FX_TRACER) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            cs2d_render_line(sx, sy, p->x2 - cam_x, p->y2 - cam_y, 1.0f, 0.9f, 0.4f, 0.6f * t);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (p->kind == CS2D_FX_GLOWDOT && bank) {
            cs2d_render_sprite_frame(&bank->tex_glowdot, 0, 0, 0, sx, sy, p->size + 1.0f, p->size + 1.0f,
                                     0.0f, p->r, p->g, p->b, t, 3);
        } else if (p->kind == CS2D_FX_EXPLOSION && bank) {
            int frame = (int)((1.0f - t) * 25.0f);
            if (frame > 24) frame = 24;
            /* Additive (LIGHTBLEND) */
            cs2d_render_sprite_frame(&bank->tex_explosion, 64, 64, frame, sx, sy, p->size, p->size,
                                     p->rot, 1.0f, 1.0f, 1.0f, 1.0f, 4);
        } else if (p->kind == CS2D_FX_KNIFESLASH && bank && bank->tex_knifeslash.id) {
            /* Tp type 13 (0x140265220..0x140265361): gfx/knifeslash.bmp, LIGHTBLEND (4), rotated by angle */
            float w = (float)bank->tex_knifeslash.width;
            float h = (float)bank->tex_knifeslash.height;
            cs2d_render_sprite_frame(&bank->tex_knifeslash, 0, 0, 0, sx, sy, w, h,
                                     p->rot, 1.0f, 1.0f, 1.0f, t, 4);
        }
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
