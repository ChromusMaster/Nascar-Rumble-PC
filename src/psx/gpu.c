/* GPU: command processor, software rasterizer, VRAM transfers and video
 * timing. Reference: psx-spx "Graphics Processing Unit (GPU)".
 *
 * Fidelity notes (see docs/STATUS.md):
 *  - Pixels are produced immediately, but the GPU stays "busy" for an
 *    estimated drawing time (gpu_add_ticks) so that DrawSync/DMA completion
 *    and therefore the game's frame pacing follow the hardware's fill-rate
 *    limits. The per-pixel costs are an estimate (no public measurements).
 *  - Attribute interpolation uses 16.16 fixed point plane equations; results
 *    are close to hardware but not guaranteed bit-exact on every edge.
 *  - The texture cache is not emulated (only visible in demos that rely on
 *    stale cache contents). */
#include "psx.h"
#include "../render/rlist.h"
#include "../render/pgxp.h"

#include <stdlib.h>
#include <string.h>

#define VRAM(g, x, y) ((g)->vram[(((y) & 511) << 10) | ((x) & 1023)])

static const s8 k_dither[4][4] = {
    { -4, 0, -3, 1 },
    { 2, -2, 3, -1 },
    { -3, 1, -4, 0 },
    { 3, -1, 2, -2 },
};

/* Per-video-line timing (NTSC): 3413 video cycles of the 53.693175 MHz
 * NTSC video clock = ~2152.9 CPU cycles; 263 lines -> 59.817 Hz
 * (566 204 CPU cycles per field). */
#define LINE_CYCLES_NUM (3413ull * PSX_VCLK_TO_CPU_NUM)
#define LINE_CYCLES_DEN PSX_VCLK_TO_CPU_DEN
#define LINES_NTSC 263u
#define LINES_PAL 314u

void gpu_init(psx *p)
{
    p->gpu.vram = (u16 *)calloc(PSX_VRAM_W * PSX_VRAM_H, sizeof(u16));
    p->gpu.timing = true;
}

void gpu_free(psx *p)
{
    free(p->gpu.vram);
    p->gpu.vram = NULL;
}

static void schedule_line(psx *p)
{
    u64 num = LINE_CYCLES_NUM + p->gpu.line_frac;
    u64 cyc = num / LINE_CYCLES_DEN;
    p->gpu.line_frac = num % LINE_CYCLES_DEN;
    psx_schedule_periodic(p, EV_GPU_LINE, cyc);
}

static void soft_reset(psx *p)
{
    psx_gpu *g = &p->gpu;
    g->stat = 0x14802000u;
    g->texpage = 0;
    g->tex_window = 0;
    g->draw_x1 = g->draw_y1 = 0;
    g->draw_x2 = g->draw_y2 = 0;
    g->off_x = g->off_y = 0;
    g->set_mask = g->check_mask = false;
    g->rect_flip_x = g->rect_flip_y = false;
    g->disp_x = g->disp_y = 0;
    g->hrange1 = 0x200; g->hrange2 = 0x200 + 256 * 10;
    g->vrange1 = 0x10; g->vrange2 = 0x10 + 240;
    g->disp_mode = 0;
    g->disp_enabled = false;
    g->cmd_len = 0; g->cmd_need = 0; g->state = 0;
    g->dma_dir = 0;
}

void gpu_reset(psx *p)
{
    psx_gpu *g = &p->gpu;
    if (g->vram) memset(g->vram, 0, PSX_VRAM_W * PSX_VRAM_H * 2);
    soft_reset(p);
    g->line = 0;
    g->in_vblank = false;
    g->frame_count = 0;
    g->gpuread = 0;
    g->busy_until = 0;
    g->prim_pixels = 0;
    g->line_frac = 0;
    schedule_line(p);
}

/* ---------------------------------------------------------------- status */

static u32 compute_stat(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 s = 0;
    s |= g->texpage & 0x7FF;
    if (g->set_mask) s |= 1u << 11;
    if (g->check_mask) s |= 1u << 12;
    if (!(g->disp_mode & 0x20) || g->interlace_field) s |= 1u << 13;
    if (g->texpage & 0x800) s |= 1u << 15;
    if (g->disp_mode & 0x40) s |= 1u << 16;
    s |= (g->disp_mode & 3) << 17;
    if (g->disp_mode & 0x04) s |= 1u << 19;
    if (g->disp_mode & 0x08) s |= 1u << 20;
    if (g->disp_mode & 0x10) s |= 1u << 21;
    if (g->disp_mode & 0x20) s |= 1u << 22;
    if (!g->disp_enabled) s |= 1u << 23;
    s |= g->stat & (1u << 24); /* IRQ flag */
    /* Ready bits: commands are accepted immediately, but while the drawing
     * time of previous primitives has not elapsed the GPU reports busy. */
    if (g->state != 1 && !gpu_busy(p)) s |= 1u << 26;   /* ready to receive command */
    if (g->state == 2) s |= 1u << 27;                   /* ready to send VRAM to CPU */
    if (!gpu_busy(p)) s |= 1u << 28;                    /* ready to receive DMA block */
    s |= (g->dma_dir & 3) << 29;
    switch (g->dma_dir) {
    case 1: s |= 1u << 25; break;
    case 2: if (!gpu_busy(p)) s |= 1u << 25; break;
    case 3: if (g->state == 2) s |= 1u << 25; break;
    default: break;
    }
    if (!g->in_vblank) {
        if (g->disp_mode & 0x24) { if (g->interlace_field) s |= 1u << 31; }
        else if (g->line & 1) s |= 1u << 31;
    }
    return s;
}

u32 gpu_read(psx *p, u32 reg)
{
    if (reg == 1) return compute_stat(p);
    return gpu_dma_read(p);
}

/* ------------------------------------------------------------- rasterizer */

typedef struct {
    s32 x, y;
    s32 r, g, b;
    s32 u, v;
    u32 raw;     /* packed coordinate as sent by the game (PGXP lookup key) */
} vtx;

typedef struct {
    bool textured, raw, semi, gouraud, dither;
    u32 clut_x, clut_y;
    u32 tp_x, tp_y, tp_mode;   /* texture page */
    u32 semi_mode;
} prim_state;

