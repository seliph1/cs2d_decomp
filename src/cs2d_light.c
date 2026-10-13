/*
 * Light engine: port of TSmartLighting, TLightSource, TLightingRoom, TLightingStripe, TLightingSprite(Floor) and
 * ObstacleShadow(2) from CS2D.exe (docs/dump/cs2d/lighting/). KNOWLEDGE.md "Light engine" has the overview.
 *
 * Every function was checked call by call against the original run in Unicorn (tools/agent/emu_light.py,
 * tools/agent/light_diff.py): the Max2D layer below prints the same lines as the emulator when a trace is set.
 */
#include "cs2d_light.h"
#include "cs2d_render.h"
#include "cs2d_gl.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD 0.017453292519943295
#define RAD2DEG 57.29577951308232
#define COS90   6.123233995736766e-17   /* cos(90 deg) in double, as the original's constants */
#define SIN180  1.2246467991473532e-16

#define MAIN_W         854     /* MainBuffer: (ScreenWidth + 4) / SCALE_NIGHTBUFFER, GlobalInit 0x140078bb0 */
#define MAIN_H         480
#define SCREEN_W       850.0f  /* TSmartLighting.ScreenWidth (0x44548000 at 0x1403060d8) */
#define SCREEN_H       480.0f
#define TILES_X        27      /* TSmartLighting.TilesX (0x1b at 0x1403060b0, SetCustomScreen is never called) */
#define PENUMBRA       55.0f   /* SETTING_PENUMBRA (0x425c0000 at 0x140305fab) */
#define LIGHT_MAX_SIZE 512     /* MaxLightSize of Env_Light (FUN_1401d6790 passes 0x200) */
#define MAX_BUFFERS    64

/* ------------------------------------------------------------------------------------------------------------
 * Images (loaded at 0x14022ee87..0x14022f04a from the gfx package; ours sit in gfx/lighting/)
 * ------------------------------------------------------------------------------------------------------------ */
enum {
    IMG_LIGHT, IMG_FLASHLIGHT, IMG_PENUMBRA, IMG_ROOMMAP, IMG_ROOMMAP_INVERT, IMG_STREAM, IMG_BLUR, IMG_SHADOWMAP,
    IMG_OCCLUSION, IMG_COUNT
};
#define IMG_EXT_MAX 64      /* other images the light sprites use (registered by texture) */

typedef struct {
    const char    *name;      /* trace name (tools/agent/emu_light.py IMAGES) */
    const char    *path;
    int            cell_w, cell_h;
    bool           midhandle;
    cs2d_texture_t tex;
    bool           tried;
    const cs2d_texture_t *ext;  /* an image of the renderer (light sprites) instead of tex */
    float          hx, hy;      /* its handle */
} light_image_t;

