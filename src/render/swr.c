/* Software enhanced renderer (see swr.h).
 *
 * The high-resolution VRAM holds one u32 per sample: R | G<<8 | B<<16 |
 * mask<<24. Each console VRAM pixel maps to an sx x sy block, where
 * sx = internal_scale * aa_x and sy = internal_scale * aa_y. Values written by
 * CPU uploads, fills and copies are stored expanded from 15-bit exactly, so
 * texture and CLUT words can be reconstructed bit-exactly (to15()). Drawn
 * pixels keep 8-bit precision in true-colour mode.
 *
 * Rasterisation samples the centre of every high-resolution pixel; texture
 * coordinates and colours are interpolated affinely (as on the console).
 * Parallelism: the list is cut into segments at read-after-write hazards
 * (a texture/CLUT/copy source overlapping anything written earlier in the
 * segment); each segment is replayed by every worker on its own horizontal
 * band of the high-resolution VRAM. */
#include "swr.h"
#include "../common/thread.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCALE 16

struct nr_swr {
    nr_gfx_settings s;
    int S, ax, ay, sx, sy;
    int W, H;
    u32 *vram;
    u32 copy_line[1024 * MAX_SCALE];
    size_t vram_count;
    u32 *out;
    size_t out_cap;
    nr_pool *pool;
    int bands;
    u64 barriers;
    /* job context */
    const nr_rlist *l;
    u32 seg_a, seg_b;
    const nr_rdisplay *disp;
    int out_w, out_h;
};

static const s8 k_dither[4][4] = {
    { -4, 0, -3, 1 },
    { 2, -2, 3, -1 },
    { -3, 1, -4, 0 },
    { 3, -1, 2, -2 },
};

static inline u32 expand15(u16 v)
{
    u32 r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return r | (g << 8) | (b << 16) | ((u32)(v >> 15) << 24);
}

static inline u16 to15(u32 p)
{
    return (u16)(((p >> 3) & 31) | (((p >> 11) & 31) << 5) | (((p >> 19) & 31) << 10) | (((p >> 24) & 1) << 15));
}

nr_swr *nr_swr_new(int threads)
{
    nr_swr *r = (nr_swr *)calloc(1, sizeof(nr_swr));
    if (!r) return NULL;
    r->pool = nr_pool_new(threads);
    r->bands = nr_pool_size(r->pool) * 2;
    if (r->bands < 1) r->bands = 1;
    nr_gfx_defaults(&r->s);
    return r;
}

void nr_swr_free(nr_swr *r)
{
    if (!r) return;
    nr_pool_free(r->pool);
    free(r->vram);
    free(r->out);
    free(r);
}

bool nr_swr_configure(nr_swr *r, const nr_gfx_settings *s, bool *needs_sync)
{
    int S, ax, ay;
    if (!r || !s || !needs_sync) return false;
    S = s->internal_scale;
    if (S < 1) S = 1;
    if (S > 8) S = 8;
    nr_gfx_aa_grid(s->antialias, &ax, &ay);
    while (S * ax > MAX_SCALE || S * ay > MAX_SCALE) { if (S > 1) S--; else { ax = ay = 1; } }
    *needs_sync = false;
    if (!r->vram || S * ax != r->sx || S * ay != r->sy) {
        size_t n = (size_t)1024 * S * ax * 512 * S * ay;
        u32 *v;
        if (n > SIZE_MAX / sizeof(u32)) return false;
        v = (u32 *)calloc(n, sizeof(u32));
        if (!v) {
            NR_ERR("render", "not enough memory for internal resolution %dx with AA %dx (%lu MB)", S, ax * ay, (unsigned long)(n * sizeof(u32) >> 20));
            return false;
        }
        free(r->vram);
        r->vram = v;
        r->vram_count = n;
        r->sx = S * ax; r->sy = S * ay;
        r->W = 1024 * r->sx; r->H = 512 * r->sy;
        *needs_sync = true;
    }
    r->s = *s;
    r->S = S; r->ax = ax; r->ay = ay;
    r->s.internal_scale = S;
    return true;
}

void nr_swr_scale(const nr_swr *r, int *sx, int *sy) { *sx = r->sx; *sy = r->sy; }
u64 nr_swr_barriers(const nr_swr *r) { return r->barriers; }
u32 nr_swr_pixel(const nr_swr *r, int hx, int hy)
{
    if (!r || !r->vram || hx < 0 || hy < 0 || hx >= r->W || hy >= r->H) return 0;
    return r->vram[(size_t)hy * (size_t)r->W + (size_t)hx];
}

