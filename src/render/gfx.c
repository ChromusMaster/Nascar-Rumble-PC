#include "gfx.h"

#include <math.h>

void nr_gfx_defaults(nr_gfx_settings *s)
{
    s->internal_scale = 1;
    s->antialias = 1;
    s->true_color = false;
    s->dithering = true;
    s->texture_filter = 0;
    s->scaling = GFX_SCALE_BILINEAR;
    s->aspect = GFX_AR_4_3;
    s->post_fx = GFX_FX_OFF;
    s->post_fx_strength = 1.0f;
    s->tonemap = GFX_TM_OFF;
    s->brightness = 0.0f;
    s->contrast = 1.0f;
    s->saturation = 1.0f;
    s->gamma = 1.0f;
    s->vsync = true;
    s->pgxp = false;
    s->widescreen = false;
}

void nr_gfx_aa_grid(int aa, int *ax, int *ay)
{
    switch (aa) {
    case 2: *ax = 2; *ay = 1; break;
    case 4: *ax = 2; *ay = 2; break;
    case 8: *ax = 4; *ay = 2; break;
    case 16: *ax = 4; *ay = 4; break;
    default: *ax = 1; *ay = 1; break;
    }
}

static int g_memory_scale_limit = 16;

u64 nr_gfx_render_budget(u64 ram_total, u64 gpu_memory, bool gpu_renderer)
{
    if (gpu_renderer) return gpu_memory / 2;
    return ram_total / 4;
}

int nr_gfx_memory_scale_limit(u64 budget_bytes, unsigned copies)
{
    int f;
    if (!budget_bytes || !copies) return 16;
    for (f = 16; f > 1; f--)
        if ((u64)1024 * (u64)f * 512u * (u64)f * 4u * (u64)copies <= budget_bytes) break;
    return f;
}

void nr_gfx_set_memory_scale_limit(int max_scale)
{
    g_memory_scale_limit = max_scale < 1 ? 1 : max_scale > 16 ? 16 : max_scale;
}

int nr_gfx_get_memory_scale_limit(void) { return g_memory_scale_limit; }

const nr_gfx_settings *nr_gfx_fit(const nr_gfx_settings *s, int max_scale, nr_gfx_settings *tmp)
{
    int ax, ay;
    *tmp = *s;
    if (tmp->internal_scale < 1) tmp->internal_scale = 1;
    if (max_scale > g_memory_scale_limit) max_scale = g_memory_scale_limit;
    if (max_scale < 1) max_scale = 1;
    for (;;) {
        nr_gfx_aa_grid(tmp->antialias, &ax, &ay);
        if (tmp->internal_scale * ax <= max_scale && tmp->internal_scale * ay <= max_scale) break;
        if (tmp->antialias > 1) tmp->antialias /= 2;
        else if (tmp->internal_scale > 1) tmp->internal_scale--;
        else break;
    }
    if (tmp->antialias == s->antialias && tmp->internal_scale == s->internal_scale) return s;
    NR_WARN("video", "internal resolution %dx with AA %dx does not fit this renderer or the detected memory (limit %d per axis); using %dx with AA %dx",
            s->internal_scale, s->antialias, max_scale, tmp->internal_scale, tmp->antialias);
    return tmp;
}

const char *nr_gfx_fx_name(nr_gfx_fx fx)
{
    static const char *n[] = { "off", "crt", "scanlines", "vignette", "colorgrade", "grain", "sharpen", "cinematic" };
    return (unsigned)fx < GFX_FX_COUNT ? n[fx] : "off";
}