static light_image_t s_img[IMG_COUNT + IMG_EXT_MAX] = {
    {"light",          "gfx/lighting/gfx_light.png",          0,  0,  false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df98 */
    {"flashlight",     "gfx/lighting/gfx_flashlight.png",     0,  0,  true,  {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df90 */
    {"penumbra",       "gfx/lighting/gfx_penumbra.png",       0,  0,  false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df88 */
    {"roommap",        "gfx/lighting/gfx_roommap.png",        32, 32, false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df80 */
    {"roommap_invert", "gfx/lighting/gfx_roommap_invert.png", 32, 32, false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df78 */
    {"stream",         "gfx/lighting/gfx_stream.png",         64, 64, false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df70 */
    {"blur",           "gfx/lighting/gfx_blur.png",           0,  0,  false, {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df68 */
    {"shadowmap",      "gfx/lighting/gfx_shadowmap.png",      32, 32, true,  {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df60, 6 frames */
    {"occlusion",      "gfx/lighting/gfx_oclussionmap.png",   32, 32, true,  {0}, false, NULL, 0.0f, 0.0f}, /* 0x140a0df58, 11 frames */
};

/* ------------------------------------------------------------------------------------------------------------
 * Engine state
 * ------------------------------------------------------------------------------------------------------------ */
typedef struct {                    /* TLightingRoom (scope 0x140a09520) */
    int   x, y, w, h, mode, r, g, b, height, state, shadow, daycolor;
    float transparency;
    int   offx, offy, layer;
} light_room_t;

typedef struct {                    /* TLightSource (scope 0x140a0a3a0) */
    float r, g, b;
    int   pos_x, pos_y;
    float size;
    int   last_x, last_y, last_size, last_rot, last_r, last_g, last_b, last_astart, last_aend;
    float alpha, rotation;
    int   buffer;                   /* index in s_buf, -1 = none (Buffer) */
    int   astart, aend, in_screen, state, id, type, max_size;
    int   ent;                      /* map entity, -1 for none */
    int   rot_start, rot_end;       /* Te.ints[7] / ints[8]: angles with the rotation speed applied */
} light_source_t;

typedef struct {                    /* TLightingStripe (scope 0x140a09100) */
    float x, y, w, h;
    int   rot;
    float alpha;
    int   r, g, b, day, state, height;
} light_stripe_t;

typedef struct {                    /* LightFBOManager + SmartImageBuffer */
    bool   used;
    bool   free;
    int    size;
    GLuint fbo, tex;
} light_buffer_t;

static struct {
    cs2d_light_config_t cfg;
    int   mode;                     /* SETTING_MODE: light - 1, -1 = off */
    float scale_night;              /* SCALE_NIGHTBUFFER */
    float scale_lights;             /* SCALE_LIGHTS */
    int   lightfixes;               /* SETTING_LIGHTFIXES */
    int   day_brightness;           /* SETTING_DAYSHADOWBRIGHTNESS = 255 - shadows * 2.55 */
    int   global_brightness;        /* SETTING_GLOBALBRIGHTNESS (0 at map load) */
    int   night_r, night_g, night_b;/* SETTING_NIGHTCOLOR (0; night vision sets it) */
    float sun_angle;                /* SETTING_SUNANGLE (0) */
    float shadow_rot, shadow_rot_set;
    float cam_x, cam_y;
    bool  map_loaded;
    int   map_w, map_h;
    int  *tilemap;                  /* TileMap [x * h + y]: tile + 1, 0x100 for an entity wall, -1 void */
    int  *tilemode;                 /* TileMapMode: 2 wall, 1 obstacle, 0 floor, -1 void */
    float tile_height[256];         /* TileSetHeight */
    int   tileset_count;            /* TileSetCount */
    int   wall_size[1024];          /* WallSize */
    uint32_t toning[1024];          /* DayToningColor (gfx_toning.png row 0, ARGB) */
    bool  toning_loaded;
    light_room_t   *rooms;   int room_count;
    light_source_t *lights;  int light_count;
    light_stripe_t *stripes; int stripe_count;
    int   light_counter;            /* TLightSource.Counter */
    uint32_t solid_version;
    int   server_daylight;          /* sv_daylighttime (0x140a14ec8[1]) + 1; 0 = the map's time */
    bool  frame_ready;              /* the light buffer holds this frame */
} L;

static light_buffer_t s_buf[MAX_BUFFERS];
static GLuint s_main_fbo, s_main_tex;

/* ------------------------------------------------------------------------------------------------------------
 * Max2D layer: the original's draw state and calls, printed when traced, drawn with OpenGL otherwise
 * ------------------------------------------------------------------------------------------------------------ */
static void (*s_trace)(const char *line, void *ud);
static void *s_trace_ud;

static struct {
    int   r, g, b;
    float alpha;
    int   blend;
    float rot, sx, sy;
    float ox, oy;
    int   target;                   /* -1 screen, 0 main buffer, 1.. light buffer */
} M = {255, 255, 255, 1.0f, 3, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -1};

static void tracef(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    s_trace(line, s_trace_ud);
}

static void trace_floats(const char *head, const float *v, int n)
{
    char line[1024];
    int len = snprintf(line, sizeof(line), "%s", head);
    for (int i = 0; i < n && len < (int)sizeof(line) - 16; i++) len += snprintf(line + len, sizeof(line) - (size_t)len, " %.3f", v[i]);
    s_trace(line, s_trace_ud);
}

typedef void (APIENTRY *PFN_GenFramebuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_BindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_DeleteFramebuffers)(GLsizei, const GLuint *);
#define LGL_FRAMEBUFFER         0x8D40
#define LGL_COLOR_ATTACHMENT0   0x8CE0
#define LGL_FRAMEBUFFER_COMPLETE 0x8CD5

/*
 * FBO entry points are core in 3.3, but opengl32.dll only exports GL 1.1, so they still come
 * from SDL_GL_GetProcAddress. The drawing itself goes through cs2d_gl (cs2d_gl_ready gates it).
 */
static struct {
    bool tried, ok;
    PFN_GenFramebuffers        GenFramebuffers;
    PFN_BindFramebuffer        BindFramebuffer;
    PFN_FramebufferTexture2D   FramebufferTexture2D;
    PFN_CheckFramebufferStatus CheckFramebufferStatus;
    PFN_DeleteFramebuffers     DeleteFramebuffers;
} G;

static bool gl_ready(void)
{
    if (s_trace) return false;
    if (!cs2d_gl_ready()) return false;
    if (!G.tried) {
        G.tried = true;
        G.GenFramebuffers        = (PFN_GenFramebuffers)SDL_GL_GetProcAddress("glGenFramebuffers");
        G.BindFramebuffer        = (PFN_BindFramebuffer)SDL_GL_GetProcAddress("glBindFramebuffer");
        G.FramebufferTexture2D   = (PFN_FramebufferTexture2D)SDL_GL_GetProcAddress("glFramebufferTexture2D");
        G.CheckFramebufferStatus = (PFN_CheckFramebufferStatus)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
        G.DeleteFramebuffers     = (PFN_DeleteFramebuffers)SDL_GL_GetProcAddress("glDeleteFramebuffers");
        G.ok = G.GenFramebuffers && G.BindFramebuffer && G.FramebufferTexture2D && G.CheckFramebufferStatus;
    }
    return G.ok;
}

static void load_images(void)
{
    for (int i = 0; i < IMG_COUNT; i++) {
        light_image_t *im = &s_img[i];
        if (im->tried) continue;
        im->tried = true;
        cs2d_texture_load_grid(&im->tex, im->path, 0, im->cell_w, im->cell_h);
    }
}

/* SDL renderer blend modes of the TSDLRenderMax2DDriver (SetBlend 0x14031afe0): 1/3 BLEND, 2 NONE, 4 ADD, 5 MOD */
static void gl_apply_blend(void)
{
    switch (M.blend) {
        case 2:  cs2d_gl_set_blend(CS2D_GL_BLEND_NONE);  break;
        case 4:  cs2d_gl_set_blend(CS2D_GL_BLEND_ADD);   break;
        case 5:  cs2d_gl_set_blend(CS2D_GL_BLEND_MULT);  break;
        default: cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA); break;
    }
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* SetColor clamps to 0..255 (TSDLRenderMax2DDriver.SetColor 0x14031b170); the alpha reaches the vertices */
static float col_r(void) { return (float)clamp255(M.r) / 255.0f; }
static float col_g(void) { return (float)clamp255(M.g) / 255.0f; }
static float col_b(void) { return (float)clamp255(M.b) / 255.0f; }
static float col_a(void) { return (float)clamp255((int)(M.alpha * 255.0f)) / 255.0f; }

/* Max2D matrix: ix = cos * sx, iy = -sin * sy, jx = sin * sx, jy = cos * sy */
static void xform(float px, float py, float tx, float ty, float *ox, float *oy)
{
    double rad = (double)M.rot * DEG2RAD;
    float c = (float)cos(rad), s = (float)sin(rad);
    *ox = px * c * M.sx - py * s * M.sy + tx;
    *oy = px * s * M.sx + py * c * M.sy + ty;
}

static void m2d_color(int r, int g, int b)
{
    M.r = r; M.g = g; M.b = b;
    if (s_trace) tracef("color %d %d %d", r, g, b);
}

static void m2d_alpha(float a)
{
    M.alpha = a;
    if (s_trace) tracef("alpha %.3f", a);
}

static void m2d_blend(int b)
{
    M.blend = b;
    if (s_trace) tracef("blend %d", b);
}

static void m2d_transform(float rot, float sx, float sy)
{
    M.rot = rot; M.sx = sx; M.sy = sy;
    if (s_trace) tracef("transform %.3f %.3f %.3f", rot, sx, sy);
}

static void m2d_rotation(float rot)
{
    M.rot = rot;
    if (s_trace) tracef("rotation %.3f", rot);
}

static void m2d_scale(float sx, float sy)
{
    M.sx = sx; M.sy = sy;
    if (s_trace) tracef("scale %.3f %.3f", sx, sy);
}

static void m2d_origin(float x, float y)
{
    M.ox = x; M.oy = y;
    if (s_trace) tracef("origin %.3f %.3f", x, y);
}

static void m2d_rect(float x, float y, float w, float h)
{
    if (s_trace) { tracef("rect %.3f %.3f %.3f %.3f", x, y, w, h); return; }
    if (!gl_ready()) return;
    float px[4] = {0.0f, w, w, 0.0f}, py[4] = {0.0f, 0.0f, h, h};
    gl_apply_blend();
    cs2d_gl_set_texture(0);
    cs2d_gl_vertex_t q[4];
    for (int i = 0; i < 4; i++) {
        float vx, vy;
        xform(px[i], py[i], x + M.ox, y + M.oy, &vx, &vy);
        q[i].x = vx; q[i].y = vy; q[i].u = 0.0f; q[i].v = 0.0f;
        q[i].r = col_r(); q[i].g = col_g(); q[i].b = col_b(); q[i].a = col_a();
    }
    cs2d_gl_vertex_t tri[6];
    tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[2]; tri[3] = q[0]; tri[4] = q[2]; tri[5] = q[3];
    cs2d_gl_push_triangles(tri, 6);
}

static const cs2d_texture_t *img_tex(const light_image_t *im)
{
    return im->ext ? im->ext : &im->tex;
}

/* UV rectangle of frame `frame` (cell atlases built by cs2d_texture_load_grid carry a 4 px border per cell) */
static void image_uv(const light_image_t *im, int frame, float *u0, float *v0, float *u1, float *v1, float *fw, float *fh)
{
    const cs2d_texture_t *t = img_tex(im);
    int cw = im->cell_w ? im->cell_w : t->width, ch = im->cell_h ? im->cell_h : t->height;
    int cols = cw > 0 ? t->width / cw : 1;
    if (cols < 1) cols = 1;
    int col = frame % cols, row = frame / cols;
    float gw = (float)(t->gpu_w ? t->gpu_w : t->width), gh = (float)(t->gpu_h ? t->gpu_h : t->height);
    int pad = t->pad, sx = cw + pad * 2, sy = ch + pad * 2;
    if (t->cell_w <= 0) { sx = cw; sy = ch; pad = 0; }
    *u0 = (float)(col * sx + pad) / gw;
    *v0 = (float)(row * sy + pad) / gh;
    *u1 = (float)(col * sx + pad + cw) / gw;
    *v1 = (float)(row * sy + pad + ch) / gh;
    *fw = (float)cw;
    *fh = (float)ch;
}

static void gl_textured_quad(const light_image_t *im, int frame, float x0, float y0, float x1, float y1, float tx, float ty)
{
    if (!img_tex(im)->id) return;
    float u0, v0, u1, v1, fw, fh;
    image_uv(im, frame, &u0, &v0, &u1, &v1, &fw, &fh);
    gl_apply_blend();
    float px[4] = {x0, x1, x1, x0}, py[4] = {y0, y0, y1, y1};
    float pu[4] = {u0, u1, u1, u0}, pv[4] = {v0, v0, v1, v1};
    cs2d_gl_vertex_t q[4];
    for (int i = 0; i < 4; i++) {
        float vx, vy;
        xform(px[i], py[i], tx, ty, &vx, &vy);
        q[i].x = vx; q[i].y = vy; q[i].u = pu[i]; q[i].v = pv[i];
        q[i].r = col_r(); q[i].g = col_g(); q[i].b = col_b(); q[i].a = col_a();
    }
    cs2d_gl_set_texture(img_tex(im)->id);
    cs2d_gl_vertex_t tri[6];
    tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[2]; tri[3] = q[0]; tri[4] = q[2]; tri[5] = q[3];
    cs2d_gl_push_triangles(tri, 6);
}

static void m2d_image(int img, float x, float y, int frame)
{
    const light_image_t *im = &s_img[img];
    if (s_trace) { tracef("image %s %.3f %.3f %d", im->name, x, y, frame); return; }
    if (!gl_ready()) return;
    float u0, v0, u1, v1, fw, fh;
    image_uv(im, frame, &u0, &v0, &u1, &v1, &fw, &fh);
    float hx = im->midhandle ? fw * 0.5f : im->hx, hy = im->midhandle ? fh * 0.5f : im->hy;
    gl_textured_quad(im, frame, -hx, -hy, fw - hx, fh - hy, x + M.ox, y + M.oy);
}

static void m2d_image_rect(int img, float x, float y, float w, float h, int frame)
{
    const light_image_t *im = &s_img[img];
    if (s_trace) { tracef("imagerect %s %.3f %.3f %.3f %.3f %d", im->name, x, y, w, h, frame); return; }
    if (!gl_ready()) return;
    float u0, v0, u1, v1, fw, fh;
    image_uv(im, frame, &u0, &v0, &u1, &v1, &fw, &fh);
    float hx = im->midhandle ? fw * 0.5f : 0.0f, hy = im->midhandle ? fh * 0.5f : 0.0f;
    gl_textured_quad(im, frame, -hx, -hy, w - hx, h - hy, x + M.ox, y + M.oy);
}

/* Ear clipping, as the triangulator of DrawPoly (FUN_14076b360) for the concave cases */
static int triangulate(const float *xy, int n, int *idx)
{
    int v[32], m = n, out = 0;
    if (n < 3 || n > 32) return 0;
    for (int i = 0; i < n; i++) v[i] = i;
    double area = 0.0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += (double)xy[i * 2] * xy[j * 2 + 1] - (double)xy[j * 2] * xy[i * 2 + 1];
    }
    double sign = area >= 0.0 ? 1.0 : -1.0;
    int guard = 0;
    while (m > 3 && guard++ < 200) {
        bool cut = false;
        for (int i = 0; i < m; i++) {
            int a = v[(i + m - 1) % m], b = v[i], c = v[(i + 1) % m];
            double cr = ((double)xy[b * 2] - xy[a * 2]) * ((double)xy[c * 2 + 1] - xy[a * 2 + 1]) -
                        ((double)xy[b * 2 + 1] - xy[a * 2 + 1]) * ((double)xy[c * 2] - xy[a * 2]);
            if (cr * sign <= 0.0) continue;
            bool inside = false;
            for (int k = 0; k < m && !inside; k++) {
                int p = v[k];
                if (p == a || p == b || p == c) continue;
                double px = xy[p * 2], py = xy[p * 2 + 1];
                double d1 = (xy[b * 2] - xy[a * 2]) * (py - xy[a * 2 + 1]) - (xy[b * 2 + 1] - xy[a * 2 + 1]) * (px - xy[a * 2]);
                double d2 = (xy[c * 2] - xy[b * 2]) * (py - xy[b * 2 + 1]) - (xy[c * 2 + 1] - xy[b * 2 + 1]) * (px - xy[b * 2]);
                double d3 = (xy[a * 2] - xy[c * 2]) * (py - xy[c * 2 + 1]) - (xy[a * 2 + 1] - xy[c * 2 + 1]) * (px - xy[c * 2]);
                inside = d1 * sign > 0.0 && d2 * sign > 0.0 && d3 * sign > 0.0;
            }
            if (inside) continue;
            idx[out++] = a; idx[out++] = b; idx[out++] = c;
            for (int k = i; k < m - 1; k++) v[k] = v[k + 1];
            m--;
            cut = true;
            break;
        }
        if (!cut) break;
    }
    if (m == 3) { idx[out++] = v[0]; idx[out++] = v[1]; idx[out++] = v[2]; }
    return out / 3;
}

/* DrawPoly (FUN_14013b1b0): x, y pairs; transform, handle (0) and origin applied */
static void m2d_poly(const float *xy, int n)
{
    if (s_trace) { trace_floats("poly", xy, n * 2); return; }
    if (!gl_ready() || n < 3) return;
    int idx[96];
    int tris = triangulate(xy, n, idx);
    gl_apply_blend();
    cs2d_gl_set_texture(0);
    cs2d_gl_vertex_t v[96 * 3];
    for (int i = 0; i < tris * 3; i++) {
        float vx, vy;
        xform(xy[idx[i] * 2], xy[idx[i] * 2 + 1], M.ox, M.oy, &vx, &vy);
        v[i].x = vx; v[i].y = vy; v[i].u = 0.0f; v[i].v = 0.0f;
        v[i].r = col_r(); v[i].g = col_g(); v[i].b = col_b(); v[i].a = col_a();
    }
    cs2d_gl_push_triangles(v, tris * 3);
}

/*
 * cs2d_DrawTexturedPoly (0x140131cd0): x, y, u, v per vertex, UV over the whole frame, origin added, no transform.
 * It builds a fan and emits every triangle twice (indices 0, k + 1, k + 2 and 0, k + 2, k + 1), so the frame is
 * applied twice: squared under the shade blend, doubled under the light blend.
 */
static void m2d_textured_poly(int img, const float *xyuv, int n)
{
    const light_image_t *im = &s_img[img];
    if (s_trace) {
        char head[32];
        snprintf(head, sizeof(head), "tpoly %s", im->name);
        trace_floats(head, xyuv, n * 4);
        return;
    }
    if (!gl_ready() || n < 3 || !img_tex(im)->id) return;
    float u0, v0, u1, v1, fw, fh;
    image_uv(im, 0, &u0, &v0, &u1, &v1, &fw, &fh);
    gl_apply_blend();
    cs2d_gl_set_texture(img_tex(im)->id);
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k + 2 < n; k++) {
            int tri[3] = {0, k + 1, k + 2};
            cs2d_gl_vertex_t v[3];
            for (int i = 0; i < 3; i++) {
                const float *p = &xyuv[tri[i] * 4];
                v[i].x = p[0] + M.ox; v[i].y = p[1] + M.oy;
                v[i].u = u0 + (u1 - u0) * p[2]; v[i].v = v0 + (v1 - v0) * p[3];
                v[i].r = col_r(); v[i].g = col_g(); v[i].b = col_b(); v[i].a = col_a();
            }
            cs2d_gl_push_triangles(v, 3);
        }
    }
}

/* ---- buffers (SmartImageBuffer: BindBuffer 0x1400a4940, Clear 0x140055430, UnBindBuffer 0x140089100) ---- */

static bool gl_make_target(GLuint *fbo, GLuint *tex, int w, int h)
{
    if (*fbo) return true;
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    G.GenFramebuffers(1, fbo);
    G.BindFramebuffer(LGL_FRAMEBUFFER, *fbo);
    G.FramebufferTexture2D(LGL_FRAMEBUFFER, LGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    bool ok = G.CheckFramebufferStatus(LGL_FRAMEBUFFER) == LGL_FRAMEBUFFER_COMPLETE;
    G.BindFramebuffer(LGL_FRAMEBUFFER, 0);
    return ok;
}

static void buffer_name(int target, char *out, size_t n)
{
    if (target == 0) snprintf(out, n, "main");
    else snprintf(out, n, "buf%d", target);
}

static void m2d_bind(int target, int size_w, int size_h)
{
    M.target = target;
    if (s_trace) { char nm[16]; buffer_name(target, nm, sizeof(nm)); tracef("bind %s", nm); return; }
    if (!gl_ready()) return;
    GLuint fbo = target == 0 ? s_main_fbo : s_buf[target - 1].fbo;
    cs2d_gl_bind_target(fbo, size_w, size_h);
}

static void m2d_clear(int r, int g, int b, float a, bool traced_args)
{
    if (s_trace) {
        char nm[16];
        buffer_name(M.target, nm, sizeof(nm));
        if (traced_args) tracef("clear %s %.3f %.3f %.3f %.3f", nm, (float)r, (float)g, (float)b, a);
        else tracef("clear %s", nm);
        return;
    }
    if (!gl_ready()) return;
    cs2d_gl_flush();
    /* SetClsColor stores the alpha as (byte)(int)a (TSDLRenderMax2DDriver.SetClsColor 0x14031b220) */
    glClearColor((float)clamp255(r) / 255.0f, (float)clamp255(g) / 255.0f, (float)clamp255(b) / 255.0f,
                 (float)((int)a & 0xff) / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

static void m2d_unbind(void)
{
    if (s_trace) { char nm[16]; buffer_name(M.target, nm, sizeof(nm)); tracef("unbind %s", nm); M.target = -1; return; }
    M.target = -1;
    if (!gl_ready()) return;
    cs2d_gl_unbind_target();
}

/* DrawBuffer / DrawBufferRect: the render target stretched to (w, h) at (x, y) */
static void m2d_draw_buffer(int target, float x, float y, float w, float h, bool rect_op)
{
    if (s_trace) {
        char nm[16];
        buffer_name(target, nm, sizeof(nm));
        if (rect_op) tracef("drawbufferrect %s", nm);
        else tracef("drawbuffer %s %.3f %.3f %.3f %.3f", nm, x, y, w, h);
        return;
    }
    if (!gl_ready()) return;
    GLuint tex = target == 0 ? s_main_tex : s_buf[target - 1].tex;
    if (!tex) return;
    gl_apply_blend();
    /* GL render targets are bottom-up: v = 1 is the top row */
    cs2d_gl_quad(tex, x + M.ox, y + M.oy, w, h, 0.0f, 1.0f, 1.0f, 0.0f,
                 col_r(), col_g(), col_b(), col_a());
}

static void m2d_marker(const char *what)
{
    if (s_trace) tracef("call %s", what);
}

/* Back to the state the rest of our renderer expects */
static void gl_restore(void)
{
    if (s_trace || !cs2d_gl_ready()) return;
    cs2d_gl_flush();
    cs2d_gl_set_blend(CS2D_GL_BLEND_ALPHA);
    cs2d_gl_set_texture(0);
}

/* ------------------------------------------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------------------------------------------ */
/* BlitzMax String.ToInt / ToFloat: leading number, "" = 0 */
static int str_int(const char *s) { return s && s[0] ? atoi(s) : 0; }
static float str_float(const char *s) { return s && s[0] ? (float)atof(s) : 0.0f; }

/* The x87 fprem loops of the original: remainder with the sign of the dividend */
static double fmod360(double v) { return fmod(v, 360.0); }

static int mode_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= L.map_w || y >= L.map_h) return 0;
    return L.tilemode[x * L.map_h + y];
}

static int tilemap_at(int x, int y)
{
    return L.tilemap[x * L.map_h + y];
}

/* (int)TileSetHeight[TileMap - 1] with the bounds checks of cs2d_draw_texturedpoly_3; 0 outside */
static int height_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= L.map_w || y >= L.map_h) return 0;
    unsigned t = (unsigned)(tilemap_at(x, y) - 1);
    return t < 0x100 ? (int)L.tile_height[t] : 0;
}

/* Toning index of ShadowRotation + add (GetNightColor 0x1400717a0 adds 180, DrawToning 0x140120eb0 adds 200) */
static bool toning_rgb(float add, int *r, int *g, int *b)
{
    double f = fmod360((double)(L.shadow_rot + 360.0f));
    f = fmod360((double)((float)f + add));
    unsigned idx = (unsigned)(fabsf((float)(f * 0.002777777777777778)) * 1024.0f);
    if (idx >= 0x400) return false;
    uint32_t c = L.toning[idx];
    *r = (int)((c >> 16) & 0xff) + L.night_r;
    *g = (int)((c >> 8) & 0xff) + L.night_g;
    *b = (int)(c & 0xff) + L.night_b;
    return true;
}

static int night_color(void)
{
    int r, g, b;
    if (!toning_rgb(180.0f, &r, &g, &b)) return 0;
    return (r + g + b) / 3;
}

/* ------------------------------------------------------------------------------------------------------------
 * Light buffers (LightFBOManager.PullImageFrameBuffer 0x1400814b0)
 * ------------------------------------------------------------------------------------------------------------ */
static int buffer_pull(int size)
{
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (s_buf[i].used && s_buf[i].free && s_buf[i].size == size) {
            s_buf[i].free = false;
            return i + 1;
        }
    }
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (!s_buf[i].used) {
            s_buf[i].used = true;
            s_buf[i].free = false;
            s_buf[i].size = size;
            if (gl_ready() && !gl_make_target(&s_buf[i].fbo, &s_buf[i].tex, size, size)) {
                /* No render target: the light is skipped (the original turns the engine off) */
            }
            return i + 1;
        }
    }
    return -1;
}

static void buffer_release(int b)
{
    if (b > 0 && b <= MAX_BUFFERS) s_buf[b - 1].free = true;
}

/* ------------------------------------------------------------------------------------------------------------
 * TLightSource
 * ------------------------------------------------------------------------------------------------------------ */
/* TLightSource.ChangeState (0x140004d10) */
static void light_change_state(light_source_t *ls, int state)
{
    ls->state = state;
    if (state == 0) {
        if (ls->in_screen == 1) {
            buffer_release(ls->buffer);
            ls->buffer = -1;
        }
        ls->in_screen = 0;
    }
}

/*
 * TLightSource.DrawLineLight (0x14013e7d0): the shadow of edge A-B away from the light, 2 * len long. The far
 * points are joined through C, 6 * len beyond their middle. hard_a / hard_b: the wall goes on past that end, so
 * the edge needs no penumbra; otherwise a textured triangle (gfx_penumbra) fades the shadow in over PENUMBRA px.
 */
static void draw_line_light(float ax, float ay, float lx, float ly, float bx, float by, float len, int hard_a, int hard_b)
{
    double ang_a = atan2((double)(ay - ly), (double)(ax - lx));
    double dlen = (double)(len + len);
    double ang_b = atan2((double)(by - ly), (double)(bx - lx));
    float deg_a = (float)(ang_a * RAD2DEG), deg_b = (float)(ang_b * RAD2DEG);
    double pen = (double)PENUMBRA;
    double pa_x = cos((double)(deg_a + 90.0f) * DEG2RAD) * pen;
    double pa_y = sin((double)(deg_a + 90.0f) * DEG2RAD) * pen;
    float afx = (float)(cos((double)deg_a * DEG2RAD) * dlen + (double)ax);
    float afy = (float)(sin((double)deg_a * DEG2RAD) * dlen + (double)ay);
    float bfx = (float)(cos((double)deg_b * DEG2RAD) * dlen + (double)bx);
    float bfy = (float)(sin((double)deg_b * DEG2RAD) * dlen + (double)by);
    float mx = (afx + bfx) * 0.5f, my = (afy + bfy) * 0.5f;
    float deg_m = (float)(atan2((double)(ay - my), (double)(ax - mx)) * RAD2DEG);
    float cx = (float)((double)mx - cos((double)deg_m * DEG2RAD) * dlen * 3.0);
    float cy = (float)((double)my - dlen * 3.0 * sin((double)deg_m * DEG2RAD));
    m2d_color(255, 255, 255);
    if (hard_a || !hard_b) {
        double pb_x = cos((double)(deg_b - 90.0f) * DEG2RAD) * pen;
        double pb_y = sin((double)(deg_b - 90.0f) * DEG2RAD) * pen;
        float bix = (float)pb_x + bfx, biy = bfy + (float)pb_y;
        if (hard_b || !hard_a) {
            if (hard_a || hard_b) {
                float poly[10] = {ax, ay, afx, afy, cx, cy, bfx, bfy, bx, by};
                m2d_color(0, 0, 0);
                m2d_poly(poly, 5);
                return;
            }
            float aix = afx + (float)pa_x, aiy = afy + (float)pa_y;
            float ta[12] = {ax, ay, 0.0f, 1.0f, afx, afy, 0.0f, 0.0f, aix, aiy, 1.0f, 0.0f};
            m2d_textured_poly(IMG_PENUMBRA, ta, 3);
            float tb[12] = {bx, by, 0.0f, 1.0f, bfx, bfy, 0.0f, 0.0f, bix, biy, 1.0f, 0.0f};
            m2d_textured_poly(IMG_PENUMBRA, tb, 3);
            float poly[10] = {ax, ay, aix, aiy, cx, cy, bix, biy, bx, by};
            m2d_color(0, 0, 0);
            m2d_poly(poly, 5);
        } else {
            float tb[12] = {bx, by, 0.0f, 1.0f, bfx, bfy, 0.0f, 0.0f, bix, biy, 1.0f, 0.0f};
            m2d_textured_poly(IMG_PENUMBRA, tb, 3);
            float poly[10] = {ax, ay, afx, afy, cx, cy, bix, biy, bx, by};
            m2d_color(0, 0, 0);
            m2d_poly(poly, 5);
        }
    } else {
        float aix = afx + (float)pa_x, aiy = afy + (float)pa_y;
        float ta[12] = {ax, ay, 0.0f, 1.0f, afx, afy, 0.0f, 0.0f, aix, aiy, 1.0f, 0.0f};
        m2d_textured_poly(IMG_PENUMBRA, ta, 3);
        float poly[10] = {ax, ay, aix, aiy, cx, cy, bfx, bfy, bx, by};
        m2d_color(0, 0, 0);
        m2d_poly(poly, 5);
    }
}

/*
 * TLightSource.CalculateShadow (0x1400e60e0) for wall tile (tx, ty) and the light in tile (lx, ly). The silhouette
 * edge is a fixed pair of tile corners per direction of the light; an end is hard (no penumbra) when one of the
 * listed neighbours is a wall. Recovered by running the original on every neighbour pattern
 * (tools/agent/emu_lightshadow.py); test_light_shadow_cases holds a sample of its output.
 */
enum { NB_L, NB_R, NB_U, NB_D, NB_UL, NB_UR, NB_DL, NB_DR };
enum { CR_TL, CR_TR, CR_BL, CR_BR };
static const struct { int a, b; uint8_t hard_a, hard_b; } s_shadow_case[3][3] = {
    /* [dy + 1][dx + 1], dx / dy = sign of light - tile */
    {
        {CR_TR, CR_BL, 1 << NB_U | 1 << NB_UR, 1 << NB_L | 1 << NB_DL},              /* up-left */
        {CR_TR, CR_TL, 1 << NB_R | 1 << NB_UR, 1 << NB_L | 1 << NB_UL},              /* up */
        {CR_BR, CR_TL, 1 << NB_R | 1 << NB_DR, 1 << NB_L | 1 << NB_U | 1 << NB_UL},  /* up-right */
    },
    {
        {CR_TL, CR_BL, 1 << NB_U | 1 << NB_UL, 1 << NB_D | 1 << NB_DL},              /* left */
        {0, 0, 0, 0},
        {CR_BR, CR_TR, 1 << NB_D | 1 << NB_DR, 1 << NB_U | 1 << NB_UR},              /* right */
    },
    {
        {CR_TL, CR_BR, 1 << NB_L | 1 << NB_UL, 1 << NB_D | 1 << NB_DR},              /* down-left */
        {CR_BL, CR_BR, 1 << NB_L | 1 << NB_DL, 1 << NB_R | 1 << NB_DR},              /* down */
        {CR_BL, CR_TR, 1 << NB_D | 1 << NB_DL, 1 << NB_R | 1 << NB_UR},              /* down-right */
    },
};

static void calculate_shadow(int tx, int ty, float lcx, float lcy, int ltx, int lty, float px, float py, int range, float size)
{
    int dx = ltx - tx, dy = lty - ty;
    if ((dx == 0 && dy == 0) || abs(dx) > range || abs(dy) > range) return;
    int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0), sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    static const int nb[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    int walls = 0;
    for (int i = 0; i < 8; i++) {
        if (mode_at(tx + nb[i][0], ty + nb[i][1]) > 1) walls |= 1 << i;
    }
    const float t = 32.0f / L.scale_lights;
    const float cx[4] = {px, px + t, px, px + t}, cy[4] = {py, py, py + t, py + t};
    int c = s_shadow_case[sy + 1][sx + 1].a, d = s_shadow_case[sy + 1][sx + 1].b;
    draw_line_light(cx[c], cy[c], lcx, lcy, cx[d], cy[d], size,
                    (walls & s_shadow_case[sy + 1][sx + 1].hard_a) != 0, (walls & s_shadow_case[sy + 1][sx + 1].hard_b) != 0);
}

/* TLightSource.UpdateLightBuffer (0x14013f070): renders the light into its own buffer */
static void light_update_buffer(light_source_t *ls, int force)
{
    float sl = L.scale_lights;
    if (ls->state == 0 || ls->buffer < 0) return;
    if (!force && ls->last_x == ls->pos_x && ls->last_y == ls->pos_y && ls->last_size == (int)ls->size &&
        (int)ls->rotation == ls->last_rot && ls->last_r == (int)ls->r && ls->last_g == (int)ls->g &&
        ls->last_b == (int)ls->b && ls->last_astart == ls->astart && ls->last_aend == ls->aend) {
        return;
    }
    ls->last_x = ls->pos_x;
    ls->last_y = ls->pos_y;
    ls->last_rot = (int)ls->rotation;
    ls->last_size = (int)ls->size;
    ls->last_r = (int)ls->r;
    ls->last_g = (int)ls->g;
    ls->last_b = (int)ls->b;
    ls->last_astart = ls->astart;
    ls->last_aend = ls->aend;
    int bsize = s_buf[ls->buffer - 1].size;
    m2d_bind(ls->buffer, bsize, bsize);
    float half = (float)(ls->max_size / 2), size = ls->size;
    float f21 = half / sl, f15 = size / sl;
    m2d_transform(0.0f, 1.0f, 1.0f);
    if (ls->pos_x >= 0 && ls->pos_y >= 0 && (ls->pos_x >> 5) < L.map_w && (ls->pos_y >> 5) < L.map_h) {
        m2d_clear(0, 0, 0, 0.0f, false);
        m2d_alpha(1.0f);
        m2d_blend(3);
        m2d_color(255, 255, 255);
        m2d_rect(0.0f, 0.0f, (float)(ls->max_size * 2), (float)(ls->max_size * 2));
        double d26 = sl != 2.0f ? 160.0 : 80.0;
        if (ls->type != 0) {
            double s = sin((double)(ls->rotation - 90.0f) * DEG2RAD), c = cos((double)(ls->rotation - 90.0f) * DEG2RAD);
            m2d_origin((float)-(c * d26), (float)-(s * d26));
        }
        float f23 = 32.0f / sl;
        float f16 = half - (float)ls->pos_x, f13 = half - (float)ls->pos_y;
        float range = (float)((int)(size * 0.03125f) + 4);
        float ltx = (float)(int)((float)ls->pos_x * 0.03125f), lty = (float)(int)((float)ls->pos_y * 0.03125f);
        float f14 = ltx - range, f24 = lty - range;
        int x1 = (int)(f14 + range + range), y1 = (int)(range + range + f24);
        m2d_color(0, 0, 0);
        m2d_blend(5);
        for (int x = (int)f14; x <= x1; x++) {
            for (int y = (int)f24; y <= y1; y++) {
                if (x >= 0 && y >= 0 && x < L.map_w && y < L.map_h && L.tilemode[x * L.map_h + y] > 1) {
                    calculate_shadow(x, y, f21, half / sl, (int)ltx, (int)lty, (float)x * f23 + f16 / sl,
                                     (float)y * f23 + f13 / sl, (int)range, size);
                }
            }
        }
        m2d_color(255, 255, 255);
        m2d_blend(3);
        for (int x = (int)(f14 - 1.0f); x <= x1 + 2; x++) {
            for (int y = (int)(f24 - 1.0f); y <= y1 + 2; y++) {
                if (x >= 0 && y >= 0 && x < L.map_w && y < L.map_h && L.tilemode[x * L.map_h + y] > 1) {
                    m2d_rect((float)x * f23 + f16 / sl, (float)y * f23 + f13 / sl, f23, f23);
                }
            }
        }
        m2d_blend(5);
        m2d_color(255, 255, 255);
        if ((ls->astart != 0 || ls->aend != 359) && ls->type == 0) {
            m2d_color(0, 0, 0);
            double r = (double)f21;
            float end_a = ls->rotation + (float)(ls->aend - 354);
            float start_a = ls->rotation + (float)(ls->astart - 5);
            int a = (int)end_a;
            if (a <= (int)start_a) {
                double c = cos((double)a * DEG2RAD), s = sin((double)a * DEG2RAD);
                do {
                    a += 5;
                    float p0x = (float)(c * r) + f21, p0y = (float)(s * r) + f21;
                    c = cos((double)a * DEG2RAD);
                    s = sin((double)a * DEG2RAD);
                    float tri[6] = {f21, f21, p0x, p0y, (float)(c * r) + f21, (float)(s * r) + f21};
                    m2d_poly(tri, 3);
                } while (a <= (int)start_a);
            }
            m2d_color(255, 255, 255);
            double c1 = cos((double)(start_a + 10.0f) * DEG2RAD), s1 = sin((double)(start_a + 10.0f) * DEG2RAD);
            double c2 = cos((double)start_a * DEG2RAD), s2 = sin((double)start_a * DEG2RAD);
            float p1[12] = {(float)(c1 * r) + f21, (float)(s1 * r) + f21, 0.0f, 0.0f, f21, f21, 0.0f, 1.0f,
                            (float)(c2 * r) + f21, (float)(s2 * r) + f21, 1.0f, 0.0f};
            m2d_textured_poly(IMG_PENUMBRA, p1, 3);
            c1 = cos((double)(end_a - 10.0f) * DEG2RAD); s1 = sin((double)(end_a - 10.0f) * DEG2RAD);
            c2 = cos((double)end_a * DEG2RAD); s2 = sin((double)end_a * DEG2RAD);
            float p2[12] = {(float)(c1 * r) + f21, (float)(s1 * r) + f21, 0.0f, 0.0f, f21, f21, 0.0f, 1.0f,
                            (float)(c2 * r) + f21, (float)(s2 * r) + f21, 1.0f, 0.0f};
            m2d_textured_poly(IMG_PENUMBRA, p2, 3);
        }
        if (ls->type != 0) m2d_origin(0.0f, 0.0f);
        m2d_color(255, 255, 255);
        if (ls->type == 0) {
            float o = (half - size) / sl;
            m2d_image_rect(IMG_LIGHT, o, o, f15 + f15, f15 + f15, 0);
        } else {
            m2d_rotation(ls->rotation);
            m2d_scale(f15 * 0.0078125f, f15 * 0.0078125f);
            m2d_image(IMG_FLASHLIGHT, f21, f21, 0);
            m2d_scale(1.0f, 1.0f);
            m2d_rotation(0.0f);
        }
    } else {
        m2d_clear(0, 0, 0, 0.0f, false);
    }
    m2d_color(0, 0, 0);
    if (ls->type == 0) {
        float hi = (half + size) / sl, lo = (half - size) / sl;
        m2d_rect(0.0f, 0.0f, 1000.0f, lo);
        m2d_rect(hi, lo, 500.0f, f15 + f15);
        m2d_rect(0.0f, lo, lo, f15 + f15);
        m2d_rect(0.0f, hi, 1000.0f, 500.0f);
    } else {
        /* Flashlight: the four quads from the corners of the rotated square outwards are cleared (rot + 45 + 90 k) */
        double r = (double)f21, rr = (double)sqrtf(f15 * f15 + f15 * f15), reach = (double)ls->max_size;
        float rot = ls->rotation;
        float qx[4], qy[4], fx[5], fy[5];
        for (int k = 0; k < 4; k++) {
            double a = (double)(rot + 45.0f + 90.0f * (float)k) * DEG2RAD;
            qx[k] = (float)(cos(a) * rr + r);
            qy[k] = (float)(sin(a) * rr + r);
        }
        for (int k = 0; k < 5; k++) {
            double a = (double)(rot + 45.0f + 90.0f * (float)k) * DEG2RAD;
            fx[k] = (float)(cos(a) * reach + (double)qx[k & 3]);
            fy[k] = (float)(sin(a) * reach + (double)qy[k & 3]);
        }
        m2d_blend(5);
        for (int k = 0; k < 4; k++) {
            int n = (k + 1) & 3;
            float q[8] = {qx[k], qy[k], fx[k], fy[k], fx[k + 1], fy[k + 1], qx[n], qy[n]};
            m2d_poly(q, 4);
        }
    }
    m2d_color(255, 255, 255);
    m2d_unbind();
}

/* TLightSource.Update (0x140081610) */
static void light_update(light_source_t *ls, int scale)
{
    if (ls->state == 0) return;
    float f3 = L.cam_y - ls->size, f2 = L.cam_x - ls->size;
    if ((float)ls->pos_x < f2 || (float)ls->pos_y < f3 || ls->size + ls->size + f2 + 850.0f < (float)ls->pos_x ||
        ls->size + ls->size + f3 + 480.0f < (float)ls->pos_y) {
        if (ls->in_screen == 1) {
            buffer_release(ls->buffer);
            ls->buffer = -1;
        }
        ls->in_screen = 0;
        return;
    }
    if (ls->in_screen != 0) {
        light_update_buffer(ls, 0);   /* (render3d && LIGHTFIXES) is 0: we have no 3D mode */
        return;
    }
    ls->buffer = buffer_pull(ls->max_size / scale);
    light_update_buffer(ls, 1);
    ls->in_screen = 1;
}

static bool light_on_screen(const light_source_t *ls, float px, float py)
{
    float f9 = L.cam_y - ls->size, f12 = L.cam_x - ls->size;
    return f9 <= py && f12 <= px && px <= ls->size + ls->size + f12 + 850.0f && py <= ls->size + ls->size + f9 + 480.0f;
}

/* TLightSource.DrawLightsList (0x140047f50): each light buffer added in the light color */
static void draw_lights_list(float p1)
{
    float sl = L.scale_lights;
    m2d_alpha(1.0f);
    for (int i = 0; i < L.light_count; i++) {
        light_source_t *ls = &L.lights[i];
        if (ls->state == 0 || !light_on_screen(ls, (float)ls->pos_x, (float)ls->pos_y) || ls->buffer < 0) continue;
        float inv = 1.0f / p1;
        int bsize = s_buf[ls->buffer - 1].size;
        float f9 = (float)ls->pos_x - L.cam_x, f12 = (float)ls->pos_y - L.cam_y;
        m2d_color((int)ls->r, (int)ls->g, (int)ls->b);
        if (ls->type == 0) {
            m2d_draw_buffer(ls->buffer, (f9 - (float)ls->max_size * 0.5f) * inv, (f12 - (float)ls->max_size * 0.5f) * inv,
                            (float)bsize * sl * inv, (float)bsize * sl * inv, false);
        } else {
            double d = sl != 2.0f ? 160.0 : 80.0;
            double s = sin((double)(ls->rotation - 90.0f) * DEG2RAD), c = cos((double)(ls->rotation - 90.0f) * DEG2RAD);
            m2d_draw_buffer(ls->buffer, (float)(c * d + (double)((f9 - (float)ls->max_size * 0.5f) * inv)),
                            (float)((double)((f12 - (float)ls->max_size * 0.5f) * inv) + s * d),
                            (float)bsize * sl * inv, (float)bsize * sl * inv, false);
        }
    }
}

/* TLightSource.DrawLightsColorList (0x14004f670): the additive tint of strs[0], after the players */
static void draw_lights_color_list(void)
{
    float sl = L.scale_lights;
    m2d_blend(4);
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_alpha(1.0f);
    for (int i = 0; i < L.light_count; i++) {
        light_source_t *ls = &L.lights[i];
        if (ls->state == 0 || ls->buffer < 0) continue;
        if (!light_on_screen(ls, (float)ls->pos_x, (float)ls->pos_y) || !(0.01f < ls->alpha)) continue;
        int bsize = s_buf[ls->buffer - 1].size;
        float f10 = (float)ls->last_x - L.cam_x, f14 = (float)ls->last_y - L.cam_y;
        m2d_color((int)(ls->r * ls->alpha * 0.65f), (int)(ls->g * ls->alpha * 0.65f), (int)(ls->b * ls->alpha * 0.65f));
        if (ls->type == 0) {
            m2d_draw_buffer(ls->buffer, f10 - (float)ls->max_size * 0.5f, f14 - (float)ls->max_size * 0.5f,
                            (float)bsize * sl, (float)bsize * sl, false);
        } else {
            double s = sin((double)(ls->last_rot - 90) * DEG2RAD), c = cos((double)(ls->last_rot - 90) * DEG2RAD);
            m2d_draw_buffer(ls->buffer, (float)(c * 160.0 + (double)(f10 - (float)ls->max_size * 0.5f)),
                            (float)(s * 160.0 + (double)(f14 - (float)ls->max_size * 0.5f)),
                            (float)bsize * sl, (float)bsize * sl, false);
        }
    }
    m2d_alpha(1.0f);
}

/* ------------------------------------------------------------------------------------------------------------
 * TLightingRoom.DrawRoomsLight (0x140122be0), DrawToning (0x140120eb0), TLightingStripe.DrawStripesLight
 * (0x140114040), TLightingSprite(Floor).DrawSprites (0x140111ec0 / 0x140112110: no sprite lights yet)
 * ------------------------------------------------------------------------------------------------------------ */
static void draw_rooms_light(float p1)
{
    float camx = L.cam_x, camy = L.cam_y, w = 32.0f / p1;
    int nc = night_color();
    nc = (int)((float)(L.night_g + L.night_r + L.night_b) * 0.33333334f) + nc;
    if (nc > 255) nc = 255;
    float f16 = (float)nc * 0.003921569f;
    for (int i = 0; i < L.room_count; i++) {
        const light_room_t *rm = &L.rooms[i];
        if (rm->state == 0 || rm->layer != 0 || !(rm->transparency <= 0.95f)) continue;
        float f15 = (float)(rm->x << 5), f12 = (float)(rm->y << 5);
        float f13 = ((float)rm->offy - 240.0f) + f12;
        float f11 = SCREEN_W * 0.5f + L.cam_x;
        float f14 = ((float)rm->offx - SCREEN_W * 0.5f) + f15;
        if (!(f13 <= L.cam_y + 240.0f && f14 <= f11)) continue;
        if (!(f11 <= SCREEN_W + (float)(rm->w << 5) + f14 && L.cam_y + 240.0f <= (float)((rm->h + 15) * 32) + f13)) continue;
        float inv = 1.0f / p1;
        m2d_alpha(1.0f - rm->transparency);
        if (rm->daycolor == 0) m2d_color(rm->r, rm->g, rm->b);
        else m2d_color((int)((float)rm->r * f16), (int)((float)rm->g * f16), (int)((float)rm->b * f16));
        if (rm->w < 2 || rm->h < 2) {
            float y = (f12 + ((float)rm->offy - camy)) * inv;
            float x = inv * (((float)rm->offx - camx) + f15);
            m2d_rect(x, y, (float)rm->w * w, (float)rm->h * w);
        } else {
            float ox = (camx - (float)rm->offx) * inv, oy = inv * (camy - (float)rm->offy);
            int fb = rm->mode * 8;
            int x = rm->x, y = rm->y, rw = rm->w, rh = rm->h;
            m2d_rect((float)(x + 1) * w - ox, (float)(y + 1) * w - oy, (float)(rw - 2) * w, (float)(rh - 2) * w);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)x * w - ox, (float)y * w - oy, w, w, fb + 7);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)(x + 1) * w - ox, (float)y * w - oy, (float)(rw - 2) * w, w, fb);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)(rw + x - 1) * w - ox, (float)y * w - oy, w, w, fb + 1);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)(rw + x - 1) * w - ox, (float)(y + 1) * w - oy, w, (float)(rh - 2) * w, fb + 2);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)(rw + x - 1) * w - ox, (float)(rh + y - 1) * w - oy, w, w, fb + 3);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)(x + 1) * w - ox, (float)(rh + y - 1) * w - oy, (float)(rw - 2) * w, w, fb + 4);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)x * w - ox, (float)(rh + y - 1) * w - oy, w, w, fb + 5);
            m2d_image_rect(IMG_ROOMMAP_INVERT, (float)x * w - ox, (float)(y + 1) * w - oy, w, (float)(rh - 2) * w, fb + 6);
        }
    }
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
}

