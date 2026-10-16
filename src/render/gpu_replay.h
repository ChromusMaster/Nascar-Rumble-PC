/* GPU replay of the render command list (rlist.h): translates console
 * primitives into batched GPU operations (draws, uploads, fills, copies and
 * refreshes of the read copy). Shared by the Direct3D and OpenGL backends;
 * the API-specific part is the nr_gpu_ops callbacks and the shaders. */
#ifndef NR_GPU_REPLAY_H
#define NR_GPU_REPLAY_H

#include "rlist.h"
#include "gfx.h"

/* vertex layout shared by every pipeline (28 floats) */
typedef struct {
    float x, y;            /* position in target pixels */
    float r, g, b, a;      /* colour 0..255 */
    float u, v;            /* texel coordinates */
    float tpx, tpy, clx, cly;
    float mode, flags, pad0, pad1;
    float umin, vmin, umax, vmax;
    float twmx, twmy, twox, twoy;
} nr_gvtx;

enum { GV_TEXTURED = 1, GV_RAW = 2, GV_SEMI = 4, GV_DITHER = 8, GV_SPRITE = 16, GV_FILTER = 32, GV_SETMASK = 64 };

/* constant buffer layout (see d3d_shaders.h), 11 float4 */
typedef struct {
    float target[4], vram[4], pass[4], quad[4], color[4], psize[4], pfx[4], pgrade[4], pmisc[4], resolve[4], tsize[4];
} nr_gcb;

typedef struct {
    void *ctx;
    /* pass 0: opaque (no blending); pass 1: semi-transparent texels with the
     * console blend mode (0..3). scissor = high-resolution rectangle
     * [x0, x1) x [y0, y1). */
    void (*draw)(void *ctx, const nr_gvtx *v, int n, int pass, int semi_mode, const int scissor[4]);
    /* native rectangle from 15-bit data (already expanded to RGBA8, alpha = bit 15) */
    void (*upload)(void *ctx, int x, int y, int w, int h, const u32 *rgba);
    void (*fill)(void *ctx, int x, int y, int w, int h, const float rgba[4]);
    void (*copy)(void *ctx, int sx, int sy, int dx, int dy, int w, int h);
    /* make the texture-read copy of the render target current for a native rectangle */
    void (*refresh)(void *ctx, int x0, int y0, int x1, int y1);
} nr_gpu_ops;

typedef struct {
    nr_gvtx *v;
    int n, cap;
    int scissor[4];
    bool any;
    /* rectangle written since the read copy was refreshed (native) */
    int dx0, dy0, dx1, dy1;
    bool dirty;
    u32 *tmp;
    size_t tmp_cap;
    u64 draws, refreshes;
} nr_gpu_replay_state;

void nr_gpu_replay_init(nr_gpu_replay_state *st);
void nr_gpu_replay_free(nr_gpu_replay_state *st);
void nr_gpu_replay(nr_gpu_replay_state *st, const nr_rlist *l, const nr_gfx_settings *s, int sx, int sy, const nr_gpu_ops *ops);
/* full VRAM upload (sync) */
void nr_gpu_sync(nr_gpu_replay_state *st, const u16 *vram, const nr_gpu_ops *ops);
/* fills a quad (2 triangles) covering [x0,x1) x [y0,y1) in target pixels */
void nr_gpu_quad(nr_gvtx q[6], float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1);

/* expands 15-bit console pixels to RGBA8 (alpha = bit 15) */
static inline u32 nr_expand15(u16 v)
{
    u32 r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return r | (g << 8) | (b << 16) | ((v & 0x8000) ? 0xFF000000u : 0);
}

#endif
