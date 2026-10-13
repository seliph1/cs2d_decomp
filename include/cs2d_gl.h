#ifndef CS2D_GL_H
#define CS2D_GL_H

/*
 * GL 3.3 core backend (Phase 3 of the GL 1.x / GDI+ removal; docs/agents/OPEN_ISSUES.md).
 *
 * The renderer used to be 100% fixed-function OpenGL 1.x: glBegin/glEnd, glOrtho + the
 * matrix stack, glEnable(GL_TEXTURE_2D), glAlphaFunc, GL_QUADS. None of that exists in a
 * core profile, so this module owns the whole drawing path:
 *   - it loads the GL 3.3 entry points (opengl32.dll only exports GL 1.1, so glCreateShader,
 *     glGenBuffers, glGenVertexArrays, glUniform*, FBO and glBlendFuncSeparate all have to
 *     come from SDL_GL_GetProcAddress);
 *   - it keeps one shader (texture * vertex colour, with an optional alpha-test discard that
 *     replaces glAlphaFunc) and one streaming VAO/VBO of triangles;
 *   - it exposes a small batcher: geometry is appended as vertices and flushed on a state
 *     change (texture, blend, alpha reference, scissor, render target).
 *
 * Every function is a safe no-op before cs2d_gl_load() succeeds (the regression suite runs
 * with no GL context: tests/test_cs2d.c drives cs2d_light through its trace hook).
 */

#include <stdbool.h>

/*
 * Sticky blend modes. The RGB factors are the ones the fixed-function renderer used; the
 * alpha factors are the separate ones the original Max2D driver set (TSDLRenderMax2DDriver
 * SetBlend 0x14031afe0). The default framebuffer has no readable alpha, so they only matter
 * for the light engine's RGBA render targets.
 */
enum {
    CS2D_GL_BLEND_ALPHA = 0, /* SRC_ALPHA, ONE_MINUS_SRC_ALPHA (+ ONE, ONE_MINUS_SRC_ALPHA) */
    CS2D_GL_BLEND_ADD   = 1, /* SRC_ALPHA, ONE                 (+ ZERO, ONE)                 */
    CS2D_GL_BLEND_MULT  = 2, /* ZERO, SRC_COLOR                (+ ZERO, ONE)                 */
    CS2D_GL_BLEND_NONE  = 3  /* blending disabled                                            */
};

/* One batched vertex: pixel position, texture coordinate and RGBA colour. */
typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} cs2d_gl_vertex_t;

/* --- lifecycle --- */
/* Loads the entry points and builds the shader/program; false when no 3.3 context is current */
bool cs2d_gl_load(void);
/* The backend is loaded and can draw */
bool cs2d_gl_ready(void);
void cs2d_gl_shutdown(void);

/* --- frame / render target --- */
/* Sets the projection for the default framebuffer (top-left origin, y down, like glOrtho(0,w,h,0,-1,1)) */
void cs2d_gl_viewport(int width, int height);
/* Starts a frame on the default framebuffer and resets the batch */
void cs2d_gl_begin(void);
/* Flushes the batch */
void cs2d_gl_end(void);
/* Submits the pending vertices without touching the target */
void cs2d_gl_flush(void);

/* Binds a colour-only FBO of size (width, height); fbo == 0 is the default framebuffer */
void cs2d_gl_bind_target(unsigned int fbo, int width, int height);
/* Restores the target/projection saved by the matching cs2d_gl_bind_target */
void cs2d_gl_unbind_target(void);

/* --- sticky state (a change flushes the batch) --- */
/* 0 keeps a 1x1 white texture bound, so a textured draw with no texture equals a solid colour */
void cs2d_gl_set_texture(unsigned int tex_id);
void cs2d_gl_set_blend(int mode);
/* Discards fragments under ref (replaces glAlphaFunc); < 0 disables the test */
void cs2d_gl_set_alpha_ref(float ref);
void cs2d_gl_scissor(int x, int y, int w, int h);
void cs2d_gl_scissor_off(void);

/* --- depth shadow pass (cs2d_render_player_light_shadow / the player day-shadow silhouette) ---
 * mark: colour writes off, depth test GL_ALWAYS with writes on, alpha reference 0.5, so the
 * sprite's opaque texels only mark the depth buffer; fill: a black quad kept by GL_EQUAL. */
void cs2d_gl_shadow_mark_begin(void);
void cs2d_gl_shadow_mark_end(void);
void cs2d_gl_shadow_fill(float x, float y, float w, float h, float r, float g, float b, float a);

/* --- geometry --- */
/* Low-level primitive pushes; they select the texture/blend already set and flush on a mode change */
void cs2d_gl_push_triangles(const cs2d_gl_vertex_t *verts, int count);
void cs2d_gl_push_lines(const cs2d_gl_vertex_t *verts, int count);
void cs2d_gl_push_line_loop(const cs2d_gl_vertex_t *verts, int count);
void cs2d_gl_push_fan(const cs2d_gl_vertex_t *verts, int count);

/* --- batch statistics (draw calls) ------------------------------------------------------
 * A "draw call" is one submitted glDrawArrays, i.e. one fragment of the batch. The batcher
 * splits on a texture switch, a primitive-mode switch (triangles/lines/fan), an alpha-reference
 * change or a full 65536-vertex batch, so these counters say what limits batching on a frame.
 * They are only read by CS2D_DRAWCALLS in main_client.c; nothing in the renderer depends on them. */
typedef struct {
    unsigned draws;          /* submitted batches (glDrawArrays calls) */
    unsigned tex_switches;   /* texture changes that flushed the pending batch */
    unsigned mode_switches;  /* primitive-mode changes that flushed the pending batch */
    unsigned overflows;      /* batches flushed because they were full */
    unsigned long long verts;/* vertices submitted */
    int      max_batch;      /* largest batch (in vertices) seen since the reset */
} cs2d_gl_stats_t;
void cs2d_gl_stats_reset(void);
cs2d_gl_stats_t cs2d_gl_stats_read(void);

/* Axis-aligned textured quad: (x, y) top-left, (w, h) size, UV rectangle (u0, v0)-(u1, v1) */
void cs2d_gl_quad(unsigned int tex_id, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  float r, float g, float b, float a);
/* Textured quad centred on (cx, cy), rotated by rot degrees (clockwise, screen space) */
void cs2d_gl_quad_rot(unsigned int tex_id, float cx, float cy, float w, float h, float u0, float v0, float u1, float v1,
                      float rot, float r, float g, float b, float a);
/* Four arbitrary corners with their UVs, in TL, TR, BR, BL order (tile frames, particles) */
void cs2d_gl_quad_corners(unsigned int tex_id, const float xy[8], const float uv[8],
                          float r, float g, float b, float a);
/* Solid rectangle and vertical gradient (top colour then bottom colour) */
void cs2d_gl_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void cs2d_gl_rect_grad(float x, float y, float w, float h,
                       float r0, float g0, float b0, float a0,
                       float r1, float g1, float b1, float a1);
void cs2d_gl_line(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
void cs2d_gl_circle(float cx, float cy, float radius, float r, float g, float b, float a, bool fill);

#endif /* CS2D_GL_H */