static void draw_toning(void)
{
    int b = L.global_brightness, tr = 0, tg = 0, tb = 0;
    if (!toning_rgb(200.0f, &tr, &tg, &tb)) tr = tg = tb = 0;
    m2d_blend(5);
    m2d_color(b + tr, b + tg, b + tb);
    m2d_rect(0.0f, 0.0f, 854.0f, 480.0f);
    m2d_blend(4);
    m2d_color(L.night_r + b, L.night_g + b, b + L.night_b);
    m2d_rect(0.0f, 0.0f, 854.0f, 480.0f);
}

static void draw_stripes_light(float p1)
{
    int nc = night_color();
    if (nc > 255) nc = 255;
    float inv = 1.0f / p1, f10 = (float)nc * 0.003921569f, t = inv * 32.0f;
    for (int i = 0; i < L.stripe_count; i++) {
        const light_stripe_t *st = &L.stripes[i];
        if (st->state == 0) continue;
        float f5 = L.cam_x + SCREEN_W * 0.5f, f8 = st->x * 32.0f - SCREEN_W * 0.5f;
        float f7 = st->y * 32.0f - SCREEN_H * 0.5f, f6 = SCREEN_H * 0.5f + L.cam_y;
        if (f5 < f8 || f6 < f7 || st->w * 32.0f + SCREEN_W + f8 < f5 || st->h * 32.0f + SCREEN_H + f7 < f6) continue;
        if (st->day != 0) m2d_color((int)((float)st->r * f10), (int)((float)st->g * f10), (int)((float)st->b * f10));
        else m2d_color(st->r, st->g, st->b);
        m2d_image_rect(IMG_STREAM, st->x * t - L.cam_x * inv, st->y * t - L.cam_y * inv, st->w * t, st->h * t, st->rot);
    }
    m2d_color(255, 255, 255);
}

