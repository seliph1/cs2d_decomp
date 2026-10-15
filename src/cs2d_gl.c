#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "cs2d_gl.h"

/*
 * GL 3.3 core backend. opengl32.dll only exports GL 1.1, so every 2.0+/3.0+ entry point
 * (shaders, VBO/VAO, uniforms, FBO, glBlendFuncSeparate) is loaded from SDL_GL_GetProcAddress.
 * The fixed-function pipeline the renderer used is replaced by:
 *   - one shader: fragment = texture(u_tex, uv) * vertex colour, plus an optional alpha-test
 *     discard that stands in for glAlphaFunc;
 *   - one streaming VAO/VBO of triangles/lines that the batcher fills and flushes.
 * cs2d_light.c keeps using its own CPU matrix (xform); the projection here is the same
 * glOrtho(0, w, h, 0, -1, 1) the renderer always used (top-left origin, y down).
 */

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

/* --- GL 3.3 entry points (loaded at run time) --- */
static GLuint  (GLAPIENTRY *pCreateShader)(GLenum) = NULL;
static void    (GLAPIENTRY *pShaderSource)(GLuint, GLsizei, const char *const *, const GLint *) = NULL;
static void    (GLAPIENTRY *pCompileShader)(GLuint) = NULL;
static void    (GLAPIENTRY *pGetShaderiv)(GLuint, GLenum, GLint *) = NULL;
static void    (GLAPIENTRY *pGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, char *) = NULL;
static void    (GLAPIENTRY *pDeleteShader)(GLuint) = NULL;
static GLuint  (GLAPIENTRY *pCreateProgram)(void) = NULL;
static void    (GLAPIENTRY *pAttachShader)(GLuint, GLuint) = NULL;
static void    (GLAPIENTRY *pLinkProgram)(GLuint) = NULL;
static void    (GLAPIENTRY *pGetProgramiv)(GLuint, GLenum, GLint *) = NULL;
static void    (GLAPIENTRY *pGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, char *) = NULL;
static void    (GLAPIENTRY *pUseProgram)(GLuint) = NULL;
static void    (GLAPIENTRY *pDeleteProgram)(GLuint) = NULL;
static GLint   (GLAPIENTRY *pGetUniformLocation)(GLuint, const char *) = NULL;
static void    (GLAPIENTRY *pUniform1i)(GLint, GLint) = NULL;
static void    (GLAPIENTRY *pUniform1f)(GLint, GLfloat) = NULL;
static void    (GLAPIENTRY *pUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *) = NULL;
static void    (GLAPIENTRY *pGenBuffers)(GLsizei, GLuint *) = NULL;
static void    (GLAPIENTRY *pBindBuffer)(GLenum, GLuint) = NULL;
static void    (GLAPIENTRY *pBufferData)(GLenum, ptrdiff_t, const void *, GLenum) = NULL;
static void    (GLAPIENTRY *pBufferSubData)(GLenum, ptrdiff_t, ptrdiff_t, const void *) = NULL;
static void    (GLAPIENTRY *pDeleteBuffers)(GLsizei, const GLuint *) = NULL;
static void    (GLAPIENTRY *pGenVertexArrays)(GLsizei, GLuint *) = NULL;
static void    (GLAPIENTRY *pBindVertexArray)(GLuint) = NULL;
static void    (GLAPIENTRY *pDeleteVertexArrays)(GLsizei, const GLuint *) = NULL;
static void    (GLAPIENTRY *pVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) = NULL;
static void    (GLAPIENTRY *pEnableVertexAttribArray)(GLuint) = NULL;
static void    (GLAPIENTRY *pGenFramebuffers)(GLsizei, GLuint *) = NULL;
static void    (GLAPIENTRY *pBindFramebuffer)(GLenum, GLuint) = NULL;
static void    (GLAPIENTRY *pFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = NULL;
static GLenum  (GLAPIENTRY *pCheckFramebufferStatus)(GLenum) = NULL;
static void    (GLAPIENTRY *pDeleteFramebuffers)(GLsizei, const GLuint *) = NULL;
static void    (GLAPIENTRY *pBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = NULL;

#define CS2D_GL_BATCH_MAX 65536
static cs2d_gl_vertex_t s_verts[CS2D_GL_BATCH_MAX];

/* Batcher statistics (CS2D_DRAWCALLS): what limits batching on a frame, see cs2d_gl.h */
static cs2d_gl_stats_t s_stats;

static struct {
    bool     loaded;
    GLuint   prog;
    GLuint   vao, vbo;
    GLuint   white_tex;
    GLint    u_proj, u_tex, u_alpha_ref;
    /* batch */
    GLenum   mode;
    GLuint   tex;                 /* texture of the pending batch (0 = white) */
    int      count;
    /* target / projection */
    int      vp_w, vp_h;
    float    proj[16];
    /* sticky state */
    int      blend;
    float    alpha_ref;
    /* saved target for bind/unbind */
    int      saved_viewport[4];
    int      saved_vp_w, saved_vp_h;
    float    saved_proj[16];
} S;

/* --- helpers --------------------------------------------------------------------------- */

static void gl3_set_ortho(int w, int h, float out[16])
{
    /* glOrtho(0, w, h, 0, -1, 1) column-major: (0, 0) is the top-left, y grows down */
    float m[16] = {
        2.0f / (float)w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / (float)h, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    memcpy(out, m, sizeof(m));
}

static GLuint gl3_compile(GLenum type, const char *src)
{
    GLuint sh = pCreateShader(type);
    pShaderSource(sh, 1, &src, NULL);
    pCompileShader(sh);
    GLint ok = 0;
    pGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        pGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "[cs2d_gl] %s shader compile failed: %s\n",
                (type == GL_VERTEX_SHADER) ? "vertex" : "fragment", log);
        pDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* GLSL 3.30 on the desktop; WebGL2 takes the same shaders as GLSL ES 3.00 with an explicit precision */
#ifdef __EMSCRIPTEN__
#define CS2D_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define CS2D_GLSL_HEADER "#version 330 core\n"
#endif

static bool gl3_build_program(void)
{
    static const char *vs_src =
        CS2D_GLSL_HEADER
        "layout(location = 0) in vec2 a_pos;\n"
        "layout(location = 1) in vec2 a_uv;\n"
        "layout(location = 2) in vec4 a_col;\n"
        "uniform mat4 u_proj;\n"
        "out vec2 v_uv;\n"
        "out vec4 v_col;\n"
        "void main() {\n"
        "    v_uv = a_uv;\n"
        "    v_col = a_col;\n"
        "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs_src =
        CS2D_GLSL_HEADER
        "in vec2 v_uv;\n"
        "in vec4 v_col;\n"
        "uniform sampler2D u_tex;\n"
        "uniform float u_alpha_ref;\n"
        "out vec4 o_col;\n"
        "void main() {\n"
        "    vec4 c = texture(u_tex, v_uv) * v_col;\n"
        "    if (u_alpha_ref >= 0.0 && c.a < u_alpha_ref) discard;\n"
        "    o_col = c;\n"
        "}\n";

    GLuint vs = gl3_compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = gl3_compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) pDeleteShader(vs);
        if (fs) pDeleteShader(fs);
        return false;
    }
    S.prog = pCreateProgram();
    pAttachShader(S.prog, vs);
    pAttachShader(S.prog, fs);
    pLinkProgram(S.prog);
    pDeleteShader(vs);
    pDeleteShader(fs);
    GLint ok = 0;
    pGetProgramiv(S.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        pGetProgramInfoLog(S.prog, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "[cs2d_gl] program link failed: %s\n", log);
        pDeleteProgram(S.prog);
        S.prog = 0;
        return false;
    }
    S.u_proj      = pGetUniformLocation(S.prog, "u_proj");
    S.u_tex       = pGetUniformLocation(S.prog, "u_tex");
    S.u_alpha_ref = pGetUniformLocation(S.prog, "u_alpha_ref");
    return true;
}

static void gl3_make_white_texture(void)
{
    const unsigned char px[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &S.white_tex);
    glBindTexture(GL_TEXTURE_2D, S.white_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void gl3_make_geometry(void)
{
    pGenVertexArrays(1, &S.vao);
    pGenBuffers(1, &S.vbo);
    pBindVertexArray(S.vao);
    pBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    pBufferData(GL_ARRAY_BUFFER, (ptrdiff_t)CS2D_GL_BATCH_MAX * (ptrdiff_t)sizeof(cs2d_gl_vertex_t), NULL, GL_STREAM_DRAW);
    pEnableVertexAttribArray(0);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(cs2d_gl_vertex_t),
                         (const void *)offsetof(cs2d_gl_vertex_t, x));
    pEnableVertexAttribArray(1);
    pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(cs2d_gl_vertex_t),
                         (const void *)offsetof(cs2d_gl_vertex_t, u));
    pEnableVertexAttribArray(2);
    pVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(cs2d_gl_vertex_t),
                         (const void *)offsetof(cs2d_gl_vertex_t, r));
    pBindBuffer(GL_ARRAY_BUFFER, 0);
    pBindVertexArray(0);
}