static inline u16 tex_fetch(psx_gpu *g, const prim_state *ps, u32 u, u32 v)
{
    u32 mx = (g->tex_window & 0x1F) * 8, my = ((g->tex_window >> 5) & 0x1F) * 8;
    u32 ox = ((g->tex_window >> 10) & 0x1F) * 8, oy = ((g->tex_window >> 15) & 0x1F) * 8;
    u &= 0xFF; v &= 0xFF;
    u = (u & ~mx) | (ox & mx);
    v = (v & ~my) | (oy & my);
    switch (ps->tp_mode) {
    case 0: {
        u16 w = VRAM(g, ps->tp_x + (u >> 2), ps->tp_y + v);
        u32 idx = (w >> ((u & 3) * 4)) & 0xF;
        return VRAM(g, ps->clut_x + idx, ps->clut_y);
    }
    case 1: {
        u16 w = VRAM(g, ps->tp_x + (u >> 1), ps->tp_y + v);
        u32 idx = (w >> ((u & 1) * 8)) & 0xFF;
        return VRAM(g, ps->clut_x + idx, ps->clut_y);
    }
    default:
        return VRAM(g, ps->tp_x + u, ps->tp_y + v);
    }
}

/* Drawing time model, in GPU clock ticks (53.69 MHz). Estimates:
 *   per primitive setup 16; untextured 1 tick/pixel; textured 2 ticks/pixel;
 *   semi-transparency +1 tick/pixel (read-modify-write); quick fill 0.5;
 *   VRAM copy 2 ticks/pixel; CPU->VRAM 1 tick/pixel. */
static void gpu_add_ticks(psx *p, u64 ticks)
{
    psx_gpu *g = &p->gpu;
    u64 start;
    if (!g->timing) return;
    start = g->busy_until > p->cycles ? g->busy_until : p->cycles;
    g->busy_until = start + (ticks * PSX_VCLK_TO_CPU_NUM) / PSX_VCLK_TO_CPU_DEN;
}

static void charge_prim(psx *p, bool textured, bool semi)
{
    u64 px = p->gpu.prim_pixels;
    gpu_add_ticks(p, 16 + px * (textured ? 2 : 1) + (semi ? px : 0));
    p->gpu.prim_pixels = 0;
}

bool gpu_busy(psx *p) { return p->gpu.timing && p->cycles < p->gpu.busy_until; }
u64 gpu_busy_remaining(psx *p) { return gpu_busy(p) ? p->gpu.busy_until - p->cycles : 0; }

static inline void plot(psx_gpu *g, const prim_state *ps, s32 x, s32 y, s32 r, s32 gg, s32 b, s32 u, s32 v, bool dith)
{
    u16 back, out;
    u32 rr, gg5, bb, mask_bit;
    bool semi_px;
    g->prim_pixels++;
    if (g->check_mask && (VRAM(g, x, y) & 0x8000)) return;
    if (ps->textured) {
        u16 t = tex_fetch(g, ps, (u32)u, (u32)v);
        if (t == 0) return;
        semi_px = ps->semi && (t & 0x8000);
        mask_bit = t & 0x8000;
        if (ps->raw) {
            rr = t & 31; gg5 = (t >> 5) & 31; bb = (t >> 10) & 31;
        } else {
            s32 tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
            s32 cr = (tr * r) >> 7, cg = (tg * gg) >> 7, cb = (tb * b) >> 7;
            if (dith) {
                s32 d = k_dither[y & 3][x & 3];
                cr += d; cg += d; cb += d;
            }
            if (cr < 0) cr = 0;
            if (cg < 0) cg = 0;
            if (cb < 0) cb = 0;
            if (cr > 255) cr = 255;
            if (cg > 255) cg = 255;
            if (cb > 255) cb = 255;
            rr = (u32)cr >> 3; gg5 = (u32)cg >> 3; bb = (u32)cb >> 3;
        }
    } else {
        s32 cr = r, cg = gg, cb = b;
        semi_px = ps->semi;
        mask_bit = 0;
        if (dith) {
            s32 d = k_dither[y & 3][x & 3];
            cr += d; cg += d; cb += d;
            if (cr < 0) cr = 0;
            if (cg < 0) cg = 0;
            if (cb < 0) cb = 0;
            if (cr > 255) cr = 255;
            if (cg > 255) cg = 255;
            if (cb > 255) cb = 255;
        }
        rr = (u32)cr >> 3; gg5 = (u32)cg >> 3; bb = (u32)cb >> 3;
    }
    if (semi_px) {
        s32 br, bg, bbk, fr = (s32)rr, fg = (s32)gg5, fb = (s32)bb;
        back = VRAM(g, x, y);
        br = back & 31; bg = (back >> 5) & 31; bbk = (back >> 10) & 31;
        switch (ps->semi_mode) {
        case 0: fr = (br + fr) >> 1; fg = (bg + fg) >> 1; fb = (bbk + fb) >> 1; break;
        case 1: fr = br + fr; fg = bg + fg; fb = bbk + fb; break;
        case 2: fr = br - fr; fg = bg - fg; fb = bbk - fb; break;
        default: fr = br + (fr >> 2); fg = bg + (fg >> 2); fb = bbk + (fb >> 2); break;
        }
        if (fr < 0) fr = 0;
        if (fg < 0) fg = 0;
        if (fb < 0) fb = 0;
        if (fr > 31) fr = 31;
        if (fg > 31) fg = 31;
        if (fb > 31) fb = 31;
        rr = (u32)fr; gg5 = (u32)fg; bb = (u32)fb;
    }
    out = (u16)(rr | (gg5 << 5) | (bb << 10) | mask_bit);
    if (g->set_mask) out |= 0x8000;
    VRAM(g, x, y) = out;
}

static inline s64 edge(const vtx *a, const vtx *b, s32 px, s32 py)
{
    return (s64)(b->x - a->x) * (py - a->y) - (s64)(b->y - a->y) * (px - a->x);
}

/* Top-left rule for the winding used below (area > 0 in y-down coordinates):
 * pixels exactly on a top or left edge are drawn, on bottom/right edges not.
 * This yields the documented "lower-right coordinates excluded" behaviour. */
static inline s64 tl_bias(const vtx *a, const vtx *b)
{
    return ((a->y == b->y && b->x > a->x) || (b->y < a->y)) ? 0 : -1;
}