/* An image of the renderer for the light sprites, by texture and handle */
static int ext_image_cells(const cs2d_texture_t *tex, float hx, float hy, int cell_w, int cell_h)
{
    for (int i = IMG_COUNT; i < IMG_COUNT + IMG_EXT_MAX; i++) {
        light_image_t *im = &s_img[i];
        if (im->ext == tex && im->hx == hx && im->hy == hy && im->cell_w == cell_w && im->cell_h == cell_h) return i;
        if (!im->ext) {
            im->name = "ext";
            im->ext = tex;
            im->hx = hx;
            im->hy = hy;
            im->cell_w = cell_w;
            im->cell_h = cell_h;
            return i;
        }
    }
    return -1;
}

static int ext_image(const cs2d_texture_t *tex, float hx, float hy)
{
    return ext_image_cells(tex, hx, hy, 0, 0);
}

/* Player silhouettes of FUN_140112340 (screen positions at the frame's camera) */
#define PLAYER_PART_MAX 256
typedef struct {
    int   img, frame;
    float x, y, rot, sx, sy;
} light_part_t;

static light_part_t s_parts[PLAYER_PART_MAX];
static int s_part_count;

/* FUN_140112340: black, alpha blend, 4 px along ShadowRotation - 180, at the night buffer scale */
static void draw_player_shadows(void)
{
    float p1 = 1.0f / L.scale_night;
    double rad = (double)(L.shadow_rot - 180.0f) * DEG2RAD;
    float ox = (float)(cos(rad) * (double)p1 * 4.0), oy = (float)((double)p1 * 4.0 * sin(rad));
    m2d_blend(3);
    m2d_color(0, 0, 0);
    m2d_alpha(1.0f);
    for (int i = 0; i < s_part_count; i++) {
        const light_part_t *pt = &s_parts[i];
        m2d_transform(pt->rot, pt->sx * p1, pt->sy * p1);
        m2d_image(pt->img, (float)(int)(pt->x * p1) + ox, (float)(int)(pt->y * p1) + oy, pt->frame);
    }
    m2d_transform(0.0f, 1.0f, 1.0f);
}

/* TLightingSprite / TLightingSpriteFloor queues (499 sprites, DrawCount 0x140dde1c8 / 0x140dde168) */
#define SPRITE_MAX 499
typedef struct {
    int   img, frame;
    float x, y, sx, sy, rot, alpha;
    int   r, g, b;
} light_sprite_t;

static light_sprite_t s_sprites[2][SPRITE_MAX];
static int s_sprite_count[2];

/* TLightingSprite.DrawSprite (0x140013ac0) / TLightingSpriteFloor.DrawSprite (0x140013c70): world position */
static void queue_sprite(bool floor, int img, float x, float y, float sx, float sy, int r, int g, int b, float rot,
                         float alpha, int frame)
{
    int *n = &s_sprite_count[floor ? 1 : 0];
    if (img < 0 || !(L.cam_y - 64.0f <= y && L.cam_x - 64.0f <= x && x <= L.cam_x + 914.0f && y <= L.cam_y + 544.0f) ||
        *n >= SPRITE_MAX) {
        return;
    }
    light_sprite_t *sp = &s_sprites[floor ? 1 : 0][(*n)++];
    sp->img = img; sp->frame = frame;
    sp->x = x; sp->y = y; sp->sx = sx; sp->sy = sy; sp->rot = rot; sp->alpha = alpha;
    sp->r = r; sp->g = g; sp->b = b;
}

/* TLightingSprite.DrawSprites (0x140111ec0) / TLightingSpriteFloor.DrawSprites (0x140112110): added to the light
 * buffer in their color x alpha, then the queue is emptied */
static void draw_sprites(bool floor)
{
    int k = floor ? 1 : 0;
    float inv = 1.0f / L.scale_night;
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    if (floor) { m2d_alpha(1.0f); m2d_blend(4); }
    else { m2d_blend(4); m2d_alpha(1.0f); }
    for (int i = 0; i < s_sprite_count[k]; i++) {
        const light_sprite_t *sp = &s_sprites[k][i];
        m2d_color((int)((float)sp->r * sp->alpha), (int)((float)sp->g * sp->alpha), (int)((float)sp->b * sp->alpha));
        m2d_transform(sp->rot, sp->sx * inv, sp->sy * inv);
        m2d_image(sp->img, (sp->x - L.cam_x) * inv, (sp->y - L.cam_y) * inv, sp->frame);
    }
    m2d_color(255, 255, 255);
    m2d_transform(0.0f, 1.0f, 1.0f);
    s_sprite_count[k] = 0;
}

/* ObstacleShadow2 list (0x140dde178): images added white into the day buffer, so what they cover is not shaded */
#define OBSTACLE_MAX 1024
typedef struct {
    int   img, frame;
    float x, y, sx, sy, rot;
} light_obstacle_t;

static light_obstacle_t s_obstacles[OBSTACLE_MAX];
static int s_obstacle_count;

/* ObstacleShadow.Draw (0x1401116b0; its list stays empty: player parts with the 3D mode) / ObstacleShadow2.Draw
 * (0x140111550): LIGHTBLEND white, transform (rot, sx / p, sy / p) at (x / p, y / p), then the list is emptied */
static void draw_obstacle_shadows(bool second)
{
    float p1 = 1.0f / L.scale_night;
    m2d_blend(4);
    m2d_color(255, 255, 255);
    m2d_alpha(1.0f);
    if (second) {
        for (int i = 0; i < s_obstacle_count; i++) {
            const light_obstacle_t *o = &s_obstacles[i];
            m2d_transform(o->rot, o->sx * p1, o->sy * p1);
            m2d_image(o->img, o->x * p1, o->y * p1, o->frame);
        }
        s_obstacle_count = 0;
    }
    m2d_transform(0.0f, 1.0f, 1.0f);
    if (second) m2d_blend(3);
}

/* ------------------------------------------------------------------------------------------------------------
 * Day shadows: the hexagon of a rectangle swept along the sun (cs2d_draw_poly_3 0x14025e420, _5 0x14025f8d0,
 * FUN_14025fff0) and its blurred rim (cs2d_draw_texturedpoly_3 0x14025ba10)
 * ------------------------------------------------------------------------------------------------------------ */
static void shadow_hexagon(float x, float y, float ang, float len, int w, int h, float *p)
{
    double rad = (double)ang * DEG2RAD;
    float c = (float)cos(rad), s = (float)sin(rad);
    float fw = (float)w, fh = (float)h;
    if (90.0f <= ang) {
        if (180.0f <= ang) {
            if (ang < 270.0f) {
                float f7 = fh + y + s * len, f6 = c * len + fw + x;
                float v[12] = {c * len + x, f7, f6, f7, f6, s * len + y, fw + x, y, x, y, x, fh + y};
                memcpy(p, v, sizeof(v));
            } else if (ang < 360.0f) {
                float f5 = fw + x, f6 = c * len + x, f7 = s * len + fh + y;
                float v[12] = {f6, s * len + y, f6, f7, c * len + f5, f7, f5, fh + y, f5, y, x, y};
                memcpy(p, v, sizeof(v));
            }
        } else {
            float f5 = fh + y, f6 = c * len + fw + x, f7 = s * len + y;
            float v[12] = {x, f5, fw + x, f5, f6, s * len + f5, f6, f7, c * len + x, f7, x, y};
            memcpy(p, v, sizeof(v));
        }
    } else {
        float f6 = fw + x, f5 = fh + y, f8 = c * len + x, f9 = s * len + y;
        float v[12] = {x, f5, f6, f5, f6, y, c * len + f6, f9, f8, f9, f8, f5 + s * len};
        memcpy(p, v, sizeof(v));
    }
}

