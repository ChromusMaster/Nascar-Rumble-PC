/* OpenGL 3.3 (core profile) video backend - Windows, Linux and other SDL2
 * platforms.
 *
 * Same pipeline as the Direct3D backends (see win32/vb_d3d11.c): the render
 * command list is rasterised into a high-resolution VRAM render target
 * (internal resolution x supersampling) with the GLSL port of the HLSL
 * shaders, the displayed area is resolved with a box filter (the
 * anti-aliasing step) and presented with the scaling filters, effects and
 * colour grading. GL entry points are resolved through SDL_GL_GetProcAddress,
 * so no GL loader library is needed. */
#include "../nr_sdl.h"
#ifdef NR_SDL3
#include <SDL3/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../video.h"
#include "../../render/gpu_replay.h"
#include "../gl_shaders.h"

/* GL 2.0+ entry points (GL 1.1 ones come from the system GL header/library
 * on every platform, but are also resolved here for uniformity). */
#define NR_GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader) X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(PFNGLUNIFORM4FVPROC, Uniform4fv) \
    X(PFNGLUNIFORM1IPROC, Uniform1i) X(PFNGLGENBUFFERSPROC, GenBuffers) X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer) \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate) \
    X(PFNGLBLENDCOLORPROC, BlendColor) X(PFNGLACTIVETEXTUREPROC, ActiveTexture)

