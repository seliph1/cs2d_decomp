#include "cs2d_render.h"
#include "cs2d_gl.h"
#include "cs2d_light.h"
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

bool cs2d_render_has_context(void)
{
    return g_gl_ctx != NULL;
}

/*
 * Image files are decoded by the portable asset decoder (src/cs2d_image.c). GDI+ is no longer part
 * of that path: the only thing still starting gdiplus.dll is the TrueType text engine further down.
 */
#include "cs2d_image.h"

/* Minimal GDI+ Flat API bindings — still used by the TrueType text engine (see the font block) */
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

static HMODULE            g_gdiplus_mod = NULL;
static ULONG_PTR          g_gdiplus_token = 0;
static PFN_GdiplusStartup pGdiplusStartup = NULL;

static bool init_gdiplus_once(void)
{
    if (pGdiplusStartup) return true;
    g_gdiplus_mod = LoadLibraryA("gdiplus.dll");
    if (!g_gdiplus_mod) return false;

    pGdiplusStartup = (PFN_GdiplusStartup)(void *)GetProcAddress(g_gdiplus_mod, "GdiplusStartup");
    if (!pGdiplusStartup) return false;

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

static bool s_shadows_enabled   = true;
static int  s_shadow_intensity  = 50;   /* "shadows" (0..100, default 50, DAT_140a14f7b) */
static bool s_player_shadows    = true; /* "playershadows" (0|1, default 1, DAT_140ddf184) */
static bool s_item_shadows      = true; /* "itemshadows" (0|1, default 1, DAT_140ddf183) */
static bool s_vsync             = true; /* "vsync" (0|1, default 1, DAT_140a12960) */

/* Player cast shadow offset in px, set from the map's sun angle by cs2d_render_map_ex */
static float s_player_shadow_dx = 3.62f;
static float s_player_shadow_dy = 3.62f;

/*
 * Our choice, not from REA: the original Tpl.draw (0x140121160) draws incbindata/shadow.bmp at scale 1.5 under the
 * player. We cast the player's silhouette instead, along the same sun direction and with the same length formula
 * as an obstacle in the shadow map (dz = -(h / 2) - h * |sin(sun - 180)| / 2), for h = 6 px so the shadow stays
 * close to the body (225 deg -> (+3.6, +3.6)).
 */
static void set_player_shadow_offset(const cs2d_map_t *map)
{
    float sun_angle = 225.0f;
    if (map->daylight_time > 999) {
        sun_angle = (float)((map->daylight_time - 1000) % 360);
    } else if (map->daylight_time >= 0 && map->daylight_time < 360) {
        sun_angle = (float)map->daylight_time;
    }
    const float h = 6.0f;
    float rad_sun  = sun_angle * (3.14159265f / 180.0f);
    float s_factor = fabsf(sinf((sun_angle - 180.0f) * (3.14159265f / 180.0f))) * 0.5f;
    float dz       = -(h * 0.5f) - h * s_factor;
    s_player_shadow_dx = dz * cosf(rad_sun);
    s_player_shadow_dy = dz * sinf(rad_sun);
}

/*
 * Reconstructed from CS2D.exe maskMode jump table (0x1401ad6a9 & 0x140062db0):
 *   mask_mode == 0: Opaque / embedded alpha only
 *   mask_mode == 1: Black (0,0,0) color key (hud_nums.bmp, hud_symbols.bmp, hud_radar.bmp)
 *   mask_mode == 2: Magenta (255,0,255) color key or embedded 32-bit alpha (players, weapons, pointer.bmp)
 *   mask_mode == 3: Grayscale luminance-to-alpha mask (0.21*R + 0.72*G + 0.07*B -> A, RGB = 255,255,255)
 *                   (shadowmap.bmp, decals.bmp, flare1.bmp, flare3.bmp, fragments.bmp, blood.bmp)
 */
void cs2d_render_apply_mask_mode(uint32_t *rgba, int total_px, bool has_embedded_alpha, int mask_mode)
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
void cs2d_render_dilate_transparent(uint32_t *rgba, int w, int h, int cell_w, int cell_h)
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
uint32_t *cs2d_render_downsample_2x(const uint32_t *src, int src_w, int src_h, int *out_w, int *out_h)
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

/* CPU copy of an image as uploaded (after its mask mode), kept when s_capture points to it during a load */
typedef struct {
    uint32_t *px;
    int       w, h;
} rgba_image_t;

static rgba_image_t *s_capture = NULL;

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
    cs2d_render_apply_mask_mode(rgba, w * h, has_embedded_alpha, mask_mode);
    cs2d_render_dilate_transparent(rgba, w, h, cell_w, cell_h);
    if (s_capture && !s_capture->px) {
        s_capture->px = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
        if (s_capture->px) {
            memcpy(s_capture->px, rgba, (size_t)w * (size_t)h * 4);
            s_capture->w = w;
            s_capture->h = h;
        }
    }

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
        uint32_t *mip1 = (gpu_w >= 4 && gpu_h >= 4) ? cs2d_render_downsample_2x(upload_buf, gpu_w, gpu_h, &m1_w, &m1_h) : NULL;
        if (mip1) {
            glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, (GLsizei)m1_w, (GLsizei)m1_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip1);
            int m2_w = 0, m2_h = 0;
            uint32_t *mip2 = (m1_w >= 4 && m1_h >= 4) ? cs2d_render_downsample_2x(mip1, m1_w, m1_h, &m2_w, &m2_h) : NULL;
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
    tex->src_x  = 0;
    tex->src_y  = 0;
}

uint32_t cs2d_render_upload_atlas_page(const uint32_t *rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0 || !g_gl_ctx) return 0;

    GLuint tid = 0;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    int m1_w = 0, m1_h = 0;
    uint32_t *mip1 = (w >= 4 && h >= 4) ? cs2d_render_downsample_2x(rgba, w, h, &m1_w, &m1_h) : NULL;
    if (mip1) {
        glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, (GLsizei)m1_w, (GLsizei)m1_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mip1);
        int m2_w = 0, m2_h = 0;
        uint32_t *mip2 = (m1_w >= 4 && m1_h >= 4) ? cs2d_render_downsample_2x(mip1, m1_w, m1_h, &m2_w, &m2_h) : NULL;
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
    return (uint32_t)tid;
}

/* Applies the CS2D mask mode to a decoded image and uploads it (the pixels are modified in place) */
static bool upload_decoded_image(cs2d_texture_t *tex, cs2d_image_t *img, int mask_mode, int cell_w, int cell_h)
{
    if (!img || !img->rgba || img->width <= 0 || img->height <= 0) return false;
    upload_rgba_to_texture(tex, img->rgba, img->width, img->height,
                           img->has_embedded_alpha, mask_mode, cell_w, cell_h);
    return true;
}

/*
 * Loader for everything that is not a hand-rolled 24/32-bit BMP: PNG tilesets and sprites, paletted
 * BMPs such as gfx/flare3.bmp, and gfx/hud_symbols.bmp, which is PNG data inside a .bmp file.
 */
static bool load_image_portable(cs2d_texture_t *tex, const char *filepath, int mask_mode, int cell_w, int cell_h)
{
    cs2d_image_t img;
    if (!cs2d_image_load(&img, filepath)) return false;

    bool ok = upload_decoded_image(tex, &img, mask_mode, cell_w, cell_h);
    cs2d_image_free(&img);
    return ok;
}

/*
 * Loads the largest frame of a .ico file (the 256x256 logo frame of cs2d.ico is what the original
 * menu shows; GdipCreateBitmapFromFile used to pick one of the 16x16 / 32x32 frames instead).
 */
static bool load_ico_largest_frame(cs2d_texture_t *tex, const char *filepath, int mask_mode)
{
    cs2d_image_t img;
    if (!cs2d_image_load_ico(&img, filepath)) {
        /* Not an icon container after all: let the general decoder sniff the bytes */
        return load_image_portable(tex, filepath, mask_mode, 0, 0);
    }

    bool ok = upload_decoded_image(tex, &img, mask_mode, 0, 0);
    cs2d_image_free(&img);
    return ok;
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
    cs2d_render_set_vsync(s_vsync);

    /* Load the GL 3.3 entry points and build the shader/batcher backend (cs2d_gl.c) */
    if (!cs2d_gl_load()) {
        SDL_GL_DestroyContext(g_gl_ctx);
        g_gl_ctx = NULL;
        return false;
    }

    glEnable(GL_MULTISAMPLE);
    cs2d_render_resize(width, height);

    /* Automatically initialize the global CS2D graphics bank now that GL context is active */
    g_gfx_bank.initialized = false;
    cs2d_gfx_bank_init(&g_gfx_bank);

    return true;
}

void cs2d_render_shutdown(SDL_Window *window)
{
    (void)window;
    cs2d_gfx_bank_free(&g_gfx_bank);
    cs2d_gl_shutdown();
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
    cs2d_gl_viewport(width, height);
}

void cs2d_render_begin_frame(void)
{
    cs2d_gl_begin();
    glClear(GL_COLOR_BUFFER_BIT);
}

void cs2d_render_end_frame(SDL_Window *window)
{
    cs2d_gl_end();
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
        /* Not a raw BMP (e.g. PNG tileset or gfx/hud_symbols.bmp): portable decoder */
        return load_image_portable(tex, filepath, mask_mode, cell_w, cell_h);
    }

    int width = *(int32_t *)&header[18];
    int height = *(int32_t *)&header[22];
    int bpp = *(int16_t *)&header[28];
    uint32_t data_offset = *(uint32_t *)&header[10];

    if (width <= 0 || height == 0 || (bpp != 24 && bpp != 32)) {
        fclose(f);
        /* Paletted (gfx/flare3.bmp), 16-bit or RLE-compressed BMP: portable decoder */
        return load_image_portable(tex, filepath, mask_mode, cell_w, cell_h);
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
 * Tile blending (Ttileblend). A tile whose modifier has (mod & 0xc0) == 0x40 carries a blend frame (map byte, kept in
 * tile_data[3]). Ttileblend.updatetile (0x1400ae560) takes the neighbour in direction frame & 7 (0 up, 1 up-right,
 * 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left; outside the map = tile 0); when it is the same tile
 * the 0x40 flag is cleared, else FUN_14005e2e0 builds (once per frame, tile, neighbour) an image that mixes the two
 * tileset frames: per pixel and per channel (alpha too) a * (1 - f) + b * f, f = red of gfx/blendmap.bmp frame
 * `frame` (40 frames of 32x32, 0x140a0dcb0) at (x, y) / (tile size / 32). The tile is then drawn with that image and
 * its own rotation and brightness (0x140270a40). The game builds them at map load when DAT_140ddf17f is 1
 * ("Generating Tile Blending"); we build each image the first time it is drawn.
 */
#define TILEBLEND_SOURCES    4
#define TILEBLEND_MAX        1024
#define TILEBLEND_ATLAS_COLS 16

typedef struct {
    uint32_t       tileset_id;            /* GL id of the tileset these pixels were uploaded to */
    rgba_image_t   tileset;               /* tileset pixels after its mask mode */
    int            tile_src;              /* cell size of the atlas below */
    uint8_t        key[TILEBLEND_MAX][3]; /* blend frame, own tile, neighbour tile */
    int            count;
    bool           dirty;
    cs2d_texture_t atlas;                 /* TILEBLEND_ATLAS_COLS cells per row */
    uint32_t      *atlas_px;
    int            atlas_rows;
} tileblend_source_t;

static tileblend_source_t s_tileblend[TILEBLEND_SOURCES];
static rgba_image_t       s_blendmap;
static bool               s_blendmap_tried = false;

static void tileblend_free(tileblend_source_t *src)
{
    free(src->tileset.px);
    free(src->atlas_px);
    cs2d_texture_free(&src->atlas);
    memset(src, 0, sizeof(*src));
}

/* Called when a tileset is loaded: GL ids are reused, so a new upload with the same id replaces the old entry */
static void tileblend_register(uint32_t tileset_id, rgba_image_t *img)
{
    tileblend_source_t *slot = NULL;
    for (int i = 0; i < TILEBLEND_SOURCES && !slot; i++) {
        if (s_tileblend[i].tileset_id == tileset_id) slot = &s_tileblend[i];
    }
    for (int i = 0; i < TILEBLEND_SOURCES && !slot; i++) {
        if (s_tileblend[i].tileset_id == 0) slot = &s_tileblend[i];
    }
    if (!slot) slot = &s_tileblend[0];
    tileblend_free(slot);
    slot->tileset_id = tileset_id;
    slot->tileset = *img;
    img->px = NULL;
}

static tileblend_source_t *tileblend_find(uint32_t tileset_id)
{
    for (int i = 0; i < TILEBLEND_SOURCES; i++) {
        if (tileset_id && s_tileblend[i].tileset_id == tileset_id && s_tileblend[i].tileset.px) return &s_tileblend[i];
    }
    return NULL;
}

static bool tileblend_load_blendmap(void)
{
    if (!s_blendmap_tried) {
        s_blendmap_tried = true;
        cs2d_texture_t tmp;
        s_capture = &s_blendmap;
        cs2d_texture_load_ex(&tmp, "gfx/blendmap.bmp", 2);
        s_capture = NULL;
        cs2d_texture_free(&tmp);
    }
    return s_blendmap.px != NULL;
}

/* Ttileblend.updatetile: the neighbour tile id in direction dir, 0 outside the map */
uint8_t cs2d_render_tileblend_neighbour(const cs2d_map_t *map, int tx, int ty, int dir)
{
    static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    int nx = tx + dx[dir], ny = ty + dy[dir];
    if (nx < 0 || ny < 0 || nx >= (int)map->width || ny >= (int)map->height) return 0;
    return map->tiles[(size_t)ny * map->width + (size_t)nx];
}

/* FUN_14005e2e0: per channel (alpha too) a * (1 - f) + b * f, f = mask * 0.003921569, truncated */
uint32_t cs2d_render_tileblend_mix(uint32_t a, uint32_t b, uint8_t mask)
{
    float f = (float)mask * 0.003921569f;
    uint32_t out = 0;
    for (int c = 0; c < 32; c += 8) {
        float v = (float)((a >> c) & 0xff) * (1.0f - f) + (float)((b >> c) & 0xff) * f;
        out |= ((uint32_t)(int)v & 0xffu) << c;
    }
    return out;
}

int cs2d_render_tileblend_mask(int frame, int x, int y)
{
    if (frame < 0 || frame >= 40 || x < 0 || y < 0 || x >= 32 || y >= 32 || !tileblend_load_blendmap()) return -1;
    if (s_blendmap.w < 256 || s_blendmap.h < 160) return -1;
    return (int)(s_blendmap.px[(size_t)((frame / 8) * 32 + y) * s_blendmap.w + (frame % 8) * 32 + x] & 0xff);
}

/* FUN_14005e2e0: mixes tileset frames t1 and t2 with blendmap frame `frame` into atlas cell n */
static void tileblend_build(tileblend_source_t *src, int n)
{
    int s = src->tile_src;
    int tcols = src->tileset.w / s;
    int need_rows = n / TILEBLEND_ATLAS_COLS + 1;
    if (need_rows > src->atlas_rows) {
        int rows = need_rows * 2;
        size_t row_px = (size_t)TILEBLEND_ATLAS_COLS * (size_t)s * (size_t)s;
        uint32_t *px = (uint32_t *)realloc(src->atlas_px, row_px * (size_t)rows * 4);
        if (!px) return;
        memset(px + row_px * (size_t)src->atlas_rows, 0, row_px * (size_t)(rows - src->atlas_rows) * 4);
        src->atlas_px = px;
        src->atlas_rows = rows;
    }
    int frame = src->key[n][0], t1 = src->key[n][1], t2 = src->key[n][2];
    int aw = TILEBLEND_ATLAS_COLS * s;
    int ox = (n % TILEBLEND_ATLAS_COLS) * s, oy = (n / TILEBLEND_ATLAS_COLS) * s;
    int x1 = (t1 % tcols) * s, y1 = (t1 / tcols) * s;
    int x2 = (t2 % tcols) * s, y2 = (t2 / tcols) * s;
    int mx = (frame % 8) * 32, my = (frame / 8) * 32;
    int div = s >> 5;
    for (int y = 0; y < s; y++) {
        for (int x = 0; x < s; x++) {
            uint32_t a = (y1 + y < src->tileset.h) ? src->tileset.px[(size_t)(y1 + y) * src->tileset.w + x1 + x] : 0;
            uint32_t b = (y2 + y < src->tileset.h) ? src->tileset.px[(size_t)(y2 + y) * src->tileset.w + x2 + x] : 0;
            uint8_t mask = (uint8_t)(s_blendmap.px[(size_t)(my + y / div) * s_blendmap.w + mx + x / div] & 0xff);
            src->atlas_px[(size_t)(oy + y) * aw + ox + x] = cs2d_render_tileblend_mix(a, b, mask);
        }
    }
    src->dirty = true;
}

/*
 * The atlas cell holding the blended image of tile (tx, ty), or -1 when the tile is drawn from the tileset
 * (no 0x40 flag, the same tile next to it, a blend frame outside blendmap.bmp, or no pixels to blend).
 */
static int tileblend_cell(tileblend_source_t *src, const cs2d_map_t *map, int tx, int ty, int tile_src)
{
    size_t idx = (size_t)ty * map->width + (size_t)tx;
    if (!src || !map->modifiers || !map->tile_data || (map->modifiers[idx] & 0xc0) != 0x40) return -1;
    uint8_t frame = map->tile_data[idx * 4 + 3];
    uint8_t t1 = map->tiles[idx];
    uint8_t t2 = cs2d_render_tileblend_neighbour(map, tx, ty, frame & 7);
    if (t1 == t2 || frame >= 40 || !tileblend_load_blendmap()) return -1;
    if (s_blendmap.w < 256 || s_blendmap.h < 160 || src->tileset.w < tile_src || tile_src < 32) return -1;
    if (src->tile_src != tile_src) {
        /* The map's tile size changed: start a new atlas */
        free(src->atlas_px);
        cs2d_texture_free(&src->atlas);
        src->atlas_px = NULL;
        src->atlas_rows = 0;
        src->count = 0;
        src->tile_src = tile_src;
    }
    for (int i = 0; i < src->count; i++) {
        if (src->key[i][0] == frame && src->key[i][1] == t1 && src->key[i][2] == t2) return i;
    }
    if (src->count >= TILEBLEND_MAX) return -1;
    int n = src->count;
    src->key[n][0] = frame;
    src->key[n][1] = t1;
    src->key[n][2] = t2;
    tileblend_build(src, n);
    if (!src->atlas_px) return -1;
    src->count++;
    return n;
}

/* Uploads the atlas again after new cells were built (padded like a tileset) */
static void tileblend_upload(tileblend_source_t *src)
{
    if (!src->dirty || !src->atlas_px) return;
    src->dirty = false;
    int w = TILEBLEND_ATLAS_COLS * src->tile_src, h = src->atlas_rows * src->tile_src;
    uint32_t *copy = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!copy) return;
    memcpy(copy, src->atlas_px, (size_t)w * (size_t)h * 4);
    cs2d_texture_free(&src->atlas);
    upload_rgba_to_texture(&src->atlas, copy, w, h, true, 0, src->tile_src, src->tile_src);
    free(copy);
}