/* Bisector direction at P between A (next along the outline) and B (previous), in degrees, as each block of
 * cs2d_draw_texturedpoly_3 computes it (law of cosines for the angle at P) */
static float rim_angle(const float *P, const float *A, const float *B)
{
    float dax = P[0] - A[0], day = P[1] - A[1];
    double ang = atan2((double)day, (double)dax) * RAD2DEG;
    float a2 = dax * dax + day * day;
    float b2 = (P[0] - B[0]) * (P[0] - B[0]) + (P[1] - B[1]) * (P[1] - B[1]);
    float ab = (B[0] - A[0]) * (A[0] - B[0]) + (B[1] - A[1]) * (A[1] - B[1]);
    float cv = (ab + a2 + b2) / sqrtf(a2 * b2 * 4.0f);
    if (cv <= 1.0f) {
        if (cv < -1.0f) ang -= 90.0;
        else ang -= (double)((float)(acos((double)cv) * RAD2DEG) * 0.5f);
    }
    return (float)ang;
}

static void rim_point(const float *P, float ang, double blur, float *out)
{
    out[0] = (float)(cos((double)ang * DEG2RAD) * blur + (double)P[0]);
    out[1] = (float)(sin((double)ang * DEG2RAD) * blur + (double)P[1]);
}

/* A blur quad: the outline edge P-Q and its offset points, UV (1,1) (0,0) (1,0) (1,1) */
static void rim_quad(const float *P, const float *OP, const float *OQ, const float *Q)
{
    float v[16] = {P[0], P[1], 1.0f, 1.0f, OP[0], OP[1], 0.0f, 0.0f, OQ[0], OQ[1], 1.0f, 0.0f, Q[0], Q[1], 1.0f, 1.0f};
    m2d_textured_poly(IMG_BLUR, v, 4);
}

static void shadow_rim(const float *arr, float blur_f, int tx, int ty)
{
    m2d_color(255, 255, 255);
    bool both = (tx & ty) == -1;                 /* bVar20: no tile (rooms, entities) */
    int hs = height_at(tx, ty), hl = height_at(tx - 1, ty), hu = height_at(tx, ty - 1);
    int hd = height_at(tx, ty + 1), hr = height_at(tx + 1, ty);
    bool b21 = hs != hu, b10 = b21 || both, b22 = hl != hs, b23 = false, b24 = false;
    const float *P0 = &arr[0], *P1 = &arr[2], *P2 = &arr[4], *P3 = &arr[6], *P4 = &arr[8], *P5 = &arr[10];
    double blur = (double)blur_f;
    float O0[2], O1[2], O2[2], O3[2], O4[2], O5[2];
    if (L.shadow_rot < 180.0f || 270.0f < L.shadow_rot) {
        if (b10) rim_point(P0, rim_angle(P0, P1, P4), blur, O0);
        else { O0[0] = (float)(-1.0 * blur + (double)P0[0]); O0[1] = (float)(SIN180 * blur + (double)P0[1]); }
        double d29, d27;
        if (both) {
            rim_point(P1, rim_angle(P1, P2, P0), blur, O1);
            b24 = hr != hs;
            goto c032;
        } else {
            double c1, s1;
            if (hd == hs) { c1 = -1.0; s1 = blur * SIN180; }
            else if (b22 || both) {
                float a = rim_angle(P1, P2, P0);
                c1 = cos((double)a * DEG2RAD);
                s1 = sin((double)a * DEG2RAD) * blur;
            } else { c1 = COS90; s1 = blur; }
            b23 = hr != hs;
            O1[0] = (float)(c1 * blur + (double)P1[0]);
            O1[1] = (float)(s1 + (double)P1[1]);
            b24 = true;
            if (b23) goto c032;
            d29 = COS90;
            d27 = blur;
            goto o2;
        }
    c032:
        b23 = b24;
        {
            float a = rim_angle(P2, P3, P1);
            d29 = cos((double)a * DEG2RAD);
            d27 = sin((double)a * DEG2RAD) * blur;
        }
    o2:
        O2[0] = (float)(d29 * blur + (double)P2[0]);
        O2[1] = (float)(d27 + (double)P2[1]);
        {
            float raw3 = rim_angle(P3, P4, P2), a3 = raw3;
            if (hd == hs) a3 = 0.0f;
            if (both) a3 = raw3;
            rim_point(P3, a3, blur, O3);
            float raw4 = rim_angle(P4, P5, P3), a4 = raw4;
            if (hs == hu) a4 = 0.0f;
            if (hr == hs) a4 = -90.0f;
            if (both) a4 = raw4;
            rim_point(P4, a4, blur, O4);
            float raw5 = rim_angle(P5, P0, P4), a5 = raw5;
            if (hl == hs) a5 = -90.0f;
            if (both) {
                rim_point(P5, raw5, blur, O5);
                goto d930;
            }
            rim_point(P5, a5, blur, O5);
            if (hd != hs) goto d930;
            if (b22 || both) goto db84;
            goto c664;
        }
    d930:
        rim_quad(P1, O1, O2, P2);
        if (b22 || both) {
    db84:
            rim_quad(P0, O0, O1, P1);
            if (both || (b23 && hd != hs)) goto da1a;
    dc48:
            if (!b21 || !b22) goto c664;
        } else {
            if (!b23 || hd == hs) goto c664;
    da1a:
            rim_quad(P2, O2, O3, P3);
            if (!both) goto dc48;
        }
        rim_quad(P5, O5, O0, P0);
    c664:
        if (both || b23) rim_quad(P3, O3, O4, P4);
        if (!b21 && !both) return;
        rim_quad(P4, O4, O5, P5);
        return;
    }

    /* ShadowRotation in [180, 270] */
    if (b22 || both) rim_point(P0, rim_angle(P0, P1, P4), blur, O0);
    else { O0[0] = (float)(COS90 * blur + (double)P0[0]); O0[1] = (float)(1.0 * blur + (double)P0[1]); }
    {
        double dx, dy;
        if (both) {
            float a = rim_angle(P1, P2, P0);
            dx = cos((double)a * DEG2RAD) * blur;
            dy = sin((double)a * DEG2RAD) * blur;
        } else if (hd == hs) {
            dy = 0.0;
            dx = blur;
        } else if (hr != hs) {
            float a = rim_angle(P1, P2, P0);
            dx = cos((double)a * DEG2RAD) * blur;
            dy = sin((double)a * DEG2RAD) * blur;
        } else {
            dx = blur * COS90;
            dy = blur;
        }
        O1[0] = (float)(dx + (double)P1[0]);
        O1[1] = (float)(dy + (double)P1[1]);
    }
    {
        double d29 = blur, d27 = 0.0;
        if (b10) {
            float a = rim_angle(P2, P3, P1);
            d29 = cos((double)a * DEG2RAD) * blur;
            d27 = sin((double)a * DEG2RAD) * blur;
        }
        b24 = hr != hs;
        O2[0] = (float)(d29 + (double)P2[0]);
        O2[1] = (float)(d27 + (double)P2[1]);
    }
    {
        double d29, d27;
        if (b24 || both) {
            rim_point(P3, rim_angle(P3, P4, P2), blur, O3);
            if (!both) {
                if (hd != hs) goto dd39;
                if (!b22 && !both) goto df5b;
                b23 = false;
                goto dd50;
            }
            float a = rim_angle(P4, P5, P3);
            d29 = cos((double)a * DEG2RAD);
            d27 = sin((double)a * DEG2RAD);
            b23 = hd != hs;
        } else {
            O3[0] = (float)(blur * COS90 + (double)P3[0]);
            O3[1] = (float)((double)P3[1] - blur);
    dd39:
            b23 = hd != hs;
            if (hs == hu && (hr == hs || b23)) {
    dd50:
                d29 = -1.0;
                d27 = SIN180;
            } else if (b22 || both) {
                float a = rim_angle(P4, P5, P3);
                d29 = cos((double)a * DEG2RAD);
                d27 = sin((double)a * DEG2RAD);
            } else {
    df5b:
                b23 = hd != hs;
                d29 = COS90;
                d27 = -1.0;
            }
        }
        O4[0] = (float)(d29 * blur + (double)P4[0]);
        O4[1] = (float)(d27 * blur + (double)P4[1]);
    }
    if (both || b23) {
        rim_point(P5, rim_angle(P5, P0, P4), blur, O5);
        if (!b24 && !both) {
    e055:
            rim_quad(P0, O0, O1, P1);
            if (both || (b24 && b21)) {
    d3da:
                rim_quad(P2, O2, O3, P3);
                if (!both) goto e110;
            } else {
    e110:
                if (!b23 || !b22) goto d520;
            }
            rim_quad(P5, O5, O0, P0);
            goto d520;
        }
    d308:
        rim_quad(P1, O1, O2, P2);
        if (both || b23) goto e055;
        if (b10) goto d3da;
    } else {
        O5[0] = (float)((double)P5[0] - blur);
        O5[1] = (float)((double)P5[1] + blur * SIN180);
        if (b24 || both) goto d308;
    d520:
        if (b10) rim_quad(P3, O3, O4, P4);
    }
    if (!b22 && !both) return;
    rim_quad(P4, O4, O5, P5);
}

/* cs2d_draw_poly_3: black hexagon and its rim */
static void shadow_poly3(float x, float y, float ang, float len, int blur, int w, int h, int tx, int ty)
{
    float p[12] = {0};
    shadow_hexagon(x, y, ang, len, w, h, p);
    m2d_color(0, 0, 0);
    m2d_poly(p, 6);
    shadow_rim(p, (float)blur, tx, ty);
    m2d_color(255, 255, 255);
}

/* cs2d_draw_poly_5: black hexagon only */
static void shadow_poly5(float x, float y, float ang, float len, int w, int h)
{
    float p[12] = {0};
    shadow_hexagon(x, y, ang, len, w, h, p);
    m2d_color(0, 0, 0);
    m2d_poly(p, 6);
}

/* FUN_14025fff0: rim only */
static void shadow_rim_only(float x, float y, float ang, float len, int blur, int w, int h, int tx, int ty)
{
    float p[12] = {0};
    shadow_hexagon(x, y, ang, len, w, h, p);
    m2d_color(0, 0, 0);
    shadow_rim(p, (float)blur, tx, ty);
}

/* White rectangles over runs of tiles that match (GenerateDayBuffer's row loops) */
static void white_runs(int y, int x0, int x1, float h, float camx, float camy, int which)
{
    bool run = false;
    int start = 0, end = 0;
    for (int x = x0; x <= x1; x++) {
        bool hit;
        if (which == 1) hit = x < L.map_w && y < L.map_h && L.tilemode[x * L.map_h + y] == 1;
        else if (which == 2) hit = x < L.map_w && y < L.map_h && L.tilemode[x * L.map_h + y] > 1;
        else hit = !(x < L.map_w && y < L.map_h && L.tilemap[x * L.map_h + y] != -1);
        if (hit) {
            if (run) {
                end = x;
                if (x == x1) {
                    m2d_rect((float)start * h - camx, (float)y * h - camy, (float)((x - start) + 1) * h, h);
                    run = false;
                }
            } else if (x == x1) {
                m2d_rect((float)x1 * h - camx, (float)y * h - camy, h, h);
            } else {
                run = true;
                start = end = x;
            }
        } else if (run) {
            m2d_rect((float)start * h - camx, (float)y * h - camy, (float)((end - start) + 1) * h, h);
            run = false;
        }
    }
}

static bool room_visible(const light_room_t *rm)
{
    float f42 = SCREEN_W * 0.5f + L.cam_x;
    float f24 = (float)(rm->y * 32 - 440);
    float f36 = (float)(rm->x << 5) + (-200.0f - SCREEN_W * 0.5f);
    return f36 <= f42 && f24 <= L.cam_y + 240.0f && f42 <= f36 + (float)(rm->w * 32 + 400) + SCREEN_W &&
           L.cam_y + 240.0f <= (float)(rm->h * 32 + 880) + f24 && rm->state != 0 && rm->shadow != 0;
}

static void room_roof_shadow(const light_room_t *rm, float p1, float h, float camx, float camy, float sun, float cosv, float blur)
{
    float len = ((float)(-rm->height / 2) - (float)rm->height * 0.5f * cosv) * p1;
    shadow_poly3(((float)rm->x * h + (float)rm->offx * p1) - camx, ((float)rm->y * h + (float)rm->offy * p1) - camy,
                 sun, len, (int)blur, (int)((float)rm->w * h), (int)((float)rm->h * h), -1, -1);
}