/* ----------------------------------------------------------- VRAM access */

static inline u32 at_native(const nr_swr *r, u32 x, u32 y)
{
    return r->vram[(size_t)(y & 511u) * (size_t)r->sy * (size_t)r->W + (size_t)(x & 1023u) * (size_t)r->sx];
}

/* Texel lookup in native texel coordinates. Returns false for transparent
 * texels (15-bit value 0000h). */
static inline bool texel(const nr_swr *r, const nr_rcmd *c, s32 u, s32 v, u32 *out)
{
    u32 mx = (c->texwin & 0x1F) * 8, my = ((c->texwin >> 5) & 0x1F) * 8;
    u32 ox = ((c->texwin >> 10) & 0x1F) * 8, oy = ((c->texwin >> 15) & 0x1F) * 8;
    u32 uu = (u32)u & 0xFF, vv = (u32)v & 0xFF, p;
    uu = (uu & ~mx) | (ox & mx);
    vv = (vv & ~my) | (oy & my);
    switch (c->tp_mode) {
    case 0: {
        u16 w = to15(at_native(r, c->tp_x + (uu >> 2), c->tp_y + vv));
        p = at_native(r, c->clut_x + ((w >> ((uu & 3) * 4)) & 0xF), c->clut_y);
        break;
    }
    case 1: {
        u16 w = to15(at_native(r, c->tp_x + (uu >> 1), c->tp_y + vv));
        p = at_native(r, c->clut_x + ((w >> ((uu & 1) * 8)) & 0xFF), c->clut_y);
        break;
    }
    default:
        p = at_native(r, c->tp_x + uu, c->tp_y + vv);
        break;
    }
    if (to15(p) == 0) return false;
    *out = p;
    return true;
}

