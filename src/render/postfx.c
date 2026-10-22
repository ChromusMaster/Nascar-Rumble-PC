#include "postfx.h"
#include "../common/thread.h"

#include <math.h>
#include <string.h>

bool nr_postfx_needed(const nr_gfx_settings *s)
{
    return s->post_fx != GFX_FX_OFF || s->tonemap != GFX_TM_OFF || s->brightness != 0.0f || s->contrast != 1.0f ||
           s->saturation != 1.0f || s->gamma != 1.0f || s->scaling == GFX_SCALE_SHARP_BILINEAR;
}

typedef struct {
    const nr_gfx_settings *s;
    const u32 *src;
    int sw, sh, native_h;
    u32 *dst;
    int dw, dh;
    u32 frame;
    int bands;
} job;

static inline void fetch(const job *j, int x, int y, float c[3])
{
    u32 p;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= j->sw) x = j->sw - 1;
    if (y >= j->sh) y = j->sh - 1;
    p = j->src[(size_t)y * j->sw + x];
    c[0] = (float)(p & 0xFF) / 255.0f;
    c[1] = (float)((p >> 8) & 0xFF) / 255.0f;
    c[2] = (float)((p >> 16) & 0xFF) / 255.0f;
}

/* texel-space bilinear (tx, ty in source pixels, centres at +0.5) */
static void bilinear(const job *j, float tx, float ty, float c[3])
{
    float fx = tx - 0.5f, fy = ty - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - (float)x0, ay = fy - (float)y0;
    float a[3], b[3], cc[3], d[3];
    int i;
    fetch(j, x0, y0, a); fetch(j, x0 + 1, y0, b); fetch(j, x0, y0 + 1, cc); fetch(j, x0 + 1, y0 + 1, d);
    for (i = 0; i < 3; i++) c[i] = (a[i] * (1 - ax) + b[i] * ax) * (1 - ay) + (cc[i] * (1 - ax) + d[i] * ax) * ay;
}

static void sample(const job *j, float u, float v, float c[3])
{
    float tx = u * (float)j->sw, ty = v * (float)j->sh;
    switch (j->s->scaling) {
    case GFX_SCALE_NEAREST:
    case GFX_SCALE_INTEGER:
        fetch(j, (int)floorf(tx), (int)floorf(ty), c);
        break;
    case GFX_SCALE_SHARP_BILINEAR: {
        float kx = floorf((float)j->dw / (float)j->sw), ky = floorf((float)j->dh / (float)j->sh);
        float fx = floorf(tx), fy = floorf(ty), sx = tx - fx - 0.5f, sy = ty - fy - 0.5f;
        float rx, ry;
        if (kx < 1) kx = 1;
        if (ky < 1) ky = 1;
        rx = 0.5f - 0.5f / kx; ry = 0.5f - 0.5f / ky;
        sx = (sx - (sx < -rx ? -rx : sx > rx ? rx : sx)) * kx + 0.5f;
        sy = (sy - (sy < -ry ? -ry : sy > ry ? ry : sy)) * ky + 0.5f;
        bilinear(j, fx + sx, fy + sy, c);
        break;
    }
    default:
        bilinear(j, tx, ty, c);
        break;
    }
}

static inline float hash_noise(u32 x, u32 y, u32 f)
{
    u32 h = x * 374761393u + y * 668265263u + f * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFF) / 65535.0f - 0.5f;
}