/* TSmartLighting.GenerateDayBuffer (0x140260330) */
static void generate_day_buffer(void)
{
    float s = L.scale_night;
    int nc = night_color();
    (void)nc;
    float camx = L.cam_x, camy = L.cam_y;
    float p1 = 1.0f / s;
    float sun = (float)fmod360((double)(L.sun_angle + L.shadow_rot));
    float blur = p1 * 7.0f, h = p1 * 32.0f, camx1 = p1 * camx, camy1 = p1 * camy;
    float cosv = (float)fabs(cos((double)(sun - 180.0f) * DEG2RAD));
    float fx = camx * 0.03125f;
    int ctx = (int)fx, cty = (int)(camy * 0.03125f);
    L.tile_height[255] = 32.0f;
    float maxw = 0.0f;
    if (L.tileset_count - 1 >= 0) {
        float half = cosv * 0.5f;
        for (int i = 0; i < L.tileset_count && i < 1024; i++) {
            int hh = (int)(i < 256 ? L.tile_height[i] : 0.0f);
            int ws = (int)(((float)(-hh / 2) - (float)hh * half) * p1);
            L.wall_size[i] = ws;
            float a = (float)abs(ws) * s;
            if (maxw <= a) maxw = a;
        }
        fx = (maxw + maxw + camx) * 0.03125f;
    }
    L.wall_size[255] = (int)((-32.0f - cosv * 32.0f) * p1 * 0.5f);
    int xr = (int)((float)TILES_X + 1.0f + fx);
    float ext = s * 100.0f;
    if (maxw <= s * 100.0f) ext = maxw;
    m2d_color(50, 50, 50);
    m2d_blend(5);
    {
        float yb = (ext + ext + camy) * 0.03125f + 15.0f;
        float ya = (camy - ext) * 0.03125f - 1.0f;
        if ((float)(L.map_h - 1) <= yb) yb = (float)(L.map_h - 1);
        if (ya <= 0.0f) ya = 0.0f;
        for (int y = (int)ya; y <= (int)yb; y++) {
            int xe = xr <= L.map_w ? xr : L.map_w;
            float xa = (camx - ext) * 0.03125f - 1.0f;
            if (xa <= 0.0f) xa = 0.0f;
            int x = (int)xa;
            if (xe < x) break;
            for (; x <= xe; x++) {
                if (x >= L.map_w || y >= L.map_h) continue;
                int mode = L.tilemode[x * L.map_h + y];
                unsigned t = (unsigned)(L.tilemap[x * L.map_h + y] - 1);
                if (mode < 1 || t > 0xff) continue;
                shadow_poly3((float)x * h - camx1, (float)y * h - camy1, sun, (float)L.wall_size[t], (int)blur, (int)h, (int)h, x, y);
            }
        }
    }
    if (L.cfg.playershadows && L.cfg.mapshadows > 1) {
        m2d_blend(5);
        m2d_color(0, 0, 0);
        /* The emulator skips FUN_140112340 (no Tpl objects there): a trace without players prints a marker */
        if (s_trace && s_part_count == 0) m2d_marker("playershadows");
        else draw_player_shadows();
    }
    s_part_count = 0;
    m2d_alpha(1.0f);
    m2d_marker("projectileshadows");
    m2d_alpha(1.0f);
    int row0 = cty - 1 < 0 ? 0 : cty - 1;
    {
        m2d_color(255, 255, 255);
        m2d_blend(3);
        int x1 = TILES_X + ctx;
        int x0 = ctx - 1 < 0 ? 0 : ctx - 1;
        if (row0 <= cty + 16 && x0 <= x1 + 1) {
            for (int y = row0; y <= cty + 16; y++) white_runs(y, x0, x1 + 1, h, camx1, camy1, 1);
        }
    }
    draw_obstacle_shadows(false);
    m2d_color(255, 255, 255);
    m2d_blend(5);
    m2d_color(50, 50, 50);
    m2d_blend(5);
    if (3 < L.cfg.mapshadows) {
        float xa = (camx - ext) * 0.03125f - 1.0f;
        if (xa <= 0.0f) xa = 0.0f;
        int x0 = (int)xa;
        float yb = (camy + ext + ext) * 0.03125f + 15.0f, ya = (camy - ext) * 0.03125f - 1.0f;
        int xe = xr > L.map_w ? L.map_w : xr;
        if (ya <= 0.0f) ya = 0.0f;
        int y0 = (int)ya;
        if ((float)(L.map_h - 1) <= yb) yb = (float)(L.map_h - 1);
        int y1 = (int)yb;
        int last = xe + 1;
        if (y0 <= y1 + 1 && x0 <= last) {
            for (int y = y0; y != y1 + 2; y++) {
                int size = 0, start = 0;
                for (int x = x0; x != xe + 2; x++) {
                    unsigned t = (unsigned)(x < L.map_w && y < L.map_h ? L.tilemap[x * L.map_h + y] - 1 : -1);
                    if (t > 0xff || x >= L.map_w || y >= L.map_h || L.tilemode[x * L.map_h + y] < 2 || L.tile_height[t] <= 18.0f) {
                        size = 0;
                        continue;
                    }
                    int ws = L.wall_size[t];
                    if (size != 0 && ws != size) {
                        shadow_poly5((float)start * h - camx1, (float)y * h - camy1, sun, (float)(size + 10), (int)h, (int)h);
                        size = 0;
                    }
                    if (size == 0) {
                        size = ws;
                        start = x;
                        if (x == last) size = 0;
                        shadow_poly5((float)start * h - camx1, (float)y * h - camy1, sun, (float)(ws + 10), (int)h, (int)h);
                    } else {
                        shadow_poly5((float)start * h - camx1, (float)y * h - camy1, sun, (float)(ws + 10),
                                     (int)((float)((x - start) + 1) * h), (int)h);
                        start = x;
                        if (x == last) size = 0;
                    }
                }
            }
        }
        if (y0 <= y1 && x0 <= xe) {
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= xe; x++) {
                    if (x >= L.map_w || y >= L.map_h) continue;
                    unsigned t = (unsigned)(L.tilemap[x * L.map_h + y] - 1);
                    if (t < 0x100 && L.tilemode[x * L.map_h + y] > 1 && 18.0f < L.tile_height[t]) {
                        shadow_rim_only((float)x * h - camx1, (float)y * h - camy1, sun, (float)(L.wall_size[t] + 10),
                                        (int)blur, (int)h, (int)h, x, y);
                    }
                }
            }
        }
    }
    m2d_color(50, 50, 50);
    m2d_blend(5);
    for (int i = 0; i < L.room_count; i++) {
        const light_room_t *rm = &L.rooms[i];
        if (room_visible(rm) && rm->layer == 0) room_roof_shadow(rm, p1, h, camx1, camy1, sun, cosv, blur);
    }
    draw_obstacle_shadows(true);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    m2d_blend(3);
    int xr2 = ctx - 1 + TILES_X;
    int x0 = ctx - 1 < 0 ? 0 : ctx - 1;
    int yend = cty + 16;
    if (yend < row0) {
        m2d_alpha(1.0f);
        if (L.cfg.mapshadows < 3) {
            m2d_color(255, 255, 255);
            m2d_blend(3);
            goto final;
        }
        goto layers;
    }
    for (int y = row0; y <= yend; y++) white_runs(y, x0, xr2 + 2, h, camx1, camy1, 2);
    m2d_alpha(1.0f);
    if (2 < L.cfg.mapshadows) goto layers;
    m2d_color(255, 255, 255);
    m2d_blend(3);
    goto voids;
layers:
    for (int layer = 1; layer != 6; layer++) {
        m2d_color(255, 255, 255);
        m2d_blend(5);
        for (int i = 0; i < L.room_count; i++) {
            const light_room_t *rm = &L.rooms[i];
            if (room_visible(rm) && rm->layer == layer) room_roof_shadow(rm, p1, h, camx1, camy1, sun, cosv, blur);
        }
        m2d_blend(3);
        m2d_color(255, 255, 255);
        for (int i = 0; i < L.room_count; i++) {
            const light_room_t *rm = &L.rooms[i];
            if (room_visible(rm) && rm->layer == layer) {
                m2d_rect(((float)rm->x * h + (float)rm->offx * p1) - camx1, ((float)rm->offy * p1 + (float)rm->y * h) - camy1,
                         (float)rm->w * h, (float)rm->h * h);
            }
        }
    }
    m2d_color(255, 255, 255);
    m2d_blend(3);
    if (yend < row0) goto final;
voids:
    for (int y = row0; y <= yend; y++) white_runs(y, x0, xr2 + 2, h, camx1, camy1, 0);
final:
    {
        int v = (255 - night_color()) - L.global_brightness;
        if (v < L.day_brightness) v = L.day_brightness;
        m2d_color(v, v, v);
        m2d_blend(4);
        m2d_alpha(1.0f);
        m2d_rect(0.0f, 0.0f, SCREEN_W + 4.0f, 480.0f);
        m2d_blend(3);
    }
}

/* ------------------------------------------------------------------------------------------------------------
 * TSmartLighting.DrawTilesFloor (0x14010abe0): occlusion next to walls, two passes. Pass 1 (gfx_shadowmap)
 * shades every tile that is not a wall from its wall neighbours; pass 2 (gfx_oclussionmap) every floor tile from its
 * wall and obstacle neighbours. The draws for each 8-neighbour pattern come from the original
 * (tools/agent/gen_occlusion_table.py): bit i = neighbour (-1,-1) (0,-1) (1,-1) (-1,0) (1,0) (-1,1) (0,1) (1,1).
 * ------------------------------------------------------------------------------------------------------------ */