static void draw_triangle(psx *p, vtx v0, vtx v1, vtx v2, const prim_state *ps)
{
    psx_gpu *g = &p->gpu;
    s64 area;
    s32 minx, maxx, miny, maxy, x, y;
    s64 dr_dx = 0, dr_dy = 0, dg_dx = 0, dg_dy = 0, db_dx = 0, db_dy = 0, du_dx = 0, du_dy = 0, dv_dx = 0, dv_dy = 0;
    s64 b0, b1, b2, sx0, sx1, sx2;

    /* Hardware rejects primitives that are too large. */
    if (abs(v0.x - v1.x) >= 1024 || abs(v1.x - v2.x) >= 1024 || abs(v2.x - v0.x) >= 1024) return;
    if (abs(v0.y - v1.y) >= 512 || abs(v1.y - v2.y) >= 512 || abs(v2.y - v0.y) >= 512) return;

    area = edge(&v0, &v1, v2.x, v2.y);
    if (area == 0) return;
    if (area < 0) { vtx t = v1; v1 = v2; v2 = t; area = -area; }

    minx = v0.x; if (v1.x < minx) minx = v1.x; if (v2.x < minx) minx = v2.x;
    maxx = v0.x; if (v1.x > maxx) maxx = v1.x; if (v2.x > maxx) maxx = v2.x;
    miny = v0.y; if (v1.y < miny) miny = v1.y; if (v2.y < miny) miny = v2.y;
    maxy = v0.y; if (v1.y > maxy) maxy = v1.y; if (v2.y > maxy) maxy = v2.y;
    if (minx < g->draw_x1) minx = g->draw_x1;
    if (miny < g->draw_y1) miny = g->draw_y1;
    if (maxx > g->draw_x2) maxx = g->draw_x2;
    if (maxy > g->draw_y2) maxy = g->draw_y2;
    if (minx > maxx || miny > maxy) return;

    {
        s64 x10 = v1.x - v0.x, x20 = v2.x - v0.x, y10 = v1.y - v0.y, y20 = v2.y - v0.y;
#define GRAD(a, ddx, ddy) do { \
            s64 a10 = (s64)(v1.a - v0.a), a20 = (s64)(v2.a - v0.a); \
            ddx = ((a10 * y20 - a20 * y10) * 65536) / area; \
            ddy = ((a20 * x10 - a10 * x20) * 65536) / area; } while (0)
        if (ps->gouraud) { GRAD(r, dr_dx, dr_dy); GRAD(g, dg_dx, dg_dy); GRAD(b, db_dx, db_dy); }
        if (ps->textured) { GRAD(u, du_dx, du_dy); GRAD(v, dv_dx, dv_dy); }
#undef GRAD
    }

    b0 = tl_bias(&v1, &v2);
    b1 = tl_bias(&v2, &v0);
    b2 = tl_bias(&v0, &v1);
    sx0 = -(s64)(v2.y - v1.y);
    sx1 = -(s64)(v0.y - v2.y);
    sx2 = -(s64)(v1.y - v0.y);

    for (y = miny; y <= maxy; y++) {
        s64 w0 = edge(&v1, &v2, minx, y) + b0;
        s64 w1 = edge(&v2, &v0, minx, y) + b1;
        s64 w2 = edge(&v0, &v1, minx, y) + b2;
        s64 rr = 0, gg = 0, bb = 0, uu = 0, vv = 0;
        s32 dx0 = minx - v0.x, dy0 = y - v0.y;
        if (ps->gouraud) {
            rr = ((s64)v0.r << 16) + dr_dx * dx0 + dr_dy * dy0 + 0x8000;
            gg = ((s64)v0.g << 16) + dg_dx * dx0 + dg_dy * dy0 + 0x8000;
            bb = ((s64)v0.b << 16) + db_dx * dx0 + db_dy * dy0 + 0x8000;
        }
        if (ps->textured) {
            uu = ((s64)v0.u << 16) + du_dx * dx0 + du_dy * dy0 + 0x8000;
            vv = ((s64)v0.v << 16) + dv_dx * dx0 + dv_dy * dy0 + 0x8000;
        }
        for (x = minx; x <= maxx; x++) {
            if ((w0 | w1 | w2) >= 0) {
                s32 r, gc, b;
                if (ps->gouraud) {
                    r = (s32)(rr >> 16); gc = (s32)(gg >> 16); b = (s32)(bb >> 16);
                    if (r < 0) r = 0; if (r > 255) r = 255;
                    if (gc < 0) gc = 0; if (gc > 255) gc = 255;
                    if (b < 0) b = 0; if (b > 255) b = 255;
                } else { r = v0.r; gc = v0.g; b = v0.b; }
                plot(g, ps, x, y, r, gc, b, ps->textured ? (s32)(uu >> 16) : 0, ps->textured ? (s32)(vv >> 16) : 0, ps->dither);
            }
            w0 += sx0; w1 += sx1; w2 += sx2;
            if (ps->gouraud) { rr += dr_dx; gg += dg_dx; bb += db_dx; }
            if (ps->textured) { uu += du_dx; vv += dv_dx; }
        }
    }
    g->prims++;
}

static void decode_texpage(psx_gpu *g, u32 tp, prim_state *ps)
{
    ps->tp_x = (tp & 0xF) * 64;
    ps->tp_y = ((tp >> 4) & 1) * 256;
    ps->tp_mode = (tp >> 7) & 3;
    if (ps->tp_mode == 3) ps->tp_mode = 2;
    ps->semi_mode = (tp >> 5) & 3;
    (void)g;
}

static inline s32 sext11(u32 v) { return ((s32)(v << 21)) >> 21; }

/* ---------------------------------------------------- render recording */

static nr_rcmd *rec_begin(psx_gpu *g, u8 type, const prim_state *ps, u16 flags)
{
    nr_rcmd *c = nr_rlist_push(g->rec);
    if (!c) return NULL;
    c->type = type;
    c->flags = flags | (g->set_mask ? RF_SET_MASK : 0) | (g->check_mask ? RF_CHECK_MASK : 0);
    if (ps) {
        c->tp_mode = (u8)ps->tp_mode;
        c->semi_mode = (u8)ps->semi_mode;
        c->tp_x = (u16)ps->tp_x; c->tp_y = (u16)ps->tp_y;
        c->clut_x = (u16)ps->clut_x; c->clut_y = (u16)ps->clut_y;
    }
    c->texwin = g->tex_window;
    c->clip_x1 = (s16)g->draw_x1; c->clip_y1 = (s16)g->draw_y1;
    c->clip_x2 = (s16)g->draw_x2; c->clip_y2 = (s16)g->draw_y2;
    return c;
}

