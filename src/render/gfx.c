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

const nr_gfx_settings *nr_gfx_fit(const nr_gfx_settings *s, int max_scale, nr_gfx_settings *tmp)
{
    int ax, ay;
    *tmp = *s;
    if (tmp->internal_scale < 1) tmp->internal_scale = 1;
    if (max_scale < 1) max_scale = 1;
    for (;;) {
        nr_gfx_aa_grid(tmp->antialias, &ax, &ay);
        if (tmp->internal_scale * ax <= max_scale && tmp->internal_scale * ay <= max_scale) break;
        if (tmp->antialias > 1) tmp->antialias /= 2;
        else if (tmp->internal_scale > 1) tmp->internal_scale--;
        else break;
    }
    if (tmp->antialias == s->antialias && tmp->internal_scale == s->internal_scale) return s;
    NR_WARN("video", "internal resolution %dx with AA %dx does not fit this renderer (limit %d per axis); using %dx with AA %dx",
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

/* Same formulas as the HLSL in src/platform/win32/d3d_shaders.h. */
void nr_gfx_grade_pixel(const nr_gfx_settings *s, float c[3])
{
    int i;
    float l;
    switch (s->tonemap) {
    case GFX_TM_REINHARD:
        for (i = 0; i < 3; i++) { float x = c[i] * 1.6f; c[i] = x / (1.0f + x) * 1.25f; }
        break;
    case GFX_TM_ACES:
        for (i = 0; i < 3; i++) {
            float x = c[i] * 0.8f;
            c[i] = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
        }
        break;
    case GFX_TM_FILMIC:
        for (i = 0; i < 3; i++) {
            float x = c[i] * 1.2f - 0.004f;
            if (x < 0) x = 0;
            c[i] = (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
            c[i] = powf(c[i], 2.2f);   /* the curve outputs gamma-encoded values */
        }
        break;
    default: break;
    }
    for (i = 0; i < 3; i++) c[i] = clamp01(c[i] + s->brightness * 0.5f);
    for (i = 0; i < 3; i++) c[i] = clamp01((c[i] - 0.5f) * s->contrast + 0.5f);
    l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    for (i = 0; i < 3; i++) c[i] = clamp01(l + (c[i] - l) * s->saturation);
    if (s->gamma != 1.0f) for (i = 0; i < 3; i++) c[i] = powf(c[i], 1.0f / s->gamma);
}