static const uint8_t s_occl_count[256] = {
    0, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2,
    2, 4, 4, 4, 4, 6, 4, 4, 2, 2, 2, 2, 4, 4, 2, 2, 4, 6, 4, 4, 4, 6, 4, 4, 3, 3, 2, 2, 3, 3, 2, 2,
    2, 4, 3, 3, 4, 6, 3, 3, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 4, 3, 3, 4, 6, 3, 3, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 4, 4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 6, 6, 4, 4, 2, 4, 2, 2, 2, 4, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2,
    4, 6, 6, 6, 6, 8, 6, 6, 4, 4, 4, 4, 6, 6, 4, 4, 4, 6, 4, 4, 4, 6, 4, 4, 3, 3, 2, 2, 3, 3, 2, 2,
    2, 4, 3, 3, 4, 6, 3, 3, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 4, 3, 3, 4, 6, 3, 3, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
};
/* {image (0 shadowmap, 1 occlusion), frame, rotation / 90, mirror x, mirror y} */
static const int8_t s_occl_draw[256][8][5] = {
    {{0}},
    {{0,0,0,0,0}, {1,8,0,0,0}},
    {{0,1,0,0,0}, {1,5,0,0,0}},
    {{0,1,0,0,0}, {1,7,0,0,0}},
    {{0,0,1,0,0}, {1,8,1,0,0}},
    {{0,0,0,0,0}, {0,0,1,0,0}, {1,8,0,0,0}, {1,8,1,0,0}},
    {{0,1,0,0,0}, {1,7,0,1,0}},
    {{0,1,0,0,0}, {1,6,0,0,0}},
    {{0,1,3,0,0}, {1,5,3,0,0}},
    {{0,1,3,0,0}, {1,7,1,0,1}},
    {{0,2,0,0,0}, {1,2,2,0,0}},
    {{0,2,0,0,0}, {1,2,2,0,0}},
    {{0,0,1,0,0}, {0,1,3,0,0}, {1,8,1,0,0}, {1,5,3,0,0}},
    {{0,0,1,0,0}, {0,1,3,0,0}, {1,8,1,0,0}, {1,7,1,0,1}},
    {{0,2,0,0,0}, {1,4,0,0,0}},
    {{0,2,0,0,0}, {1,4,0,0,0}},
    {{0,1,1,0,0}, {1,5,1,0,0}},
    {{0,0,0,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,5,1,0,0}},
    {{0,2,1,0,0}, {1,2,3,0,0}},
    {{0,2,1,0,0}, {1,4,0,1,0}},
    {{0,1,1,0,0}, {1,7,3,1,1}},
    {{0,0,0,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,7,3,1,1}},
    {{0,2,1,0,0}, {1,2,3,0,0}},
    {{0,2,1,0,0}, {1,4,0,1,0}},
    {{0,3,0,0,0}, {1,5,1,0,0}, {1,5,3,0,0}},
    {{0,3,0,0,0}, {1,5,1,0,0}, {1,7,1,0,1}},
    {{0,4,0,0,0}, {1,10,3,0,0}},
    {{0,4,0,0,0}, {1,10,3,0,0}},
    {{0,3,0,0,0}, {1,5,3,0,0}, {1,7,3,1,1}},
    {{0,3,0,0,0}, {1,7,1,0,1}, {1,7,3,1,1}},
    {{0,4,0,0,0}, {1,10,3,0,0}},
    {{0,4,0,0,0}, {1,10,3,0,0}},
    {{0,0,3,0,0}, {1,8,3,0,0}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {1,8,0,0,0}, {1,8,3,0,0}},
    {{0,0,3,0,0}, {0,1,0,0,0}, {1,8,3,0,0}, {1,5,0,0,0}},
    {{0,0,3,0,0}, {0,1,0,0,0}, {1,8,3,0,0}, {1,7,0,0,0}},
    {{0,0,3,0,0}, {0,0,1,0,0}, {1,8,1,0,0}, {1,8,3,0,0}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {0,0,1,0,0}, {1,8,0,0,0}, {1,8,1,0,0}, {1,8,3,0,0}},
    {{0,0,3,0,0}, {0,1,0,0,0}, {1,8,3,0,0}, {1,7,0,1,0}},
    {{0,0,3,0,0}, {0,1,0,0,0}, {1,8,3,0,0}, {1,6,0,0,0}},
    {{0,1,3,0,0}, {1,7,3,0,0}},
    {{0,1,3,0,0}, {1,6,3,0,0}},
    {{0,2,0,0,0}, {1,4,3,1,0}},
    {{0,2,0,0,0}, {1,4,3,1,0}},
    {{0,0,1,0,0}, {0,1,3,0,0}, {1,8,1,0,0}, {1,7,3,0,0}},
    {{0,0,1,0,0}, {0,1,3,0,0}, {1,8,1,0,0}, {1,6,3,0,0}},
    {{0,2,0,0,0}, {1,3,0,0,0}},
    {{0,2,0,0,0}, {1,3,0,0,0}},
    {{0,0,3,0,0}, {0,1,1,0,0}, {1,8,3,0,0}, {1,5,1,0,0}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,8,3,0,0}, {1,5,1,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,2,3,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,4,0,1,0}},
    {{0,0,3,0,0}, {0,1,1,0,0}, {1,8,3,0,0}, {1,7,3,1,1}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,8,3,0,0}, {1,7,3,1,1}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,2,3,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,4,0,1,0}},
    {{0,3,0,0,0}, {1,5,1,0,0}, {1,7,3,0,0}},
    {{0,3,0,0,0}, {1,5,1,0,0}, {1,6,3,0,0}},
    {{0,4,0,0,0}, {1,9,3,0,1}},
    {{0,4,0,0,0}, {1,9,3,0,1}},
    {{0,3,0,0,0}, {1,7,3,0,0}, {1,7,3,1,1}},
    {{0,3,0,0,0}, {1,7,3,1,1}, {1,6,3,0,0}},
    {{0,4,0,0,0}, {1,9,3,0,1}},
    {{0,4,0,0,0}, {1,9,3,0,1}},
    {{0,1,2,0,0}, {1,5,2,0,0}},
    {{0,0,0,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,5,2,0,0}},
    {{0,3,1,0,0}, {1,5,0,0,0}, {1,5,2,0,0}},
    {{0,3,1,0,0}, {1,5,2,0,0}, {1,7,0,0,0}},
    {{0,0,1,0,0}, {0,1,2,0,0}, {1,8,1,0,0}, {1,5,2,0,0}},
    {{0,0,0,0,0}, {0,0,1,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,8,1,0,0}, {1,5,2,0,0}},
    {{0,3,1,0,0}, {1,5,2,0,0}, {1,7,0,1,0}},
    {{0,3,1,0,0}, {1,5,2,0,0}, {1,6,0,0,0}},
    {{0,2,3,0,0}, {1,2,1,0,0}},
    {{0,2,3,0,0}, {1,4,3,0,0}},
    {{0,4,3,0,0}, {1,10,2,0,0}},
    {{0,4,3,0,0}, {1,10,2,0,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,2,1,0,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,4,3,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,0}},
    {{0,2,2,0,0}, {1,2,0,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,2,0,0,0}},
    {{0,4,1,0,0}, {1,10,0,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,1}},
    {{0,2,2,0,0}, {1,4,1,1,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,4,1,1,0}},
    {{0,4,1,0,0}, {1,10,0,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,1}},
    {{0,4,2,0,0}, {1,10,1,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,1}},
    {{0,4,2,0,0}, {1,1,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,1,2,0,0}, {1,7,2,1,0}},
    {{0,0,0,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,7,2,1,0}},
    {{0,3,1,0,0}, {1,5,0,0,0}, {1,7,2,1,0}},
    {{0,3,1,0,0}, {1,7,0,0,0}, {1,7,2,1,0}},
    {{0,0,1,0,0}, {0,1,2,0,0}, {1,8,1,0,0}, {1,7,2,1,0}},
    {{0,0,0,0,0}, {0,0,1,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,8,1,0,0}, {1,7,2,1,0}},
    {{0,3,1,0,0}, {1,7,0,1,0}, {1,7,2,1,0}},
    {{0,3,1,0,0}, {1,7,2,1,0}, {1,6,0,0,0}},
    {{0,2,3,0,0}, {1,2,1,0,0}},
    {{0,2,3,0,0}, {1,4,3,0,0}},
    {{0,4,3,0,0}, {1,10,2,0,0}},
    {{0,4,3,0,0}, {1,10,2,0,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,2,1,0,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,4,3,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,0}},
    {{0,2,2,0,0}, {1,4,2,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,4,2,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,0}},
    {{0,4,1,0,0}, {1,1,0,0,0}},
    {{0,2,2,0,0}, {1,3,2,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,3,2,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,0}},
    {{0,4,1,0,0}, {1,1,0,0,0}},
    {{0,4,2,0,0}, {1,10,1,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,1}},
    {{0,4,2,0,0}, {1,1,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,0,2,0,0}, {1,8,2,0,0}},
    {{0,0,0,0,0}, {0,0,2,0,0}, {1,8,0,0,0}, {1,8,2,0,0}},
    {{0,0,2,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,5,0,0,0}},
    {{0,0,2,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,7,0,0,0}},
    {{0,0,2,0,0}, {0,0,1,0,0}, {1,8,2,0,0}, {1,8,1,0,0}},
    {{0,0,0,0,0}, {0,0,2,0,0}, {0,0,1,0,0}, {1,8,0,0,0}, {1,8,2,0,0}, {1,8,1,0,0}},
    {{0,0,2,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,7,0,1,0}},
    {{0,0,2,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,6,0,0,0}},
    {{0,0,2,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,5,3,0,0}},
    {{0,0,2,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,7,1,0,1}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,2,2,0,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,2,2,0,0}},
    {{0,0,2,0,0}, {0,0,1,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,5,3,0,0}},
    {{0,0,2,0,0}, {0,0,1,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,7,1,0,1}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,4,0,0,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,4,0,0,0}},
    {{0,1,1,0,0}, {1,7,1,1,0}},
    {{0,0,0,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,7,1,1,0}},
    {{0,2,1,0,0}, {1,4,1,0,0}},
    {{0,2,1,0,0}, {1,3,1,0,0}},
    {{0,1,1,0,0}, {1,6,1,0,0}},
    {{0,0,0,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,6,1,0,0}},
    {{0,2,1,0,0}, {1,4,1,0,0}},
    {{0,2,1,0,0}, {1,3,1,0,0}},
    {{0,3,0,0,0}, {1,5,3,0,0}, {1,7,1,1,0}},
    {{0,3,0,0,0}, {1,7,1,0,1}, {1,7,1,1,0}},
    {{0,4,0,0,0}, {1,9,3,0,0}},
    {{0,4,0,0,0}, {1,9,3,0,0}},
    {{0,3,0,0,0}, {1,5,3,0,0}, {1,6,1,0,0}},
    {{0,3,0,0,0}, {1,7,1,0,1}, {1,6,1,0,0}},
    {{0,4,0,0,0}, {1,9,3,0,0}},
    {{0,4,0,0,0}, {1,9,3,0,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {1,8,2,0,0}, {1,8,3,0,0}},
    {{0,0,0,0,0}, {0,0,2,0,0}, {0,0,3,0,0}, {1,8,0,0,0}, {1,8,2,0,0}, {1,8,3,0,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,8,3,0,0}, {1,5,0,0,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,8,3,0,0}, {1,7,0,0,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {0,0,1,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,8,3,0,0}},
    {{0,0,0,0,0}, {0,0,2,0,0}, {0,0,3,0,0}, {0,0,1,0,0}, {1,8,0,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,8,3,0,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,8,3,0,0}, {1,7,0,1,0}},
    {{0,0,2,0,0}, {0,0,3,0,0}, {0,1,0,0,0}, {1,8,2,0,0}, {1,8,3,0,0}, {1,6,0,0,0}},
    {{0,0,2,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,7,3,0,0}},
    {{0,0,2,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,6,3,0,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,4,3,1,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,4,3,1,0}},
    {{0,0,2,0,0}, {0,0,1,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,7,3,0,0}},
    {{0,0,2,0,0}, {0,0,1,0,0}, {0,1,3,0,0}, {1,8,2,0,0}, {1,8,1,0,0}, {1,6,3,0,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,3,0,0,0}},
    {{0,0,2,0,0}, {0,2,0,0,0}, {1,8,2,0,0}, {1,3,0,0,0}},
    {{0,0,3,0,0}, {0,1,1,0,0}, {1,8,3,0,0}, {1,7,1,1,0}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,8,3,0,0}, {1,7,1,1,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,4,1,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,3,1,0,0}},
    {{0,0,3,0,0}, {0,1,1,0,0}, {1,8,3,0,0}, {1,6,1,0,0}},
    {{0,0,0,0,0}, {0,0,3,0,0}, {0,1,1,0,0}, {1,8,0,0,0}, {1,8,3,0,0}, {1,6,1,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,4,1,0,0}},
    {{0,0,3,0,0}, {0,2,1,0,0}, {1,8,3,0,0}, {1,3,1,0,0}},
    {{0,3,0,0,0}, {1,7,3,0,0}, {1,7,1,1,0}},
    {{0,3,0,0,0}, {1,7,1,1,0}, {1,6,3,0,0}},
    {{0,4,0,0,0}, {1,1,3,0,0}},
    {{0,4,0,0,0}, {1,1,3,0,0}},
    {{0,3,0,0,0}, {1,7,3,0,0}, {1,6,1,0,0}},
    {{0,3,0,0,0}, {1,6,3,0,0}, {1,6,1,0,0}},
    {{0,4,0,0,0}, {1,1,3,0,0}},
    {{0,4,0,0,0}, {1,1,3,0,0}},
    {{0,1,2,0,0}, {1,7,2,0,0}},
    {{0,0,0,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,7,2,0,0}},
    {{0,3,1,0,0}, {1,5,0,0,0}, {1,7,2,0,0}},
    {{0,3,1,0,0}, {1,7,0,0,0}, {1,7,2,0,0}},
    {{0,0,1,0,0}, {0,1,2,0,0}, {1,8,1,0,0}, {1,7,2,0,0}},
    {{0,0,0,0,0}, {0,0,1,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,8,1,0,0}, {1,7,2,0,0}},
    {{0,3,1,0,0}, {1,7,0,1,0}, {1,7,2,0,0}},
    {{0,3,1,0,0}, {1,7,2,0,0}, {1,6,0,0,0}},
    {{0,2,3,0,0}, {1,4,2,1,0}},
    {{0,2,3,0,0}, {1,3,3,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,1}},
    {{0,4,3,0,0}, {1,9,2,0,1}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,4,2,1,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,3,3,0,0}},
    {{0,4,3,0,0}, {1,1,2,0,0}},
    {{0,4,3,0,0}, {1,1,2,0,0}},
    {{0,2,2,0,0}, {1,2,0,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,2,0,0,0}},
    {{0,4,1,0,0}, {1,10,0,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,1}},
    {{0,2,2,0,0}, {1,4,1,1,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,4,1,1,0}},
    {{0,4,1,0,0}, {1,10,0,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,1}},
    {{0,4,2,0,0}, {1,10,1,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,1}},
    {{0,4,2,0,0}, {1,1,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,1,2,0,0}, {1,6,2,0,0}},
    {{0,0,0,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,6,2,0,0}},
    {{0,3,1,0,0}, {1,5,0,0,0}, {1,6,2,0,0}},
    {{0,3,1,0,0}, {1,7,0,0,0}, {1,6,2,0,0}},
    {{0,0,1,0,0}, {0,1,2,0,0}, {1,8,1,0,0}, {1,6,2,0,0}},
    {{0,0,0,0,0}, {0,0,1,0,0}, {0,1,2,0,0}, {1,8,0,0,0}, {1,8,1,0,0}, {1,6,2,0,0}},
    {{0,3,1,0,0}, {1,7,0,1,0}, {1,6,2,0,0}},
    {{0,3,1,0,0}, {1,6,0,0,0}, {1,6,2,0,0}},
    {{0,2,3,0,0}, {1,4,2,1,0}},
    {{0,2,3,0,0}, {1,3,3,0,0}},
    {{0,4,3,0,0}, {1,9,2,0,1}},
    {{0,4,3,0,0}, {1,9,2,0,1}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,4,2,1,0}},
    {{0,0,1,0,0}, {0,2,3,0,0}, {1,8,1,0,0}, {1,3,3,0,0}},
    {{0,4,3,0,0}, {1,1,2,0,0}},
    {{0,4,3,0,0}, {1,1,2,0,0}},
    {{0,2,2,0,0}, {1,4,2,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,4,2,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,0}},
    {{0,4,1,0,0}, {1,1,0,0,0}},
    {{0,2,2,0,0}, {1,3,2,0,0}},
    {{0,0,0,0,0}, {0,2,2,0,0}, {1,8,0,0,0}, {1,3,2,0,0}},
    {{0,4,1,0,0}, {1,9,0,0,0}},
    {{0,4,1,0,0}, {1,1,0,0,0}},
    {{0,4,2,0,0}, {1,10,1,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,4,2,0,0}, {1,9,1,0,1}},
    {{0,4,2,0,0}, {1,1,1,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
    {{0,5,0,0,0}, {1,0,0,0,0}},
};

static int neighbour_mask(int x, int y, int min_mode)
{
    static const int nb[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    int m = 0;
    for (int i = 0; i < 8; i++) {
        int nx = x + nb[i][0], ny = y + nb[i][1];
        if (nx >= 0 && ny >= 0 && nx < L.map_w && ny < L.map_h && L.tilemode[nx * L.map_h + ny] >= min_mode) m |= 1 << i;
    }
    return m;
}

static void draw_tiles_floor(void)
{
    float camx = L.cam_x, camy = L.cam_y;
    float fa = camx * 0.03125f - 1.0f;
    if (fa <= -1.0f) fa = -1.0f;
    int x0 = (int)(fa + 1.0f);
    fa = camy * 0.03125f - 1.0f;
    if (fa <= -1.0f) fa = -1.0f;
    int y0 = (int)(fa + 1.0f);
    m2d_blend(5);
    float fx = (float)(TILES_X - 1) + camx * 0.03125f + 1.0f;
    if ((float)(L.map_w - 1) <= fx) fx = (float)(L.map_w - 1);
    float fy = camy * 0.03125f + 15.0f;
    if ((float)(L.map_h - 1) <= fy) fy = (float)(L.map_h - 1);
    for (int pass = 0; pass < 2; pass++) {
        for (int x = x0; x <= (int)fx; x++) {
            for (int y = y0; y <= (int)fy; y++) {
                int mode = (x >= 0 && y >= 0 && x < L.map_w && y < L.map_h) ? L.tilemode[x * L.map_h + y] : 0;
                if (pass == 0 ? mode > 1 : mode >= 1) continue;
                int mask = neighbour_mask(x, y, pass == 0 ? 2 : 1);
                for (int k = 0; k < s_occl_count[mask]; k++) {
                    const int8_t *d = s_occl_draw[mask][k];
                    if (d[0] != pass) continue;
                    float rot = (float)(d[2] * 90);
                    if (d[3] || d[4]) m2d_transform(rot, d[3] ? -1.0f : 1.0f, d[4] ? -1.0f : 1.0f);
                    else m2d_rotation(rot);
                    m2d_image(pass == 0 ? IMG_SHADOWMAP : IMG_OCCLUSION, (float)(x << 5) + (16.0f - camx), (float)(y << 5) + (16.0f - camy), d[1]);
                    if (d[3] || d[4]) m2d_scale(1.0f, 1.0f);
                }
            }
        }
        if (pass == 0) m2d_rotation(0.0f);
    }
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
}

/* ------------------------------------------------------------------------------------------------------------
 * TSmartLighting.GenerateMainBuffer (0x1402626d0) and Update (0x140262890)
 * ------------------------------------------------------------------------------------------------------------ */
static void generate_main_buffer(void)
{
    m2d_alpha(1.0f);
    float s = L.scale_night;
    m2d_bind(0, MAIN_W, MAIN_H);
    m2d_clear(255, 255, 255, 1.0f, true);
    if (1 < L.cfg.mapshadows && 180.0f < L.shadow_rot && L.shadow_rot < 360.0f) {
        generate_day_buffer();
        m2d_blend(4);
        m2d_color(255, 255, 255);
        draw_sprites(true);
    }
    m2d_color(255, 255, 255);
    m2d_blend(3);
    draw_rooms_light(s);
    draw_toning();
    draw_sprites(false);
    draw_lights_list(s);
    draw_stripes_light(s);
    m2d_unbind();
}

static void update_engine(float dt)
{
    float r = L.shadow_rot_set;
    if (fabsf(L.shadow_rot - L.shadow_rot_set) <= 90.0f) r = L.shadow_rot_set * dt * 0.1f + (1.0f - dt * 0.1f) * L.shadow_rot;
    L.shadow_rot = r;
    if (L.mode == -1) return;
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    for (int i = 0; i < L.light_count; i++) light_update(&L.lights[i], (int)L.scale_lights);
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    generate_main_buffer();
    m2d_color(255, 255, 255);
    m2d_alpha(1.0f);
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
}

/* ------------------------------------------------------------------------------------------------------------
 * Map: cells (cs2d_update_mapdata 0x1401413b0 -> SetTile FUN_14005c150) and the light entities (FUN_1401d6790)
 * ------------------------------------------------------------------------------------------------------------ */
/* g_map_cellprop: the tile mode, overridden by Env_Breakable (ints[6]), closed Func_DynWall (ints[1]) and solid
 * buildings (0x140141b58..0x140141c48) */
static int cell_prop(const cs2d_map_t *map, int x, int y, const int *ent_prop)
{
    size_t idx = (size_t)y * map->width + (size_t)x;
    if (map->object_shadow && map->object_shadow[idx]) return 1;
    if (ent_prop && ent_prop[idx] >= 0) return ent_prop[idx];
    return map->tile_modes[map->tiles[idx]];
}

static void set_tile(int x, int y, int tile, int mode)
{
    int *tm = &L.tilemap[x * L.map_h + y];
    if (*tm == tile) return;
    *tm = tile;
    L.tilemode[x * L.map_h + y] = mode;
    for (int i = 0; i < L.light_count; i++) {
        light_source_t *ls = &L.lights[i];
        float dy = (float)ls->pos_y - (float)(y * 32 - 16), dx = (float)ls->pos_x - (float)(x * 32 - 16);
        if (!(sqrtf(dx * dx + dy * dy) < ls->size + 16.0f)) continue;
        float f6 = L.cam_y - ls->size, f5 = L.cam_x - ls->size;
        if (f5 <= (float)ls->pos_x && f6 <= (float)ls->pos_y && (float)ls->pos_x <= ls->size + ls->size + f5 + SCREEN_W &&
            (float)ls->pos_y <= ls->size + ls->size + f6 + 480.0f) {
            light_update_buffer(ls, 1);
        }
    }
}

static void sync_tiles(const cs2d_map_t *map)
{
    int w = (int)map->width, h = (int)map->height;
    int *ent_prop = (int *)malloc((size_t)w * (size_t)h * sizeof(int));
    if (!ent_prop) return;
    for (int i = 0; i < w * h; i++) ent_prop[i] = -1;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        if (e->x < 0 || e->y < 0 || e->x >= w || e->y >= h || map->entity_state[i] != 0) continue;
        size_t idx = (size_t)e->y * (size_t)w + (size_t)e->x;
        if (e->type == CS2D_ENT_ENV_BREAKABLE) {
            int m = e->int_settings[6];
            ent_prop[idx] = m == 2 ? 0 : (m == 3 ? 2 : 1);
        } else if (e->type == CS2D_ENT_FUNC_DYNWALL) {
            int m = e->int_settings[1];
            if (m == 1) ent_prop[idx] = 2;
            else if (m == 2) ent_prop[idx] = 3;
            else if (m == 3) ent_prop[idx] = 4;
            else if (m == 4 && e->int_settings[0] >= 0 && e->int_settings[0] <= (int)map->tile_count) ent_prop[idx] = map->tile_modes[e->int_settings[0]];
            else ent_prop[idx] = 1;
        }
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int tile = map->tiles[(size_t)y * (size_t)w + (size_t)x];
            int prop = cell_prop(map, x, y, ent_prop);
            int tprop = map->tile_modes[tile];
            if (prop == tprop) {
                if (tile == 0) set_tile(x, y, -1, -1);
                else if (prop == 1) set_tile(x, y, tile + 1, 2);
                else if (prop == 2) set_tile(x, y, tile + 1, 1);
                else set_tile(x, y, tile + 1, 0);
            } else if (prop == 1) {
                set_tile(x, y, 0x100, 2);
            } else if (tile == 0) {
                set_tile(x, y, -1, -1);
            } else if (prop == 2) {
                set_tile(x, y, tile + 1, 1);
            } else {
                set_tile(x, y, tile + 1, 0);
            }
        }
    }
    free(ent_prop);
}