static bool rec_vtx(psx_gpu *g, nr_rvtx *o, const vtx *v)
{
    float px, py, pw;
    o->x = (s16)v->x; o->y = (s16)v->y;
    o->u = (s16)v->u; o->v = (s16)v->v;
    o->r = (u8)v->r; o->g = (u8)v->g; o->b = (u8)v->b; o->a = 255;
    o->fx = (float)v->x; o->fy = (float)v->y; o->fw = 1.0f;
    if (g->pgxp && v->raw && nr_pgxp_lookup(g->pgxp, v->raw, &px, &py, &pw)) {
        px += (float)g->off_x; py += (float)g->off_y;
        if (px > (float)v->x - 1.0f && px < (float)v->x + 1.0f && py > (float)v->y - 1.0f && py < (float)v->y + 1.0f) {
            o->fx = px; o->fy = py; o->fw = pw;
            return true;
        }
    }
    return false;
}

static u16 prim_flags(const prim_state *ps)
{
    return (u16)((ps->textured ? RF_TEXTURED : 0) | (ps->raw ? RF_RAW : 0) | (ps->semi ? RF_SEMI : 0) |
                 (ps->gouraud ? RF_GOURAUD : 0) | (ps->dither ? RF_DITHER : 0));
}

static bool tri_too_large(const vtx *v0, const vtx *v1, const vtx *v2)
{
    return abs(v0->x - v1->x) >= 1024 || abs(v1->x - v2->x) >= 1024 || abs(v2->x - v0->x) >= 1024 ||
           abs(v0->y - v1->y) >= 512 || abs(v1->y - v2->y) >= 512 || abs(v2->y - v0->y) >= 512;
}

static void rec_tri(psx_gpu *g, const vtx *a, const vtx *b, const vtx *c, const prim_state *ps, u16 extra, const s16 uvb[4])
{
    nr_rcmd *r;
    if (tri_too_large(a, b, c)) return;
    r = rec_begin(g, R_TRI, ps, (u16)(prim_flags(ps) | extra));
    if (!r) return;
    {
        /* each vertex keeps its own precise position (shared vertices stay
         * identical between neighbouring polygons: no cracks); depth is only
         * used when all three were found (fw = 0 marks a miss) */
        bool h0 = rec_vtx(g, &r->v[0], a), h1 = rec_vtx(g, &r->v[1], b), h2 = rec_vtx(g, &r->v[2], c);
        if (g->pgxp) {
            r->flags |= RF_PRECISE;
            if (!(h0 && h1 && h2)) { r->v[0].fw = 0; r->v[1].fw = 0; r->v[2].fw = 0; }
        }
    }
    r->uv_min_u = uvb[0]; r->uv_min_v = uvb[1]; r->uv_max_u = uvb[2]; r->uv_max_v = uvb[3];
}

static void rec_upload(psx_gpu *g, u32 x, u32 y, u32 w, u32 h)
{
    nr_rcmd *c;
    u32 at, i, j;
    if (!w || !h) return;
    at = nr_rlist_alloc_data(g->rec, w * h);
    if (at == 0xFFFFFFFFu) return;
    c = rec_begin(g, R_UPLOAD, NULL, 0);
    if (!c) return;
    c->x = (u16)x; c->y = (u16)y; c->w = (u16)w; c->h = (u16)h; c->data = at;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) g->rec->data[at + j * w + i] = VRAM(g, x + i, y + j);
}

static void cmd_polygon(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 c0 = g->cmd[0];
    bool quad = (c0 >> 27) & 1, gouraud = (c0 >> 28) & 1, tex = (c0 >> 26) & 1;
    int n = quad ? 4 : 3, i, w = 1;
    vtx v[4];
    prim_state ps;
    memset(&ps, 0, sizeof(ps));
    ps.textured = tex;
    ps.raw = tex && ((c0 >> 24) & 1);
    ps.semi = (c0 >> 25) & 1;
    ps.gouraud = gouraud && !ps.raw;
    for (i = 0; i < n; i++) {
        u32 col = (i == 0) ? c0 : (gouraud ? g->cmd[w++] : c0);
        u32 xy;
        if (i == 0 && gouraud) { /* color already in c0 */ }
        xy = g->cmd[w++];
        v[i].raw = xy;
        v[i].r = col & 0xFF; v[i].g = (col >> 8) & 0xFF; v[i].b = (col >> 16) & 0xFF;
        v[i].x = sext11(xy) + g->off_x;
        v[i].y = sext11(xy >> 16) + g->off_y;
        v[i].u = v[i].v = 0;
        if (tex) {
            u32 uv = g->cmd[w++];
            v[i].u = uv & 0xFF; v[i].v = (uv >> 8) & 0xFF;
            if (i == 0) { ps.clut_x = ((uv >> 16) & 0x3F) * 16; ps.clut_y = (uv >> 22) & 0x1FF; }
            if (i == 1) {
                u32 tp = uv >> 16;
                g->texpage = (g->texpage & ~0x9FFu) | (tp & 0x9FF);
            }
        }
    }
    decode_texpage(g, g->texpage, &ps);
    if (!tex) ps.semi_mode = (g->texpage >> 5) & 3;
    ps.dither = ((g->texpage >> 9) & 1) && (ps.gouraud || (tex && !ps.raw));
    if (!ps.gouraud && !ps.raw) {
        /* flat: all vertices share color 0 */
        for (i = 1; i < n; i++) { v[i].r = v[0].r; v[i].g = v[0].g; v[i].b = v[0].b; }
    }
    if (tex && !ps.gouraud && !ps.raw) {
        /* flat modulated: keep constant color */
    }
    if (ps.raw) { for (i = 0; i < n; i++) { v[i].r = v[i].g = v[i].b = 128; } }
    if (g->rec) {
        s16 uvb[4] = { 255, 255, 0, 0 };
        for (i = 0; i < n; i++) {
            if (v[i].u < uvb[0]) uvb[0] = (s16)v[i].u;
            if (v[i].v < uvb[1]) uvb[1] = (s16)v[i].v;
            if (v[i].u > uvb[2]) uvb[2] = (s16)v[i].u;
            if (v[i].v > uvb[3]) uvb[3] = (s16)v[i].v;
        }
        rec_tri(g, &v[0], &v[1], &v[2], &ps, 0, uvb);
        if (quad) rec_tri(g, &v[1], &v[2], &v[3], &ps, 0, uvb);
    }
    p->gpu.prim_pixels = 0;
    draw_triangle(p, v[0], v[1], v[2], &ps);
    if (quad) draw_triangle(p, v[1], v[2], v[3], &ps);
    charge_prim(p, tex, ps.semi);
}