const char *nr_gfx_tonemap_name(nr_gfx_tonemap t)
{
    static const char *n[] = { "off", "reinhard", "aces", "filmic" };
    return (unsigned)t < GFX_TM_COUNT ? n[t] : "off";
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

float nr_gfx_srgb_to_linear(float c)
{
    c = clamp01(c);
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float nr_gfx_linear_to_srgb(float c)
{
    c = clamp01(c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static float tone_curve(nr_gfx_tonemap t, float x)
{
    switch (t) {
    case GFX_TM_REINHARD: return x / (1.0f + x);
    case GFX_TM_ACES: return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    case GFX_TM_FILMIC: return (x * (0.15f * x + 0.05f) + 0.004f) / (x * (0.15f * x + 0.5f) + 0.06f) - 0.02f / 0.3f;
    default: return x;
    }
}

float nr_gfx_tonemap_exposure(nr_gfx_tonemap t)
{
    switch (t) {
    case GFX_TM_REINHARD: return 1.0f;
    case GFX_TM_ACES: return 1.2f;
    case GFX_TM_FILMIC: return 2.0f;
    default: return 1.0f;
    }
}

void nr_gfx_fit_gamut(float c[3], float y)
{
    float m = c[0] > c[1] ? c[0] : c[1];
    int i;
    if (c[2] > m) m = c[2];
    y = clamp01(y);
    if (m > 1.0f && m - y > 1e-6f) {
        float t = (m - 1.0f) / (m - y);
        for (i = 0; i < 3; i++) c[i] = c[i] + (y - c[i]) * t;
    }
    for (i = 0; i < 3; i++) c[i] = clamp01(c[i]);
}

void nr_gfx_tonemap_pixel(nr_gfx_tonemap t, float c[3])
{
    float l[3], y, yo, e, k, m;
    int i;
    if (t <= GFX_TM_OFF || t >= GFX_TM_COUNT) return;
    for (i = 0; i < 3; i++) l[i] = nr_gfx_srgb_to_linear(c[i]);
    y = 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
    if (y <= 0.0f) { c[0] = c[1] = c[2] = 0.0f; return; }
    e = nr_gfx_tonemap_exposure(t);
    yo = tone_curve(t, y * e) / tone_curve(t, e);
    k = yo / y;
    m = l[0] > l[1] ? (l[0] > l[2] ? l[0] : l[2]) : (l[1] > l[2] ? l[1] : l[2]);
    if (m * k > 1.0f) k = 1.0f / m;
    for (i = 0; i < 3; i++) l[i] *= k;
    for (i = 0; i < 3; i++) c[i] = nr_gfx_linear_to_srgb(l[i]);
}

void nr_gfx_color_grade(float c[3], float k)
{
    float l[3], y0, y1, mx, mn, v;
    int i;
    for (i = 0; i < 3; i++) l[i] = nr_gfx_srgb_to_linear(c[i]);
    y0 = 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
    l[0] *= 1.0f + 0.05f * k; l[2] *= 1.0f - 0.06f * k;
    y1 = 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
    if (y1 > 0.0f) for (i = 0; i < 3; i++) l[i] *= y0 / y1;
    mx = l[0] > l[1] ? (l[0] > l[2] ? l[0] : l[2]) : (l[1] > l[2] ? l[1] : l[2]);
    mn = l[0] < l[1] ? (l[0] < l[2] ? l[0] : l[2]) : (l[1] < l[2] ? l[1] : l[2]);
    v = 1.0f + 0.15f * k * (1.0f - (mx > 0.0f ? (mx - mn) / mx : 0.0f));
    for (i = 0; i < 3; i++) l[i] = y0 + (l[i] - y0) * v;
    nr_gfx_fit_gamut(l, y0);
    for (i = 0; i < 3; i++) c[i] = nr_gfx_linear_to_srgb(l[i]);
}

void nr_gfx_grade_pixel(const nr_gfx_settings *s, float c[3])
{
    int i;
    float l;
    nr_gfx_tonemap_pixel(s->tonemap, c);
    for (i = 0; i < 3; i++) c[i] = clamp01(c[i] + s->brightness * 0.5f);
    for (i = 0; i < 3; i++) c[i] = clamp01((c[i] - 0.5f) * s->contrast + 0.5f);
    l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    for (i = 0; i < 3; i++) c[i] = clamp01(l + (c[i] - l) * s->saturation);
    if (s->gamma != 1.0f) for (i = 0; i < 3; i++) c[i] = powf(c[i], 1.0f / s->gamma);
}