static void gl3_apply_blend(void)
{
    switch (S.blend) {
        case CS2D_GL_BLEND_NONE:
            glDisable(GL_BLEND);
            return;
        case CS2D_GL_BLEND_ADD:
            pBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            break;
        case CS2D_GL_BLEND_MULT:
            pBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE);
            break;
        default:
            pBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
    glEnable(GL_BLEND);
}

static void gl3_flush(void)
{
    if (S.count <= 0 || !S.loaded) {
        S.count = 0;
        return;
    }
    if (S.count > s_stats.max_batch) s_stats.max_batch = S.count;
    s_stats.draws++;
    s_stats.verts += (unsigned long long)S.count;
    pUseProgram(S.prog);
    pUniformMatrix4fv(S.u_proj, 1, GL_FALSE, S.proj);
    pUniform1i(S.u_tex, 0);
    pUniform1f(S.u_alpha_ref, S.alpha_ref);
    glBindTexture(GL_TEXTURE_2D, S.tex ? S.tex : S.white_tex);
    pBindVertexArray(S.vao);
    pBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    pBufferSubData(GL_ARRAY_BUFFER, 0, (ptrdiff_t)S.count * (ptrdiff_t)sizeof(cs2d_gl_vertex_t), s_verts);
    glDrawArrays(S.mode, 0, S.count);
    pBindVertexArray(0);
    pBindBuffer(GL_ARRAY_BUFFER, 0);
    S.count = 0;
}