static void draw_line(psx *p, vtx a, vtx b, bool gouraud, bool semi)
{
    psx_gpu *g = &p->gpu;
    prim_state ps;
    s32 dx = b.x - a.x, dy = b.y - a.y, steps, i;
    s64 x, y, r, gg, bl, sx, sy, sr, sg, sb;
    memset(&ps, 0, sizeof(ps));
    ps.semi = semi;
    ps.semi_mode = (g->texpage >> 5) & 3;
    if (abs(dx) >= 1024 || abs(dy) >= 512) return;
    steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (!gouraud) { b.r = a.r; b.g = a.g; b.b = a.b; }
    if (g->rec) {
        nr_rcmd *c = rec_begin(g, R_LINE, &ps, (u16)((semi ? RF_SEMI : 0) | (gouraud ? RF_GOURAUD : 0) | (((g->texpage >> 9) & 1) ? RF_DITHER : 0)));
        if (c) { rec_vtx(g, &c->v[0], &a); rec_vtx(g, &c->v[1], &b); c->v[0].fw = c->v[1].fw = 1.0f; }
    }
    x = ((s64)a.x << 16) + 0x8000; y = ((s64)a.y << 16) + 0x8000;
    r = (s64)a.r << 16; gg = (s64)a.g << 16; bl = (s64)a.b << 16;
    if (steps) {
        sx = ((s64)dx << 16) / steps; sy = ((s64)dy << 16) / steps;
        sr = ((s64)(b.r - a.r) << 16) / steps; sg = ((s64)(b.g - a.g) << 16) / steps; sb = ((s64)(b.b - a.b) << 16) / steps;
    } else { sx = sy = sr = sg = sb = 0; }
    for (i = 0; i <= steps; i++) {
        s32 px = (s32)(x >> 16), py = (s32)(y >> 16);
        if (px >= g->draw_x1 && px <= g->draw_x2 && py >= g->draw_y1 && py <= g->draw_y2)
            plot(g, &ps, px, py, (s32)(r >> 16), (s32)(gg >> 16), (s32)(bl >> 16), 0, 0, (g->texpage >> 9) & 1);
        x += sx; y += sy; r += sr; gg += sg; bl += sb;
    }
    g->prims++;
    gpu_add_ticks(p, 16 + (u64)steps + 1);
}

static void cmd_rect(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 c0 = g->cmd[0];
    u32 sz = (c0 >> 27) & 3;
    bool tex = (c0 >> 26) & 1;
    int w = 1;
    s32 x0, y0, wd, ht, x, y;
    u32 u0 = 0, v0 = 0;
    prim_state ps;
    u32 xy = g->cmd[w++];
    memset(&ps, 0, sizeof(ps));
    ps.textured = tex;
    ps.raw = tex && ((c0 >> 24) & 1);
    ps.semi = (c0 >> 25) & 1;
    decode_texpage(g, g->texpage, &ps);
    ps.semi_mode = (g->texpage >> 5) & 3;
    if (tex) {
        u32 uv = g->cmd[w++];
        u0 = uv & 0xFF; v0 = (uv >> 8) & 0xFF;
        ps.clut_x = ((uv >> 16) & 0x3F) * 16; ps.clut_y = (uv >> 22) & 0x1FF;
    }
    switch (sz) {
    case 0: { u32 s = g->cmd[w++]; wd = s & 0x3FF; ht = (s >> 16) & 0x1FF; break; }
    case 1: wd = ht = 1; break;
    case 2: wd = ht = 8; break;
    default: wd = ht = 16; break;
    }
    x0 = sext11(xy) + g->off_x;
    y0 = sext11(xy >> 16) + g->off_y;
    if (g->rec && wd > 0 && ht > 0) {
        /* two triangles whose UVs map texel edges to pixel edges */
        vtx q[4];
        s32 ul = g->rect_flip_x ? (s32)u0 + 1 : (s32)u0, ur = g->rect_flip_x ? (s32)u0 + 1 - wd : (s32)u0 + wd;
        s32 vt = g->rect_flip_y ? (s32)v0 + 1 : (s32)v0, vb = g->rect_flip_y ? (s32)v0 + 1 - ht : (s32)v0 + ht;
        s16 uvb[4];
        int k;
        for (k = 0; k < 4; k++) {
            q[k].x = x0 + ((k & 1) ? wd : 0);
            q[k].y = y0 + ((k & 2) ? ht : 0);
            q[k].u = (k & 1) ? ur : ul;
            q[k].v = (k & 2) ? vb : vt;
            q[k].raw = 0;
            q[k].r = ps.raw ? 128 : (s32)(c0 & 0xFF);
            q[k].g = ps.raw ? 128 : (s32)((c0 >> 8) & 0xFF);
            q[k].b = ps.raw ? 128 : (s32)((c0 >> 16) & 0xFF);
        }
        uvb[0] = (s16)(g->rect_flip_x ? (s32)u0 - wd + 1 : (s32)u0);
        uvb[1] = (s16)(g->rect_flip_y ? (s32)v0 - ht + 1 : (s32)v0);
        uvb[2] = (s16)(g->rect_flip_x ? (s32)u0 : (s32)u0 + wd - 1);
        uvb[3] = (s16)(g->rect_flip_y ? (s32)v0 : (s32)v0 + ht - 1);
        ps.dither = false;
        rec_tri(g, &q[0], &q[1], &q[2], &ps, RF_SPRITE, uvb);
        rec_tri(g, &q[1], &q[3], &q[2], &ps, RF_SPRITE, uvb);
    }
    {
        s32 r = c0 & 0xFF, gc = (c0 >> 8) & 0xFF, b = (c0 >> 16) & 0xFF;
        if (ps.raw) r = gc = b = 128;
        for (y = 0; y < ht; y++) {
            s32 py = y0 + y;
            u32 vv;
            if (py < g->draw_y1 || py > g->draw_y2) continue;
            vv = g->rect_flip_y ? (v0 - (u32)y) : (v0 + (u32)y);
            for (x = 0; x < wd; x++) {
                s32 px = x0 + x;
                u32 uu;
                if (px < g->draw_x1 || px > g->draw_x2) continue;
                uu = g->rect_flip_x ? (u0 - (u32)x) : (u0 + (u32)x);
                plot(g, &ps, px, py, r, gc, b, (s32)(uu & 0xFF), (s32)(vv & 0xFF), false);
            }
        }
    }
    g->prims++;
    charge_prim(p, tex, ps.semi);
}