static inline s32 clampi(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Upper texel bound used when sampling. Above 1x a polygon also covers the
 * fractional area next to its right/bottom edges, which the console never
 * draws, where the interpolated coordinate reaches the vertex maximum (one
 * texel outside the picture: the seam between the two halves of a 320-pixel
 * background). The console's last pixel uses max - 1, so clamp to that. */
static inline s32 uv_hi(const nr_swr *r, const nr_rcmd *c, int axis, bool sprite)
{
    s32 lo = axis ? c->uv_min_v : c->uv_min_u, hi = axis ? c->uv_max_v : c->uv_max_u;
    if (!sprite && (r->sx > 1 || r->sy > 1) && hi > lo) hi--;
    return hi;
}

/* bilinear lookup, taps clamped to the primitive's own texel rectangle */
static bool texel_bilinear(const nr_swr *r, const nr_rcmd *c, float u, float v, bool sprite, u32 *out)
{
    float fu = sprite ? u - 0.5f : u, fv = sprite ? v - 0.5f : v;
    s32 iu = (s32)floorf(fu), iv = (s32)floorf(fv);
    float tu = fu - (float)iu, tv = fv - (float)iv;
    float acc[3] = { 0, 0, 0 }, wsum = 0;
    u32 nearest = 0, t;
    bool have_nearest = false;
    int k;
    for (k = 0; k < 4; k++) {
        s32 su = clampi(iu + (k & 1), c->uv_min_u, uv_hi(r, c, 0, sprite));
        s32 sv = clampi(iv + (k >> 1), c->uv_min_v, uv_hi(r, c, 1, sprite));
        float w = ((k & 1) ? tu : 1.0f - tu) * ((k >> 1) ? tv : 1.0f - tv);
        if (texel(r, c, su, sv, &t)) {
            acc[0] += w * (float)(t & 0xFF);
            acc[1] += w * (float)((t >> 8) & 0xFF);
            acc[2] += w * (float)((t >> 16) & 0xFF);
            wsum += w;
            if (!have_nearest || ((k & 1) == (tu >= 0.5f) && (k >> 1) == (tv >= 0.5f))) { nearest = t; have_nearest = true; }
        }
    }
    if (wsum < 0.5f) return false;
    {
        u32 cr = (u32)(acc[0] / wsum + 0.5f), cg = (u32)(acc[1] / wsum + 0.5f), cb = (u32)(acc[2] / wsum + 0.5f);
        *out = cr | (cg << 8) | (cb << 16) | (nearest & 0xFF000000u);
    }
    return true;
}

/* ------------------------------------------------------------ pixel stage */

typedef struct {
    const nr_rcmd *c;
    bool textured, raw, semi, dither, check_mask, filter, sprite;
    u32 set_mask;
} shade_ctx;

static inline void put(const nr_swr *r, const shade_ctx *sc, int X, int Y, float fr, float fg, float fb, float u, float v)
{
    u32 *dst = &r->vram[(size_t)Y * r->W + X];
    u32 t = 0, outm, res;
    s32 cr, cg, cb;
    bool semi_px;
    if (sc->check_mask && (*dst & 0x01000000u)) return;
    if (sc->textured) {
        if (sc->filter) {
            if (!texel_bilinear(r, sc->c, u, v, sc->sprite, &t)) return;
        } else {
            s32 tu = (s32)floorf(sc->sprite ? u : u + 0.5f), tv = (s32)floorf(sc->sprite ? v : v + 0.5f);
            tu = clampi(tu, sc->c->uv_min_u, uv_hi(r, sc->c, 0, sc->sprite));
            tv = clampi(tv, sc->c->uv_min_v, uv_hi(r, sc->c, 1, sc->sprite));
            if (!texel(r, sc->c, tu, tv, &t)) return;
        }
        semi_px = sc->semi && (t & 0x01000000u);
        outm = t & 0x01000000u;
        if (sc->raw) {
            cr = (s32)(t & 0xFF); cg = (s32)((t >> 8) & 0xFF); cb = (s32)((t >> 16) & 0xFF);
            if (!r->s.true_color) { cr &= 0xF8; cg &= 0xF8; cb &= 0xF8; }
        } else if (r->s.true_color) {
            cr = (s32)((float)(t & 0xFF) * fr / 128.0f + 0.5f);
            cg = (s32)((float)((t >> 8) & 0xFF) * fg / 128.0f + 0.5f);
            cb = (s32)((float)((t >> 16) & 0xFF) * fb / 128.0f + 0.5f);
        } else {
            cr = (((s32)(t & 0xF8)) * (s32)fr) >> 7;
            cg = (((s32)((t >> 8) & 0xF8)) * (s32)fg) >> 7;
            cb = (((s32)((t >> 16) & 0xF8)) * (s32)fb) >> 7;
        }
    } else {
        semi_px = sc->semi;
        outm = 0;
        cr = (s32)fr; cg = (s32)fg; cb = (s32)fb;
        if (r->s.true_color) { cr = (s32)(fr + 0.5f); cg = (s32)(fg + 0.5f); cb = (s32)(fb + 0.5f); }
    }
    if (sc->dither) {
        s32 d = k_dither[(Y / r->sy) & 3][(X / r->sx) & 3];
        cr += d; cg += d; cb += d;
    }
    cr = clampi(cr, 0, 255); cg = clampi(cg, 0, 255); cb = clampi(cb, 0, 255);
    if (!r->s.true_color) {
        cr >>= 3; cg >>= 3; cb >>= 3;
        if (semi_px) {
            u32 b = *dst;
            s32 br = (s32)((b >> 3) & 31), bg = (s32)((b >> 11) & 31), bb = (s32)((b >> 19) & 31);
            switch (sc->c->semi_mode) {
            case 0: cr = (br + cr) >> 1; cg = (bg + cg) >> 1; cb = (bb + cb) >> 1; break;
            case 1: cr += br; cg += bg; cb += bb; break;
            case 2: cr = br - cr; cg = bg - cg; cb = bb - cb; break;
            default: cr = br + (cr >> 2); cg = bg + (cg >> 2); cb = bb + (cb >> 2); break;
            }
            cr = clampi(cr, 0, 31); cg = clampi(cg, 0, 31); cb = clampi(cb, 0, 31);
        }
        cr = (cr << 3) | (cr >> 2); cg = (cg << 3) | (cg >> 2); cb = (cb << 3) | (cb >> 2);
    } else if (semi_px) {
        u32 b = *dst;
        s32 br = (s32)(b & 0xFF), bg = (s32)((b >> 8) & 0xFF), bb = (s32)((b >> 16) & 0xFF);
        switch (sc->c->semi_mode) {
        case 0: cr = (br + cr) >> 1; cg = (bg + cg) >> 1; cb = (bb + cb) >> 1; break;
        case 1: cr += br; cg += bg; cb += bb; break;
        case 2: cr = br - cr; cg = bg - cg; cb = bb - cb; break;
        default: cr = br + (cr >> 2); cg = bg + (cg >> 2); cb = bb + (cb >> 2); break;
        }
        cr = clampi(cr, 0, 255); cg = clampi(cg, 0, 255); cb = clampi(cb, 0, 255);
    }
    res = (u32)cr | ((u32)cg << 8) | ((u32)cb << 16) | outm | sc->set_mask;
    *dst = res;
}

static void shade_init(const nr_swr *r, const nr_rcmd *c, shade_ctx *sc)
{
    sc->c = c;
    sc->textured = (c->flags & RF_TEXTURED) != 0;
    sc->raw = (c->flags & RF_RAW) != 0;
    sc->semi = (c->flags & RF_SEMI) != 0;
    sc->sprite = (c->flags & RF_SPRITE) != 0;
    sc->dither = (c->flags & RF_DITHER) && r->s.dithering && !r->s.true_color;
    sc->check_mask = (c->flags & RF_CHECK_MASK) != 0;
    sc->set_mask = (c->flags & RF_SET_MASK) ? 0x01000000u : 0;
    sc->filter = sc->textured && (r->s.texture_filter >= 2 || (r->s.texture_filter == 1 && !sc->sprite));
}

/* ------------------------------------------------------------- triangles */

static void draw_tri(const nr_swr *r, const nr_rcmd *c, int by0, int by1)
{
    double x[3], y[3], area, w[3], dwx[3], ny, inv;
    float cr[3], cg[3], cb[3], cu[3], cv[3], iw[3];
    bool persp = false;
    bool incl[3];
    int i, X0, X1, Y0, Y1, X, Y, order[3] = { 0, 1, 2 };
    shade_ctx sc;
    double sx = r->sx, sy = r->sy;
    for (i = 0; i < 3; i++) {
        const nr_rvtx *v = &c->v[i];
        x[i] = (c->flags & RF_PRECISE) ? v->fx : v->x;
        y[i] = (c->flags & RF_PRECISE) ? v->fy : v->y;
        /* sprites are pixel-edge rectangles: covering [x, x+w) native pixels
         * means covering the sample points x..x+w-1 */
        if (c->flags & RF_SPRITE) { x[i] -= 0.5; y[i] -= 0.5; }
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    if (area == 0) return;
    if (area < 0) { order[1] = 2; order[2] = 1; area = -area; }
    {
        double tx[3], ty[3];
        for (i = 0; i < 3; i++) {
            const nr_rvtx *v = &c->v[order[i]];
            tx[i] = x[order[i]]; ty[i] = y[order[i]];
            cr[i] = v->r; cg[i] = v->g; cb[i] = v->b; cu[i] = v->u; cv[i] = v->v;
            iw[i] = ((c->flags & RF_PRECISE) && v->fw > 0.0f) ? 1.0f / v->fw : 0.0f;
        }
        memcpy(x, tx, sizeof(x)); memcpy(y, ty, sizeof(y));
    }
    if (!(c->flags & RF_GOURAUD)) for (i = 1; i < 3; i++) { cr[i] = cr[0]; cg[i] = cg[0]; cb[i] = cb[0]; }
    persp = (c->flags & RF_PRECISE) && iw[0] > 0 && iw[1] > 0 && iw[2] > 0 && (iw[0] != iw[1] || iw[1] != iw[2]);
    /* top-left rule, as in the native rasterizer: edge i is opposite vertex i */
    for (i = 0; i < 3; i++) {
        int a = (i + 1) % 3, b = (i + 2) % 3;
        incl[i] = (y[a] == y[b] && x[b] > x[a]) || (y[b] < y[a]);
    }
    {
        double minx = x[0], maxx = x[0], miny = y[0], maxy = y[0];
        for (i = 1; i < 3; i++) {
            if (x[i] < minx) minx = x[i];
            if (x[i] > maxx) maxx = x[i];
            if (y[i] < miny) miny = y[i];
            if (y[i] > maxy) maxy = y[i];
        }
        X0 = (int)floor((minx + 0.5) * sx) - 1; X1 = (int)ceil((maxx + 0.5) * sx) + 1;
        Y0 = (int)floor((miny + 0.5) * sy) - 1; Y1 = (int)ceil((maxy + 0.5) * sy) + 1;
    }
    if (X0 < c->clip_x1 * r->sx) X0 = c->clip_x1 * r->sx;
    if (X1 > (c->clip_x2 + 1) * r->sx) X1 = (c->clip_x2 + 1) * r->sx;
    if (Y0 < c->clip_y1 * r->sy) Y0 = c->clip_y1 * r->sy;
    if (Y1 > (c->clip_y2 + 1) * r->sy) Y1 = (c->clip_y2 + 1) * r->sy;
    if (Y0 < by0) Y0 = by0;
    if (Y1 > by1) Y1 = by1;
    if (X0 < 0) X0 = 0;
    if (X1 > r->W) X1 = r->W;
    if (X0 >= X1 || Y0 >= Y1) return;
    shade_init(r, c, &sc);
    inv = 1.0 / area;
    for (i = 0; i < 3; i++) {
        int a = (i + 1) % 3, b = (i + 2) % 3;
        dwx[i] = -(y[b] - y[a]) / sx;
    }
    for (Y = Y0; Y < Y1; Y++) {
        /* the console samples polygons at integer coordinates: native pixel n
         * is the point n, so hires sample X maps to (X + 0.5) / S - 0.5 */
        double nx0 = (X0 + 0.5) / sx - 0.5;
        ny = (Y + 0.5) / sy - 0.5;
        for (i = 0; i < 3; i++) {
            int a = (i + 1) % 3, b = (i + 2) % 3;
            w[i] = (x[b] - x[a]) * (ny - y[a]) - (y[b] - y[a]) * (nx0 - x[a]);
        }
        for (X = X0; X < X1; X++) {
            if ((w[0] > 0 || (w[0] == 0 && incl[0])) && (w[1] > 0 || (w[1] == 0 && incl[1])) &&
                (w[2] > 0 || (w[2] == 0 && incl[2]))) {
                float l0 = (float)(w[0] * inv), l1 = (float)(w[1] * inv), l2 = (float)(w[2] * inv);
                if (persp) {
                    /* perspective-correct weights (PGXP depth) */
                    float a0 = l0 * iw[0], a1 = l1 * iw[1], a2 = l2 * iw[2], s = a0 + a1 + a2;
                    if (s > 0) { l0 = a0 / s; l1 = a1 / s; l2 = a2 / s; }
                }
                put(r, &sc, X, Y,
                    l0 * cr[0] + l1 * cr[1] + l2 * cr[2],
                    l0 * cg[0] + l1 * cg[1] + l2 * cg[2],
                    l0 * cb[0] + l1 * cb[1] + l2 * cb[2],
                    l0 * cu[0] + l1 * cu[1] + l2 * cu[2],
                    l0 * cv[0] + l1 * cv[1] + l2 * cv[2]);
            }
            w[0] += dwx[0]; w[1] += dwx[1]; w[2] += dwx[2];
        }
    }
}

/* ---------------------------------------------------------------- lines */

static void draw_line(const nr_swr *r, const nr_rcmd *c, int by0, int by1)
{
    double ax = c->v[0].x + 0.5, ay = c->v[0].y + 0.5, bx = c->v[1].x + 0.5, by = c->v[1].y + 0.5;
    double dx = bx - ax, dy = by - ay;
    bool xmajor = fabs(dx) >= fabs(dy);
    int steps, i;
    shade_ctx sc;
    shade_init(r, c, &sc);
    if (fabs(dx) >= 1024 || fabs(dy) >= 512) return;
    steps = (int)ceil((xmajor ? fabs(dx) * r->sx : fabs(dy) * r->sy)) + (xmajor ? r->sx : r->sy);
    for (i = 0; i < steps; i++) {
        double t = steps > 1 ? (double)i / (double)(steps - 1) : 0.0;
        double px = ax + dx * t, py = ay + dy * t;
        float fr = (float)(c->v[0].r + (c->v[1].r - c->v[0].r) * t);
        float fg = (float)(c->v[0].g + (c->v[1].g - c->v[0].g) * t);
        float fb = (float)(c->v[0].b + (c->v[1].b - c->v[0].b) * t);
        int k, n = xmajor ? r->sy : r->sx;
        int X = (int)floor(px * r->sx), Y = (int)floor(py * r->sy);
        if (!(c->flags & RF_GOURAUD)) { fr = c->v[0].r; fg = c->v[0].g; fb = c->v[0].b; }
        /* a span one native pixel thick across the minor axis */
        if (xmajor) Y = (int)floor(py) * r->sy; else X = (int)floor(px) * r->sx;
        for (k = 0; k < n; k++) {
            int XX = xmajor ? X : X + k, YY = xmajor ? Y + k : Y;
            if (XX < c->clip_x1 * r->sx || XX >= (c->clip_x2 + 1) * r->sx) continue;
            if (YY < c->clip_y1 * r->sy || YY >= (c->clip_y2 + 1) * r->sy) continue;
            if (YY < by0 || YY >= by1 || XX < 0 || XX >= r->W) continue;
            put(r, &sc, XX, YY, fr, fg, fb, 0, 0);
        }
    }
}

/* ------------------------------------------------------- VRAM operations */

static void fill_block(nr_swr *r, u32 nx, u32 ny, u32 val, int by0, int by1)
{
    int Y0 = (int)((ny & 511) * r->sy), X0 = (int)((nx & 1023) * r->sx), j, i;
    for (j = 0; j < r->sy; j++) {
        int Y = Y0 + j;
        u32 *row;
        if (Y < by0 || Y >= by1) continue;
        row = &r->vram[(size_t)Y * r->W + X0];
        for (i = 0; i < r->sx; i++) row[i] = val;
    }
}

static bool rows_touch(const nr_swr *r, u32 ny, int by0, int by1)
{
    int Y0 = (int)((ny & 511) * r->sy);
    return Y0 + r->sy > by0 && Y0 < by1;
}

static void do_upload(nr_swr *r, const nr_rlist *l, const nr_rcmd *c, int by0, int by1)
{
    u32 i, j;
    const u16 *src = l->data + c->data;
    for (j = 0; j < c->h; j++) {
        if (!rows_touch(r, c->y + j, by0, by1)) continue;
        for (i = 0; i < c->w; i++) fill_block(r, c->x + i, c->y + j, expand15(src[j * c->w + i]), by0, by1);
    }
}

static void do_fill(nr_swr *r, const nr_rcmd *c, int by0, int by1)
{
    u32 i, j, val = expand15((u16)c->color);
    for (j = 0; j < c->h; j++) {
        if (!rows_touch(r, c->y + j, by0, by1)) continue;
        for (i = 0; i < c->w; i++) fill_block(r, c->x + i, c->y + j, val, by0, by1);
    }
}

/* executed serially (whole height): source and destination may overlap */
static void do_copy(nr_swr *r, const nr_rcmd *c)
{
    u32 j, i;
    int k, n = r->sx;
    u32 *line = r->copy_line;
    bool chk = (c->flags & RF_CHECK_MASK) != 0;
    u32 setm = (c->flags & RF_SET_MASK) ? 0x01000000u : 0;
    if (c->w > 1024 || c->h > 512) { NR_ERR("render", "invalid VRAM copy dimensions"); return; }
    for (j = 0; j < c->h; j++) {
        for (k = 0; k < r->sy; k++) {
            int sY = (int)(((c->sy + j) & 511) * r->sy) + k, dY = (int)(((c->y + j) & 511) * r->sy) + k;
            for (i = 0; i < c->w; i++) {
                const u32 *s = &r->vram[(size_t)sY * r->W + (size_t)((c->sx + i) & 1023u) * (size_t)r->sx];
                memcpy(line + i * n, s, (size_t)n * sizeof(u32));
            }
            for (i = 0; i < c->w; i++) {
                u32 *d = &r->vram[(size_t)dY * r->W + (size_t)((c->x + i) & 1023u) * (size_t)r->sx];
                int q;
                for (q = 0; q < n; q++) {
                    if (chk && (d[q] & 0x01000000u)) continue;
                    d[q] = line[i * n + q] | setm;
                }
            }
        }
    }
}

/* --------------------------------------------------------------- replay */

static void run_cmd(nr_swr *r, const nr_rlist *l, const nr_rcmd *c, int by0, int by1)
{
    switch (c->type) {
    case R_TRI: draw_tri(r, c, by0, by1); break;
    case R_LINE: draw_line(r, c, by0, by1); break;
    case R_FILL: do_fill(r, c, by0, by1); break;
    case R_UPLOAD: do_upload(r, l, c, by0, by1); break;
    case R_COPY: if (by0 == 0) do_copy(r, c); break;   /* serial segment */
    default: break;
    }
}

static void band_job(void *ctx, int b)
{
    nr_swr *r = (nr_swr *)ctx;
    int by0 = (int)((long long)r->H * b / r->bands), by1 = (int)((long long)r->H * (b + 1) / r->bands);
    u32 i;
    for (i = r->seg_a; i < r->seg_b; i++) run_cmd(r, r->l, &r->l->cmd[i], by0, by1);
}

static void run_segment(nr_swr *r, const nr_rlist *l, u32 a, u32 b)
{
    if (a >= b) return;
    r->l = l; r->seg_a = a; r->seg_b = b;
    nr_pool_run(r->pool, r->bands, band_job, r);
}

typedef struct { int x0, y0, x1, y1; bool any; } bbox;

static void bb_add(bbox *b, int x0, int y0, int x1, int y1)
{
    if (x1 > 1024 || y1 > 512) { x0 = 0; y0 = 0; x1 = 1024; y1 = 512; }   /* wraps: be conservative */
    if (!b->any) { b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1; b->any = true; return; }
    if (x0 < b->x0) b->x0 = x0;
    if (y0 < b->y0) b->y0 = y0;
    if (x1 > b->x1) b->x1 = x1;
    if (y1 > b->y1) b->y1 = y1;
}

static bool bb_hit(const bbox *b, int x0, int y0, int x1, int y1)
{
    if (!b->any) return false;
    if (x1 > 1024 || y1 > 512) return true;
    return x0 < b->x1 && x1 > b->x0 && y0 < b->y1 && y1 > b->y0;
}

static bool reads_hazard(const bbox *w, const nr_rcmd *c)
{
    if (c->type == R_COPY) return true;
    if ((c->type == R_TRI) && (c->flags & RF_TEXTURED)) {
        int tw = c->tp_mode == 0 ? 64 : c->tp_mode == 1 ? 128 : 256;
        if (bb_hit(w, c->tp_x, c->tp_y, c->tp_x + tw, c->tp_y + 256)) return true;
        if (c->tp_mode < 2 && bb_hit(w, c->clut_x, c->clut_y, c->clut_x + (c->tp_mode ? 256 : 16), c->clut_y + 1)) return true;
    }
    return false;
}

static void writes_of(const nr_rcmd *c, bbox *w)
{
    switch (c->type) {
    case R_TRI: case R_LINE: {
        int i, n = c->type == R_TRI ? 3 : 2, x0 = 1024, y0 = 512, x1 = 0, y1 = 0;
        for (i = 0; i < n; i++) {
            if (c->v[i].x < x0) x0 = c->v[i].x;
            if (c->v[i].y < y0) y0 = c->v[i].y;
            if (c->v[i].x + 1 > x1) x1 = c->v[i].x + 1;
            if (c->v[i].y + 1 > y1) y1 = c->v[i].y + 1;
        }
        if (x0 < c->clip_x1) x0 = c->clip_x1;
        if (y0 < c->clip_y1) y0 = c->clip_y1;
        if (x1 > c->clip_x2 + 1) x1 = c->clip_x2 + 1;
        if (y1 > c->clip_y2 + 1) y1 = c->clip_y2 + 1;
        if (x0 < x1 && y0 < y1) bb_add(w, x0, y0, x1, y1);
        break;
    }
    default:
        bb_add(w, c->x, c->y, c->x + c->w, c->y + c->h);
        break;
    }
}

void nr_swr_execute(nr_swr *r, const nr_rlist *l)
{
    u32 i, seg = 0;
    bbox w = { 0, 0, 0, 0, false };
    if (!r || !r->vram || !l || r->sx < 1 || r->sx > MAX_SCALE || r->sy < 1 || r->sy > MAX_SCALE ||
        r->W != 1024 * r->sx || r->H != 512 * r->sy ||
        r->vram_count != (size_t)r->W * (size_t)r->H || l->n > l->cap || (l->n && !l->cmd)) {
        NR_ERR("render", "invalid renderer state; replay rejected"); return;
    }
    for (i = 0; i < l->n; i++) {
        const nr_rcmd *c = &l->cmd[i];
        if ((c->type == R_COPY || c->type == R_FILL || c->type == R_UPLOAD) && (c->w > 1024 || c->h > 512)) {
            NR_ERR("render", "invalid VRAM command dimensions; replay rejected"); return;
        }
        if (c->type == R_UPLOAD && (!l->data || c->data > l->ndata ||
            (size_t)c->w * (size_t)c->h > l->ndata - c->data)) {
            NR_ERR("render", "invalid VRAM upload bounds; replay rejected"); return;
        }
    }
    for (i = 0; i < l->n; i++) {
        const nr_rcmd *c = &l->cmd[i];
        if (reads_hazard(&w, c)) {
            run_segment(r, l, seg, i);
            seg = i;
            w.any = false;
            r->barriers++;
            if (c->type == R_COPY) {
                /* copies run alone, on one thread */
                do_copy(r, c);
                seg = i + 1;
                continue;
            }
        }
        writes_of(c, &w);
    }
    run_segment(r, l, seg, l->n);
}

void nr_swr_sync(nr_swr *r, const u16 *vram)
{
    u32 x, y;
    if (!r || !r->vram || !vram) return;
    for (y = 0; y < 512; y++)
        for (x = 0; x < 1024; x++) fill_block(r, x, y, expand15(vram[y * 1024 + x]), 0, r->H);
}

/* ---------------------------------------------------------------- output */

static void out_job(void *ctx, int band)
{
    nr_swr *r = (nr_swr *)ctx;
    const nr_rdisplay *d = r->disp;
    int y0 = r->out_h * band / r->bands, y1 = r->out_h * (band + 1) / r->bands, x, y;
    int S = r->S, ax = r->ax, ay = r->ay;
    for (y = y0; y < y1; y++) {
        u32 *o = r->out + (size_t)y * r->out_w;
        if (d->rgb24) {
            const u8 *line = (const u8 *)(r->l->disp24 + (size_t)(y / S) * 1024);
            for (x = 0; x < r->out_w; x++) {
                u32 bx = (d->disp_x * 2 + (u32)(x / S) * 3) % 2048;
                o[x] = 0xFF000000u | line[bx] | ((u32)line[(bx + 1) % 2048] << 8) | ((u32)line[(bx + 2) % 2048] << 16);
            }
            continue;
        }
        for (x = 0; x < r->out_w; x++) {
            u32 sr = 0, sg = 0, sb = 0;
            int i, j, n = ax * ay;
            int hx = (int)((d->disp_x * (u32)r->sx + (u32)(x * ax)) % (u32)r->W);
            int hy = (int)((d->disp_y * (u32)r->sy + (u32)(y * ay)) % (u32)r->H);
            for (j = 0; j < ay; j++) {
                const u32 *row = &r->vram[(size_t)((hy + j) % r->H) * r->W];
                for (i = 0; i < ax; i++) {
                    u32 p = row[(hx + i) % r->W];
                    sr += p & 0xFF; sg += (p >> 8) & 0xFF; sb += (p >> 16) & 0xFF;
                }
            }
            if (n > 1) { sr = (sr + (u32)n / 2) / (u32)n; sg = (sg + (u32)n / 2) / (u32)n; sb = (sb + (u32)n / 2) / (u32)n; }
            o[x] = 0xFF000000u | sr | (sg << 8) | (sb << 16);
        }
    }
}

const u32 *nr_swr_output(nr_swr *r, const nr_rlist *l, int *w, int *h)
{
    const nr_rdisplay *d = &l->display;
    size_t need;
    int dw = d->width > 0 ? d->width : 320, dh = d->height > 0 ? d->height : 240;
    if (!r->vram || r->S < 1 || dw > 1024 || dh > 512 || (d->rgb24 && !l->disp24)) {
        NR_ERR("render", "invalid output dimensions or display buffer"); return NULL;
    }
    r->out_w = dw * r->S;
    r->out_h = dh * r->S;
    need = (size_t)r->out_w * r->out_h;
    if (need > r->out_cap) {
        u32 *n = (u32 *)realloc(r->out, need * sizeof(u32));
        if (!n) return NULL;
        r->out = n; r->out_cap = need;
    }
    *w = r->out_w; *h = r->out_h;
    if (!d->enabled) { memset(r->out, 0, need * sizeof(u32)); return r->out; }
    r->disp = d;
    r->l = l;
    nr_pool_run(r->pool, r->bands, out_job, r);
    return r->out;
}