static void pixel(const job *j, int x, int y, float c[3])
{
    const nr_gfx_settings *s = j->s;
    float u = ((float)x + 0.5f) / (float)j->dw, v = ((float)y + 0.5f) / (float)j->dh;
    float k = s->post_fx_strength, dx, dy, r2;
    int i;
    if (s->post_fx == GFX_FX_CRT) {
        /* slight barrel curvature */
        float cx = u - 0.5f, cy = v - 0.5f, rr = cx * cx + cy * cy, warp = 1.0f + 0.08f * k * rr;
        u = 0.5f + cx * warp; v = 0.5f + cy * warp;
        if (u < 0 || u > 1 || v < 0 || v > 1) { c[0] = c[1] = c[2] = 0; return; }
    }
    sample(j, u, v, c);
    switch (s->post_fx) {
    case GFX_FX_SHARPEN: {
        float tx = u * (float)j->sw, ty = v * (float)j->sh, n[3], acc[3] = { 0, 0, 0 };
        int xi = (int)floorf(tx), yi = (int)floorf(ty);
        fetch(j, xi - 1, yi, n); for (i = 0; i < 3; i++) acc[i] += n[i];
        fetch(j, xi + 1, yi, n); for (i = 0; i < 3; i++) acc[i] += n[i];
        fetch(j, xi, yi - 1, n); for (i = 0; i < 3; i++) acc[i] += n[i];
        fetch(j, xi, yi + 1, n); for (i = 0; i < 3; i++) acc[i] += n[i];
        for (i = 0; i < 3; i++) c[i] = c[i] + 0.6f * k * (c[i] - acc[i] * 0.25f);
        break;
    }
    case GFX_FX_CRT:
    case GFX_FX_SCANLINES: {
        float line = v * (float)j->native_h, f = line - floorf(line);
        float sl = 1.0f - (s->post_fx == GFX_FX_CRT ? 0.45f : 0.35f) * k * (0.5f - 0.5f * cosf(f * 6.2831853f + 3.1415927f));
        for (i = 0; i < 3; i++) c[i] *= sl;
        if (s->post_fx == GFX_FX_CRT) {
            int m = x % 3;
            for (i = 0; i < 3; i++) c[i] *= (i == m) ? 1.0f + 0.15f * k : 1.0f - 0.10f * k;
            for (i = 0; i < 3; i++) c[i] *= 1.0f + 0.12f * k;   /* compensate the mask's darkening */
        }
        break;
    }
    case GFX_FX_COLOR_GRADE: nr_gfx_color_grade(c, k); break;
    case GFX_FX_FILM_GRAIN: {
        float n = hash_noise((u32)x, (u32)y, j->frame) * 0.12f * k;
        for (i = 0; i < 3; i++) c[i] += n;
        break;
    }
    case GFX_FX_CINEMATIC: {
        float n = hash_noise((u32)x, (u32)y, j->frame) * 0.06f * k;
        nr_gfx_color_grade(c, k);
        for (i = 0; i < 3; i++) c[i] += n;
        break;
    }
    default: break;
    }
    if (s->post_fx == GFX_FX_VIGNETTE || s->post_fx == GFX_FX_CRT || s->post_fx == GFX_FX_CINEMATIC) {
        float vk = s->post_fx == GFX_FX_VIGNETTE ? 0.75f : 0.45f;
        dx = u - 0.5f; dy = v - 0.5f; r2 = (dx * dx + dy * dy) * 2.0f;
        for (i = 0; i < 3; i++) c[i] *= 1.0f - vk * k * r2;
    }
    for (i = 0; i < 3; i++) c[i] = c[i] < 0 ? 0 : c[i] > 1 ? 1 : c[i];
    nr_gfx_grade_pixel(s, c);
}

static void band(void *ctx, int b)
{
    const job *j = (const job *)ctx;
    int y0 = j->dh * b / j->bands, y1 = j->dh * (b + 1) / j->bands, x, y;
    for (y = y0; y < y1; y++) {
        u32 *o = j->dst + (size_t)y * j->dw;
        for (x = 0; x < j->dw; x++) {
            float c[3];
            pixel(j, x, y, c);
            o[x] = 0xFF000000u | (u32)(c[0] * 255.0f + 0.5f) | ((u32)(c[1] * 255.0f + 0.5f) << 8) | ((u32)(c[2] * 255.0f + 0.5f) << 16);
        }
    }
}

void nr_postfx_render(const nr_gfx_settings *s, const u32 *src, int sw, int sh, int native_h,
                      u32 *dst, int dw, int dh, u32 frame, struct nr_pool *pool)
{
    job j;
    j.s = s; j.src = src; j.sw = sw; j.sh = sh; j.native_h = native_h > 0 ? native_h : sh;
    j.dst = dst; j.dw = dw; j.dh = dh; j.frame = frame;
    j.bands = nr_pool_size(pool) * 4;
    nr_pool_run(pool, j.bands, band, &j);
}