static void cmd_fill(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 c = g->cmd[0];
    u32 xy = g->cmd[1], wh = g->cmd[2];
    u32 x0 = xy & 0x3F0, y0 = (xy >> 16) & 0x1FF;
    u32 w = ((wh & 0x3FF) + 0xF) & ~0xFu, h = (wh >> 16) & 0x1FF;
    u16 col = (u16)(((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10));
    u32 x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            VRAM(g, x0 + x, y0 + y) = col;
    if (g->rec && w && h) {
        nr_rcmd *r = rec_begin(g, R_FILL, NULL, 0);
        if (r) { r->x = (u16)x0; r->y = (u16)y0; r->w = (u16)w; r->h = (u16)h; r->color = col; }
    }
    gpu_add_ticks(p, 46 + (u64)w * h / 2);
}

static void cmd_copy(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 s = g->cmd[1], d = g->cmd[2], wh = g->cmd[3];
    u32 sx = s & 0x3FF, sy = (s >> 16) & 0x1FF, dx = d & 0x3FF, dy = (d >> 16) & 0x1FF;
    u32 w = ((wh & 0x3FF) - 1) & 0x3FF, h = (((wh >> 16) & 0x1FF) - 1) & 0x1FF;
    u32 x, y;
    u16 *line;
    w++; h++;
    line = (u16 *)malloc(w * sizeof(u16));
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) line[x] = VRAM(g, sx + x, sy + y);
        for (x = 0; x < w; x++) {
            u16 *dst = &VRAM(g, dx + x, dy + y);
            if (g->check_mask && (*dst & 0x8000)) continue;
            *dst = line[x] | (g->set_mask ? 0x8000 : 0);
        }
    }
    free(line);
    if (g->rec) {
        nr_rcmd *r = rec_begin(g, R_COPY, NULL, 0);
        if (r) { r->sx = (u16)sx; r->sy = (u16)sy; r->x = (u16)dx; r->y = (u16)dy; r->w = (u16)w; r->h = (u16)h; }
    }
    gpu_add_ticks(p, (u64)w * h * 2);
}

/* ---------------------------------------------------------- GP0 / GP1 */

static int params_for(u32 cmd)
{
    u32 op = cmd >> 24;
    switch (op >> 5) {
    case 1: { /* polygon */
        bool quad = (op >> 3) & 1, gour = (op >> 4) & 1, tex = (op >> 2) & 1;
        int n = quad ? 4 : 3;
        return 1 + n + (tex ? n : 0) + (gour ? n - 1 : 0);
    }
    case 2: { /* line */
        bool gour = (op >> 4) & 1;
        if ((op >> 3) & 1) return -1; /* polyline */
        return gour ? 4 : 3;
    }
    case 3: { /* rect */
        u32 sz = (op >> 3) & 3;
        bool tex = (op >> 2) & 1;
        return 2 + (tex ? 1 : 0) + (sz == 0 ? 1 : 0);
    }
    case 4: return 4;
    case 5: return 3;
    case 6: return 3;
    default:
        if (op == 0x02) return 3;
        return 1;
    }
}

static void exec_env(psx *p, u32 c)
{
    psx_gpu *g = &p->gpu;
    switch (c >> 24) {
    case 0xE1:
        g->texpage = (g->texpage & ~0x3FFFu) | (c & 0x3FFF);
        g->rect_flip_x = (c >> 12) & 1;
        g->rect_flip_y = (c >> 13) & 1;
        break;
    case 0xE2: g->tex_window = c & 0xFFFFF; break;
    case 0xE3: g->draw_x1 = c & 0x3FF; g->draw_y1 = (c >> 10) & 0x1FF; break;
    case 0xE4: g->draw_x2 = c & 0x3FF; g->draw_y2 = (c >> 10) & 0x1FF; break;
    case 0xE5: g->off_x = sext11(c); g->off_y = sext11(c >> 11); break;
    case 0xE6: g->set_mask = c & 1; g->check_mask = (c >> 1) & 1; break;
    default: break;
    }
}

static void exec_cmd(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 c = g->cmd[0];
    u32 op = c >> 24;
    switch (op >> 5) {
    case 0:
        if (op == 0x02) cmd_fill(p);
        else if (op == 0x1F) { g->stat |= 1u << 24; psx_irq(p, IRQ_GPU); }
        break;
    case 1: cmd_polygon(p); break;
    case 2: {
        vtx a, b;
        bool gour = (op >> 4) & 1;
        u32 ca = c, cb = gour ? g->cmd[2] : c;
        u32 xa = g->cmd[1], xb = gour ? g->cmd[3] : g->cmd[2];
        a.r = ca & 0xFF; a.g = (ca >> 8) & 0xFF; a.b = (ca >> 16) & 0xFF;
        b.r = cb & 0xFF; b.g = (cb >> 8) & 0xFF; b.b = (cb >> 16) & 0xFF;
        a.x = sext11(xa) + g->off_x; a.y = sext11(xa >> 16) + g->off_y;
        b.x = sext11(xb) + g->off_x; b.y = sext11(xb >> 16) + g->off_y;
        a.u = a.v = b.u = b.v = 0; a.raw = b.raw = 0;
        draw_line(p, a, b, gour, (op >> 1) & 1);
        break;
    }
    case 3: cmd_rect(p); break;
    case 4: cmd_copy(p); break;
    case 5: {
        u32 xy = g->cmd[1], wh = g->cmd[2];
        g->xfer_x = xy & 0x3FF; g->xfer_y = (xy >> 16) & 0x1FF;
        g->xfer_w = ((wh & 0x3FF) - 1) & 0x3FF; g->xfer_w++;
        g->xfer_h = (((wh >> 16) & 0x1FF) - 1) & 0x1FF; g->xfer_h++;
        g->xfer_cx = g->xfer_cy = 0;
        g->state = 1;
        gpu_add_ticks(p, (u64)g->xfer_w * g->xfer_h);
        break;
    }
    case 6: {
        u32 xy = g->cmd[1], wh = g->cmd[2];
        g->xfer_x = xy & 0x3FF; g->xfer_y = (xy >> 16) & 0x1FF;
        g->xfer_w = ((wh & 0x3FF) - 1) & 0x3FF; g->xfer_w++;
        g->xfer_h = (((wh >> 16) & 0x1FF) - 1) & 0x1FF; g->xfer_h++;
        g->xfer_cx = g->xfer_cy = 0;
        g->state = 2;
        break;
    }
    case 7: exec_env(p, c); break;
    }
}