/*
 * Reconstructed from FUN_140113d50 (lines 236-263 in cs2d_dedicated.exe):
 * Resolves gfx/tiles/<tileset_name> (trying exact name, .bmp, and .png)
 * and builds a 32x32 padded cell atlas to prevent tileset edge bleeding.
 */
bool cs2d_texture_load_tileset(cs2d_texture_t *tex, const char *tileset_name)
{
    if (!tex || !tileset_name || tileset_name[0] == '\0') return false;

    static const char *const fmt[3] = { "gfx/tiles/%s", "gfx/tiles/%s.bmp", "gfx/tiles/%s.png" };
    rgba_image_t img = { 0 };
    bool ok = false;
    s_capture = &img;
    for (int i = 0; i < 3 && !ok; i++) {
        char path[256];
        snprintf(path, sizeof(path), fmt[i], tileset_name);
        ok = cs2d_texture_load_grid(tex, path, 2, 32, 32);
    }
    s_capture = NULL;
    if (ok && tex->id) {
        tileblend_register(tex->id, &img); /* takes img.px */
    } else {
        free(img.px);
    }
    return ok;
}

void cs2d_texture_free(cs2d_texture_t *tex)
{
    if (tex && tex->id) {
        GLuint tid = tex->id;
        glDeleteTextures(1, &tid);
        tex->id = 0;
    }
}

