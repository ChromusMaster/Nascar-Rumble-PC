#include <stdlib.h>
#include <string.h>

#include "gpu_replay.h"

/* ------------------------------------------------------------ replay */

void nr_gpu_replay_init(nr_gpu_replay_state *st) { memset(st, 0, sizeof(*st)); }

void nr_gpu_replay_free(nr_gpu_replay_state *st)
{
    free(st->v);
    free(st->tmp);
    memset(st, 0, sizeof(*st));
}

void nr_gpu_quad(nr_gvtx q[6], float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    static const int idx[6] = { 0, 1, 2, 1, 3, 2 };
    int i;
    memset(q, 0, sizeof(nr_gvtx) * 6);
    for (i = 0; i < 6; i++) {
        int c = idx[i];
        q[i].x = (c & 1) ? x1 : x0;
        q[i].y = (c & 2) ? y1 : y0;
        q[i].u = (c & 1) ? u1 : u0;
        q[i].v = (c & 2) ? v1 : v0;
        q[i].r = q[i].g = q[i].b = q[i].a = 255;
    }
}

static nr_gvtx *grow(nr_gpu_replay_state *st, int n)
{
    if (st->n + n > st->cap) {
        int nc = st->cap ? st->cap * 2 : 4096;
        nr_gvtx *p;
        while (nc < st->n + n) nc *= 2;
        p = (nr_gvtx *)realloc(st->v, (size_t)nc * sizeof(nr_gvtx));
        if (!p) return NULL;
        st->v = p;
        st->cap = nc;
    }
    st->n += n;
    return st->v + st->n - n;
}

static void flush(nr_gpu_replay_state *st, const nr_gpu_ops *ops)
{
    if (st->n > 0) {
        ops->draw(ops->ctx, st->v, st->n, 0, 0, st->scissor);
        st->draws++;
    }
    st->n = 0;
}

