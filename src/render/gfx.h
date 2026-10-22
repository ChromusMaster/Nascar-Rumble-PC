/* Graphics enhancement settings shared by every renderer backend.
 *
 * All of these act only on the picture shown to the player. They are applied
 * to a parallel, higher-resolution copy of VRAM built from the render command
 * list (rlist.h); the console VRAM that the game reads stays exactly as the
 * hardware would produce it, and no game file is read differently or
 * modified. Defaults reproduce the console's look at native resolution. */
#ifndef NR_GFX_H
#define NR_GFX_H

#include "../common/common.h"

typedef enum { GFX_SCALE_INTEGER = 0, GFX_SCALE_NEAREST, GFX_SCALE_BILINEAR, GFX_SCALE_SHARP_BILINEAR } nr_gfx_scaling;
typedef enum {
    GFX_FX_OFF = 0, GFX_FX_CRT, GFX_FX_SCANLINES, GFX_FX_VIGNETTE, GFX_FX_COLOR_GRADE,
    GFX_FX_FILM_GRAIN, GFX_FX_SHARPEN, GFX_FX_CINEMATIC, GFX_FX_COUNT
} nr_gfx_fx;
typedef enum { GFX_TM_OFF = 0, GFX_TM_REINHARD, GFX_TM_ACES, GFX_TM_FILMIC, GFX_TM_COUNT } nr_gfx_tonemap;
typedef enum { GFX_AR_4_3 = 0, GFX_AR_STRETCH, GFX_AR_16_9 } nr_gfx_aspect;

typedef struct {
    int internal_scale;      /* 1..8: output resolution multiplier */
    int antialias;           /* 1 (off), 2, 4, 8, 16: supersampling samples per pixel */
    bool true_color;         /* 24-bit shading/blending instead of the console's 15-bit */
    bool dithering;          /* apply the console's 4x4 dither where the game requests it */
    int texture_filter;      /* 0 nearest (console), 1 bilinear, 2 bilinear also on 2D sprites */
    nr_gfx_scaling scaling;  /* how the picture is scaled to the window */
    nr_gfx_aspect aspect;
    nr_gfx_fx post_fx;
    float post_fx_strength;  /* 0..1 */
    nr_gfx_tonemap tonemap;
    float brightness;        /* -1..1, 0 = unchanged */
    float contrast;          /* 0..2, 1 = unchanged */
    float saturation;        /* 0..2, 1 = unchanged */
    float gamma;             /* 0.5..2, 1 = unchanged */
    bool vsync;
    bool pgxp;               /* sub-pixel vertices + perspective-correct textures (experimental) */
    bool widescreen;         /* 16:9 projection hack (experimental, changes what the game computes) */
} nr_gfx_settings;

void nr_gfx_defaults(nr_gfx_settings *s);
/* supersampling grid for an AA level: 1 (1x1), 2 (2x1), 4 (2x2), 8 (4x2), 16 (4x4) */
void nr_gfx_aa_grid(int aa, int *ax, int *ay);
/* Returns s, or a copy in *tmp reduced so that internal scale x AA grid fits
 * max_scale per axis (anti-aliasing is lowered first, then the internal
 * resolution). Logs a warning when something had to be lowered. */
const nr_gfx_settings *nr_gfx_fit(const nr_gfx_settings *s, int max_scale, nr_gfx_settings *tmp);
u64 nr_gfx_render_budget(u64 ram_total, u64 gpu_memory, bool gpu_renderer);
int nr_gfx_memory_scale_limit(u64 budget_bytes, unsigned copies);
void nr_gfx_set_memory_scale_limit(int max_scale);
int nr_gfx_get_memory_scale_limit(void);
const char *nr_gfx_fx_name(nr_gfx_fx fx);
const char *nr_gfx_tonemap_name(nr_gfx_tonemap t);

/* Final colour adjustment chain shared by the software and GPU paths
 * (documented formulas so all backends match). Input/output 0..1. */
void nr_gfx_grade_pixel(const nr_gfx_settings *s, float rgb[3]);
float nr_gfx_srgb_to_linear(float c);
float nr_gfx_linear_to_srgb(float c);
float nr_gfx_tonemap_exposure(nr_gfx_tonemap t);
void nr_gfx_fit_gamut(float linear_rgb[3], float luminance);
void nr_gfx_tonemap_pixel(nr_gfx_tonemap t, float rgb[3]);
void nr_gfx_color_grade(float rgb[3], float strength);

#endif