static void gl3_set_texture_id(GLuint tex_id)
{
    if (tex_id == S.tex) return;
    if (S.count > 0) s_stats.tex_switches++;
    gl3_flush();
    S.tex = tex_id;
}

static void gl3_push(GLenum mode, const cs2d_gl_vertex_t *verts, int count)
{
    if (!S.loaded) return;
    if (S.mode != mode) {
        if (S.count > 0) s_stats.mode_switches++;
        gl3_flush();
        S.mode = mode;
    } else if (mode == GL_LINE_LOOP || mode == GL_LINE_STRIP || mode == GL_TRIANGLE_FAN || mode == GL_TRIANGLE_STRIP) {
        /* Connected primitives cannot be concatenated: two loops in one draw join into a single loop */
        gl3_flush();
    }
    if (S.count + count > CS2D_GL_BATCH_MAX) {
        s_stats.overflows++;
        gl3_flush();
    }
    memcpy(&s_verts[S.count], verts, (size_t)count * sizeof(cs2d_gl_vertex_t));
    S.count += count;
}

static void gl3_vtx(cs2d_gl_vertex_t *v, float x, float y, float u, float vv, float r, float g, float b, float a)
{
    v->x = x; v->y = y; v->u = u; v->v = vv;
    v->r = r; v->g = g; v->b = b; v->a = a;
}

static void gl3_quad_tris(const cs2d_gl_vertex_t q[4])
{
    cs2d_gl_vertex_t tri[6];
    tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[2];
    tri[3] = q[0]; tri[4] = q[2]; tri[5] = q[3];
    gl3_push(GL_TRIANGLES, tri, 6);
}

/* --- lifecycle ------------------------------------------------------------------------- */