static void mark(nr_gpu_replay_state *st, int x0, int y0, int x1, int y1)
{
    if (x1 > 1024 || y1 > 512) { x0 = 0; y0 = 0; x1 = 1024; y1 = 512; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (!st->dirty) { st->dx0 = x0; st->dy0 = y0; st->dx1 = x1; st->dy1 = y1; st->dirty = true; return; }
    if (x0 < st->dx0) st->dx0 = x0;
    if (y0 < st->dy0) st->dy0 = y0;
    if (x1 > st->dx1) st->dx1 = x1;
    if (y1 > st->dy1) st->dy1 = y1;
}

static bool hits(const nr_gpu_replay_state *st, int x0, int y0, int x1, int y1)
{
    if (!st->dirty) return false;
    if (x1 > 1024 || y1 > 512) return true;
    return x0 < st->dx1 && x1 > st->dx0 && y0 < st->dy1 && y1 > st->dy0;
}

static void refresh(nr_gpu_replay_state *st, const nr_gpu_ops *ops)
{
    flush(st, ops);
    if (st->dirty) {
        ops->refresh(ops->ctx, st->dx0, st->dy0, st->dx1, st->dy1);
        st->refreshes++;
    }
    st->dirty = false;
}

static void fill_common(nr_gvtx *o, const nr_rcmd *c, const nr_gfx_settings *s, bool hires)
{
    u32 fl = 0;
    bool sprite = (c->flags & RF_SPRITE) != 0;
    if (c->flags & RF_TEXTURED) fl |= GV_TEXTURED;
    if (c->flags & RF_RAW) fl |= GV_RAW;
    if (c->flags & RF_SEMI) fl |= GV_SEMI;
    if ((c->flags & RF_DITHER) && s->dithering && !s->true_color) fl |= GV_DITHER;
    if (sprite) fl |= GV_SPRITE;
    if ((c->flags & RF_TEXTURED) && (s->texture_filter >= 2 || (s->texture_filter == 1 && !sprite))) fl |= GV_FILTER;
    if (c->flags & RF_SET_MASK) fl |= GV_SETMASK;
    o->tpx = c->tp_x; o->tpy = c->tp_y; o->clx = c->clut_x; o->cly = c->clut_y;
    o->mode = c->tp_mode;
    o->flags = (float)fl;
    o->umin = c->uv_min_u; o->vmin = c->uv_min_v; o->umax = c->uv_max_u; o->vmax = c->uv_max_v;
    /* Above 1x a polygon also covers the fractional area next to its right
     * and bottom edges, which the console never draws; its texture
     * coordinate there reaches the vertex maximum (one texel outside the
     * picture, e.g. the seam between the two halves of a 320-pixel
     * background). Clamp to max - 1 as the console's last pixel does. */
    if (hires && !sprite) {
        if (o->umax > o->umin) o->umax -= 1;
        if (o->vmax > o->vmin) o->vmax -= 1;
    }
    o->twmx = (float)((c->texwin & 0x1F) * 8);
    o->twmy = (float)(((c->texwin >> 5) & 0x1F) * 8);
    o->twox = (float)(((c->texwin >> 10) & 0x1F) * 8);
    o->twoy = (float)(((c->texwin >> 15) & 0x1F) * 8);
}

static void set_vtx(nr_gvtx *o, const nr_rvtx *v, const nr_rvtx *flat, bool gouraud, int sx, int sy, const nr_rcmd *c, bool precise)
{
    /* The console samples polygons at integer coordinates (pixel n is the
     * point n) while D3D samples at pixel centres: shift polygons by half a
     * console pixel so 1x output matches the console. Sprites are already
     * expressed as pixel-edge rectangles. */
    float off = (c->flags & RF_SPRITE) ? 0.0f : 0.5f;
    o->x = ((precise ? v->fx : (float)v->x) + off) * (float)sx;
    o->y = ((precise ? v->fy : (float)v->y) + off) * (float)sy;
    o->r = gouraud ? v->r : flat->r;
    o->g = gouraud ? v->g : flat->g;
    o->b = gouraud ? v->b : flat->b;
    o->a = 255;
    o->u = v->u;
    o->v = v->v;
    o->pad0 = precise && c->v[0].fw > 0.0f && c->v[1].fw > 0.0f && c->v[2].fw > 0.0f ? v->fw : 1.0f;   /* w: perspective-correct interpolation */
    (void)c;
}

static void emit_prim(nr_gpu_replay_state *st, const nr_rcmd *c, const nr_gfx_settings *s, int sx, int sy, const nr_gpu_ops *ops)
{
    int sc[4], i;
    nr_gvtx tri[6];
    int nv = 0;
    bool gour = (c->flags & RF_GOURAUD) != 0;
    sc[0] = c->clip_x1 * sx; sc[1] = c->clip_y1 * sy;
    sc[2] = (c->clip_x2 + 1) * sx; sc[3] = (c->clip_y2 + 1) * sy;
    if (sc[0] >= sc[2] || sc[1] >= sc[3]) return;
    if (c->type == R_TRI) {
        for (i = 0; i < 3; i++) {
            fill_common(&tri[i], c, s, sx > 1 || sy > 1);
            set_vtx(&tri[i], &c->v[i], &c->v[0], gour, sx, sy, c, (c->flags & RF_PRECISE) != 0);
        }
        nv = 3;
    } else {
        /* line: a quad one console pixel thick along the minor axis */
        const nr_rvtx *a = &c->v[0], *b = &c->v[1];
        float ax = a->x, ay = a->y, bx = b->x, by = b->y;
        bool xm = (bx - ax) * (bx - ax) >= (by - ay) * (by - ay);
        float ex = xm ? (bx >= ax ? 1.0f : -1.0f) : 0, ey = xm ? 0 : (by >= ay ? 1.0f : -1.0f);
        float px[4], py[4];
        const nr_rvtx *src[4] = { a, b, b, a };
        static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        if (ex < 0) ax += 1;
        if (ey < 0) ay += 1;
        if (ex > 0) bx += 1;
        if (ey > 0) by += 1;
        px[0] = ax; py[0] = ay; px[1] = bx; py[1] = by;
        px[2] = bx + (xm ? 0 : 1); py[2] = by + (xm ? 1 : 0);
        px[3] = ax + (xm ? 0 : 1); py[3] = ay + (xm ? 1 : 0);
        for (i = 0; i < 6; i++) {
            int k = idx[i];
            fill_common(&tri[i], c, s, sx > 1 || sy > 1);
            tri[i].x = px[k] * (float)sx;
            tri[i].y = py[k] * (float)sy;
            tri[i].r = gour ? src[k]->r : a->r;
            tri[i].g = gour ? src[k]->g : a->g;
            tri[i].b = gour ? src[k]->b : a->b;
            tri[i].a = 255;
            tri[i].pad0 = 1.0f;
        }
        nv = 6;
    }
    if (st->n > 0 && memcmp(sc, st->scissor, sizeof(sc)) != 0) flush(st, ops);
    memcpy(st->scissor, sc, sizeof(sc));
    if (c->flags & RF_SEMI) {
        /* opaque texels first, then the semi-transparent ones with blending */
        nr_gvtx *o;
        flush(st, ops);
        if (c->flags & RF_TEXTURED) ops->draw(ops->ctx, tri, nv, 0, c->semi_mode, sc);
        ops->draw(ops->ctx, tri, nv, 1, c->semi_mode, sc);
        st->draws += 2;
        (void)o;
    } else {
        nr_gvtx *o = grow(st, nv);
        if (o) memcpy(o, tri, sizeof(nr_gvtx) * (size_t)nv);
    }
}

static void prim_bounds(const nr_rcmd *c, int *x0, int *y0, int *x1, int *y1)
{
    int i, n = c->type == R_TRI ? 3 : 2;
    *x0 = 1024; *y0 = 512; *x1 = 0; *y1 = 0;
    for (i = 0; i < n; i++) {
        if (c->v[i].x < *x0) *x0 = c->v[i].x;
        if (c->v[i].y < *y0) *y0 = c->v[i].y;
        if (c->v[i].x + 1 > *x1) *x1 = c->v[i].x + 1;
        if (c->v[i].y + 1 > *y1) *y1 = c->v[i].y + 1;
    }
    if (*x0 < c->clip_x1) *x0 = c->clip_x1;
    if (*y0 < c->clip_y1) *y0 = c->clip_y1;
    if (*x1 > c->clip_x2 + 1) *x1 = c->clip_x2 + 1;
    if (*y1 > c->clip_y2 + 1) *y1 = c->clip_y2 + 1;
}

static u32 *tmp_buf(nr_gpu_replay_state *st, size_t n)
{
    if (n > st->tmp_cap) {
        u32 *p = (u32 *)realloc(st->tmp, n * 4);
        if (!p) return NULL;
        st->tmp = p;
        st->tmp_cap = n;
    }
    return st->tmp;
}

void nr_gpu_replay(nr_gpu_replay_state *st, const nr_rlist *l, const nr_gfx_settings *s, int sx, int sy, const nr_gpu_ops *ops)
{
    u32 i;
    st->n = 0;
    for (i = 0; i < l->n; i++) {
        const nr_rcmd *c = &l->cmd[i];
        switch (c->type) {
        case R_TRI:
        case R_LINE: {
            int x0, y0, x1, y1;
            if ((c->flags & RF_TEXTURED) && st->dirty) {
                int tw = c->tp_mode == 0 ? 64 : c->tp_mode == 1 ? 128 : 256;
                if (hits(st, c->tp_x, c->tp_y, c->tp_x + tw, c->tp_y + 256) ||
                    (c->tp_mode < 2 && hits(st, c->clut_x, c->clut_y, c->clut_x + (c->tp_mode ? 256 : 16), c->clut_y + 1)))
                    refresh(st, ops);
            }
            emit_prim(st, c, s, sx, sy, ops);
            prim_bounds(c, &x0, &y0, &x1, &y1);
            if (x0 < x1 && y0 < y1) mark(st, x0, y0, x1, y1);
            break;
        }
        case R_UPLOAD: {
            u32 *t = tmp_buf(st, (size_t)c->w * c->h), k;
            flush(st, ops);
            if (!t) break;
            for (k = 0; k < (u32)c->w * c->h; k++) t[k] = nr_expand15(l->data[c->data + k]);
            ops->upload(ops->ctx, c->x, c->y, c->w, c->h, t);
            mark(st, c->x, c->y, c->x + c->w, c->y + c->h);
            break;
        }
        case R_FILL: {
            u32 e = nr_expand15((u16)c->color);
            float col[4];
            flush(st, ops);
            col[0] = (float)(e & 0xFF) / 255.0f; col[1] = (float)((e >> 8) & 0xFF) / 255.0f;
            col[2] = (float)((e >> 16) & 0xFF) / 255.0f; col[3] = 0;
            ops->fill(ops->ctx, c->x, c->y, c->w, c->h, col);
            mark(st, c->x, c->y, c->x + c->w, c->y + c->h);
            break;
        }
        case R_COPY:
            flush(st, ops);
            if (hits(st, c->sx, c->sy, c->sx + c->w, c->sy + c->h)) refresh(st, ops);
            ops->copy(ops->ctx, c->sx, c->sy, c->x, c->y, c->w, c->h);
            mark(st, c->x, c->y, c->x + c->w, c->y + c->h);
            break;
        default: break;
        }
    }
    flush(st, ops);
}

void nr_gpu_sync(nr_gpu_replay_state *st, const u16 *vram, const nr_gpu_ops *ops)
{
    u32 *t = tmp_buf(st, 1024 * 512), k;
    if (!t) return;
    for (k = 0; k < 1024 * 512; k++) t[k] = nr_expand15(vram[k]);
    ops->upload(ops->ctx, 0, 0, 1024, 512, t);
    st->dirty = true;
    st->dx0 = 0; st->dy0 = 0; st->dx1 = 1024; st->dy1 = 512;
    refresh(st, ops);
}