typedef void(APIENTRY *pfn_glGenTextures)(GLsizei, GLuint *);
typedef void(APIENTRY *pfn_glDeleteTextures)(GLsizei, const GLuint *);
typedef void(APIENTRY *pfn_glBindTexture)(GLenum, GLuint);
typedef void(APIENTRY *pfn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void(APIENTRY *pfn_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void(APIENTRY *pfn_glTexParameteri)(GLenum, GLenum, GLint);
typedef void(APIENTRY *pfn_glPixelStorei)(GLenum, GLint);
typedef void(APIENTRY *pfn_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *pfn_glScissor)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRY *pfn_glEnable)(GLenum);
typedef void(APIENTRY *pfn_glDisable)(GLenum);
typedef void(APIENTRY *pfn_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY *pfn_glClear)(GLbitfield);
typedef void(APIENTRY *pfn_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void(APIENTRY *pfn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void(APIENTRY *pfn_glGetIntegerv)(GLenum, GLint *);
typedef const GLubyte *(APIENTRY *pfn_glGetString)(GLenum);
typedef GLenum(APIENTRY *pfn_glGetError)(void);
typedef void(APIENTRY *pfn_glFinish)(void);

#define NR_GL11_FUNCS(X) \
    X(pfn_glGenTextures, GenTextures) X(pfn_glDeleteTextures, DeleteTextures) X(pfn_glBindTexture, BindTexture) \
    X(pfn_glTexImage2D, TexImage2D) X(pfn_glTexSubImage2D, TexSubImage2D) X(pfn_glTexParameteri, TexParameteri) \
    X(pfn_glPixelStorei, PixelStorei) X(pfn_glViewport, Viewport) X(pfn_glScissor, Scissor) X(pfn_glEnable, Enable) \
    X(pfn_glDisable, Disable) X(pfn_glClearColor, ClearColor) X(pfn_glClear, Clear) X(pfn_glDrawArrays, DrawArrays) \
    X(pfn_glReadPixels, ReadPixels) X(pfn_glGetIntegerv, GetIntegerv) X(pfn_glGetString, GetString) \
    X(pfn_glGetError, GetError) X(pfn_glFinish, Finish)

typedef struct {
#define DECL(T, N) T N;
    NR_GL_FUNCS(DECL)
    NR_GL11_FUNCS(DECL)
#undef DECL
} glfn;

typedef struct {
    GLuint tex, fbo;
    int w, h;
} gtex;

enum { GP_PRIM, GP_UPLOAD, GP_COPY, GP_FILL, GP_RESOLVE, GP_PRESENT, GP_OVERLAY, GP_COUNT };

typedef struct {
    SDL_Window *win;
    SDL_GLContext ctx;
    glfn gl;
    GLuint prog[GP_COUNT];
    GLint uloc[GP_COUNT], tloc[GP_COUNT];
    GLuint vao, vbo;
    gtex vram, vread, internal, upload, picture, overlay;
    int sx, sy, S, ax, ay;
    int win_w, win_h;
    int max_tex;
    nr_gfx_settings s;
    nr_gcb cbd;
    int cur_prog;
    nr_gpu_replay_state rp;
    nr_gpu_ops ops;
} glb;

#define GL (d->gl)

/* ------------------------------------------------------------ resources */

static void t_free(glb *d, gtex *t)
{
    if (t->fbo) GL.DeleteFramebuffers(1, &t->fbo);
    if (t->tex) GL.DeleteTextures(1, &t->tex);
    memset(t, 0, sizeof(*t));
}

/* RGBA8 texture, nearest sampling; with an FBO when it is a render target */
static bool t_make(glb *d, gtex *t, int w, int h, bool target)
{
    if (t->tex && t->w == w && t->h == h && (!target || t->fbo)) return true;
    t_free(d, t);
    GL.GetError();
    GL.GenTextures(1, &t->tex);
    GL.BindTexture(GL_TEXTURE_2D, t->tex);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (GL.GetError() != GL_NO_ERROR) { t_free(d, t); return false; }
    if (target) {
        GL.GenFramebuffers(1, &t->fbo);
        GL.BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
        if (GL.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
            t_free(d, t);
            return false;
        }
        GL.ClearColor(0, 0, 0, 0);
        GL.Disable(GL_SCISSOR_TEST);
        GL.Clear(GL_COLOR_BUFFER_BIT);
        GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    t->w = w; t->h = h;
    return true;
}

/* ------------------------------------------------------------ drawing */

/* off-screen targets are addressed top-down (see gl_shaders.h) */
static void set_target(glb *d, const gtex *t)
{
    if (t) {
        GL.BindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        GL.Viewport(0, 0, t->w, t->h);
        d->cbd.target[0] = (float)t->w; d->cbd.target[1] = (float)t->h; d->cbd.target[3] = 0;
    } else {
        GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
        GL.Viewport(0, 0, d->win_w, d->win_h);
        d->cbd.target[0] = (float)d->win_w; d->cbd.target[1] = (float)d->win_h; d->cbd.target[3] = 1;
    }
    d->cbd.target[2] = 0;
}

static void scissor(glb *d, int x0, int y0, int x1, int y1)
{
    GL.Enable(GL_SCISSOR_TEST);
    if (d->cbd.target[3] > 0.5f) GL.Scissor(x0, d->win_h - y1, x1 - x0, y1 - y0);   /* window: bottom-up */
    else GL.Scissor(x0, y0, x1 - x0, y1 - y0);
}

static void use(glb *d, int prog, GLuint tex)
{
    if (d->cur_prog != prog) { GL.UseProgram(d->prog[prog]); d->cur_prog = prog; }
    GL.Uniform4fv(d->uloc[prog], 11, (const GLfloat *)&d->cbd);
    GL.ActiveTexture(GL_TEXTURE0);
    GL.BindTexture(GL_TEXTURE_2D, tex);
}

/* blend: -1 none, 0..3 console semi-transparency modes, 4 alpha (overlay) */
static void blend(glb *d, int mode)
{
    if (mode < 0) { GL.Disable(GL_BLEND); return; }
    GL.Enable(GL_BLEND);
    switch (mode) {
    case 0: /* B/2 + F/2 */
        GL.BlendColor(0.5f, 0.5f, 0.5f, 0.5f);
        GL.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        GL.BlendFuncSeparate(GL_CONSTANT_COLOR, GL_CONSTANT_COLOR, GL_ONE, GL_ZERO);
        break;
    case 1: /* B + F */
        GL.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        GL.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    case 2: /* B - F */
        GL.BlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        GL.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    case 3: /* B + F/4 */
        GL.BlendColor(0.25f, 0.25f, 0.25f, 0.25f);
        GL.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        GL.BlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
        break;
    default: /* overlay */
        GL.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        break;
    }
}

static void draw_verts(glb *d, const nr_gvtx *v, int n)
{
    GL.BindBuffer(GL_ARRAY_BUFFER, d->vbo);
    GL.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)n * sizeof(nr_gvtx)), v, GL_STREAM_DRAW);
    GL.DrawArrays(GL_TRIANGLES, 0, n);
}

static void quad_draw(glb *d, int prog, GLuint tex, int x0, int y0, int x1, int y1)
{
    nr_gvtx q[6];
    use(d, prog, tex);
    blend(d, -1);
    scissor(d, x0, y0, x1, y1);
    nr_gpu_quad(q, (float)x0, (float)y0, (float)x1, (float)y1, 0, 0, 0, 0);
    draw_verts(d, q, 6);
}

/* ------------------------------------------------------------ replay ops */

static void op_draw(void *ctx, const nr_gvtx *v, int n, int pass, int mode, const int sc[4])
{
    glb *d = (glb *)ctx;
    d->cbd.pass[0] = (float)pass;
    use(d, GP_PRIM, d->vread.tex);
    blend(d, pass ? (mode & 3) : -1);
    scissor(d, sc[0], sc[1], sc[2], sc[3]);
    draw_verts(d, v, n);
}

static void op_upload(void *ctx, int x, int y, int w, int h, const u32 *rgba)
{
    glb *d = (glb *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    GL.BindTexture(GL_TEXTURE_2D, d->upload.tex);
    GL.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GL.PixelStorei(GL_UNPACK_ROW_LENGTH, w);
    GL.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GL.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    quad_draw(d, GP_UPLOAD, d->upload.tex, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy);
}

static void op_fill(void *ctx, int x, int y, int w, int h, const float col[4])
{
    glb *d = (glb *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    memcpy(d->cbd.color, col, sizeof(d->cbd.color));
    quad_draw(d, GP_FILL, 0, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy);
}

static void op_copy(void *ctx, int sx, int sy, int dx, int dy, int w, int h)
{
    glb *d = (glb *)ctx;
    int cw = w, ch = h;
    if (dx + cw > 1024) cw = 1024 - dx;
    if (dy + ch > 512) ch = 512 - dy;
    if (sx + cw > 1024) cw = 1024 - sx;
    if (sy + ch > 512) ch = 512 - sy;
    if (cw <= 0 || ch <= 0) return;
    d->cbd.quad[0] = (float)(dx * d->sx); d->cbd.quad[1] = (float)(dy * d->sy);
    d->cbd.quad[2] = (float)(sx * d->sx); d->cbd.quad[3] = (float)(sy * d->sy);
    quad_draw(d, GP_COPY, d->vread.tex, dx * d->sx, dy * d->sy, (dx + cw) * d->sx, (dy + ch) * d->sy);
}

static void op_refresh(void *ctx, int x0, int y0, int x1, int y1)
{
    glb *d = (glb *)ctx;
    GL.Disable(GL_SCISSOR_TEST);      /* blits honour the scissor test */
    GL.BindFramebuffer(GL_READ_FRAMEBUFFER, d->vram.fbo);
    GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, d->vread.fbo);
    GL.BlitFramebuffer(x0 * d->sx, y0 * d->sy, x1 * d->sx, y1 * d->sy, x0 * d->sx, y0 * d->sy, x1 * d->sx, y1 * d->sy,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL.BindFramebuffer(GL_FRAMEBUFFER, d->vram.fbo);
}

/* ------------------------------------------------------------ setup */

static GLuint compile(glb *d, GLenum type, const char *a, const char *b, char *err, size_t n)
{
    const char *src[2] = { a, b ? b : "" };
    GLuint s = GL.CreateShader(type);
    GLint ok = 0;
    GL.ShaderSource(s, 2, src, NULL);
    GL.CompileShader(s);
    GL.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[600] = "";
        GL.GetShaderInfoLog(s, sizeof(log), NULL, log);
        snprintf(err, n, "GLSL compile error: %s", log);
        GL.DeleteShader(s);
        return 0;
    }
    return s;
}

static bool make_program(glb *d, int idx, const char *entry, char *err, size_t n)
{
    char tail[96];
    GLuint vs, fs, p;
    GLint ok = 0;
    snprintf(tail, sizeof(tail), "void main() { o_col = %s(); }\n", entry);
    {
        size_t l1 = strlen(k_glsl_fs_common), l2 = strlen(tail);
        char *full = (char *)malloc(l1 + l2 + 1);
        if (!full) return false;
        memcpy(full, k_glsl_fs_common, l1);
        memcpy(full + l1, tail, l2 + 1);
        fs = compile(d, GL_FRAGMENT_SHADER, full, NULL, err, n);
        free(full);
    }
    if (!fs) return false;
    vs = compile(d, GL_VERTEX_SHADER, k_glsl_vs, NULL, err, n);
    if (!vs) { GL.DeleteShader(fs); return false; }
    p = GL.CreateProgram();
    GL.AttachShader(p, vs);
    GL.AttachShader(p, fs);
    GL.LinkProgram(p);
    GL.DeleteShader(vs);
    GL.DeleteShader(fs);
    GL.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[600] = "";
        GL.GetProgramInfoLog(p, sizeof(log), NULL, log);
        snprintf(err, n, "GLSL link error (%s): %s", entry, log);
        GL.DeleteProgram(p);
        return false;
    }
    d->prog[idx] = p;
    d->uloc[idx] = GL.GetUniformLocation(p, "U");
    d->tloc[idx] = GL.GetUniformLocation(p, "t0");
    GL.UseProgram(p);
    if (d->tloc[idx] >= 0) GL.Uniform1i(d->tloc[idx], 0);
    return true;
}

static bool gl_configure(nr_vbackend *b, const nr_gfx_settings *s);

static bool gl_init(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t n)
{
    nr_gfx_settings fit;
    glb *d;
    int i;
    static const char *const entries[GP_COUNT] = { "ps_prim", "ps_upload", "ps_copy", "ps_fill", "ps_resolve", "ps_present", "ps_overlay" };
    if (!window) { snprintf(err, n, "no window"); return false; }
    d = (glb *)calloc(1, sizeof(glb));
    if (!d) return false;
    b->impl = d;
    d->win = (SDL_Window *)window;
    d->cur_prog = -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    d->ctx = SDL_GL_CreateContext(d->win);
    if (!d->ctx) { snprintf(err, n, "OpenGL 3.3 core context: %s", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(d->win, d->ctx);
#define LOAD(T, N) d->gl.N = (T)SDL_GL_GetProcAddress("gl" #N); if (!d->gl.N) { snprintf(err, n, "missing OpenGL function gl%s", #N); return false; }
    NR_GL_FUNCS(LOAD)
    NR_GL11_FUNCS(LOAD)
#undef LOAD
    for (i = 0; i < GP_COUNT; i++) if (!make_program(d, i, entries[i], err, n)) return false;
    GL.GenVertexArrays(1, &d->vao);
    GL.BindVertexArray(d->vao);
    GL.GenBuffers(1, &d->vbo);
    GL.BindBuffer(GL_ARRAY_BUFFER, d->vbo);
    {
        static const int comps[7] = { 2, 4, 2, 4, 4, 4, 4 }, offs[7] = { 0, 8, 24, 32, 48, 64, 80 };
        for (i = 0; i < 7; i++) {
            GL.EnableVertexAttribArray((GLuint)i);
            GL.VertexAttribPointer((GLuint)i, comps[i], GL_FLOAT, GL_FALSE, sizeof(nr_gvtx), (const void *)(size_t)offs[i]);
        }
    }
    GL.GetIntegerv(GL_MAX_TEXTURE_SIZE, &d->max_tex);
    {
        int mx = d->max_tex / 1024, my = d->max_tex / 512;
        b->max_vram_scale = mx < my ? mx : my;
        if (b->max_vram_scale > 16) b->max_vram_scale = 16;
        if (b->max_vram_scale < 1) { snprintf(err, n, "GL_MAX_TEXTURE_SIZE %d too small", d->max_tex); return false; }
    }
    if (!t_make(d, &d->upload, 1024, 512, false)) { snprintf(err, n, "upload texture"); return false; }
    snprintf(b->description, sizeof(b->description), "OpenGL %s, %s", (const char *)GL.GetString(GL_VERSION),
             (const char *)GL.GetString(GL_RENDERER));
    nr_gpu_replay_init(&d->rp);
    d->ops.ctx = d;
    d->ops.draw = op_draw; d->ops.upload = op_upload; d->ops.fill = op_fill; d->ops.copy = op_copy; d->ops.refresh = op_refresh;
    nr_sdl_gl_size(d->win, &d->win_w, &d->win_h);
    if (!gl_configure(b, nr_gfx_fit(s, b->max_vram_scale, &fit))) { snprintf(err, n, "initial settings not supported (texture size limit %d)", d->max_tex); return false; }
    return true;
}

static bool gl_configure(nr_vbackend *b, const nr_gfx_settings *s)
{
    glb *d = (glb *)b->impl;
    int S = s->internal_scale < 1 ? 1 : s->internal_scale, ax, ay;
    nr_gfx_aa_grid(s->antialias, &ax, &ay);
    if (S * ax > b->max_vram_scale || S * ay > b->max_vram_scale) return false;
    if (!t_make(d, &d->vram, 1024 * S * ax, 512 * S * ay, true) || !t_make(d, &d->vread, 1024 * S * ax, 512 * S * ay, true)) {
        t_free(d, &d->vram); t_free(d, &d->vread);
        return false;
    }
    d->S = S; d->ax = ax; d->ay = ay; d->sx = S * ax; d->sy = S * ay;
    d->s = *s;
    d->cbd.vram[0] = (float)d->vram.w; d->cbd.vram[1] = (float)d->vram.h;
    d->cbd.vram[2] = (float)d->sx; d->cbd.vram[3] = (float)d->sy;
    d->cbd.pass[1] = s->true_color ? 1.0f : 0.0f;
    d->cbd.pass[2] = s->dithering ? 1.0f : 0.0f;
    SDL_GL_SetSwapInterval(s->vsync ? 1 : 0);
    return true;
}

static void gl_sync(nr_vbackend *b, const u16 *vram)
{
    glb *d = (glb *)b->impl;
    set_target(d, &d->vram);
    nr_gpu_sync(&d->rp, vram, &d->ops);
}

static void gl_execute(nr_vbackend *b, const nr_rlist *l)
{
    glb *d = (glb *)b->impl;
    set_target(d, &d->vram);
    nr_gpu_replay(&d->rp, l, &d->s, d->sx, d->sy, &d->ops);
}

static bool upload_rgba(glb *d, gtex *t, const u32 *px, int w, int h)
{
    if (!t_make(d, t, w, h, false)) return false;
    GL.BindTexture(GL_TEXTURE_2D, t->tex);
    GL.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    GL.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    GL.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return true;
}

static void capture(nr_vbackend *b, glb *d)
{
    int w = d->win_w, h = d->win_h, y;
    u32 *px = (u32 *)realloc(b->cap_px, (size_t)w * h * 4), *row;
    if (!px) return;
    b->cap_px = px;
    GL.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.PixelStorei(GL_PACK_ALIGNMENT, 4);
    GL.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    row = (u32 *)malloc((size_t)w * 4);
    if (row) {
        for (y = 0; y < h / 2; y++) {          /* bottom-up -> top-down */
            memcpy(row, px + (size_t)y * w, (size_t)w * 4);
            memcpy(px + (size_t)y * w, px + (size_t)(h - 1 - y) * w, (size_t)w * 4);
            memcpy(px + (size_t)(h - 1 - y) * w, row, (size_t)w * 4);
        }
        free(row);
    }
    for (y = 0; y < w * h; y++) px[y] |= 0xFF000000u;
    b->cap_w = w; b->cap_h = h;
}

static void gl_present(nr_vbackend *b, const nr_vpresent *p)
{
    glb *d = (glb *)b->impl;
    GLuint src = 0;
    int sw = 0, sh = 0, x, y, w, h;
    nr_sdl_gl_size(d->win, &d->win_w, &d->win_h);
    if (p->use_hires && p->display && !p->display->rgb24) {
        int ow = (p->display->width > 0 ? p->display->width : 320) * d->S, oh = (p->display->height > 0 ? p->display->height : 240) * d->S;
        if (p->display->enabled && t_make(d, &d->internal, ow, oh, true)) {
            set_target(d, &d->internal);
            d->cbd.resolve[0] = (float)(p->display->disp_x * (u32)d->sx);
            d->cbd.resolve[1] = (float)(p->display->disp_y * (u32)d->sy);
            d->cbd.resolve[2] = (float)d->ax; d->cbd.resolve[3] = (float)d->ay;
            quad_draw(d, GP_RESOLVE, d->vram.tex, 0, 0, ow, oh);
            src = d->internal.tex; sw = ow; sh = oh;
        }
    } else if (p->picture && p->pic_w > 0 && p->pic_h > 0) {
        if (upload_rgba(d, &d->picture, p->picture, p->pic_w, p->pic_h)) { src = d->picture.tex; sw = p->pic_w; sh = p->pic_h; }
    }
    set_target(d, NULL);
    GL.Disable(GL_SCISSOR_TEST);
    GL.ClearColor(0, 0, 0, 1);
    GL.Clear(GL_COLOR_BUFFER_BIT);
    if (src) {
        nr_video_dest_rect(&d->s, d->win_w, d->win_h, sw, sh, p->native_w, p->native_h, &x, &y, &w, &h);
        d->cbd.psize[0] = (float)sw; d->cbd.psize[1] = (float)sh; d->cbd.psize[2] = (float)w; d->cbd.psize[3] = (float)h;
        d->cbd.pfx[0] = (float)(p->native_h > 0 ? p->native_h : sh);
        d->cbd.pfx[1] = (float)d->s.post_fx; d->cbd.pfx[2] = d->s.post_fx_strength; d->cbd.pfx[3] = (float)d->s.tonemap;
        d->cbd.pgrade[0] = d->s.brightness; d->cbd.pgrade[1] = d->s.contrast; d->cbd.pgrade[2] = d->s.saturation; d->cbd.pgrade[3] = d->s.gamma;
        d->cbd.pmisc[0] = (float)(p->frame & 0xFFFF); d->cbd.pmisc[1] = (float)d->s.scaling; d->cbd.pmisc[2] = (float)x; d->cbd.pmisc[3] = (float)y;
        quad_draw(d, GP_PRESENT, src, x, y, x + w, y + h);
    }
    if (p->overlay && p->overlay->px && upload_rgba(d, &d->overlay, p->overlay->px, p->overlay->w, p->overlay->h)) {
        nr_gvtx q[6];
        use(d, GP_OVERLAY, d->overlay.tex);
        blend(d, 4);
        scissor(d, 0, 0, d->win_w, d->win_h);
        nr_gpu_quad(q, 0, 0, (float)d->win_w, (float)d->win_h, 0, 0, (float)p->overlay->w, (float)p->overlay->h);
        draw_verts(d, q, 6);
        blend(d, -1);
    }
    if (b->capture_next) { b->capture_next = false; capture(b, d); }
    SDL_GL_SwapWindow(d->win);
}

static void gl_shutdown(nr_vbackend *b)
{
    glb *d = (glb *)b->impl;
    int i;
    if (!d) return;
    if (d->ctx) {
        if (d->gl.DeleteFramebuffers) {
            t_free(d, &d->vram); t_free(d, &d->vread); t_free(d, &d->internal);
            t_free(d, &d->upload); t_free(d, &d->picture); t_free(d, &d->overlay);
            for (i = 0; i < GP_COUNT; i++) if (d->prog[i]) GL.DeleteProgram(d->prog[i]);
            if (d->vbo) GL.DeleteBuffers(1, &d->vbo);
            if (d->vao) GL.DeleteVertexArrays(1, &d->vao);
        }
        SDL_GL_DestroyContext(d->ctx);
    }
    nr_gpu_replay_free(&d->rp);
    free(d);
    free(b->cap_px);
    b->cap_px = NULL;
    b->impl = NULL;
}

bool nr_vb_opengl_create(nr_vbackend *b)
{
    memset(b, 0, sizeof(*b));
    b->name = "opengl";
    b->gpu_hires = true;
    b->needs_gl_window = true;
    b->max_vram_scale = 16;
    b->init = gl_init;
    b->configure = gl_configure;
    b->sync = gl_sync;
    b->execute = gl_execute;
    b->present = gl_present;
    b->shutdown = gl_shutdown;
    return true;
}