static void vram_write_px(psx_gpu *g, u16 v)
{
    if (g->xfer_cy < g->xfer_h) {
        u16 *dst = &VRAM(g, g->xfer_x + g->xfer_cx, g->xfer_y + g->xfer_cy);
        if (!(g->check_mask && (*dst & 0x8000))) *dst = v | (g->set_mask ? 0x8000 : 0);
    }
    if (++g->xfer_cx >= g->xfer_w) { g->xfer_cx = 0; g->xfer_cy++; }
}

static u16 vram_read_px(psx_gpu *g)
{
    u16 v = 0;
    if (g->xfer_cy < g->xfer_h) v = VRAM(g, g->xfer_x + g->xfer_cx, g->xfer_y + g->xfer_cy);
    if (++g->xfer_cx >= g->xfer_w) { g->xfer_cx = 0; g->xfer_cy++; }
    return v;
}

void gpu_write_gp0(psx *p, u32 v)
{
    psx_gpu *g = &p->gpu;
    if (g->state == 1) {
        vram_write_px(g, (u16)v);
        vram_write_px(g, (u16)(v >> 16));
        if (g->xfer_cy >= g->xfer_h) {
            g->state = 0;
            if (g->rec) rec_upload(g, g->xfer_x, g->xfer_y, g->xfer_w, g->xfer_h);
        }
        return;
    }
    if (g->state == 3) {
        /* polyline continuation */
        bool gour = (g->cmd[0] >> 28) & 1;
        if ((v & 0xF000F000u) == 0x50005000u && g->cmd_len >= 2) {
            g->state = 0; g->cmd_len = 0;
            return;
        }
        g->cmd[g->cmd_len++] = v;
        if ((gour && g->cmd_len == 4) || (!gour && g->cmd_len == 3)) {
            vtx a, b;
            u32 ca = g->cmd[0], cb, xa, xb;
            if (gour) { xa = g->cmd[1]; cb = g->cmd[2]; xb = g->cmd[3]; }
            else { xa = g->cmd[1]; cb = ca; xb = g->cmd[2]; }
            a.r = ca & 0xFF; a.g = (ca >> 8) & 0xFF; a.b = (ca >> 16) & 0xFF;
            b.r = cb & 0xFF; b.g = (cb >> 8) & 0xFF; b.b = (cb >> 16) & 0xFF;
            a.x = sext11(xa) + g->off_x; a.y = sext11(xa >> 16) + g->off_y;
            b.x = sext11(xb) + g->off_x; b.y = sext11(xb >> 16) + g->off_y;
            a.u = a.v = b.u = b.v = 0; a.raw = b.raw = 0;
            draw_line(p, a, b, gour, (g->cmd[0] >> 25) & 1);
            /* shift: last vertex becomes first */
            if (gour) { g->cmd[0] = (g->cmd[0] & 0xFF000000u) | (cb & 0xFFFFFF); g->cmd[1] = xb; g->cmd_len = 2; }
            else { g->cmd[1] = xb; g->cmd_len = 2; }
        }
        return;
    }
    if (g->cmd_len == 0) {
        u32 op = v >> 24;
        if (op >= 0xE1 && op <= 0xE6) { exec_env(p, v); return; }
        if (op == 0x00 || op == 0x01 || (op >= 0x03 && op <= 0x1E) || op == 0xE0 || (op >= 0xE7 && op <= 0xEF)) return;
        g->cmd[0] = v;
        g->cmd_len = 1;
        g->cmd_need = params_for(v);
        if (g->cmd_need == -1) {
            /* polyline: start collecting */
            g->state = 3;
            return;
        }
    } else {
        g->cmd[g->cmd_len++] = v;
    }
    if (g->cmd_len >= g->cmd_need) {
        exec_cmd(p);
        g->cmd_len = 0;
    }
}

u32 gpu_dma_read(psx *p)
{
    psx_gpu *g = &p->gpu;
    if (g->state == 2) {
        u32 lo = vram_read_px(g), hi = vram_read_px(g);
        g->gpuread = lo | (hi << 16);
        if (g->xfer_cy >= g->xfer_h) g->state = 0;
    }
    return g->gpuread;
}