bool cs2d_gl_load(void)
{
    if (S.loaded) return true;
    if (!SDL_GL_GetCurrentContext()) return false;

#define CS2D_GL_LOAD(name) do { p##name = (void *)SDL_GL_GetProcAddress("gl" #name); \
        if (!p##name) fprintf(stderr, "[cs2d_gl] missing GL 3.3 entry point gl" #name "\n"); } while (0)
    CS2D_GL_LOAD(CreateShader);
    CS2D_GL_LOAD(ShaderSource);
    CS2D_GL_LOAD(CompileShader);
    CS2D_GL_LOAD(GetShaderiv);
    CS2D_GL_LOAD(GetShaderInfoLog);
    CS2D_GL_LOAD(DeleteShader);
    CS2D_GL_LOAD(CreateProgram);
    CS2D_GL_LOAD(AttachShader);
    CS2D_GL_LOAD(LinkProgram);
    CS2D_GL_LOAD(GetProgramiv);
    CS2D_GL_LOAD(GetProgramInfoLog);
    CS2D_GL_LOAD(UseProgram);
    CS2D_GL_LOAD(DeleteProgram);
    CS2D_GL_LOAD(GetUniformLocation);
    CS2D_GL_LOAD(Uniform1i);
    CS2D_GL_LOAD(Uniform1f);
    CS2D_GL_LOAD(UniformMatrix4fv);
    CS2D_GL_LOAD(GenBuffers);
    CS2D_GL_LOAD(BindBuffer);
    CS2D_GL_LOAD(BufferData);
    CS2D_GL_LOAD(BufferSubData);
    CS2D_GL_LOAD(DeleteBuffers);
    CS2D_GL_LOAD(GenVertexArrays);
    CS2D_GL_LOAD(BindVertexArray);
    CS2D_GL_LOAD(DeleteVertexArrays);
    CS2D_GL_LOAD(VertexAttribPointer);
    CS2D_GL_LOAD(EnableVertexAttribArray);
    CS2D_GL_LOAD(GenFramebuffers);
    CS2D_GL_LOAD(BindFramebuffer);
    CS2D_GL_LOAD(FramebufferTexture2D);
    CS2D_GL_LOAD(CheckFramebufferStatus);
    CS2D_GL_LOAD(DeleteFramebuffers);
    CS2D_GL_LOAD(BlendFuncSeparate);
#undef CS2D_GL_LOAD

    if (!pCreateShader || !pShaderSource || !pCompileShader || !pGetShaderiv || !pCreateProgram ||
        !pAttachShader || !pLinkProgram || !pUseProgram || !pGetUniformLocation || !pUniformMatrix4fv ||
        !pGenBuffers || !pBindBuffer || !pBufferData || !pBufferSubData || !pGenVertexArrays ||
        !pBindVertexArray || !pVertexAttribPointer || !pEnableVertexAttribArray ||
        !pGenFramebuffers || !pBindFramebuffer || !pFramebufferTexture2D || !pCheckFramebufferStatus ||
        !pBlendFuncSeparate) {
        return false;
    }

    if (!gl3_build_program()) return false;
    gl3_make_white_texture();
    gl3_make_geometry();

    S.mode      = GL_TRIANGLES;
    S.tex       = 0;
    S.count     = 0;
    S.blend     = CS2D_GL_BLEND_ALPHA;
    S.alpha_ref = -1.0f;
    S.vp_w = 854; S.vp_h = 480;
    gl3_set_ortho(S.vp_w, S.vp_h, S.proj);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    gl3_apply_blend();

    S.loaded = true;
    return true;
}

bool cs2d_gl_ready(void)
{
    return S.loaded;
}

void cs2d_gl_shutdown(void)
{
    if (!S.loaded && !S.prog) return;
    if (SDL_GL_GetCurrentContext()) {
        if (S.vbo) pDeleteBuffers(1, &S.vbo);
        if (S.vao) pDeleteVertexArrays(1, &S.vao);
        if (S.prog) pDeleteProgram(S.prog);
        if (S.white_tex) glDeleteTextures(1, &S.white_tex);
    }
    S.vbo = S.vao = S.prog = S.white_tex = 0;
    S.loaded = false;
    S.count = 0;
}

/* --- frame / render target ------------------------------------------------------------- */

void cs2d_gl_viewport(int width, int height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    S.vp_w = width;
    S.vp_h = height;
    if (S.loaded) glViewport(0, 0, width, height);
    gl3_set_ortho(width, height, S.proj);
}

void cs2d_gl_begin(void)
{
    S.count = 0;
    S.mode = GL_TRIANGLES;
    if (S.loaded) {
        glViewport(0, 0, S.vp_w, S.vp_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

void cs2d_gl_end(void)
{
    gl3_flush();
}

void cs2d_gl_flush(void)
{
    gl3_flush();
}

void cs2d_gl_stats_reset(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

cs2d_gl_stats_t cs2d_gl_stats_read(void)
{
    return s_stats;
}

void cs2d_gl_bind_target(unsigned int fbo, int width, int height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    gl3_flush();
    if (S.loaded) {
        GLint vp[4] = { 0, 0, S.vp_w, S.vp_h };
        glGetIntegerv(GL_VIEWPORT, vp);
        S.saved_viewport[0] = vp[0]; S.saved_viewport[1] = vp[1];
        S.saved_viewport[2] = vp[2]; S.saved_viewport[3] = vp[3];
        pBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }
    memcpy(S.saved_proj, S.proj, sizeof(S.proj));
    S.saved_vp_w = S.vp_w;
    S.saved_vp_h = S.vp_h;
    S.vp_w = width;
    S.vp_h = height;
    gl3_set_ortho(width, height, S.proj);
}

void cs2d_gl_unbind_target(void)
{
    gl3_flush();
    if (S.loaded) {
        pBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(S.saved_viewport[0], S.saved_viewport[1], S.saved_viewport[2], S.saved_viewport[3]);
    }
    S.vp_w = S.saved_vp_w;
    S.vp_h = S.saved_vp_h;
    memcpy(S.proj, S.saved_proj, sizeof(S.proj));
}

/* --- sticky state ---------------------------------------------------------------------- */

void cs2d_gl_set_texture(unsigned int tex_id)
{
    gl3_set_texture_id(tex_id);
}

void cs2d_gl_set_blend(int mode)
{
    if (mode == S.blend) return;
    gl3_flush();
    S.blend = mode;
    if (S.loaded) gl3_apply_blend();
}

void cs2d_gl_set_alpha_ref(float ref)
{
    if (ref == S.alpha_ref) return;
    gl3_flush();
    S.alpha_ref = ref;
}

void cs2d_gl_scissor(int x, int y, int w, int h)
{
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    gl3_flush();
    if (!S.loaded) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, S.vp_h - (y + h), w, h);
}

void cs2d_gl_scissor_off(void)
{
    gl3_flush();
    if (S.loaded) glDisable(GL_SCISSOR_TEST);
}

/* --- depth shadow pass ----------------------------------------------------------------- */

void cs2d_gl_shadow_mark_begin(void)
{
    if (!S.loaded) return;
    gl3_flush();
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    cs2d_gl_set_alpha_ref(0.5f);
}

void cs2d_gl_shadow_mark_end(void)
{
    if (!S.loaded) return;
    gl3_flush();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    cs2d_gl_set_alpha_ref(-1.0f);
}

void cs2d_gl_shadow_fill(float x, float y, float w, float h, float r, float g, float b, float a)
{
    gl3_flush();
    cs2d_gl_rect(x, y, w, h, r, g, b, a);
    gl3_flush();
    if (!S.loaded) return;
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_DEPTH_TEST);
}

/* --- geometry -------------------------------------------------------------------------- */

void cs2d_gl_push_triangles(const cs2d_gl_vertex_t *verts, int count)
{
    if (count > 0) gl3_push(GL_TRIANGLES, verts, count);
}

void cs2d_gl_push_lines(const cs2d_gl_vertex_t *verts, int count)
{
    if (count > 0) gl3_push(GL_LINES, verts, count);
}

void cs2d_gl_push_line_loop(const cs2d_gl_vertex_t *verts, int count)
{
    if (count > 0) gl3_push(GL_LINE_LOOP, verts, count);
}

void cs2d_gl_push_fan(const cs2d_gl_vertex_t *verts, int count)
{
    if (count > 0) gl3_push(GL_TRIANGLE_FAN, verts, count);
}

void cs2d_gl_quad(unsigned int tex_id, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  float r, float g, float b, float a)
{
    gl3_set_texture_id(tex_id);
    cs2d_gl_vertex_t q[4];
    gl3_vtx(&q[0], x,     y,     u0, v0, r, g, b, a);
    gl3_vtx(&q[1], x + w, y,     u1, v0, r, g, b, a);
    gl3_vtx(&q[2], x + w, y + h, u1, v1, r, g, b, a);
    gl3_vtx(&q[3], x,     y + h, u0, v1, r, g, b, a);
    gl3_quad_tris(q);
}

void cs2d_gl_quad_rot(unsigned int tex_id, float cx, float cy, float w, float h, float u0, float v0, float u1, float v1,
                      float rot, float r, float g, float b, float a)
{
    gl3_set_texture_id(tex_id);
    float hw = w * 0.5f, hh = h * 0.5f;
    float rad = rot * 0.017453292519943295f;
    float cs = cosf(rad), sn = sinf(rad);
    float lx[4] = { -hw,  hw, hw, -hw };
    float ly[4] = { -hh, -hh, hh,  hh };
    cs2d_gl_vertex_t q[4];
    for (int i = 0; i < 4; i++) {
        float px = cx + lx[i] * cs - ly[i] * sn;
        float py = cy + lx[i] * sn + ly[i] * cs;
        float u = (i == 0 || i == 3) ? u0 : u1;
        float v = (i == 0 || i == 1) ? v0 : v1;
        gl3_vtx(&q[i], px, py, u, v, r, g, b, a);
    }
    gl3_quad_tris(q);
}

void cs2d_gl_quad_corners(unsigned int tex_id, const float xy[8], const float uv[8],
                          float r, float g, float b, float a)
{
    gl3_set_texture_id(tex_id);
    cs2d_gl_vertex_t q[4];
    for (int i = 0; i < 4; i++) {
        gl3_vtx(&q[i], xy[i * 2], xy[i * 2 + 1], uv[i * 2], uv[i * 2 + 1], r, g, b, a);
    }
    gl3_quad_tris(q);
}

void cs2d_gl_rect(float x, float y, float w, float h, float r, float g, float b, float a)
{
    gl3_set_texture_id(0);
    cs2d_gl_vertex_t q[4];
    gl3_vtx(&q[0], x,     y,     0.0f, 0.0f, r, g, b, a);
    gl3_vtx(&q[1], x + w, y,     0.0f, 0.0f, r, g, b, a);
    gl3_vtx(&q[2], x + w, y + h, 0.0f, 0.0f, r, g, b, a);
    gl3_vtx(&q[3], x,     y + h, 0.0f, 0.0f, r, g, b, a);
    gl3_quad_tris(q);
}

void cs2d_gl_rect_grad(float x, float y, float w, float h,
                       float r0, float g0, float b0, float a0,
                       float r1, float g1, float b1, float a1)
{
    gl3_set_texture_id(0);
    cs2d_gl_vertex_t q[4];
    gl3_vtx(&q[0], x,     y,     0.0f, 0.0f, r0, g0, b0, a0);
    gl3_vtx(&q[1], x + w, y,     0.0f, 0.0f, r0, g0, b0, a0);
    gl3_vtx(&q[2], x + w, y + h, 0.0f, 0.0f, r1, g1, b1, a1);
    gl3_vtx(&q[3], x,     y + h, 0.0f, 0.0f, r1, g1, b1, a1);
    gl3_quad_tris(q);
}

void cs2d_gl_line(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    gl3_set_texture_id(0);
    cs2d_gl_vertex_t v[2];
    gl3_vtx(&v[0], x0, y0, 0.0f, 0.0f, r, g, b, a);
    gl3_vtx(&v[1], x1, y1, 0.0f, 0.0f, r, g, b, a);
    gl3_push(GL_LINES, v, 2);
}

void cs2d_gl_circle(float cx, float cy, float radius, float r, float g, float b, float a, bool fill)
{
    gl3_set_texture_id(0);
    const int segments = 24;
    cs2d_gl_vertex_t v[26];
    int n = 0;
    if (fill) gl3_vtx(&v[n++], cx, cy, 0.0f, 0.0f, r, g, b, a);
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * 3.1415926f * (float)i / (float)segments;
        gl3_vtx(&v[n++], cx + cosf(theta) * radius, cy + sinf(theta) * radius, 0.0f, 0.0f, r, g, b, a);
    }
    if (fill) gl3_push(GL_TRIANGLE_FAN, v, n);
    else      gl3_push(GL_LINE_LOOP, v, n);
}