/* RGBA pixels of an image file (mask mode 0), (a << 24) | (b << 16) | (g << 8) | r per pixel; free() the result */
uint32_t *cs2d_image_load_rgba(const char *path, int *out_w, int *out_h)
{
    rgba_image_t img = {0};
    cs2d_texture_t tmp;
    s_capture = &img;
    cs2d_texture_load_ex(&tmp, path, 0);
    s_capture = NULL;
    cs2d_texture_free(&tmp);
    if (out_w) *out_w = img.w;
    if (out_h) *out_h = img.h;
    return img.px;
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

    /* Projectile Sprites */
    cs2d_texture_load_ex(&bank->tex_proj_rocket,       "gfx/weapons/rocket.bmp",          2);
    cs2d_texture_load_ex(&bank->tex_proj_rpg,          "gfx/weapons/rpg.bmp",             2);
    cs2d_texture_load_ex(&bank->tex_proj_grenade,      "gfx/weapons/grenade.bmp",         2);
    cs2d_texture_load_ex(&bank->tex_proj_molotov,      "gfx/weapons/molotovcocktail.bmp", 2);
    cs2d_texture_load_ex(&bank->tex_proj_flare,        "gfx/weapons/flare.bmp",           2);
    cs2d_texture_load_ex(&bank->tex_proj_gutbomb,      "gfx/weapons/gutbomb.bmp",         2);
    cs2d_texture_load_ex(&bank->tex_proj_airstrike,    "gfx/weapons/airstrike.bmp",       2);
    cs2d_texture_load_ex(&bank->tex_proj_flashbang,    "gfx/weapons/flashbang.bmp",       2);
    cs2d_texture_load_ex(&bank->tex_proj_smokegrenade, "gfx/weapons/smokegrenade.bmp",    2);
    cs2d_texture_load_ex(&bank->tex_proj_gasgrenade,   "gfx/weapons/gasgrenade.bmp",      2);
    cs2d_texture_load_ex(&bank->tex_proj_snowball,     "gfx/weapons/snowball.bmp",        2);

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
    if (!cs2d_texture_load_ex(&bank->tex_shadow, "incbindata/shadow.bmp", 3)) {
        cs2d_texture_load_ex(&bank->tex_shadow, "gfx/shadow.bmp", 3);
    }
    bank->tex_leaf = bank->tex_shadow;
    if (!cs2d_texture_load_grid(&bank->tex_buttons, "incbindata/buttons.bmp", 2, 32, 32)) {
        cs2d_texture_load_grid(&bank->tex_buttons, "gfx/incbindata/buttons.bmp", 2, 32, 32);
    }
    if (!cs2d_texture_load_grid(&bank->tex_objects, "incbindata/objects.bmp", 2, 32, 32)) {
        if (!cs2d_texture_load_grid(&bank->tex_objects, "gfx/incbindata/objects.bmp", 2, 32, 32)) {
            cs2d_texture_load_ex(&bank->tex_objects, "gfx/sprites/lamp1.bmp", 2);
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

    cs2d_texture_free(&bank->tex_proj_rocket);
    cs2d_texture_free(&bank->tex_proj_rpg);
    cs2d_texture_free(&bank->tex_proj_grenade);
    cs2d_texture_free(&bank->tex_proj_molotov);
    cs2d_texture_free(&bank->tex_proj_flare);
    cs2d_texture_free(&bank->tex_proj_gutbomb);
    cs2d_texture_free(&bank->tex_proj_airstrike);
    cs2d_texture_free(&bank->tex_proj_flashbang);
    cs2d_texture_free(&bank->tex_proj_smokegrenade);
    cs2d_texture_free(&bank->tex_proj_gasgrenade);
    cs2d_texture_free(&bank->tex_proj_snowball);

    cs2d_texture_free(&bank->tex_shadowmap);
    cs2d_texture_free(&bank->tex_buttons);
    cs2d_texture_free(&bank->tex_decals);
    cs2d_texture_free(&bank->tex_buildings);
    for (int i = 0; i < 3; i++) cs2d_texture_free(&bank->tex_turret[i]);
    cs2d_texture_free(&bank->tex_buildhelper);
    cs2d_texture_free(&bank->tex_palms);
    cs2d_texture_free(&bank->tex_tree);
    cs2d_texture_free(&bank->tex_shadow);
    bank->tex_leaf.id = 0;
    cs2d_texture_free(&bank->tex_objects);
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

    cs2d_gfx_bank_free_sprites(bank);
    memset(bank, 0, sizeof(*bank));
}

void cs2d_gfx_bank_free_sprites(cs2d_gfx_bank_t *bank)
{
    if (!bank) return;

    if (bank->atlas_pages) {
        for (int i = 0; i < bank->atlas_page_count; i++) {
            if (bank->atlas_pages[i].id && g_gl_ctx) {
                GLuint tid = (GLuint)bank->atlas_pages[i].id;
                glDeleteTextures(1, &tid);
            }
        }
        free(bank->atlas_pages);
        bank->atlas_pages = NULL;
    }
    bank->atlas_page_count = 0;
    bank->atlas_page_cap = 0;

    if (bank->sprite_cache) {
        for (int i = 0; i < bank->sprite_cache_count; i++) {
            if (!bank->sprite_cache[i].is_atlas) {
                cs2d_texture_free(&bank->sprite_cache[i].tex);
            }
        }
        free(bank->sprite_cache);
        bank->sprite_cache = NULL;
    }
    bank->sprite_cache_count = 0;
    bank->sprite_cache_cap = 0;
}

const cs2d_texture_t *cs2d_gfx_get_sprite(cs2d_gfx_bank_t *bank, const char *rel_path, bool black_mask)
{
    if (!bank || !rel_path || !rel_path[0]) return NULL;

    int mask_mode = black_mask ? 3 : 1;
    for (int i = 0; i < bank->sprite_cache_count; i++) {
        if (bank->sprite_cache[i].mask_mode == mask_mode &&
            _stricmp(bank->sprite_cache[i].path, rel_path) == 0) {
            return bank->sprite_cache[i].tex.id ? &bank->sprite_cache[i].tex : NULL;
        }
    }

    /* Expand dynamic sprite cache capacity when needed (lifts the 64-sprite cache ceiling) */
    if (bank->sprite_cache_count >= bank->sprite_cache_cap) {
        int new_cap = bank->sprite_cache_cap ? bank->sprite_cache_cap * 2 : 256;
        cs2d_sprite_cache_entry_t *new_cache = (cs2d_sprite_cache_entry_t *)realloc(
            bank->sprite_cache, (size_t)new_cap * sizeof(cs2d_sprite_cache_entry_t));
        if (!new_cache) return NULL;
        bank->sprite_cache = new_cache;
        bank->sprite_cache_cap = new_cap;
    }

    cs2d_sprite_cache_entry_t *entry = &bank->sprite_cache[bank->sprite_cache_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", rel_path);
    entry->mask_mode = mask_mode;
    entry->is_atlas  = false;

    if (!cs2d_texture_load_ex(&entry->tex, rel_path, mask_mode)) {
        char alt[160];
        snprintf(alt, sizeof(alt), "gfx/sprites/%s", rel_path);
        cs2d_texture_load_ex(&entry->tex, alt, mask_mode);
    }

    return entry->tex.id ? &entry->tex : NULL;
}

const cs2d_texture_t *cs2d_render_get_projectile_texture(uint8_t type)
{
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    if (!bank) return NULL;

    const cs2d_texture_t *t = NULL;
    switch (type) {
        case CS2D_WEP_ROCKETLAUNCHER: t = &bank->tex_proj_rocket; break;
        case CS2D_WEP_RPG7:           t = &bank->tex_proj_rpg; break;
        case CS2D_WEP_GRENADELAUNCHER:
        case CS2D_WEP_HE:             t = &bank->tex_proj_grenade; break;
        case CS2D_WEP_FLASHBANG:      t = &bank->tex_proj_flashbang; break;
        case CS2D_WEP_SMOKE:          t = &bank->tex_proj_smokegrenade; break;
        case CS2D_WEP_FLARE:          t = &bank->tex_proj_flare; break;
        case CS2D_WEP_GAS:            t = &bank->tex_proj_gasgrenade; break;
        case CS2D_WEP_MOLOTOV:        t = &bank->tex_proj_molotov; break;
        case CS2D_WEP_SNOWBALL:       t = &bank->tex_proj_snowball; break;
        case CS2D_WEP_AIRSTRIKE:      t = &bank->tex_proj_airstrike; break;
        case CS2D_WEP_GUTBOMB:        t = &bank->tex_proj_gutbomb; break;
        default:                      break;
    }
    if (t && t->id) return t;

    /* Fallback to ground item texture (tex_weapons_d[type]) per requirement */
    if (type > 0 && type < CS2D_MAX_WEAPONS && bank->tex_weapons_d[type].id) {
        return &bank->tex_weapons_d[type];
    }
    return NULL;
}

void cs2d_render_rect(float x, float y, float w, float h, float r, float g, float b, float a, bool fill)
{
    /* The original forces the alpha blend here (glBlendFunc is reset before every rect) */
    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    if (fill) {
        cs2d_gl_rect(x, y, w, h, r, g, b, a);
        return;
    }
    cs2d_gl_set_texture(0);
    cs2d_gl_vertex_t v[4];
    float xs[4] = { x, x + w, x + w, x };
    float ys[4] = { y, y, y + h, y + h };
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i]; v[i].y = ys[i]; v[i].u = 0.0f; v[i].v = 0.0f;
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = a;
    }
    cs2d_gl_push_line_loop(v, 4);
}

void cs2d_render_line(float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    /* No blend change: the caller may have set the additive blend (fx tracer) */
    cs2d_gl_line(x1, y1, x2, y2, r, g, b, a);
}

void cs2d_render_circle(float cx, float cy, float radius, float r, float g, float b, float a, bool fill)
{
    cs2d_gl_circle(cx, cy, radius, r, g, b, a, fill);
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
        u0 = (float)(tex->src_x + col * stride_x + tex->pad) / (float)tex->gpu_w;
        v0 = (float)(tex->src_y + row * stride_y + tex->pad) / (float)tex->gpu_h;
        u1 = (float)(tex->src_x + col * stride_x + tex->pad + cell_w) / (float)tex->gpu_w;
        v1 = (float)(tex->src_y + row * stride_y + tex->pad + cell_h) / (float)tex->gpu_h;
    } else {
        float gw = (float)(tex->gpu_w > 0 ? tex->gpu_w : tex->width);
        float gh = (float)(tex->gpu_h > 0 ? tex->gpu_h : tex->height);
        u0 = (float)(tex->src_x + col * cell_w) / gw;
        v0 = (float)(tex->src_y + row * cell_h) / gh;
        u1 = (float)(tex->src_x + (col + 1) * cell_w) / gw;
        v1 = (float)(tex->src_y + (row + 1) * cell_h) / gh;
    }

    /* CS2D Blend Modes: 3/4 = Light/Additive, 5/6 = Shade, 0 = Alpha */
    bool additive = (blend_mode == 3 || blend_mode == 4);
    cs2d_gl_set_blend(additive ? CS2D_GL_BLEND_ADD : CS2D_GL_BLEND_ALPHA);
    cs2d_gl_quad_rot(tex->id, x, y, draw_w, draw_h, u0, v0, u1, v1, angle_deg, r, g, b, a);
    if (additive) {
        cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    }
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
/*
 * The player's silhouette for the light engine's day shadows (FUN_140112340), at the integer camera: the armor image,
 * the held weapon 16.5 px ahead (12.5 for grenades; half size when its image is 64 px or more both ways), then the
 * body frame, all midhandled at the weapon rotation. Not drawn: stealth suit (armor 206), Spectators, the dead, and
 * players more than 99 px off screen.
 */
void cs2d_render_player_light_shadow(const cs2d_player_t *player, const cs2d_texture_t *skin_tex, float cam_x, float cam_y)
{
    if (!player || !player->alive || player->team == 0 || player->armor == 206 || !cs2d_light_day_shadows()) return;
    if (!s_player_shadows) return;
    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();
    float x = player->pos.x - cam_x, y = player->pos.y - cam_y;
    if ((unsigned)((int)x + 99) > 0x418u || (unsigned)((int)y + 99) > 0x2a6u) return;
    float rot = player->angle + ((player->slash_timer > 0.0f) ? (6.0f * player->slash_timer) : 0.0f);
    if (bank && player->armor >= 201 && player->armor <= 205 && bank->tex_weapons[79 + player->armor - 201].id) {
        cs2d_light_add_player_part(&bank->tex_weapons[79 + player->armor - 201], 0, 0, 0, x, y, rot, 1.0f);
    }
    if (bank && player->current_weapon > 0 && player->current_weapon < CS2D_MAX_WEAPONS &&
        bank->tex_weapons[player->current_weapon].id) {
        const cs2d_texture_t *wt = &bank->tex_weapons[player->current_weapon];
        const cs2d_weapon_def_t *wdef = cs2d_weapon_get(player->current_weapon);
        float dist = (wdef && wdef->slot == CS2D_SLOT_GRENADE) ? 12.5f : 16.5f;
        float sc = (wt->width > 63 && wt->height > 63) ? 0.5f : 1.0f;
        double r = (double)rot * (3.14159265358979 / 180.0);
        cs2d_light_add_player_part(wt, 0, 0, 0, (float)(sin(r) * dist + (double)x), (float)((double)y - dist * cos(r)), rot, sc);
    }
    const cs2d_texture_t *skin = skin_tex;
    uint8_t look_idx = player->look & 3;
    if (bank && bank->initialized) {
        if (player->is_vip || player->team == CS2D_TEAM_VIP) { if (bank->tex_vip.id) skin = &bank->tex_vip; }
        else if (player->team == CS2D_TEAM_CT) { if (bank->tex_ct[look_idx].id) skin = &bank->tex_ct[look_idx]; }
        else if (player->team == CS2D_TEAM_T) { if (bank->tex_t[look_idx].id) skin = &bank->tex_t[look_idx]; }
    }
    if (skin && skin->id) {
        int cw = (skin->width >= 64) ? (skin->width / 2) : skin->width;
        int ch = (skin->height >= 96) ? (skin->height / 3) : skin->height;
        cs2d_light_add_player_part(skin, cw, ch, cs2d_player_get_frame(player, false), x, y, rot, 1.0f);
    }
}

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

    /* Walking legs (gfx/player/legs.bmp: 128x64 -> 8 frames of 32x32, offset 5px behind player) */
    bool moving = (player->feet_timer > 0.0f) ||
                  (fabsf(player->vel.x) > 1.0f || fabsf(player->vel.y) > 1.0f);
    bool draw_legs = moving && bank && bank->tex_legs.id;
    int leg_frame = ((int)player->feet_frame) & 7;
    float leg_ang = (player->feet_timer > 0.0f) ? player->feet_angle : player->angle;
    float lx = sx - sin_a * 5.0f;
    float ly = sy + cos_a * 5.0f;

    /* Resolve Player Skin Texture (ct1..4, t1..4, vip) */
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
    bool draw_body = active_skin && active_skin->id;
    /* Slice exact 32x32 frame from 64x96 player sheet (2 cols x 3 rows) */
    int cell_w = 0, cell_h = 0;
    if (draw_body) {
        cell_w = (active_skin->width >= 64) ? (active_skin->width / 2) : active_skin->width;
        cell_h = (active_skin->height >= 96) ? (active_skin->height / 3) : active_skin->height;
    }

    /* Held weapon in hands (0x1401215ee..0x1401218a0 & 0x140122791..0x140122a20) */
    const cs2d_texture_t *active_wep = wep_tex;
    if (bank && player->current_weapon > 0 && player->current_weapon < CS2D_MAX_WEAPONS) {
        if (bank->tex_weapons[player->current_weapon].id) {
            active_wep = &bank->tex_weapons[player->current_weapon];
        }
    }
    const cs2d_texture_t *held_tex = NULL;
    float wx = sx, wy = sy;
    if ((player->is_reloading || player->reload_timer > 0.0f) && bank && bank->tex_reload.id) {
        /* Reloading sprite at 13.5px in front of player (0x140122983) */
        held_tex = &bank->tex_reload;
        wx = sx + sin_d * 13.5f;
        wy = sy - cos_d * 13.5f;
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
        held_tex = active_wep;
        wx = sx + sin_d * wep_dist;
        wy = sy - cos_d * wep_dist;
    }

    /* 0a. Light engine on (Tpl.draw 0x140121160): incbindata/shadow.bmp at scale 1.5, black at alpha 0.5 whenever
     * "shadows" is not 0; the silhouette goes into the day shadows (cs2d_render_player_light_shadow) */
    if (cs2d_light_day_shadows()) {
        if (s_shadow_intensity > 0 && bank && bank->tex_shadow.id) {
            float player_a = (player->armor == 206) ? 0.2f : 1.0f;
            cs2d_render_sprite_frame(&bank->tex_shadow, 32, 32, 0, sx, sy, 48.0f, 48.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, player_a * 0.5f, 0);
        }
    } else
    /* 0b. Cast shadow without the light engine (our choice, see set_player_shadow_offset): the silhouette of legs,
     * torso and held weapon, shifted along the sun. Pass 1 marks the texels with alpha >= 0.5 in the depth buffer only;
     * pass 2 fills one black quad where they were marked, so the shadow has a single alpha and overlapping parts leave
     * no seams. */
    if (s_shadows_enabled && s_player_shadows && s_shadow_intensity > 0) {
        float player_a = (player->armor == 206) ? 0.2f : 1.0f;
        float shadow_a = player_a * ((float)s_shadow_intensity * 0.01f);
        float dx = s_player_shadow_dx, dy = s_player_shadow_dy;
        /* Pass 1 marks the texels with alpha >= 0.5 in the depth buffer only (colour writes off) */
        cs2d_gl_shadow_mark_begin();
        if (draw_legs) {
            cs2d_render_sprite_frame(&bank->tex_legs, 32, 32, leg_frame, lx + dx, ly + dy, 32.0f, 32.0f, leg_ang,
                                     1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
        if (draw_body) {
            cs2d_render_sprite_frame(active_skin, cell_w, cell_h, body_frame, sx + dx, sy + dy, 32.0f, 32.0f, draw_ang,
                                     1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
        if (held_tex) {
            cs2d_render_sprite(held_tex, wx + dx, wy + dy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        cs2d_gl_shadow_mark_end();

        /* Pass 2: the cleared depth is 1.0 and sprites sit at z 0, so GL_EQUAL keeps only the marked pixels */
        cs2d_gl_shadow_fill(sx + dx - 48.0f, sy + dy - 48.0f, 96.0f, 96.0f, 0.0f, 0.0f, 0.0f, shadow_a);
    }

    /* 1. Walking legs */
    if (draw_legs) {
        cs2d_render_sprite_frame(&bank->tex_legs, 32, 32, leg_frame,
                                 lx, ly, 32.0f, 32.0f, leg_ang,
                                 1.0f, 1.0f, 1.0f, 1.0f, 0);
    }

    /* 2. Player skin */
    if (draw_body) {
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

    /* 4. Held weapon, with the silencer overlay for USP (1) and M4A1 (32) when zoom_mode == 3 (0x1401217e4) */
    if (held_tex) {
        cs2d_render_sprite(held_tex, wx, wy, 32.0f, 32.0f, draw_ang, 1.0f, 1.0f, 1.0f, 1.0f);
        if (held_tex == active_wep && bank && player->zoom_mode == 3) {
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
    int frame_idx, uint8_t mod,
    float rx, float ry,
    float size,
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

    float x1 = rx + size;
    float y1 = ry + size;
    float xy[8] = { rx, ry, x1, ry, x1, y1, rx, y1 };
    float uv[8] = { tu[0], tv[0], tu[1], tv[1], tu[2], tv[2], tu[3], tv[3] };
    cs2d_gl_quad_corners(tileset_tex->id, xy, uv, cr * shade, cg * shade, cb * shade, alpha);
}

void cs2d_render_tile_frame(const cs2d_texture_t *tileset_tex, uint8_t frame, uint8_t mod, const uint8_t *rgb,
                            float x, float y, float size, float tint, float alpha)
{
    if (!tileset_tex || tileset_tex->id == 0 || tileset_tex->width < CS2D_TILE_SIZE) return;
    int cols = tileset_tex->width / CS2D_TILE_SIZE;
    uint8_t color[3];
    for (int i = 0; i < 3; i++) color[i] = (uint8_t)((float)(rgb ? rgb[i] : 255) * tint);
    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    draw_tile_frame(tileset_tex, CS2D_TILE_SIZE, cols, frame, mod, x, y, size, alpha, color);
}

#define CS2D_SHADOW_CHUNK_TILES     16
#define CS2D_SHADOW_TEXELS_PER_TILE 16 /* 2 world pixels per texel (scale = 16 texels per 32px tile) */

typedef struct {
    GLuint tex_id;
    bool   dirty;
    bool   initialized;
} cs2d_shadow_chunk_t;

typedef struct {
    cs2d_shadow_chunk_t *chunks;
    int                  chunks_x;
    int                  chunks_y;
    uint32_t             map_w;
    uint32_t             map_h;
    uint32_t             solid_version;
    int                  daylight_time;

    uint8_t             *eff_modes;
    uint16_t            *eff_heights;

    uint8_t             *last_entity_state;
    uint32_t             last_entity_count;
    uint8_t             *last_object_shadow;
    size_t               last_map_tiles;
} cs2d_shadow_system_t;

static cs2d_shadow_system_t s_shadow_sys = {0};

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
    if (sx0 >= sx1 || sy0 >= sy1) return;

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

/* Clears shadow alpha to 0 inside local rectangle [x0..x1, y0..y1] */
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
    if (sx0 >= sx1 || sy0 >= sy1) return;

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
    if (sx0 >= sx1 || sy0 >= sy1) return;

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

/* Marks chunks overlapping tile bounds [min_tx..max_tx, min_ty..max_ty] as dirty */
static void mark_shadow_region_dirty(int min_tx, int min_ty, int max_tx, int max_ty)
{
    if (!s_shadow_sys.chunks || s_shadow_sys.chunks_x <= 0 || s_shadow_sys.chunks_y <= 0) return;

    int min_cx = min_tx / CS2D_SHADOW_CHUNK_TILES;
    int min_cy = min_ty / CS2D_SHADOW_CHUNK_TILES;
    int max_cx = max_tx / CS2D_SHADOW_CHUNK_TILES;
    int max_cy = max_ty / CS2D_SHADOW_CHUNK_TILES;

    if (min_cx < 0) min_cx = 0;
    if (min_cy < 0) min_cy = 0;
    if (max_cx >= s_shadow_sys.chunks_x) max_cx = s_shadow_sys.chunks_x - 1;
    if (max_cy >= s_shadow_sys.chunks_y) max_cy = s_shadow_sys.chunks_y - 1;

    for (int cy = min_cy; cy <= max_cy; cy++) {
        for (int cx = min_cx; cx <= max_cx; cx++) {
            s_shadow_sys.chunks[cy * s_shadow_sys.chunks_x + cx].dirty = true;
        }
    }
}

/*
 * Reconstructed from 0x1401413b0..0x14014276d, 0x140260330..0x1402626d0, and 0x1401da4a6..0x1401da82d in CS2D.exe:
 * Manages the chunk grid (16x16 tiles each). When solid_version changes (e.g. Func_DynWall toggled or
 * construction built), only the affected chunks are invalidated instead of freezing the game with a full map rebuild.
 */
static void ensure_shadow_chunks(const cs2d_map_t *map)
{
    if (!map || !map->tiles || map->width == 0 || map->height == 0) return;

    size_t map_tiles = (size_t)map->width * (size_t)map->height;

    /* Reallocate system if map dimensions changed or not yet initialized */
    if (s_shadow_sys.map_w != map->width ||
        s_shadow_sys.map_h != map->height ||
        s_shadow_sys.chunks == NULL) {

        cs2d_render_invalidate_shadows();

        s_shadow_sys.chunks_x = (int)(map->width + CS2D_SHADOW_CHUNK_TILES - 1) / CS2D_SHADOW_CHUNK_TILES;
        s_shadow_sys.chunks_y = (int)(map->height + CS2D_SHADOW_CHUNK_TILES - 1) / CS2D_SHADOW_CHUNK_TILES;
        int total_chunks = s_shadow_sys.chunks_x * s_shadow_sys.chunks_y;
        s_shadow_sys.chunks = (cs2d_shadow_chunk_t *)calloc((size_t)total_chunks, sizeof(cs2d_shadow_chunk_t));
        if (!s_shadow_sys.chunks) return;

        for (int i = 0; i < total_chunks; i++) {
            s_shadow_sys.chunks[i].dirty = true;
        }

        s_shadow_sys.eff_modes   = (uint8_t *)malloc(map_tiles);
        s_shadow_sys.eff_heights = (uint16_t *)calloc(map_tiles, sizeof(uint16_t));
        if (!s_shadow_sys.eff_modes || !s_shadow_sys.eff_heights) {
            cs2d_render_invalidate_shadows();
            return;
        }

        /* Base tile modes and custom heights (0x1401414b4..0x1401415c0) */
        for (size_t idx = 0; idx < map_tiles; idx++) {
            uint8_t t = map->tiles[idx];
            s_shadow_sys.eff_modes[idx] = map->tile_modes[t];
            if (map->save_tile_heights != 0 && map->tile_heights[t] > 0) {
                s_shadow_sys.eff_heights[idx] = map->tile_heights[t];
            }
        }

        /* Override effective tile modes with active Func_DynWall and Env_Breakable */
        for (uint32_t i = 0; i < map->entity_count; i++) {
            const cs2d_entity_t *e = &map->entities[i];
            if (map->entity_state[i]) continue;
            if (e->x < 0 || e->y < 0 || (uint32_t)e->x >= map->width || (uint32_t)e->y >= map->height) continue;
            size_t idx = (size_t)e->y * (size_t)map->width + (size_t)e->x;
            int tile_f = e->int_settings[0];
            if (e->type == CS2D_ENT_FUNC_DYNWALL) {
                switch (e->int_settings[1]) {
                    case 0:  s_shadow_sys.eff_modes[idx] = CS2D_TILE_WALL; break;
                    case 1:  s_shadow_sys.eff_modes[idx] = CS2D_TILE_OBSTACLE; break;
                    case 2:  s_shadow_sys.eff_modes[idx] = CS2D_TILE_WALL_NOSHADOW; break;
                    case 3:  s_shadow_sys.eff_modes[idx] = CS2D_TILE_OBSTACLE_NOSHADOW; break;
                    default:
                        s_shadow_sys.eff_modes[idx] = (tile_f >= 0 && tile_f <= (int)map->tile_count)
                                                      ? map->tile_modes[tile_f] : CS2D_TILE_FLOOR_NOSOUND;
                        break;
                }
                s_shadow_sys.eff_heights[idx] = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                                                 ? map->tile_heights[tile_f] : 0;
            } else if (e->type == CS2D_ENT_ENV_BREAKABLE) {
                if (e->int_settings[6] == 2)      s_shadow_sys.eff_modes[idx] = CS2D_TILE_FLOOR_NOSOUND;
                else if (e->int_settings[6] == 3) s_shadow_sys.eff_modes[idx] = CS2D_TILE_OBSTACLE;
                else                              s_shadow_sys.eff_modes[idx] = CS2D_TILE_WALL;
                s_shadow_sys.eff_heights[idx] = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                                                 ? map->tile_heights[tile_f] : 0;
            }
        }

        /* Solid buildings (cs2d_dynobj_casts_shadow) shadow like a 32 px wall tile */
        if (map->object_shadow) {
            for (size_t idx = 0; idx < map_tiles; idx++) {
                if (!map->object_shadow[idx]) continue;
                s_shadow_sys.eff_modes[idx] = CS2D_TILE_WALL;
                s_shadow_sys.eff_heights[idx] = 0;
            }
        }

        /* Snapshot initial state for fast delta tracking */
        s_shadow_sys.last_entity_count = map->entity_count;
        if (map->entity_count > 0) {
            s_shadow_sys.last_entity_state = (uint8_t *)malloc(map->entity_count);
            if (s_shadow_sys.last_entity_state) {
                memcpy(s_shadow_sys.last_entity_state, map->entity_state, map->entity_count);
            }
        }
        s_shadow_sys.last_map_tiles = map_tiles;
        s_shadow_sys.last_object_shadow = (uint8_t *)calloc(map_tiles, 1);
        if (s_shadow_sys.last_object_shadow && map->object_shadow) {
            memcpy(s_shadow_sys.last_object_shadow, map->object_shadow, map_tiles);
        }

        s_shadow_sys.solid_version = map->entity_solid_version;
        s_shadow_sys.daylight_time = map->daylight_time;
        s_shadow_sys.map_w         = map->width;
        s_shadow_sys.map_h         = map->height;
        return;
    }

    /* Check for sun-angle daylight changes (invalidates all chunks) */
    if (s_shadow_sys.daylight_time != map->daylight_time) {
        s_shadow_sys.daylight_time = map->daylight_time;
        int total = s_shadow_sys.chunks_x * s_shadow_sys.chunks_y;
        for (int i = 0; i < total; i++) {
            s_shadow_sys.chunks[i].dirty = true;
        }
    }

    /* Fast delta update when solid version changed */
    if (s_shadow_sys.solid_version != map->entity_solid_version) {
        s_shadow_sys.solid_version = map->entity_solid_version;

        /* 1. Track entity state changes (Func_DynWall, Env_Breakable) */
        if (s_shadow_sys.last_entity_state) {
            uint32_t count = map->entity_count < s_shadow_sys.last_entity_count ? map->entity_count : s_shadow_sys.last_entity_count;
            for (uint32_t i = 0; i < count; i++) {
                if (map->entity_state[i] != s_shadow_sys.last_entity_state[i]) {
                    s_shadow_sys.last_entity_state[i] = map->entity_state[i];
                    const cs2d_entity_t *e = &map->entities[i];
                    if (e->x >= 0 && e->y >= 0 && (uint32_t)e->x < map->width && (uint32_t)e->y < map->height) {
                        size_t idx = (size_t)e->y * (size_t)map->width + (size_t)e->x;
                        uint8_t t = map->tiles[idx];
                        uint8_t m = map->tile_modes[t];
                        uint16_t h = (map->save_tile_heights != 0 && map->tile_heights[t] > 0) ? map->tile_heights[t] : 0;
                        if (!map->entity_state[i]) {
                            int tile_f = e->int_settings[0];
                            if (e->type == CS2D_ENT_FUNC_DYNWALL) {
                                switch (e->int_settings[1]) {
                                    case 0:  m = CS2D_TILE_WALL; break;
                                    case 1:  m = CS2D_TILE_OBSTACLE; break;
                                    case 2:  m = CS2D_TILE_WALL_NOSHADOW; break;
                                    case 3:  m = CS2D_TILE_OBSTACLE_NOSHADOW; break;
                                    default:
                                        m = (tile_f >= 0 && tile_f <= (int)map->tile_count)
                                            ? map->tile_modes[tile_f] : CS2D_TILE_FLOOR_NOSOUND;
                                        break;
                                }
                                h = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                                    ? map->tile_heights[tile_f] : 0;
                            } else if (e->type == CS2D_ENT_ENV_BREAKABLE) {
                                if (e->int_settings[6] == 2)      m = CS2D_TILE_FLOOR_NOSOUND;
                                else if (e->int_settings[6] == 3) m = CS2D_TILE_OBSTACLE;
                                else                              m = CS2D_TILE_WALL;
                                h = (map->save_tile_heights != 0 && tile_f >= 0 && tile_f < 256 && map->tile_heights[tile_f] > 0)
                                    ? map->tile_heights[tile_f] : 0;
                            }
                        }
                        if (map->object_shadow && map->object_shadow[idx]) {
                            m = CS2D_TILE_WALL;
                            h = 0;
                        }
                        s_shadow_sys.eff_modes[idx] = m;
                        s_shadow_sys.eff_heights[idx] = h;
                        mark_shadow_region_dirty(e->x - 4, e->y - 4, e->x + 4, e->y + 4);
                    }
                }
            }
        }

        /* 2. Track object_shadow changes (buildings constructed / destroyed) */
        if (map->object_shadow && s_shadow_sys.last_object_shadow) {
            for (size_t idx = 0; idx < map_tiles; idx++) {
                if (map->object_shadow[idx] != s_shadow_sys.last_object_shadow[idx]) {
                    s_shadow_sys.last_object_shadow[idx] = map->object_shadow[idx];
                    int tx = (int)(idx % map->width);
                    int ty = (int)(idx / map->width);
                    if (map->object_shadow[idx]) {
                        s_shadow_sys.eff_modes[idx] = CS2D_TILE_WALL;
                        s_shadow_sys.eff_heights[idx] = 0;
                    } else {
                        uint8_t t = map->tiles[idx];
                        s_shadow_sys.eff_modes[idx] = map->tile_modes[t];
                        s_shadow_sys.eff_heights[idx] = (map->save_tile_heights != 0 && map->tile_heights[t] > 0) ? map->tile_heights[t] : 0;
                    }
                    mark_shadow_region_dirty(tx - 4, ty - 4, tx + 4, ty + 4);
                }
            }
        }
    }
}

/* Rasterizes and uploads one 16x16-tile chunk into its GL texture (zero dynamic memory allocation) */
static void generate_shadow_chunk(const cs2d_map_t *map, int cx, int cy)
{
    if (!map || !s_shadow_sys.chunks || !g_gl_ctx) return;

    int tx0 = cx * CS2D_SHADOW_CHUNK_TILES;
    int ty0 = cy * CS2D_SHADOW_CHUNK_TILES;
    int tx1 = (cx + 1) * CS2D_SHADOW_CHUNK_TILES;
    if (tx1 > (int)map->width)  tx1 = (int)map->width;
    int ty1 = (cy + 1) * CS2D_SHADOW_CHUNK_TILES;
    if (ty1 > (int)map->height) ty1 = (int)map->height;

    int chunk_w_tiles = tx1 - tx0;
    int chunk_h_tiles = ty1 - ty0;
    if (chunk_w_tiles <= 0 || chunk_h_tiles <= 0) return;

    int tex_w = chunk_w_tiles * CS2D_SHADOW_TEXELS_PER_TILE;
    int tex_h = chunk_h_tiles * CS2D_SHADOW_TEXELS_PER_TILE;

    float c_wx0 = (float)(tx0 * CS2D_TILE_SIZE);
    float c_wy0 = (float)(ty0 * CS2D_TILE_SIZE);

    /* Zero-allocation static scratch buffers (max chunk is 256x256 texels) */
    static uint8_t s_c_alpha[256 * 256];
    static uint8_t s_c_occl[256 * 256];
    static uint8_t s_c_rgba[256 * 256 * 4];

    memset(s_c_alpha, 0, (size_t)tex_w * (size_t)tex_h);

    const uint8_t shadow_alpha = 126;

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

    /* Expanded source search region: shadows can cast up to 6 tiles away */
    int stx0 = (tx0 > 6) ? (tx0 - 6) : 0;
    int sty0 = (ty0 > 6) ? (ty0 - 6) : 0;
    int stx1 = (tx1 + 6 < (int)map->width)  ? (tx1 + 6) : (int)map->width;
    int sty1 = (ty1 + 6 < (int)map->height) ? (ty1 + 6) : (int)map->height;

    /* Step 1: Effective Obstacle Tiles cast shadows, then clear tops within chunk */
    for (int ty = sty0; ty < sty1; ty++) {
        for (int tx = stx0; tx < stx1; tx++) {
            size_t idx = (size_t)ty * map->width + (size_t)tx;
            if (s_shadow_sys.eff_modes[idx] == CS2D_TILE_OBSTACLE) {
                int h = (s_shadow_sys.eff_heights[idx] > 0) ? (int)s_shadow_sys.eff_heights[idx] : 16;
                float dz = (float)(-(h / 2)) - (float)h * s_factor;
                float dx = dz * cos_sun;
                float dy = dz * sin_sun;
                float x0 = (float)(tx * CS2D_TILE_SIZE) - c_wx0;
                float y0 = (float)(ty * CS2D_TILE_SIZE) - c_wy0;
                cast_shadow_rect(s_c_alpha, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, dx, dy, shadow_alpha);
            }
        }
    }
    for (int ty = ty0; ty < ty1; ty++) {
        for (int tx = tx0; tx < tx1; tx++) {
            uint8_t m = s_shadow_sys.eff_modes[(size_t)ty * map->width + (size_t)tx];
            if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) {
                float x0 = (float)(tx * CS2D_TILE_SIZE) - c_wx0;
                float y0 = (float)(ty * CS2D_TILE_SIZE) - c_wy0;
                clear_shadow_rect(s_c_alpha, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f);
            }
        }
    }

    /* Step 2: Effective Wall Tiles cast shadows */
    for (int ty = sty0; ty < sty1; ty++) {
        for (int tx = stx0; tx < stx1; tx++) {
            size_t idx = (size_t)ty * map->width + (size_t)tx;
            if (s_shadow_sys.eff_modes[idx] == CS2D_TILE_WALL) {
                int h = (s_shadow_sys.eff_heights[idx] > 0) ? (int)s_shadow_sys.eff_heights[idx] : 32;
                float dz = (float)(-(h / 2)) - (float)h * s_factor;
                float dx = dz * cos_sun;
                float dy = dz * sin_sun;
                float x0 = (float)(tx * CS2D_TILE_SIZE) - c_wx0;
                float y0 = (float)(ty * CS2D_TILE_SIZE) - c_wy0;
                cast_shadow_rect(s_c_alpha, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, dx, dy, shadow_alpha);
            }
        }
    }

    /* Step 3: Env_Room Layer 0 */
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

        float b_min_x = (dx >= 0.0f ? x0 : x0 + dx);
        float b_max_x = (dx >= 0.0f ? x1 + dx : x1);
        float b_min_y = (dy >= 0.0f ? y0 : y0 + dy);
        float b_max_y = (dy >= 0.0f ? y1 + dy : y1);

        if (b_max_x < c_wx0 || b_min_x > (float)(tx1 * CS2D_TILE_SIZE) ||
            b_max_y < c_wy0 || b_min_y > (float)(ty1 * CS2D_TILE_SIZE)) {
            continue;
        }

        cast_shadow_rect(s_c_alpha, tex_w, tex_h, x0 - c_wx0, y0 - c_wy0, x1 - c_wx0, y1 - c_wy0, dx, dy, shadow_alpha);
    }

    /* Step 3b: Floor contact occlusion */
    memset(s_c_occl, 0, (size_t)tex_w * (size_t)tex_h);
    int otx0 = (tx0 > 1) ? (tx0 - 1) : 0;
    int oty0 = (ty0 > 1) ? (ty0 - 1) : 0;
    int otx1 = (tx1 + 1 < (int)map->width)  ? (tx1 + 1) : (int)map->width;
    int oty1 = (ty1 + 1 < (int)map->height) ? (ty1 + 1) : (int)map->height;
    for (int ty = oty0; ty < oty1; ty++) {
        for (int tx = otx0; tx < otx1; tx++) {
            uint8_t m = s_shadow_sys.eff_modes[(size_t)ty * map->width + (size_t)tx];
            if (m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW ||
                m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW) {
                float x0 = (float)(tx * CS2D_TILE_SIZE) - c_wx0;
                float y0 = (float)(ty * CS2D_TILE_SIZE) - c_wy0;
                accumulate_tile_occlusion(s_c_occl, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f, 10.0f, 72);
            }
        }
    }

    const float inv_scale = 2.0f;
    for (int sy = 0; sy < tex_h; sy++) {
        int local_ty = (int)(((float)sy + 0.5f) * inv_scale / (float)CS2D_TILE_SIZE);
        int world_ty = ty0 + local_ty;
        if ((uint32_t)world_ty >= map->height) continue;
        for (int sx = 0; sx < tex_w; sx++) {
            size_t c_idx = (size_t)sy * (size_t)tex_w + (size_t)sx;
            uint8_t add = s_c_occl[c_idx];
            uint8_t cur = s_c_alpha[c_idx];
            if (add == 0 || cur == 0) continue;
            int local_tx = (int)(((float)sx + 0.5f) * inv_scale / (float)CS2D_TILE_SIZE);
            int world_tx = tx0 + local_tx;
            if ((uint32_t)world_tx >= map->width) continue;
            uint8_t m = s_shadow_sys.eff_modes[(size_t)world_ty * map->width + (size_t)world_tx];
            if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW ||
                m == CS2D_TILE_OBSTACLE || m == CS2D_TILE_OBSTACLE_NOSHADOW) continue;
            int val = (int)cur + (int)add;
            if (val > 200) val = 200;
            if (val > (int)cur) s_c_alpha[c_idx] = (uint8_t)val;
        }
    }

    /* Clear Wall tiles within chunk */
    for (int ty = ty0; ty < ty1; ty++) {
        for (int tx = tx0; tx < tx1; tx++) {
            uint8_t m = s_shadow_sys.eff_modes[(size_t)ty * map->width + (size_t)tx];
            if (m == CS2D_TILE_WALL || m == CS2D_TILE_WALL_NOSHADOW) {
                float x0 = (float)(tx * CS2D_TILE_SIZE) - c_wx0;
                float y0 = (float)(ty * CS2D_TILE_SIZE) - c_wy0;
                clear_shadow_rect(s_c_alpha, tex_w, tex_h, x0, y0, x0 + 32.0f, y0 + 32.0f);
            }
        }
    }

    /* Step 4: Env_Room Layers 1..5 */
    for (int layer = 1; layer <= 5; layer++) {
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

            float b_min_x = (dx >= 0.0f ? x0 : x0 + dx);
            float b_max_x = (dx >= 0.0f ? x1 + dx : x1);
            float b_min_y = (dy >= 0.0f ? y0 : y0 + dy);
            float b_max_y = (dy >= 0.0f ? y1 + dy : y1);

            if (b_max_x >= c_wx0 && b_min_x <= (float)(tx1 * CS2D_TILE_SIZE) &&
                b_max_y >= c_wy0 && b_min_y <= (float)(ty1 * CS2D_TILE_SIZE)) {
                cast_shadow_rect(s_c_alpha, tex_w, tex_h, x0 - c_wx0, y0 - c_wy0, x1 - c_wx0, y1 - c_wy0, dx, dy, shadow_alpha);
            }
        }

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

            if (x1 >= c_wx0 && x0 <= (float)(tx1 * CS2D_TILE_SIZE) &&
                y1 >= c_wy0 && y0 <= (float)(ty1 * CS2D_TILE_SIZE)) {
                clear_shadow_rect(s_c_alpha, tex_w, tex_h, x0 - c_wx0, y0 - c_wy0, x1 - c_wx0, y1 - c_wy0);
            }
        }
    }

    /* Step 5: Env_Light radial light subtraction */
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        if (ent->type != CS2D_ENT_ENV_LIGHT) continue;

        int x_off = ent->str_settings[1][0] ? atoi(ent->str_settings[1]) : 0;
        int y_off = ent->str_settings[2][0] ? atoi(ent->str_settings[2]) : 0;
        float lx = (float)(ent->x * CS2D_TILE_SIZE + 16 + x_off) - c_wx0;
        float ly = (float)(ent->y * CS2D_TILE_SIZE + 16 + y_off) - c_wy0;
        float radius = (float)(ent->int_settings[0] > 0 ? ent->int_settings[0] : 128) * 0.85f;
        float strength = (float)(ent->int_settings[1] + ent->int_settings[2] + ent->int_settings[3]) / (3.0f * 255.0f);
        if (strength <= 0.0f) strength = 0.5f;

        int sx0 = (int)((lx - radius) / inv_scale);
        int sy0 = (int)((ly - radius) / inv_scale);
        int sx1 = (int)((lx + radius) / inv_scale) + 1;
        int sy1 = (int)((ly + radius) / inv_scale) + 1;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > tex_w) sx1 = tex_w;
        if (sy1 > tex_h) sy1 = tex_h;
        if (sx0 >= sx1 || sy0 >= sy1) continue;

        for (int sy = sy0; sy < sy1; sy++) {
            float py = ((float)sy + 0.5f) * inv_scale;
            for (int sx = sx0; sx < sx1; sx++) {
                float px = ((float)sx + 0.5f) * inv_scale;
                float dist = sqrtf((px - lx) * (px - lx) + (py - ly) * (py - ly));
                if (dist < radius) {
                    float falloff = 1.0f - (dist / radius);
                    float smooth = falloff * (2.0f - falloff);
                    int sub = (int)((float)shadow_alpha * strength * smooth * 1.6f);
                    int cur = (int)s_c_alpha[sy * tex_w + sx];
                    s_c_alpha[sy * tex_w + sx] = (uint8_t)((cur > sub) ? (cur - sub) : 0);
                }
            }
        }
    }

    /* Convert alpha to RGBA */
    for (size_t i = 0; i < (size_t)tex_w * (size_t)tex_h; i++) {
        s_c_rgba[i * 4 + 0] = 0;
        s_c_rgba[i * 4 + 1] = 0;
        s_c_rgba[i * 4 + 2] = 0;
        s_c_rgba[i * 4 + 3] = s_c_alpha[i];
    }

    int c_idx = cy * s_shadow_sys.chunks_x + cx;
    cs2d_shadow_chunk_t *chunk = &s_shadow_sys.chunks[c_idx];
    if (chunk->tex_id == 0) {
        glGenTextures(1, &chunk->tex_id);
        glBindTexture(GL_TEXTURE_2D, chunk->tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_c_rgba);
    } else {
        glBindTexture(GL_TEXTURE_2D, chunk->tex_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w, tex_h, GL_RGBA, GL_UNSIGNED_BYTE, s_c_rgba);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    chunk->dirty = false;
    chunk->initialized = true;
}

/* Draws visible shadow chunks on screen */
static void render_shadow_chunks(const cs2d_map_t *map, float cam_x, float cam_y)
{
    if (!s_shadows_enabled || !s_shadow_sys.chunks || !g_gl_ctx) return;

    float vw = (g_viewport_w > 0) ? (float)g_viewport_w : 854.0f;
    float vh = (g_viewport_h > 0) ? (float)g_viewport_h : 480.0f;

    float view_x0 = cam_x;
    float view_y0 = cam_y;
    float view_x1 = cam_x + vw;
    float view_y1 = cam_y + vh;

    const float chunk_world_size = (float)(CS2D_SHADOW_CHUNK_TILES * CS2D_TILE_SIZE); /* 512 px */

    int min_cx = (int)(view_x0 / chunk_world_size);
    int min_cy = (int)(view_y0 / chunk_world_size);
    int max_cx = (int)(view_x1 / chunk_world_size);
    int max_cy = (int)(view_y1 / chunk_world_size);

    if (min_cx < 0) min_cx = 0;
    if (min_cy < 0) min_cy = 0;
    if (max_cx >= s_shadow_sys.chunks_x) max_cx = s_shadow_sys.chunks_x - 1;
    if (max_cy >= s_shadow_sys.chunks_y) max_cy = s_shadow_sys.chunks_y - 1;

    for (int cy = min_cy; cy <= max_cy; cy++) {
        for (int cx = min_cx; cx <= max_cx; cx++) {
            int c_idx = cy * s_shadow_sys.chunks_x + cx;
            cs2d_shadow_chunk_t *chunk = &s_shadow_sys.chunks[c_idx];
            if (chunk->dirty || !chunk->initialized) {
                generate_shadow_chunk(map, cx, cy);
            }
            if (chunk->tex_id != 0) {
                int tx0 = cx * CS2D_SHADOW_CHUNK_TILES;
                int ty0 = cy * CS2D_SHADOW_CHUNK_TILES;
                int tx1 = (cx + 1) * CS2D_SHADOW_CHUNK_TILES;
                if (tx1 > (int)map->width)  tx1 = (int)map->width;
                int ty1 = (cy + 1) * CS2D_SHADOW_CHUNK_TILES;
                if (ty1 > (int)map->height) ty1 = (int)map->height;

                float cw = (float)((tx1 - tx0) * CS2D_TILE_SIZE);
                float ch = (float)((ty1 - ty0) * CS2D_TILE_SIZE);
                float center_x = (float)(tx0 * CS2D_TILE_SIZE) + cw * 0.5f - cam_x;
                float center_y = (float)(ty0 * CS2D_TILE_SIZE) + ch * 0.5f - cam_y;

                cs2d_texture_t tex = {
                    .id     = chunk->tex_id,
                    .width  = (tx1 - tx0) * CS2D_SHADOW_TEXELS_PER_TILE,
                    .height = (ty1 - ty0) * CS2D_SHADOW_TEXELS_PER_TILE,
                };
                cs2d_render_sprite(&tex, center_x, center_y, cw, ch,
                                   0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            }
        }
    }
}

/*
 * Trigger_Use buttons (Te.draw layer 1, 0x140276286..0x1402764ff), within (475, 290) of the screen center (409, 224).
 * ints[0] 1..10 is a buttons.bmp frame (0 = invisible), rotated by ints[1] (0 / 1 180 / 2 -90 / 3 90). Above 10 the
 * button is frame 1 with a flare1 glow pulsing at |sin(ms / 10)| + 0.5 (sin(ms / 3) above 15), colored 11 / 16 red,
 * 12 / 17 green, 13 blue (50,50,255), 14 yellow, 15 white, set at the side ints[1] points to, turning ms / 30, then
 * again at scale 0.7 turning ms / 20; with the light engine the glow is also a light sprite. Used (state 1): types 9
 * and 10 show frames 9 and 8, above 10 frame 1, 1..8 nothing.
 */
static void render_trigger_use(const cs2d_gfx_bank_t *bank, const cs2d_entity_t *ent, int state, float cam_x, float cam_y,
                               uint32_t tick_ms)
{
    int v = ent->int_settings[0], side = ent->int_settings[1];
    float px = (float)(ent->x * 32) - cam_x, py = (float)(ent->y * 32) - cam_y;
    if (fabsf(px - 409.0f) >= 475.0f || fabsf(py - 224.0f) >= 290.0f || !bank->tex_buttons.id || v == 0) return;
    float rot = side == 1 ? 180.0f : (side == 2 ? -90.0f : (side == 3 ? 90.0f : 0.0f));
    int frame;
    if (state == 0) frame = (v >= 1 && v <= 10) ? v - 1 : 1;
    else if (v == 9) frame = 9;
    else if (v == 10) frame = 8;
    else if (v > 10) frame = 1;
    else return;
    cs2d_render_sprite_frame(&bank->tex_buttons, 32, 32, frame, px + 16.0f, py + 16.0f, 32.0f, 32.0f, rot,
                             1.0f, 1.0f, 1.0f, 1.0f, 0);
    if (state != 0 || v <= 10 || !bank->tex_flare1.id) return;
    int r = 255, g = 255, b = 255;
    if (v == 11 || v == 16) { g = 0; b = 0; }
    else if (v == 12 || v == 17) { r = 0; b = 0; }
    else if (v == 13) { r = 50; g = 50; }
    else if (v == 14) { b = 0; }
    float pulse = fabsf(sinf((float)(tick_ms / 10) * (3.14159265f / 180.0f))) + 0.5f;
    float alpha = v > 15 ? sinf((float)(tick_ms / 3) * (3.14159265f / 180.0f)) : pulse;
    int ox = 0, oy = 0;
    if (side == 0) ox = 16;
    else if (side == 1) { ox = 16; oy = 32; }
    else if (side == 2) oy = 16;
    else if (side == 3) { ox = 32; oy = 16; }
    float gx = (float)(ent->x * 32 + ox) - cam_x, gy = (float)(ent->y * 32 + oy) - cam_y;
    const cs2d_texture_t *fl = &bank->tex_flare1;
    float fr = (float)r / 255.0f, fg = (float)g / 255.0f, fb = (float)b / 255.0f;
    cs2d_render_sprite_frame(fl, 0, 0, 0, gx, gy, (float)fl->width, (float)fl->height, (float)(tick_ms / 30), fr, fg, fb, alpha, 3);
    cs2d_render_sprite_frame(fl, 0, 0, 0, gx, gy, (float)fl->width * 0.7f, (float)fl->height * 0.7f, (float)(tick_ms / 20),
                             fr, fg, fb, alpha, 3);
    cs2d_light_add_sprite(false, fl, (float)fl->width * 0.5f, (float)fl->height * 0.5f, gx + cam_x, gy + cam_y, 0.7f, 0.7f,
                          r, g, b, (float)(tick_ms / 30), pulse);
}

/* Helper to render an Env_Sprite (type 22) entity (reconstructed from Te.draw 0x1402721f0..0x1402724ef) */
/*
 * Env_Object (29), Te.draw 0x140271ee0. ints[0] type (editor list at 0x1402a8707: 0 Palm, 1 Tree, 2 Alarm, 3 Sink,
 * 4 Toilet, 5 Street Light, 6 Street Light (flicker)), ints[1..3] color, ints[4] rotation, strs[0] size,
 * strs[1] alpha. The original reads both strings with bbStringToFloat (an empty string is 0) and passes the color
 * through unchanged.
 */
static float env_object_float(const char *s)
{
    return s[0] ? (float)atof(s) : 0.0f;
}

/* Te.age: + 2 x time factor per 60 Hz frame (120 degrees per second) while the object is on screen */
static float env_object_age(uint32_t tick_ms)
{
    return (float)fmod((double)tick_ms * 0.12, 360.0);
}

/* Palm / tree sway: k % 3 == 0 takes sin, others cos (0x140272e9e) */
static float env_object_sway(int k, float deg)
{
    float rad = deg * (3.14159265f / 180.0f);
    return (k % 3 == 0) ? sinf(rad) : cosf(rad);
}

/* Sink (3) and Toilet (4): Te.draw(1) (0x1402720f0), under the players, ALPHABLEND, objects.bmp frame 0 / 1 */
static void render_env_object_ground(const cs2d_gfx_bank_t *bank, const cs2d_entity_t *ent, float cam_x, float cam_y)
{
    int type = ent->int_settings[0];
    if ((type != 3 && type != 4) || !bank->tex_objects.id) return;
    float ex = (float)(ent->x * CS2D_TILE_SIZE + 16) - cam_x;
    float ey = (float)(ent->y * CS2D_TILE_SIZE + 16) - cam_y;
    float size = env_object_float(ent->str_settings[0]);
    cs2d_render_sprite_frame(&bank->tex_objects, 32, 32, type - 3, ex, ey, 32.0f * size, 32.0f * size,
                             (float)((ent->int_settings[4] / 10) * 10),
                             (float)ent->int_settings[1] / 255.0f, (float)ent->int_settings[2] / 255.0f,
                             (float)ent->int_settings[3] / 255.0f, env_object_float(ent->str_settings[1]), 0);
}

/* Palm, Tree, Alarm and Street Lights: Te.draw(2) case 0x1d (0x140272e3a), above the players */
static void render_env_object_overhead(const cs2d_map_t *map, const cs2d_gfx_bank_t *bank, const cs2d_entity_t *ent,
                                       float cam_x, float cam_y, uint32_t tick_ms, float age)
{
    int type = ent->int_settings[0];
    float ex = (float)(ent->x * CS2D_TILE_SIZE + 16) - cam_x;
    float ey = (float)(ent->y * CS2D_TILE_SIZE + 16) - cam_y;
    float r = (float)ent->int_settings[1] / 255.0f;
    float g = (float)ent->int_settings[2] / 255.0f;
    float b = (float)ent->int_settings[3] / 255.0f;
    float size  = env_object_float(ent->str_settings[0]);
    float alpha = env_object_float(ent->str_settings[1]);
    int   rot   = ent->int_settings[4];

    /* Leaf shadows (itemshadows, DAT_140ddf183): black at shadows % x alpha, moved 4 px along
     * (cos, sin)(ShadowRotation - 180) */
    bool shadows = s_item_shadows && s_shadow_intensity > 0;
    (void)map;
    float sun_rad = (cs2d_light_shadow_rotation() - 180.0f) * (3.14159265f / 180.0f);
    float sh_dx = cosf(sun_rad) * 4.0f;
    float sh_dy = sinf(sun_rad) * 4.0f;
    float sh_alpha = (float)s_shadow_intensity * 0.01f * alpha;

    if (type == 0) {
        /* Palm: 8 palmleave.png at scale size x 0.8, rotation k * 45 + rot + 3 sway(k * 20 + age), handle (15, 2)
         * on the tile center, so the image center is 33 px (scaled) away along the rotation */
        const cs2d_texture_t *tex = &bank->tex_palms;
        if (!tex->id) return;
        float s = size * 0.8f;
        float w = (float)tex->width * s, h = (float)tex->height * s;
        float d = ((float)tex->height * 0.5f - 2.0f) * s;
        for (int pass = shadows ? 0 : 1; pass < 2; pass++) {
            float ox = pass ? 0.0f : sh_dx, oy = pass ? 0.0f : sh_dy;
            for (int k = 1; k <= 8; k++) {
                float ang = (float)(k * 45 + rot) + env_object_sway(k, (float)(k * 20) + age) * 3.0f;
                float rad = ang * (3.14159265f / 180.0f);
                float cx = ex + ox - sinf(rad) * d;
                float cy = ey + oy + cosf(rad) * d;
                if (pass) cs2d_render_sprite_frame(tex, 0, 0, 0, cx, cy, w, h, ang, r, g, b, alpha, 0);
                else      cs2d_render_sprite_frame(tex, 0, 0, 0, cx, cy, w, h, ang, 0.0f, 0.0f, 0.0f, sh_alpha, 0);
            }
        }
        /* Te.update(2): the leaves light the day buffer (white floor light sprites, handle (15, 2), mapshadows > 1) */
        if (cs2d_light_day_shadows()) {
            for (int k = 1; k <= 8; k++) {
                float ang = (float)(int)((double)(k * 45 + rot) + env_object_sway(k, (float)(k * 20) + age) * 3.0);
                cs2d_light_add_sprite(true, tex, 15.0f, 2.0f, ex + cam_x, ey + cam_y, s, s, 255, 255, 255, ang, 1.0f);
            }
        }
    } else if (type == 1) {
        /* Tree: 7 treeleaves.png at size x 30 px from the center, position angle k * 51 + rot + 2 sway(age + k * 50),
         * rotation rot + 5 sway; then one at scale size x 1.2 on the center, rotation rot + 5 sin(age) (no shadow) */
        const cs2d_texture_t *tex = &bank->tex_tree;
        if (!tex->id) return;
        float w = (float)tex->width * size, h = (float)tex->height * size;
        for (int pass = shadows ? 0 : 1; pass < 2; pass++) {
            float ox = pass ? 0.0f : sh_dx, oy = pass ? 0.0f : sh_dy;
            for (int k = 1; k <= 7; k++) {
                float sway = env_object_sway(k, age + (float)(k * 50));
                float pos = ((float)(k * 51 + rot) + sway * 2.0f) * (3.14159265f / 180.0f);
                float cx = ex + ox + sinf(pos) * size * 30.0f;
                float cy = ey + oy - cosf(pos) * size * 30.0f;
                float ang = (float)rot + sway * 5.0f;
                if (pass) cs2d_render_sprite_frame(tex, 0, 0, 0, cx, cy, w, h, ang, r, g, b, alpha, 0);
                else      cs2d_render_sprite_frame(tex, 0, 0, 0, cx, cy, w, h, ang, 0.0f, 0.0f, 0.0f, sh_alpha, 0);
            }
        }
        cs2d_render_sprite_frame(tex, 0, 0, 0, ex, ey, w * 1.2f, h * 1.2f,
                                 (float)rot + sinf(age * (3.14159265f / 180.0f)) * 5.0f, r, g, b, alpha, 0);
        /* Te.update(2): the 7 clumps and the center light the day buffer (white floor light sprites, midhandle) */
        if (cs2d_light_day_shadows()) {
            float hx = (float)tex->width * 0.5f, hy = (float)tex->height * 0.5f;
            for (int k = 1; k <= 7; k++) {
                float sway = env_object_sway(k, age + (float)(k * 50));
                double pos = ((double)(k * 51 + rot) + (double)sway * 2.0) * (3.14159265358979 / 180.0);
                float lx = (float)(sin(pos) * (double)(size * 30.0f) + (double)ex);
                float ly = (float)((double)ey - (double)(size * 30.0f) * cos(pos));
                cs2d_light_add_sprite(true, tex, hx, hy, lx + cam_x, ly + cam_y, size, size, 255, 255, 255,
                                      (float)(int)((double)rot + (double)sway * 5.0), 1.0f);
            }
            cs2d_light_add_sprite(true, tex, hx, hy, ex + cam_x, ey + cam_y, size * 1.2f, size * 1.2f, 255, 255, 255,
                                  (float)(int)((double)rot + sin((double)age * (3.14159265358979 / 180.0)) * 5.0), 1.0f);
        }
    } else if (type == 2) {
        /* Alarm: flare1 additive, rotation rot, alpha |sin(ms / 3 degrees)| x alpha */
        const cs2d_texture_t *tex = &bank->tex_flare1;
        if (!tex->id) return;
        float pulse = fabsf(sinf((float)((tick_ms / 3) % 360) * (3.14159265f / 180.0f)));
        cs2d_render_sprite_frame(tex, 0, 0, 0, ex, ey, (float)tex->width * size, (float)tex->height * size,
                                 (float)rot, r, g, b, pulse * alpha, 3);
        /* Te.draw: a light sprite in the alarm color at the same alpha */
        cs2d_light_add_sprite(false, tex, (float)tex->width * 0.5f, (float)tex->height * 0.5f, ex + cam_x, ey + cam_y,
                              size, size, ent->int_settings[1], ent->int_settings[2], ent->int_settings[3], (float)rot,
                              pulse * alpha);
    } else if (type == 5 || type == 6) {
        /* Street Light: flare3 additive turning ms / 150 + rot; the flicker variant draws at alpha x Rnd(0, 0.8)
         * one frame in five (Rand(1, 5) == 1). Then the lamp, objects.bmp frame 2, white at scale 1 and rotation
         * rot rounded down to 10 degrees */
        const cs2d_texture_t *tex = &bank->tex_flare3;
        if (tex->id) {
            float a = alpha;
            if (type == 6 && rand() % 5 == 0) a *= (float)rand() / (float)RAND_MAX * 0.8f;
            cs2d_render_sprite_frame(tex, 0, 0, 0, ex, ey, (float)tex->width * size, (float)tex->height * size,
                                     (float)((int)(tick_ms / 150) + rot), r, g, b, a, 3);
        }
        if (bank->tex_objects.id) {
            cs2d_render_sprite_frame(&bank->tex_objects, 32, 32, 2, ex, ey, 32.0f, 32.0f,
                                     (float)((rot / 10) * 10), 1.0f, 1.0f, 1.0f, 1.0f, 0);
        }
    }
}

static void render_env_sprite_entity(
    cs2d_gfx_bank_t *bank,
    const cs2d_map_t *map,
    uint32_t index,
    float cam_x, float cam_y,
    uint32_t tick_ms
)
{
    const cs2d_entity_t *ent = &map->entities[index];
    if (!bank || !ent->str_settings[0][0]) return;

    int blend_mode = ent->int_settings[9];
    bool black_mask = (blend_mode == 3 || blend_mode == 4 ||
                       strstr(ent->str_settings[0], "flare") != NULL ||
                       strstr(ent->str_settings[0], "light") != NULL);

    const cs2d_texture_t *tex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], black_mask);
    if (!tex || !tex->id) return;

    float w = (ent->int_settings[0] > 0) ? (float)ent->int_settings[0] : (float)tex->width;
    float h = (ent->int_settings[1] > 0) ? (float)ent->int_settings[1] : (float)tex->height;

    /* The size of a texture sized sprite is only known now: it can be culled from the next frame on */
    cs2d_render_entity_learn_size(map, (int)index, (int)w, (int)h, ent->int_settings[2], ent->int_settings[3]);

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

    /*
     * Light engine (Te.update(2) 0x1400e4d80, sprites not covered by players): on screen, strs[7] "1" makes the sprite
     * a white light sprite, and any blend but light (3), shade (4) and grayscale (6) keeps its area out of the day
     * shadows (ObstacleShadow2), both at scale ints[0] / width, ints[1] / height.
     */
    if (atoi(ent->str_settings[4]) == 0 && cs2d_light_active()) {
        float half = rot == 0.0f ? 0.0f : fabsf((w > h ? w : h) * 0.5f);
        float hw = rot == 0.0f ? fabsf(w * 0.5f) : half, hh = rot == 0.0f ? fabsf(h * 0.5f) : half;
        if (sx + hw >= 0.0f && sy + hh >= 0.0f && sx - hw <= 850.0f && sy - hh < 481.0f) {
            float scx = w / (float)tex->width, scy = h / (float)tex->height;
            float hx = (float)tex->width * 0.5f, hy = (float)tex->height * 0.5f;
            if (strcmp(ent->str_settings[7], "1") == 0) {
                cs2d_light_add_sprite(false, tex, hx, hy, sx + cam_x, sy + cam_y, scx, scy, 255, 255, 255, (float)(int)rot, 1.0f);
            }
            if (blend_mode != 3 && blend_mode != 4 && blend_mode != 6) {
                cs2d_light_add_obstacle(tex, hx, hy, sx, sy, scx, scy, rot);
            }
        }
    }
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

/* ------------------------------------------------------------------------------------------------------------
 * Entity index & view culling (performance)
 *
 * The map passes below used to sweep every map entity once per pass. Measured on fun_roleplay (360x512 tiles,
 * 9901 entities, of which 22 are inside a 27x15 tile view on average - 0.2 %): 5 sweeps x 9901 = 49505
 * dispatches per frame for ~20 entities that are actually drawn, and the entity array is ~8 MB, so every sweep
 * is a cache-hostile walk. Env_Sound (769), Trigger_Delay (509) and Trigger_If (387) are swept five times per
 * frame without drawing anything.
 *
 * This index is built once per map (and whenever the entity table changes, map->entity_table_version: map
 * load, cs2d_ed_entity_add / _remove) and holds one list per pass, ascending by entity index, so the draw
 * order of the original passes is preserved exactly. Entities whose draw box cannot touch the view rectangle
 * are skipped before any string parse, sprite lookup or draw call.
 * ------------------------------------------------------------------------------------------------------------ */
typedef struct {
    const cs2d_map_t *map;
    uint32_t          width, height, entity_count, table_version;
    int              *pass[CS2D_EPASS_COUNT];   /* ascending entity indices */
    int               pass_n[CS2D_EPASS_COUNT];
    int               pass_cap[CS2D_EPASS_COUNT];
    uint8_t          *margin;                   /* per entity: half extent in tiles, 0 = never culled */
    uint8_t          *sprite_layer;             /* Env_Sprite strs[4] parsed once (1 = under the players) */
} cs2d_entity_index_t;

static cs2d_entity_index_t s_entity_index;

/*
 * How far the draw of an entity can reach from its tile centre, in tiles (0 = unknown here, never culled).
 * `margin_for_size` turns an image size in px into that margin - the rotated box (32 * 1.42 / 2 = 23 px for
 * a 32x32 cell) plus a 2 tile safety margin. `entity_cull_margin` is the per-type table, derived from the
 * draw code of each type:
 *  - 32x32 sprites at the tile centre (Env_Decal rotation, Env_Item <= 64x32 weapon sprite + 1 px shadow,
 *    Info_Hostage legs + 48x48 shadow, Env_NPC 32x32 cells, Trigger_Use buttons.bmp, bomb spot decals):
 *    23 px -> 1 tile; plus the <= 10 px offsets of Info_CTF_Flag / Info_Dom_Point -> 2.
 *  - Env_Breakable / Func_DynWall are drawn as their own 32x32 tile at (x * 32, y * 32) -> 1, 2 for safety.
 *  - Env_Sprite: ints[0] / ints[1] are the size when > 0 (render_env_sprite_entity), ints[2] / ints[3] the
 *    offset; a sprite without an explicit size takes the size of its texture, which is only known once the
 *    image is loaded, so it starts unculled and is culled from the second frame on (the draw path calls
 *    cs2d_render_entity_learn_size with the size it just used).
 *  - Env_Image draws at its texture size, Env_Object at "size * 30 px" leaf offsets plus texture sized
 *    flares (render_env_object_ground / _overhead): both unknown here -> never culled.
 */
static uint8_t margin_for_size(int w, int h, int offx, int offy)
{
    int off = abs(offx) > abs(offy) ? abs(offx) : abs(offy);
    float half = 0.5f * (float)(w > h ? w : h) * 1.42f;
    return (uint8_t)(((int)(half + (float)off) / CS2D_TILE_SIZE) + 2);
}

static uint8_t entity_cull_margin(const cs2d_entity_t *ent)
{
    switch (ent->type) {
        case CS2D_ENT_INFO_HOSTAGE:
        case CS2D_ENT_INFO_BOMBSPOT:
        case CS2D_ENT_INFO_CTF_FLAG:
        case CS2D_ENT_INFO_DOM_POINT:
        case CS2D_ENT_ENV_ITEM:
        case CS2D_ENT_ENV_DECAL:
        case CS2D_ENT_ENV_BREAKABLE:
        case CS2D_ENT_FUNC_DYNWALL:
        case CS2D_ENT_ENV_NPC:
        case CS2D_ENT_TRIGGER_USE:
            return 2;
        case CS2D_ENT_ENV_OBJECT: {
            /* Palms & trees have radius around tile center proportional to size string (max ~150-200px) */
            float sz = ent->str_settings[0][0] ? (float)atof(ent->str_settings[0]) : 1.0f;
            if (sz < 0.1f) sz = 1.0f;
            if (sz > 4.0f) sz = 4.0f;
            int radius_px = (int)(sz * 70.0f) + 32;
            return (uint8_t)((radius_px / CS2D_TILE_SIZE) + 2);
        }
        case CS2D_ENT_ENV_SPRITE:
            /* ints[0] / ints[1] are the size when > 0; the texture size is learned on the first draw
             * (cs2d_render_entity_learn_size), until then the sprite is not culled */
            if (ent->int_settings[0] <= 0 || ent->int_settings[1] <= 0) return 0;
            return margin_for_size(ent->int_settings[0], ent->int_settings[1],
                                   ent->int_settings[2], ent->int_settings[3]);
        default:
            return 0;
    }
}

static const int s_pass_anim_types[]        = { CS2D_ENT_INFO_ANIMATION, -1 };
static const int s_pass_solid_types[]       = { CS2D_ENT_ENV_BREAKABLE, CS2D_ENT_FUNC_DYNWALL, -1 };
static const int s_pass_ground_types[]      = { CS2D_ENT_TRIGGER_USE, CS2D_ENT_INFO_BOMBSPOT, CS2D_ENT_ENV_DECAL,
                                                CS2D_ENT_ENV_OBJECT, CS2D_ENT_ENV_ITEM, CS2D_ENT_INFO_CTF_FLAG,
                                                CS2D_ENT_INFO_DOM_POINT, CS2D_ENT_INFO_HOSTAGE, CS2D_ENT_ENV_NPC,
                                                CS2D_ENT_ENV_SPRITE, CS2D_ENT_ENV_IMAGE, -1 };
static const int s_pass_roof_sprite_types[] = { CS2D_ENT_ENV_SPRITE, CS2D_ENT_ENV_IMAGE, -1 };
static const int s_pass_roof_object_types[] = { CS2D_ENT_ENV_SPRITE, CS2D_ENT_ENV_OBJECT, -1 };

/* The types each map pass below draws. A new case in one of those switches has to be added here too. */
static const int *const s_pass_types[CS2D_EPASS_COUNT] = {
    s_pass_anim_types, s_pass_solid_types, s_pass_ground_types, s_pass_roof_sprite_types, s_pass_roof_object_types
};

static void entity_index_clear(void)
{
    for (int p = 0; p < CS2D_EPASS_COUNT; p++) {
        free(s_entity_index.pass[p]);
        s_entity_index.pass[p] = NULL;
        s_entity_index.pass_n[p] = 0;
        s_entity_index.pass_cap[p] = 0;
    }
    free(s_entity_index.margin);
    s_entity_index.margin = NULL;
    free(s_entity_index.sprite_layer);
    s_entity_index.sprite_layer = NULL;
    s_entity_index.map = NULL;
    s_entity_index.entity_count = 0;
}

static void entity_index_build(const cs2d_map_t *map)
{
    entity_index_clear();

    uint8_t in_pass[CS2D_EPASS_COUNT][256];
    memset(in_pass, 0, sizeof(in_pass));
    for (int p = 0; p < CS2D_EPASS_COUNT; p++) {
        for (const int *t = s_pass_types[p]; *t >= 0; t++) in_pass[p][(uint8_t)*t] = 1;
    }

    if (map->entity_count > 0) {
        s_entity_index.margin = (uint8_t *)calloc(map->entity_count, 1);
        s_entity_index.sprite_layer = (uint8_t *)calloc(map->entity_count, 1);
    }

    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *ent = &map->entities[i];
        uint8_t type = (uint8_t)ent->type;
        if (s_entity_index.margin) {
            s_entity_index.margin[i] = entity_cull_margin(ent);
            s_entity_index.sprite_layer[i] = (uint8_t)(atoi(ent->str_settings[4]) == 1);
        }
        for (int p = 0; p < CS2D_EPASS_COUNT; p++) {
            if (!in_pass[p][type]) continue;
            if (s_entity_index.pass_n[p] + 1 > s_entity_index.pass_cap[p]) {
                int cap = s_entity_index.pass_cap[p] ? s_entity_index.pass_cap[p] * 2 : 256;
                int *list = (int *)realloc(s_entity_index.pass[p], (size_t)cap * sizeof(int));
                if (!list) continue;
                s_entity_index.pass[p] = list;
                s_entity_index.pass_cap[p] = cap;
            }
            s_entity_index.pass[p][s_entity_index.pass_n[p]++] = (int)i;
        }
    }

    s_entity_index.map = map;
    s_entity_index.width = map->width;
    s_entity_index.height = map->height;
    s_entity_index.entity_count = map->entity_count;
    s_entity_index.table_version = map->entity_table_version;
}

/* The lists above are only rebuilt when the map or its entity table changes, so the callers of the passes do
 * not have to know about them */
void cs2d_render_entity_index_prepare(const cs2d_map_t *map)
{
    if (!map || !map->tiles) return;
    if (s_entity_index.map == map && s_entity_index.width == map->width &&
        s_entity_index.height == map->height && s_entity_index.entity_count == map->entity_count &&
        s_entity_index.table_version == map->entity_table_version) {
        return;
    }
    entity_index_build(map);
}

int cs2d_render_entity_margin(const cs2d_map_t *map, int index)
{
    if (!map || index < 0 || (uint32_t)index >= map->entity_count) return 0;
    cs2d_render_entity_index_prepare(map);
    return (s_entity_index.map == map && s_entity_index.margin) ? (int)s_entity_index.margin[index] : 0;
}

bool cs2d_render_entity_visible(const cs2d_map_t *map, int index, float cam_x, float cam_y,
                                int screen_w, int screen_h)
{
    if (!map || index < 0 || (uint32_t)index >= map->entity_count) return false;
    int margin = cs2d_render_entity_margin(map, index);
    if (margin <= 0) return true;   /* size unknown here: always drawn (see entity_cull_margin) */

    const cs2d_entity_t *ent = &map->entities[index];
    float cx = (float)(ent->x * CS2D_TILE_SIZE + CS2D_TILE_SIZE / 2);
    float cy = (float)(ent->y * CS2D_TILE_SIZE + CS2D_TILE_SIZE / 2);
    float m = (float)(margin * CS2D_TILE_SIZE);
    return cx + m >= cam_x && cy + m >= cam_y &&
           cx - m <= cam_x + (float)screen_w && cy - m <= cam_y + (float)screen_h;
}

void cs2d_render_entity_learn_size(const cs2d_map_t *map, int index, int w, int h, int offx, int offy)
{
    if (!map || !s_entity_index.margin || s_entity_index.map != map) return;
    if (index < 0 || (uint32_t)index >= s_entity_index.entity_count) return;
    uint8_t m = margin_for_size(w, h, offx, offy);
    if (m > s_entity_index.margin[index]) s_entity_index.margin[index] = m;
}

const int *cs2d_render_entity_pass_list(const cs2d_map_t *map, cs2d_entity_pass_t pass, int *out_n)
{
    if (out_n) *out_n = 0;
    if (!map || pass < 0 || pass >= CS2D_EPASS_COUNT) return NULL;
    cs2d_render_entity_index_prepare(map);
    if (s_entity_index.map != map) return NULL;
    if (out_n) *out_n = s_entity_index.pass_n[pass];
    return s_entity_index.pass[pass];
}

int cs2d_render_entity_pass_total(const cs2d_map_t *map, cs2d_entity_pass_t pass)
{
    int n = 0;
    cs2d_render_entity_pass_list(map, pass, &n);
    return n;
}

int cs2d_render_entity_pass_count(const cs2d_map_t *map, cs2d_entity_pass_t pass,
                                  float cam_x, float cam_y, int screen_w, int screen_h)
{
    int n = 0;
    const int *list = cs2d_render_entity_pass_list(map, pass, &n);
    if (!list) return 0;
    int drawn = 0;
    for (int k = 0; k < n; k++) {
        if (cs2d_render_entity_visible(map, list[k], cam_x, cam_y, screen_w, screen_h)) drawn++;
    }
    return drawn;
}

/* Env_Sprite strs[4] == "1": drawn in the ground pass instead of the overhead one (cached by the index) */
static bool entity_sprite_is_ground(const cs2d_map_t *map, uint32_t i)
{
    if (s_entity_index.map == map && s_entity_index.sprite_layer && i < s_entity_index.entity_count) {
        return s_entity_index.sprite_layer[i] != 0;
    }
    return atoi(map->entities[i].str_settings[4]) == 1;
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

    set_player_shadow_offset(map);

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

    /* Build tile animation remap table (index list: Info_Animation entities only, ascending) */
    uint8_t anim_remap[256];
    for (int i = 0; i < 256; i++) anim_remap[i] = (uint8_t)i;
    int anim_n = 0;
    const int *anim_list = cs2d_render_entity_pass_list(map, CS2D_EPASS_ANIM, &anim_n);
    for (int k = 0; k < anim_n; k++) {
        const cs2d_entity_t *e = &map->entities[anim_list[k]];
        int start_f = e->int_settings[0];
        int end_f   = e->int_settings[1];
        int speed   = e->int_settings[2] > 0 ? e->int_settings[2] : 200;
        if (start_f >= 0 && end_f > start_f && end_f < 256) {
            int span = (end_f - start_f) + 1;
            anim_remap[start_f] = (uint8_t)(start_f + ((tick_ms / (uint32_t)speed) % (uint32_t)span));
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

    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);

    /* Pass 1: Render base tiles and tile overlays (per cs2d_spec_map_format.txt section 4).
     * Blended tiles (Ttileblend) are drawn afterwards from the blend atlas, with the same rotation and brightness */
    tileblend_source_t *blend = has_tileset ? tileblend_find(tileset_tex->id) : NULL;
    static int *blended = NULL;
    static size_t blended_cap = 0;
    size_t blended_n = 0;
    for (int ty = start_ty; ty < end_ty; ty++) {
        for (int tx = start_tx; tx < end_tx; tx++) {
            uint32_t idx = (uint32_t)(ty * map->width + tx);
            uint8_t tile_idx = anim_remap[map->tiles[idx]];
            uint8_t mod      = map->modifiers ? map->modifiers[idx] : 0;

            float rx = (float)(tx * CS2D_TILE_SIZE) - cam_x;
            float ry = (float)(ty * CS2D_TILE_SIZE) - cam_y;

            if (has_tileset) {
                int cell = tileblend_cell(blend, map, tx, ty, tile_src);
                if (cell >= 0) {
                    if (blended_n + 3 > blended_cap) {
                        size_t cap = blended_cap ? blended_cap * 2 : 768;
                        int *grown = (int *)realloc(blended, cap * sizeof(int));
                        if (grown) {
                            blended = grown;
                            blended_cap = cap;
                        }
                    }
                    if (blended_n + 3 <= blended_cap) {
                        blended[blended_n++] = tx;
                        blended[blended_n++] = ty;
                        blended[blended_n++] = cell;
                        continue;
                    }
                }
                const uint8_t *rgb = ((mod & 0xc0) == 0x80 && map->tile_data) ? &map->tile_data[idx * 4] : NULL;
                draw_tile_frame(tileset_tex, tile_src, cols, tile_idx, mod, rx, ry, (float)CS2D_TILE_SIZE, 1.0f, rgb);
            }
        }
    }
    if (blended_n > 0) {
        tileblend_upload(blend);
        if (blend->atlas.id) {
            for (size_t i = 0; i < blended_n; i += 3) {
                int tx = blended[i], ty = blended[i + 1];
                draw_tile_frame(&blend->atlas, tile_src, TILEBLEND_ATLAS_COLS, blended[i + 2],
                                map->modifiers[(size_t)ty * map->width + (size_t)tx],
                                (float)(tx * CS2D_TILE_SIZE) - cam_x, (float)(ty * CS2D_TILE_SIZE) - cam_y,
                                (float)CS2D_TILE_SIZE, 1.0f, NULL);
            }
        }
    }

    /* Pass 1b: Render Env_Breakable (type 25) and Func_DynWall (type 71) using map tileset (Te.draw 0x140272808 & 0x140273858) */
    if (has_tileset) {
        int solid_n = 0;
        const int *solid_list = cs2d_render_entity_pass_list(map, CS2D_EPASS_SOLID, &solid_n);
        for (int k = 0; k < solid_n; k++) {
            uint32_t i = (uint32_t)solid_list[k];
            const cs2d_entity_t *ent = &map->entities[i];
            if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
            if (!cs2d_render_entity_visible(map, (int)i, cam_x, cam_y, screen_w, screen_h)) continue;
            {
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
                    draw_tile_frame(tileset_tex, tile_src, cols, anim_remap[(uint8_t)tile_f], 0, rx, ry,
                                    (float)CS2D_TILE_SIZE, alpha, NULL);
                }
            }
        }
    }

    /* Pass 3: Ground Entities & Actors (below players). The index list holds the types of the switch below,
     * ascending by entity index, so the draw order is the original sweep's */
    int ground_n = 0;
    const int *ground_list = cs2d_render_entity_pass_list(map, CS2D_EPASS_GROUND, &ground_n);
    for (int k = 0; k < ground_n; k++) {
        uint32_t i = (uint32_t)ground_list[k];
        const cs2d_entity_t *ent = &map->entities[i];
        /* Hidden / open / broken by a trigger; a CTF flag base keeps its glow while the flag is away */
        if (ent->type == CS2D_ENT_TRIGGER_USE) {
            if (bank) render_trigger_use(bank, ent, map->entity_state[i], cam_x, cam_y, tick_ms);
            continue;
        }
        if (map->entity_state[i] && ent->type != CS2D_ENT_INFO_CTF_FLAG) continue;
        if (!cs2d_render_entity_visible(map, (int)i, cam_x, cam_y, screen_w, screen_h)) continue;
        float ex = (float)(ent->x * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_x;
        float ey = (float)(ent->y * CS2D_TILE_SIZE + (CS2D_TILE_SIZE / 2)) - cam_y;

        switch (ent->type) {
            case CS2D_ENT_INFO_BOMBSPOT:
                /* Info_BombSpot (5): original CS2D does not render any decals on the map floor; icons appear on radar only */
                break;
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
            case CS2D_ENT_ENV_OBJECT:
                if (bank) render_env_object_ground(bank, ent, cam_x, cam_y);
                break;
            case CS2D_ENT_ENV_ITEM: {
                /* Env_Item (21): ints[0]=weapon/item ID -> drawn with gfx/weapons/<wep>_d.bmp (Titem.draw 0x140147b80 / Te.draw 0x140260000) */
                int wep_id = ent->int_settings[0];
                if (bank && wep_id > 0 && wep_id < CS2D_MAX_WEAPONS && bank->tex_weapons_d[wep_id].id) {
                    const cs2d_texture_t *dtex = &bank->tex_weapons_d[wep_id];
                    float dw = (dtex->width > 64) ? (float)(dtex->width / 2) : (float)dtex->width;
                    float dh = (dtex->height > 32) ? (float)(dtex->height / 2) : (float)dtex->height;
                    if (s_shadows_enabled && s_item_shadows && s_shadow_intensity > 0) {
                        float shadow_a = (float)s_shadow_intensity * 0.01f;
                        const cs2d_texture_t *stex = bank->tex_shadow.id ? &bank->tex_shadow : &bank->tex_leaf;
                        if (stex && stex->id) {
                            cs2d_render_sprite_frame(stex, 32, 32, 0,
                                                     ex, ey, 32.0f, 32.0f, 0.0f,
                                                     0.0f, 0.0f, 0.0f, shadow_a, 0);
                        }
                        cs2d_render_sprite(dtex, ex + 1.0f, ey + 1.0f, dw, dh, 0.0f,
                                           0.0f, 0.0f, 0.0f, shadow_a);
                    }
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
                    if (s_shadows_enabled && s_player_shadows && s_shadow_intensity > 0) {
                        const cs2d_texture_t *stex = bank->tex_shadow.id ? &bank->tex_shadow : &bank->tex_leaf;
                        if (stex && stex->id) {
                            float shadow_a = ((float)s_shadow_intensity * 0.01f);
                            cs2d_render_sprite_frame(stex, 32, 32, 0,
                                                     ex, ey, 48.0f, 48.0f, 0.0f,
                                                     0.0f, 0.0f, 0.0f, shadow_a, 0);
                        }
                    }
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
                if (entity_sprite_is_ground(map, i)) {
                    render_env_sprite_entity(bank, map, i, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_IMAGE: {
                /* Ground pass Env_Image (28) when ints[2] == 0 (floor image) */
                if (ent->int_settings[2] == 0 && bank && ent->str_settings[0][0]) {
                    const cs2d_texture_t *itex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], false);
                    if (itex && itex->id) {
                        cs2d_render_entity_learn_size(map, (int)i, itex->width, itex->height,
                                                       ent->int_settings[0], ent->int_settings[1]);
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
 * Renders CPU shadow chunks (used when light engine is off, light == 0) onto the map floor.
 * Must be rendered before players so shadows sit underneath player bodies.
 */
void cs2d_render_map_shadows(const cs2d_map_t *map, float cam_x, float cam_y)
{
    if (!map) return;
    if (s_shadows_enabled && !cs2d_light_active()) {
        cam_x = roundf(cam_x);
        cam_y = roundf(cam_y);
        ensure_shadow_chunks(map);
        render_shadow_chunks(map, cam_x, cam_y);
    }
}

/*
 * Map Overhead Entity Pass (Pass 2):
 *   1. Non-additive rooftop sprites (dustroof.png, roof_edge1.png, dome.png, Env_Image)
 *   2. Env_Object palms, trees, alarms and street lights (Sink / Toilet are on the ground pass) & additive flares
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
    int roof_n = 0;
    const int *roof_list = cs2d_render_entity_pass_list(map, CS2D_EPASS_ROOF_SPRITE, &roof_n);
    for (int k = 0; k < roof_n; k++) {
        uint32_t i = (uint32_t)roof_list[k];
        const cs2d_entity_t *ent = &map->entities[i];
        if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
        if (!cs2d_render_entity_visible(map, (int)i, cam_x, cam_y, screen_w, screen_h)) continue;
        switch (ent->type) {
            case CS2D_ENT_ENV_SPRITE: {
                if (!entity_sprite_is_ground(map, i) &&
                    ent->int_settings[9] != 3 && ent->int_settings[9] != 4) {
                    render_env_sprite_entity(bank, map, i, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_IMAGE: {
                if (ent->int_settings[2] != 0 && ent->str_settings[0][0]) {
                    const cs2d_texture_t *itex = cs2d_gfx_get_sprite(bank, ent->str_settings[0], false);
                    if (itex && itex->id) {
                        cs2d_render_entity_learn_size(map, (int)i, itex->width, itex->height,
                                                       ent->int_settings[0], ent->int_settings[1]);
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

    /* Pass 2: Te.draw(2) Env_Object palms, trees, alarms and street lights (0x140271ee0, p3 == 2) */
    float age = env_object_age(tick_ms);
    int objects_n = 0;
    const int *objects_list = cs2d_render_entity_pass_list(map, CS2D_EPASS_ROOF_OBJECT, &objects_n);
    for (int k = 0; k < objects_n; k++) {
        uint32_t i = (uint32_t)objects_list[k];
        const cs2d_entity_t *ent = &map->entities[i];
        if (map->entity_state[i]) continue; /* Hidden / open / broken by a trigger */
        if (!cs2d_render_entity_visible(map, (int)i, cam_x, cam_y, screen_w, screen_h)) continue;

        switch (ent->type) {
            case CS2D_ENT_ENV_SPRITE: {
                if (!entity_sprite_is_ground(map, i) &&
                    (ent->int_settings[9] == 3 || ent->int_settings[9] == 4)) {
                    render_env_sprite_entity(bank, map, i, cam_x, cam_y, tick_ms);
                }
                break;
            }
            case CS2D_ENT_ENV_OBJECT:
                render_env_object_overhead(map, bank, ent, cam_x, cam_y, tick_ms, age);
                break;
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
 * Cached full-map minimap image: 2 px per map tile, box-averaged from the tileset.
 * The original advanced radar (TSmartRadar) renders the map at 1/16 scale
 * (2 px per 32 px tile) into sys/cache/radars/<map>.png and draws a circular
 * crop of it around the camera (FUN_14027c8c0 / FUN_14027db80 in CS2D.exe).
 * One pixel of this image covers 16 world pixels.
 */
static uint32_t         *s_minimap_full = NULL;
static int               s_minimap_full_w = 0;
static int               s_minimap_full_h = 0;
static const cs2d_map_t *s_minimap_full_map = NULL;
static uint32_t          s_minimap_full_tileset = 0;

void cs2d_render_invalidate_shadows(void)
{
    if (s_shadow_sys.chunks) {
        int total = s_shadow_sys.chunks_x * s_shadow_sys.chunks_y;
        for (int i = 0; i < total; i++) {
            if (g_gl_ctx && s_shadow_sys.chunks[i].tex_id != 0) {
                glDeleteTextures(1, &s_shadow_sys.chunks[i].tex_id);
            }
        }
        free(s_shadow_sys.chunks);
        s_shadow_sys.chunks = NULL;
    }
    free(s_shadow_sys.eff_modes);
    s_shadow_sys.eff_modes = NULL;
    free(s_shadow_sys.eff_heights);
    s_shadow_sys.eff_heights = NULL;
    free(s_shadow_sys.last_entity_state);
    s_shadow_sys.last_entity_state = NULL;
    free(s_shadow_sys.last_object_shadow);
    s_shadow_sys.last_object_shadow = NULL;

    s_shadow_sys.chunks_x = 0;
    s_shadow_sys.chunks_y = 0;
    s_shadow_sys.map_w = 0;
    s_shadow_sys.map_h = 0;
    s_shadow_sys.solid_version = 0;
    s_shadow_sys.daylight_time = -1;

    free(s_minimap_full);
    s_minimap_full         = NULL;
    s_minimap_full_w       = 0;
    s_minimap_full_h       = 0;
    s_minimap_full_map     = NULL;
    s_minimap_full_tileset = 0;
}

void cs2d_render_ensure_shadow_chunks(const cs2d_map_t *map)
{
    ensure_shadow_chunks(map);
}

void cs2d_render_get_shadow_chunk_info(int *out_chunks_x, int *out_chunks_y, int *out_dirty_count)
{
    if (out_chunks_x) *out_chunks_x = s_shadow_sys.chunks_x;
    if (out_chunks_y) *out_chunks_y = s_shadow_sys.chunks_y;
    if (out_dirty_count) {
        int dirty = 0;
        int total = s_shadow_sys.chunks_x * s_shadow_sys.chunks_y;
        for (int i = 0; i < total; i++) {
            if (s_shadow_sys.chunks[i].dirty) dirty++;
        }
        *out_dirty_count = dirty;
    }
}

void cs2d_render_clear_shadow_dirty_test(void)
{
    int total = s_shadow_sys.chunks_x * s_shadow_sys.chunks_y;
    for (int i = 0; i < total; i++) {
        s_shadow_sys.chunks[i].dirty = false;
    }
}

void cs2d_render_set_shadows_enabled(bool enabled)
{
    s_shadows_enabled = enabled;
}

bool cs2d_render_get_shadows_enabled(void)
{
    return s_shadows_enabled;
}

void cs2d_render_set_shadow_intensity(int intensity)
{
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    s_shadow_intensity = intensity;
}

int cs2d_render_get_shadow_intensity(void)
{
    return s_shadow_intensity;
}

void cs2d_render_set_player_shadows(bool enabled)
{
    s_player_shadows = enabled;
}

bool cs2d_render_get_player_shadows(void)
{
    return s_player_shadows;
}

void cs2d_render_set_item_shadows(bool enabled)
{
    s_item_shadows = enabled;
}

bool cs2d_render_get_item_shadows(void)
{
    return s_item_shadows;
}

void cs2d_render_set_vsync(bool enabled)
{
    s_vsync = enabled;
    if (g_gl_ctx) {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
}

bool cs2d_render_get_vsync(void)
{
    return s_vsync;
}

/* Builds the full-map minimap image (2 px per tile) once per map, box-averaging
 * the tileset pixels the same way the tile renderer samples them. */
static void build_minimap_full(const cs2d_map_t *map, const cs2d_texture_t *tileset_tex)
{
    free(s_minimap_full);
    s_minimap_full         = NULL;
    s_minimap_full_w       = (int)map->width * 2;
    s_minimap_full_h       = (int)map->height * 2;
    s_minimap_full_map     = map;
    s_minimap_full_tileset = tileset_tex ? tileset_tex->id : 0;

    if (s_minimap_full_w <= 0 || s_minimap_full_h <= 0) return;

    s_minimap_full = (uint32_t *)malloc((size_t)s_minimap_full_w * (size_t)s_minimap_full_h * 4);
    if (!s_minimap_full) return;

    /* Tileset pixels after their mask mode (the same source draw_tile_frame uses) */
    tileblend_source_t *src = tileset_tex ? tileblend_find(tileset_tex->id) : NULL;
    bool have = (src && src->tileset.px && src->tileset.w >= 32);
    int tile_src = (map->use_64x64_tiles == 1) ? 64 : 32;
    int tcols = have ? (src->tileset.w / tile_src) : 0;
    if (tcols < 1) tcols = 1;
    int half = tile_src >> 1; /* world px covered by one minimap px along one axis */

    for (int my = 0; my < s_minimap_full_h; my++) {
        int ty    = my >> 1;
        int sub_y = (my & 1) * half;
        for (int mx = 0; mx < s_minimap_full_w; mx++) {
            int tx    = mx >> 1;
            int sub_x = (mx & 1) * half;

            unsigned sr = map->bg_r, sg = map->bg_g, sb = map->bg_b, sa = 255;
            if ((uint32_t)tx < map->width && (uint32_t)ty < map->height) {
                uint8_t t = map->tiles[(size_t)ty * map->width + (size_t)tx];
                if (have && t != 0) {
                    int tcol = t % tcols;
                    int trow = t / tcols;
                    int px0  = tcol * tile_src + sub_x;
                    int py0  = trow * tile_src + sub_y;
                    unsigned rr = 0, gg = 0, bb = 0, aa = 0;
                    for (int y = 0; y < half; y++) {
                        for (int x = 0; x < half; x++) {
                            uint32_t p = src->tileset.px[(size_t)(py0 + y) * (size_t)src->tileset.w + (size_t)(px0 + x)];
                            bb += (p >> 16) & 0xff;
                            gg += (p >>  8) & 0xff;
                            rr += (p      ) & 0xff;
                            aa += (p >> 24) & 0xff;
                        }
                    }
                    int n = half * half;
                    /* Composite the tile over the map background, like the map renderer
                     * does for transparent (non-void) tiles. */
                    float af = (float)aa / (float)(n * 255.0f);
                    sr = (unsigned)((float)(rr / n) * af + (float)map->bg_r * (1.0f - af));
                    sg = (unsigned)((float)(gg / n) * af + (float)map->bg_g * (1.0f - af));
                    sb = (unsigned)((float)(bb / n) * af + (float)map->bg_b * (1.0f - af));
                    sa = 255;
                }
            }
            s_minimap_full[(size_t)my * (size_t)s_minimap_full_w + (size_t)mx] =
                (sa << 24) | (sb << 16) | (sg << 8) | sr;
        }
    }
}

static void ensure_minimap_texture(cs2d_gfx_bank_t *bank, const cs2d_map_t *map,
                                   const cs2d_player_t *player, const cs2d_texture_t *tileset_tex)
{
    if (!bank || !map || !map->tiles || map->width == 0 || map->height == 0 || !g_gl_ctx) return;

    if (!s_minimap_full || s_minimap_full_map != map ||
        s_minimap_full_tileset != (tileset_tex ? tileset_tex->id : 0)) {
        build_minimap_full(map, tileset_tex);
    }
    if (!s_minimap_full) return;

    /* Center on the player's exact pixel position (smooth, sub-tile) */
    float player_px = player ? player->pos.x : ((float)map->width * CS2D_TILE_SIZE * 0.5f);
    float player_py = player ? player->pos.y : ((float)map->height * CS2D_TILE_SIZE * 0.5f);

    static float s_last_player_px = -9999.0f, s_last_player_py = -9999.0f;
    static const cs2d_map_t *s_last_minimap_map = NULL;
    if (s_last_minimap_map == map && bank->tex_minimap.id != 0 &&
        fabsf(player_px - s_last_player_px) < 0.5f &&
        fabsf(player_py - s_last_player_py) < 0.5f) {
        return;
    }
    s_last_player_px = player_px;
    s_last_player_py = player_py;
    s_last_minimap_map = map;

    /* The original radar draws the minimap at scale * 0.0625 world px per radar px
     * (16 world px = half a tile = one full-map minimap px) -> 2 radar px per tile. */
    const float world_per_px = 16.0f;
    const float radius = 56.5f; /* opening of gfx/hud_radar_advanced.png */

    uint8_t rgba[128 * 128 * 4];
    memset(rgba, 0, sizeof(rgba));

    for (int py = 0; py < 128; py++) {
        for (int px = 0; px < 128; px++) {
            float dx = (float)px - 63.5f;
            float dy = (float)py - 63.5f;
            if (dx * dx + dy * dy > radius * radius) continue;

            float wx = player_px + dx * world_per_px;
            float wy = player_py + dy * world_per_px;
            int mx = (int)floorf(wx / world_per_px);
            int my = (int)floorf(wy / world_per_px);

            uint32_t c;
            if (mx < 0 || my < 0 || mx >= s_minimap_full_w || my >= s_minimap_full_h) {
                c = (255u << 24) | ((uint32_t)map->bg_b << 16) | ((uint32_t)map->bg_g << 8) | map->bg_r;
            } else {
                c = s_minimap_full[(size_t)my * (size_t)s_minimap_full_w + (size_t)mx];
            }
            uint8_t *o = &rgba[(py * 128 + px) * 4];
            o[0] = (uint8_t)(c & 0xff);
            o[1] = (uint8_t)((c >> 8) & 0xff);
            o[2] = (uint8_t)((c >> 16) & 0xff);
            o[3] = (uint8_t)((c >> 24) & 0xff);
        }
    }

    if (bank->tex_minimap.id == 0) {
        GLuint gl_id = 0;
        glGenTextures(1, &gl_id);
        bank->tex_minimap.id     = gl_id;
        bank->tex_minimap.width  = 128;
        bank->tex_minimap.height = 128;
        glBindTexture(GL_TEXTURE_2D, bank->tex_minimap.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        glBindTexture(GL_TEXTURE_2D, bank->tex_minimap.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
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
    const cs2d_texture_t *tileset_tex,
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
        ensure_minimap_texture(bank, &game->map, player, tileset_tex);
    }
    if (bank->tex_hud_radar_adv.id) {
        cs2d_render_sprite_frame(&bank->tex_hud_radar_adv,
                                 bank->tex_hud_radar_adv.width, bank->tex_hud_radar_adv.height, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.65f, 0);
    }
    if (bank->tex_minimap.id) {
        cs2d_render_sprite_frame(&bank->tex_minimap, 128, 128, 0,
                                 rcx, rcy, 128.0f, 128.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0);
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

    /* Radar blips (TSmartRadar): 16 world px per radar px, clipped to the radar edge */
    if (game && bank->tex_hud_radar_icons.id) {
        const float world_per_px = 16.0f;
        const float clip_r       = 51.0f;

        /* Info_RadarIcon (type 81): int_settings[0] is a direct frame index (8='A', 9='B') */
        for (uint32_t i = 0; i < game->map.entity_count; i++) {
            const cs2d_entity_t *ent = &game->map.entities[i];
            if (ent->type != CS2D_ENT_INFO_RADARICON) continue;
            int   icon_f = ent->int_settings[0];
            float dx = ((float)ent->x * CS2D_TILE_SIZE + 16.0f - player->pos.x) / world_per_px;
            float dy = ((float)ent->y * CS2D_TILE_SIZE + 16.0f - player->pos.y) / world_per_px;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > clip_r) { dx *= clip_r / dist; dy *= clip_r / dist; }
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

        /* Player blips: teammates in team color, the local player red. Enemies are
         * hidden in team modes (radarhack protection) and shown in Deathmatch. */
        bool dm = (game->gamemode == CS2D_MODE_DM);
        for (int i = 1; i <= CS2D_MAX_PLAYERS; i++) {
            const cs2d_player_t *p = &game->players[i];
            if (!p->alive || p->team == CS2D_TEAM_SPEC) continue;
            bool local = (p->id == player->id);
            if (!local && !dm && p->team != player->team) continue;

            float dx = (p->pos.x - player->pos.x) / world_per_px;
            float dy = (p->pos.y - player->pos.y) / world_per_px;
            float d  = sqrtf(dx * dx + dy * dy);
            if (d > clip_r) { dx *= clip_r / d; dy *= clip_r / d; }

            float pr, pg, pb;
            if (local)                         { pr = 1.0f; pg = 0.0f;   pb = 0.0f;   }
            else if (p->team == CS2D_TEAM_T)   { pr = 1.0f; pg = 0.588f; pb = 0.588f; } /* 0xff9696 */
            else                               { pr = 0.588f; pg = 0.588f; pb = 1.0f; } /* 0x9696ff */
            float s = local ? 12.0f : 10.0f;
            cs2d_render_sprite_frame(&bank->tex_hud_radar_icons, 32, 32, 0,
                                     rcx + dx, rcy + dy, s, s, 0.0f,
                                     pr, pg, pb, 0.90f, 0);
        }
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

    /* 6. Action Progress Bar (Reloading, Planting Bomb, Defusing Bomb) */
    cs2d_render_hud_progress_bar(player, screen_w, screen_h);
}

/*
 * In-game action progress bar layout (reconstructed from FUN_140295190 0x1402958f0..0x140295f70 in CS2D.exe):
 * Handles Reloading (sys/language 0759), Planting Bomb (0760) and Defusing Bomb (0761).
 */
bool cs2d_hud_progress_layout(
    const cs2d_player_t *player,
    int screen_w, int screen_h,
    cs2d_hud_progress_layout_t *out_layout
)
{
    if (!player || !player->alive) return false;

    const char *title = NULL;
    float progress = 0.0f;

    const cs2d_weapon_def_t *wep = cs2d_weapon_get(player->current_weapon);
    if ((player->is_reloading || player->reload_timer > 0.0f) &&
        wep && wep->clip_size > 0 && wep->slot != CS2D_SLOT_MELEE) {
        title = "Reloading"; /* sys/language 0759 */
        float total = wep->reload_time;
        if (total > 0.0f) {
            progress = 1.0f - (player->reload_timer / total);
        } else {
            progress = 1.0f;
        }
    } else if (player->is_planting) {
        title = "Planting Bomb"; /* sys/language 0760 */
        progress = player->plant_progress / 3.0f;
    } else if (player->is_defusing) {
        title = "Defusing Bomb"; /* sys/language 0761 */
        float max_time = player->has_defuse_kit ? 5.0f : 10.0f;
        progress = player->defuse_progress / max_time;
    } else {
        return false;
    }

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    /*
     * Geometry from CS2D.exe FUN_140295190 (0x140295980):
     * scale = font_height / 13.0f = 1.0f (DAT_140a1ccc8 * 0.07692308)
     */
    float scale = 1.0f;
    int bar_w = (int)(scale * 102.0f);
    int bar_h = (int)(scale * 20.0f);
    int border = (int)scale;
    if (border < 1) border = 1;

    float bar_x = (float)(screen_w / 2 - bar_w / 2);
    float bar_y = (float)(screen_h / 2) + scale * 80.0f;

    int inner_w = bar_w - border * 4;
    int inner_h = bar_h - border * 4;
    float inner_x = bar_x + (float)(border * 2);
    float inner_y = bar_y + (float)(border * 2);
    float fill_w = (inner_w > 0) ? ((float)inner_w * progress) : 0.0f;

    float text_w = cs2d_render_text_width(title);
    float text_h = 13.0f * scale;
    float text_x = (float)(screen_w / 2) - text_w * 0.5f;
    float text_y = bar_y - text_h - scale * 4.0f;

    if (out_layout) {
        out_layout->bar_x = bar_x;
        out_layout->bar_y = bar_y;
        out_layout->bar_w = (float)bar_w;
        out_layout->bar_h = (float)bar_h;
        out_layout->border = (float)border;
        out_layout->inner_x = inner_x;
        out_layout->inner_y = inner_y;
        out_layout->inner_w = (float)inner_w;
        out_layout->inner_h = (float)inner_h;
        out_layout->fill_w = fill_w;
        out_layout->text_x = text_x;
        out_layout->text_y = text_y;
        out_layout->progress = progress;
        out_layout->title = title;
    }

    return true;
}

void cs2d_render_hud_progress_bar(
    const cs2d_player_t *player,
    int screen_w, int screen_h
)
{
    cs2d_hud_progress_layout_t l;
    if (!cs2d_hud_progress_layout(player, screen_w, screen_h, &l)) return;

    cs2d_gfx_bank_t *bank = cs2d_render_get_gfx_bank();

    /* 1. Background shadow (0x1402959c0): incbindata/shadow.bmp scaled by (11.0, 5.0) at alpha 0.4 */
    float cx = (float)(screen_w / 2);
    float cy = l.bar_y + l.bar_h * 0.5f;
    const cs2d_texture_t *stex = (bank && bank->tex_shadow.id) ? &bank->tex_shadow : (bank ? &bank->tex_leaf : NULL);
    if (stex && stex->id) {
        cs2d_render_sprite_frame(stex, 32, 32, 0, cx, cy, 32.0f * 11.0f, 32.0f * 5.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.40f, 0);
    } else {
        cs2d_render_rect(l.bar_x, l.bar_y, l.bar_w, l.bar_h, 0.0f, 0.0f, 0.0f, 0.40f, true);
    }

    /* 2. Title text (0x140295a00..0x140295a50): yellow tint (255, 200, 0), alpha 0.75 */
    float r = 1.0f;
    float g = 200.0f / 255.0f; /* 0.784f (warm amber/gold HUD color * 1.64) */
    float b = 0.0f;
    float a = 0.75f;           /* Max2D_SetAlpha(0.75) */

    cs2d_render_text(l.text_x, l.text_y, l.title, r, g, b, a);

    /* 3. Outer border frame (top, bottom, left, right) (0x140295a70..0x140295b00) */
    cs2d_render_rect(l.bar_x, l.bar_y, l.bar_w, l.border, r, g, b, a, true);
    cs2d_render_rect(l.bar_x, l.bar_y + l.bar_h - l.border, l.bar_w, l.border, r, g, b, a, true);
    cs2d_render_rect(l.bar_x, l.bar_y, l.border, l.bar_h, r, g, b, a, true);
    cs2d_render_rect(l.bar_x + l.bar_w - l.border, l.bar_y, l.border, l.bar_h, r, g, b, a, true);

    /* 4. Filled progress bar (0x140295b20) */
    if (l.fill_w > 0.0f && l.inner_h > 0.0f) {
        cs2d_render_rect(l.inner_x, l.inner_y, l.fill_w, l.inner_h, r, g, b, a, true);
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
            for (int k = 0; k < CS2D_SLOT_COUNT; k++) {
                uint8_t w = player->inventory[k];
                if (w == CS2D_WEP_NONE || cs2d_weapon_hud_slot(w) != slot) continue;
                y += (float)(int)(s * 12.0f);
                cs2d_render_rect(x, y, (float)(int)(s * 10.0f), (float)(int)(s * 10.0f),
                                 100.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f, true);
            }
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

    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    cs2d_gl_quad(e->tex_id, rx, ry, rw, rh, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a);
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
    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    cs2d_gl_rect_grad(x, y, w, h, r_top, g_top, b_top, a_top, r_bot, g_bot, b_bot, a_bot);
}

void cs2d_render_scissor_begin(int x, int y, int w, int h)
{
    cs2d_gl_scissor(x, y, w, h);
}

void cs2d_render_scissor_end(void)
{
    cs2d_gl_scissor_off();
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

            cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
            cs2d_gl_quad_rot(tileset_tex->id, sx, sy, hs * 2.0f, hs * 2.0f, u0, v0, u1, v1, p->rot,
                             1.0f, 1.0f, 1.0f, t);
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
            cs2d_gl_set_blend(CS2D_GL_BLEND_ADD);
            cs2d_render_line(sx, sy, p->x2 - cam_x, p->y2 - cam_y, 1.0f, 0.9f, 0.4f, 0.6f * t);
            cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
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
        } else if (p->kind == CS2D_FX_SMOKE && bank) {
            float cur_size = p->size * (1.0f + (1.0f - t) * 0.8f);
            cs2d_render_sprite_frame(&bank->tex_glowdot, 0, 0, 0, sx, sy, cur_size, cur_size,
                                     p->rot, p->r, p->g, p->b, 0.45f * t, 3);
        } else if (p->kind == CS2D_FX_FIRE && bank) {
            float cur_size = p->size * (0.8f + t * 0.4f);
            cs2d_render_sprite_frame(&bank->tex_glowdot, 0, 0, 0, sx, sy, cur_size, cur_size,
                                     p->rot, p->r, p->g, p->b, t, 4);
        } else if (p->kind == CS2D_FX_SPARKLE && bank) {
            cs2d_render_sprite_frame(&bank->tex_glowdot, 0, 0, 0, sx, sy, p->size, p->size,
                                     p->rot, p->r, p->g, p->b, t, 4);
        } else if (p->kind == CS2D_FX_BLOOD && bank) {
            cs2d_render_sprite_frame(&bank->tex_fragments, 32, 32, p->frame, sx, sy, p->size, p->size,
                                     p->rot, p->r, p->g, p->b, t, 0);
        } else if (p->kind == CS2D_FX_SNOW && bank) {
            float cur_size = p->size * (0.8f + t * 0.3f);
            cs2d_render_sprite_frame(&bank->tex_glowdot, 0, 0, 0, sx, sy, cur_size, cur_size,
                                     p->rot, p->r, p->g, p->b, 0.7f * t, 3);
        }
    }
}