void gpu_write_gp1(psx *p, u32 v)
{
    psx_gpu *g = &p->gpu;
    u32 op = (v >> 24) & 0x3F;
    switch (op) {
    case 0x00: soft_reset(p); break;
    case 0x01:
        if (g->state == 1 && g->rec) rec_upload(g, g->xfer_x, g->xfer_y, g->xfer_w, g->xfer_h);
        g->cmd_len = 0; if (g->state != 0) g->state = 0; break;
    case 0x02: g->stat &= ~(1u << 24); break;
    case 0x03: g->disp_enabled = !(v & 1); break;
    case 0x04: g->dma_dir = v & 3; break;
    case 0x05:
        if ((v & 0x7FFFF) != (((u32)g->disp_y << 10) | g->disp_x)) { g->flips++; NR_TRACE("flip", "vblank %llu prims %llu", (unsigned long long)g->frame_count, (unsigned long long)g->prims); }
        g->disp_x = v & 0x3FF; g->disp_y = (v >> 10) & 0x1FF; NR_TRACE("gpu", "display start %u,%u frame=%llu", g->disp_x, g->disp_y, (unsigned long long)g->frame_count); break;
    case 0x06: g->hrange1 = v & 0xFFF; g->hrange2 = (v >> 12) & 0xFFF; break;
    case 0x07: g->vrange1 = v & 0x3FF; g->vrange2 = (v >> 10) & 0x3FF; break;
    case 0x08: if ((v & 0xFF) != g->disp_mode) NR_DBG("gpu", "display mode %02X frame=%llu", v & 0xFF, (unsigned long long)g->frame_count); g->disp_mode = v & 0xFF; break;
    case 0x09: break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        switch (v & 0xF) {
        case 2: g->gpuread = g->tex_window; break;
        case 3: g->gpuread = (u32)g->draw_x1 | ((u32)g->draw_y1 << 10); break;
        case 4: g->gpuread = (u32)g->draw_x2 | ((u32)g->draw_y2 << 10); break;
        case 5: g->gpuread = ((u32)g->off_x & 0x7FF) | (((u32)g->off_y & 0x7FF) << 11); break;
        case 7: g->gpuread = 2; break;
        case 8: g->gpuread = 0; break;
        default: break;
        }
        break;
    default:
        break;
    }
}

/* ----------------------------------------------------------- video timing */

static void display_geometry(psx_gpu *g, int *w, int *h)
{
    static const int hres[4] = { 256, 320, 512, 640 };
    int width = (g->disp_mode & 0x40) ? 368 : hres[g->disp_mode & 3];
    int height = (int)g->vrange2 - (int)g->vrange1;
    if (height <= 0 || height > 288) height = 240;
    if ((g->disp_mode & 0x24) == 0x24) height *= 2;
    *w = width; *h = height;
}

void gpu_record_display(psx *p)
{
    psx_gpu *g = &p->gpu;
    nr_rlist *l = g->rec;
    nr_rdisplay *d = &l->display;
    d->disp_x = g->disp_x; d->disp_y = g->disp_y;
    display_geometry(g, &d->width, &d->height);
    d->enabled = g->disp_enabled;
    d->rgb24 = (g->disp_mode & 0x10) != 0;
    d->interlaced480 = (g->disp_mode & 0x24) == 0x24;
    d->frame = g->frame_count;
    if (l->overflow) {
        /* the list was truncated: resynchronise with a full VRAM upload */
        nr_rlist_clear(l);
        rec_upload(g, 0, 0, 1024, 512);
    }
    if (d->rgb24 && d->enabled) {
        u32 need = (u32)d->height * 1024u, y;
        if (l->disp24_cap < need) {
            u16 *n = (u16 *)realloc(l->disp24, need * sizeof(u16));
            if (!n) { d->rgb24 = false; return; }
            l->disp24 = n; l->disp24_cap = need;
        }
        for (y = 0; y < (u32)d->height; y++) memcpy(l->disp24 + y * 1024, &g->vram[((d->disp_y + y) & 511) * 1024], 2048);
    }
}

void gpu_event(psx *p)
{
    psx_gpu *g = &p->gpu;
    u32 lines = (g->disp_mode & 8) ? LINES_PAL : LINES_NTSC;
    u32 vb_start = g->vrange2, vb_end = g->vrange1;
    bool was = g->in_vblank, now;

    g->line++;
    if (g->line >= lines) {
        g->line = 0;
        g->interlace_field = !g->interlace_field;
    }
    if (vb_start > lines) vb_start = lines; /* too large Y2: no vblank */
    now = (g->line >= vb_start) || (g->line < vb_end);
    timers_hblank(p);
    if (now != was) {
        timers_vblank(p, now);
        g->in_vblank = now;
        timers_vblank(p, now);
        if (now) {
            psx_irq(p, IRQ_VBLANK);
            g->frame_count++;
            g->frame_ready = true;
            if (g->rec) gpu_record_display(p);
            hle_vblank(p);
        }
    }
    if (p->pcprof) p->pcprof[(p->cpu.pc & 0x1FFFFFu) >> 2]++;
    schedule_line(p);
}

/* ------------------------------------------------------------ output */

static inline u32 rgb555_to_rgba(u16 c)
{
    u32 r = (c & 31) << 3, gg = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
    r |= r >> 5; gg |= gg >> 5; b |= b >> 5;
    return 0xFF000000u | (b << 16) | (gg << 8) | r;
}

void gpu_get_vram_rgba(psx *p, u32 *rgba)
{
    int i;
    for (i = 0; i < PSX_VRAM_W * PSX_VRAM_H; i++) rgba[i] = rgb555_to_rgba(p->gpu.vram[i]);
}

void gpu_get_display(psx *p, u32 *rgba, int *w, int *h)
{
    psx_gpu *g = &p->gpu;
    static const int hres[4] = { 256, 320, 512, 640 };
    int width = (g->disp_mode & 0x40) ? 368 : hres[g->disp_mode & 3];
    int height = (int)g->vrange2 - (int)g->vrange1;
    bool interlaced480 = (g->disp_mode & 0x24) == 0x24;
    int x, y;
    if (height <= 0 || height > 288) height = 240;
    if (interlaced480) height *= 2;
    *w = width; *h = height;
    if (!g->disp_enabled) {
        memset(rgba, 0, (size_t)width * (size_t)height * 4);
        return;
    }
    if (g->disp_mode & 0x10) {
        /* 24-bit display */
        for (y = 0; y < height; y++) {
            u32 vy = (g->disp_y + (u32)y) & 511;
            const u8 *line = (const u8 *)&g->vram[vy * 1024];
            for (x = 0; x < width; x++) {
                u32 bx = (g->disp_x * 2 + (u32)x * 3) % 2048;
                u8 r = line[bx], gg = line[(bx + 1) % 2048], b = line[(bx + 2) % 2048];
                rgba[y * width + x] = 0xFF000000u | ((u32)b << 16) | ((u32)gg << 8) | r;
            }
        }
    } else {
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                rgba[y * width + x] = rgb555_to_rgba(VRAM(g, g->disp_x + (u32)x, g->disp_y + (u32)y));
    }
}
