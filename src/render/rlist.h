/* Render command list: a side channel through which the emulated GPU reports
 * everything that changes VRAM (primitives, fills, copies, CPU uploads) and
 * the display state at each VBlank.
 *
 * The emulation never reads anything back from here. The native software
 * rasterizer in src/psx/gpu.c remains the only source of truth for the
 * console's VRAM (what the game can read), so enhanced renderers built on
 * this list (higher internal resolution, anti-aliasing, true colour, texture
 * filtering, post-processing, Direct3D backends) cannot change the game's
 * behaviour. Uploads carry a copy of the data so a list is self-contained and
 * can be replayed later (on worker threads or by a GPU). */
#ifndef NR_RLIST_H
#define NR_RLIST_H

#include "../common/common.h"

enum { R_TRI = 1, R_LINE, R_FILL, R_UPLOAD, R_COPY };

/* primitive flags */
enum {
    RF_TEXTURED = 1u << 0,
    RF_RAW = 1u << 1,         /* texture without colour modulation */
    RF_SEMI = 1u << 2,        /* semi-transparency enabled */
    RF_GOURAUD = 1u << 3,
    RF_DITHER = 1u << 4,      /* the game asked for dithering */
    RF_SET_MASK = 1u << 5,
    RF_CHECK_MASK = 1u << 6,
    RF_SPRITE = 1u << 7,      /* rectangle: UVs map 1:1 to pixels */
    RF_PRECISE = 1u << 8      /* vertices carry sub-pixel positions (PGXP) */
};

typedef struct {
    s16 x, y;            /* native pixel coordinates (drawing offset applied) */
    s16 u, v;            /* texture coordinates (may exceed 0..255 for sprites) */
    u8 r, g, b, a;
    float fx, fy, fw;    /* precise position/w when RF_PRECISE */
} nr_rvtx;

typedef struct {
    u8 type;
    u8 tp_mode;          /* 0 = 4 bpp, 1 = 8 bpp, 2 = 15 bpp */
    u8 semi_mode;        /* 0 B/2+F/2, 1 B+F, 2 B-F, 3 B+F/4 */
    u8 pad;
    u16 flags;
    u16 tp_x, tp_y;      /* texture page origin (in 16-bit VRAM pixels) */
    u16 clut_x, clut_y;
    u32 texwin;          /* GP0(E2) raw */
    s16 clip_x1, clip_y1, clip_x2, clip_y2;   /* drawing area, inclusive */
    s16 uv_min_u, uv_min_v, uv_max_u, uv_max_v; /* texel bounds of the whole primitive */
    nr_rvtx v[3];        /* R_TRI: 3 vertices, R_LINE: 2 */
    /* R_FILL / R_UPLOAD / R_COPY */
    u16 x, y, w, h, sx, sy;
    u32 color;           /* fill colour (15-bit) */
    u32 data;            /* R_UPLOAD: index into nr_rlist.data */
} nr_rcmd;

typedef struct {
    u32 disp_x, disp_y;  /* display start in VRAM */
    int width, height;   /* displayed size in native pixels */
    bool enabled;
    bool rgb24;
    bool interlaced480;
    u64 frame;           /* emulated vblank number */
} nr_rdisplay;

typedef struct nr_rlist {
    nr_rcmd *cmd;
    u32 n, cap;
    u16 *data;
    u32 ndata, data_cap;
    nr_rdisplay display;
    u16 *disp24;         /* snapshot of the native display rows for 24-bit mode */
    u32 disp24_cap;
    bool overflow;
} nr_rlist;

nr_rlist *nr_rlist_new(void);
void nr_rlist_free(nr_rlist *l);
void nr_rlist_clear(nr_rlist *l);
nr_rcmd *nr_rlist_push(nr_rlist *l);
/* reserves space for count 16-bit values; returns their index */
u32 nr_rlist_alloc_data(nr_rlist *l, u32 count);

#endif