/* Angle range of an Env_Light: start moved into 0.. by whole turns, end not below start */
static void light_angles(int start, int end, int *out_start, int *out_end)
{
    while (start < 0) { start += 360; end += 360; }
    while (end < start) end += 360;
    *out_start = start;
    *out_end = end;
}

static void load_toning(void)
{
    if (L.toning_loaded) return;
    L.toning_loaded = true;
    int w = 0, h = 0;
    uint32_t *px = cs2d_image_load_rgba("gfx/lighting/gfx_toning.png", &w, &h);
    if (!px) return;
    for (int i = 0; i < 1024 && i < w; i++) {
        uint32_t p = px[i];   /* (a << 24) | (b << 16) | (g << 8) | r */
        L.toning[i] = (p & 0xff000000u) | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
    }
    free(px);
}

void cs2d_light_configure(const cs2d_light_config_t *cfg)
{
    L.cfg = *cfg;
    int q = cfg->light > 0 ? cfg->light - 1 : -1;   /* FUN_1401733b0: SetQuality(light - 1) */
    L.mode = q;
    L.lightfixes = 0;
    if (q != -1) {
        if (q < 3) { L.scale_night = 2.0f; L.scale_lights = 2.0f; }
        else if (q == 3) { L.scale_night = 1.0f; L.scale_lights = 1.0f; }
        else { L.lightfixes = 1; L.scale_night = 1.0f; L.scale_lights = 1.0f; }
    }
    L.day_brightness = 255 - (int)((float)cfg->shadows * 2.55f);
}

void cs2d_light_set_daylight(int angle)
{
    L.server_daylight = angle + 1;
}

bool cs2d_light_active(void)
{
    return L.map_loaded && L.mode != -1;
}

void cs2d_light_load_map(const cs2d_map_t *map)
{
    free(L.tilemap); free(L.tilemode); free(L.rooms); free(L.lights); free(L.stripes);
    L.tilemap = L.tilemode = NULL;
    L.rooms = NULL; L.lights = NULL; L.stripes = NULL;
    L.room_count = L.light_count = L.stripe_count = 0;
    L.map_loaded = false;
    for (int i = 0; i < MAX_BUFFERS; i++) s_buf[i].free = true;
    s_sprite_count[0] = s_sprite_count[1] = 0;
    s_obstacle_count = 0;
    s_part_count = 0;
    if (!map || !map->tiles || map->width == 0 || map->height == 0) return;
    if (L.scale_night == 0.0f) { L.scale_night = 1.0f; L.scale_lights = 1.0f; }
    load_toning();
    L.map_w = (int)map->width;
    L.map_h = (int)map->height;
    L.tilemap = (int *)calloc((size_t)L.map_w * (size_t)L.map_h, sizeof(int));
    L.tilemode = (int *)calloc((size_t)L.map_w * (size_t)L.map_h, sizeof(int));
    if (!L.tilemap || !L.tilemode) return;
    L.global_brightness = 0;
    /* TileSetHeight: g_tile_height (0x1402a4650), [255] = 32 for entity walls */
    memset(L.tile_height, 0, sizeof(L.tile_height));
    for (int i = 0; i <= (int)map->tile_count && i < 256; i++) L.tile_height[i] = (float)map->tile_heights[i];
    L.tile_height[255] = 32.0f;
    L.tileset_count = (int)map->tile_count + 1;
    /* Map daylight: ShadowRotation = (time - 1000) % 360, 225 below 1000 (0x1401d3095) */
    L.shadow_rot = L.shadow_rot_set = (float)map->daylight_time;
    L.light_counter = 0;
    int nr = 0, nl = 0, ns = 0;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        int t = map->entities[i].type;
        nr += t == CS2D_ENT_ENV_ROOM;
        nl += t == CS2D_ENT_ENV_LIGHT;
        ns += t == CS2D_ENT_ENV_LIGHTSTRIPE;
    }
    L.rooms = (light_room_t *)calloc((size_t)(nr + 1), sizeof(light_room_t));
    L.lights = (light_source_t *)calloc((size_t)(nl + 1), sizeof(light_source_t));
    L.stripes = (light_stripe_t *)calloc((size_t)(ns + 1), sizeof(light_stripe_t));
    if (!L.rooms || !L.lights || !L.stripes) return;
    for (uint32_t i = 0; i < map->entity_count; i++) {
        const cs2d_entity_t *e = &map->entities[i];
        const int *v = e->int_settings;
        if (e->type == CS2D_ENT_ENV_ROOM) {
            light_room_t *rm = &L.rooms[L.room_count++];
            rm->x = e->x; rm->y = e->y; rm->w = v[0]; rm->h = v[1]; rm->height = v[2];
            rm->r = v[3]; rm->g = v[4]; rm->b = v[5];
            rm->shadow = v[7]; rm->mode = v[6]; rm->daycolor = 1 - v[8]; rm->layer = v[9];
            rm->offx = str_int(e->str_settings[0]); rm->offy = str_int(e->str_settings[1]);
            rm->transparency = str_float(e->str_settings[2]);
            rm->state = 1;
        } else if (e->type == CS2D_ENT_ENV_LIGHT) {
            light_source_t *ls = &L.lights[L.light_count++];
            int start = 0, end = 359;
            if (v[4] != 0 || v[5] != 0) light_angles(v[4], v[5], &start, &end);
            int range = v[0] > 256 ? 256 : v[0];
            ls->astart = start; ls->aend = end;
            ls->r = (float)v[1]; ls->g = (float)v[2]; ls->b = (float)v[3];
            ls->alpha = str_float(e->str_settings[0]);
            ls->state = 1;
            ls->id = ++L.light_counter;
            ls->max_size = LIGHT_MAX_SIZE;
            ls->size = (float)range <= (float)(LIGHT_MAX_SIZE / 2) ? (float)range : (float)(LIGHT_MAX_SIZE / 2);
            ls->pos_x = e->x * 32 + 16 + str_int(e->str_settings[1]);
            ls->pos_y = e->y * 32 + 16 + str_int(e->str_settings[2]);
            ls->rotation = (float)str_int(e->str_settings[3]);
            ls->buffer = -1;
            ls->ent = (int)i;
            ls->rot_start = v[4];
            ls->rot_end = v[5];
            if (map->entity_state[i] == 1) light_change_state(ls, 0);
        } else if (e->type == CS2D_ENT_ENV_LIGHTSTRIPE) {
            light_stripe_t *st = &L.stripes[L.stripe_count++];
            st->x = (float)(e->x + v[7]); st->y = (float)(e->y + v[8]);
            st->w = (float)v[0]; st->h = (float)v[1]; st->height = v[9]; st->alpha = str_float(e->str_settings[0]);
            st->day = 1 - v[6]; st->r = v[3]; st->g = v[4]; st->b = v[5]; st->rot = v[2];
            st->state = 1;
        }
    }
    L.solid_version = map->entity_solid_version - 1;
    L.map_loaded = true;
}

void cs2d_light_frame(const cs2d_map_t *map, int cam_x, int cam_y, float dt)
{
    L.frame_ready = false;
    if (!L.map_loaded || !map || (int)map->width != L.map_w || (int)map->height != L.map_h) return;
    if (map->entity_solid_version != L.solid_version || !L.tilemap) {
        L.solid_version = map->entity_solid_version;
        sync_tiles(map);
    }
    L.cam_x = (float)cam_x;
    L.cam_y = (float)cam_y;
    /* FUN_140262ed0: sv_daylighttime when the server sets one, else the map's time */
    L.shadow_rot_set = (float)fmod360((double)(L.server_daylight == 0 ? map->daylight_time : L.server_daylight - 1));
    if (L.mode == -1) return;
    /* Te.trigger case 0x21: the light follows its entity state */
    for (int i = 0; i < L.light_count; i++) {
        light_source_t *ls = &L.lights[i];
        if (ls->ent < 0 || (uint32_t)ls->ent >= map->entity_count) continue;
        int want = map->entity_state[ls->ent] == 0;
        if (ls->state != want) light_change_state(ls, want);
    }
    load_images();
    if (gl_ready()) {
        if (!s_main_fbo && !gl_make_target(&s_main_fbo, &s_main_tex, MAIN_W, MAIN_H)) {
            L.mode = -1;   /* No render target: the original turns the light engine off (FUN_1401733b0) */
            return;
        }
    }
    update_engine(dt);
    s_part_count = 0;
    gl_restore();
    L.frame_ready = true;
    /* Te.draw(2) case 0x21, later in the frame: rotation speed ints[6] turns ints[7] / ints[8], applied next frame */
    for (int i = 0; i < L.light_count; i++) {
        light_source_t *ls = &L.lights[i];
        if (ls->ent < 0 || (uint32_t)ls->ent >= map->entity_count) continue;
        const cs2d_entity_t *e = &map->entities[ls->ent];
        if (map->entity_state[ls->ent] == 0 && e->int_settings[6] != 0) {
            ls->rot_start += e->int_settings[6];
            ls->rot_end += e->int_settings[6];
            light_angles(ls->rot_start, ls->rot_end, &ls->astart, &ls->aend);
            ls->rotation = (float)str_int(e->str_settings[3]);
        }
    }
}

void cs2d_light_draw_floor(void)
{
    if (!cs2d_light_active() || L.cfg.mapshadows <= 1) return;
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    draw_tiles_floor();
    gl_restore();
}

void cs2d_light_draw_color(void)
{
    if (!cs2d_light_active() || !L.frame_ready) return;
    m2d_blend(4);
    m2d_transform(0.0f, 1.0f, 1.0f);
    draw_lights_color_list();
    m2d_blend(3);
    gl_restore();
}

void cs2d_light_draw_buffer(void)
{
    if (!cs2d_light_active() || !L.frame_ready) return;
    m2d_transform(0.0f, 1.0f, 1.0f);
    m2d_blend(3);
    m2d_alpha(1.0f);
    m2d_color(255, 255, 255);
    m2d_blend(5);
    /* DrawBufferRect(0, 0, 0, 0): sizes under 5 mean 854 x 480 */
    m2d_draw_buffer(0, 0.0f, 0.0f, 854.0f, 480.0f, true);
    m2d_blend(3);
    gl_restore();
}

void cs2d_light_add_sprite(bool floor, const cs2d_texture_t *tex, float hx, float hy, float x, float y, float sx, float sy,
                           int r, int g, int b, float rot, float alpha)
{
    if (!cs2d_light_active() || !tex) return;
    queue_sprite(floor, ext_image(tex, hx, hy), x, y, sx, sy, r, g, b, rot, alpha, 0);
}

void cs2d_light_add_player_part(const cs2d_texture_t *tex, int cell_w, int cell_h, int frame, float x, float y,
                                float rot, float scale)
{
    if (!cs2d_light_active() || !tex || s_part_count >= PLAYER_PART_MAX) return;
    float fw = (float)(cell_w ? cell_w : tex->width), fh = (float)(cell_h ? cell_h : tex->height);
    int img = ext_image_cells(tex, fw * 0.5f, fh * 0.5f, cell_w, cell_h);
    if (img < 0) return;
    light_part_t *pt = &s_parts[s_part_count++];
    pt->img = img; pt->frame = frame;
    pt->x = x; pt->y = y; pt->rot = rot; pt->sx = scale; pt->sy = scale;
}

void cs2d_light_add_obstacle(const cs2d_texture_t *tex, float hx, float hy, float x, float y, float sx, float sy, float rot)
{
    if (!cs2d_light_active() || L.cfg.mapshadows <= 1 || !tex || s_obstacle_count >= OBSTACLE_MAX) return;
    int img = ext_image(tex, hx, hy);
    if (img < 0) return;
    light_obstacle_t *o = &s_obstacles[s_obstacle_count++];
    o->img = img; o->frame = 0;
    o->x = x; o->y = y; o->sx = sx; o->sy = sy; o->rot = rot;
}

float cs2d_light_shadow_rotation(void)
{
    return L.map_loaded ? L.shadow_rot : 225.0f;
}

bool cs2d_light_day_shadows(void)
{
    return cs2d_light_active() && L.cfg.mapshadows > 1;
}

void cs2d_light_set_trace(void (*fn)(const char *line, void *ud), void *ud)
{
    s_trace = fn;
    s_trace_ud = ud;
}

void cs2d_light_dump_scene(FILE *f, int cam_x, int cam_y)
{
    if (!f || !L.map_loaded) return;
    fprintf(f, "map %d %d\n", L.map_w, L.map_h);
    fprintf(f, "settings %d %d %d 0\n", L.cfg.light, L.cfg.mapshadows, L.cfg.playershadows ? 1 : 0);
    fprintf(f, "shadowrot %.3f\n", L.shadow_rot);
    fprintf(f, "sunangle %.3f\n", L.sun_angle);
    fprintf(f, "dayshadowbrightness %d\n", L.day_brightness);
    fprintf(f, "brightness %d\n", L.global_brightness);
    fprintf(f, "nightcolor %d %d %d\n", L.night_r, L.night_g, L.night_b);
    fprintf(f, "camera %d %d\n", cam_x, cam_y);
    fprintf(f, "tilesetcount %d\n", L.tileset_count);
    fprintf(f, "theight");
    for (int i = 0; i < 256; i++) fprintf(f, " %.1f", L.tile_height[i]);
    fprintf(f, "\ntoning");
    for (int i = 0; i < 1024; i++) fprintf(f, " %x", (unsigned)L.toning[i]);
    fprintf(f, "\ncells\n");
    for (int y = 0; y < L.map_h; y++) {
        for (int x = 0; x < L.map_w; x++) fprintf(f, "%s%d,%d", x ? " " : "", L.tilemap[x * L.map_h + y], L.tilemode[x * L.map_h + y]);
        fprintf(f, "\n");
    }
    for (int i = 0; i < L.room_count; i++) {
        const light_room_t *r = &L.rooms[i];
        fprintf(f, "room %d %d %d %d %d %d %d %d %d %d %d %d %d %d %.3f %d\n", r->x, r->y, r->w, r->h, r->height, r->r, r->g,
                r->b, r->shadow, r->mode, r->daycolor, r->layer, r->offx, r->offy, r->transparency, r->state);
    }
    for (int i = 0; i < L.light_count; i++) {
        const light_source_t *l = &L.lights[i];
        fprintf(f, "light %d %d %.3f %d %d %d %.3f %d %d %.3f %d %d %d\n", l->pos_x, l->pos_y, l->size, (int)l->r, (int)l->g,
                (int)l->b, l->alpha, l->astart, l->aend, l->rotation, l->max_size, l->state, l->type);
    }
    for (int i = 0; i < L.stripe_count; i++) {
        const light_stripe_t *s = &L.stripes[i];
        fprintf(f, "stripe %.3f %.3f %.3f %.3f %d %d %d %d %d %d %.3f %d\n", s->x, s->y, s->w, s->h, s->rot, s->height, s->r,
                s->g, s->b, s->day, s->alpha, s->state);
    }
}
