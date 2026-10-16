/* Video output backends of the windowed frontend.
 *
 * Backends receive:
 *  - every emulated frame: the render command list (rlist.h), when the
 *    high-resolution path is active and the backend rasterises it itself
 *    (OpenGL and Direct3D backends);
 *  - when a frame is shown: either the console picture (exactly what the
 *    hardware outputs), or - for the software path - the picture already
 *    produced by the software enhanced renderer; plus an RGBA overlay with
 *    on-screen messages and menus.
 * Scaling to the window, aspect ratio, post-processing and colour grading
 * happen in the backend (GPU shaders) or in src/render/postfx.c (software). */
#ifndef NR_VIDEO_H
#define NR_VIDEO_H

#include "../render/rlist.h"
#include "../render/gfx.h"
#include "../ui/font.h"

typedef struct {
    int win_w, win_h;             /* drawable size in pixels */
    const u32 *picture;           /* RGBA picture when the backend does not render the rlist */
    int pic_w, pic_h;
    int native_w, native_h;       /* console display size (for aspect and scanlines) */
    bool use_hires;               /* GPU backend: show its own high-resolution render */
    const nr_rdisplay *display;   /* display state of the frame (hires path) */
    const nr_canvas *overlay;     /* may be NULL */
    u32 frame;
} nr_vpresent;

typedef struct nr_vbackend nr_vbackend;
struct nr_vbackend {
    const char *name;             /* "software", "d3d9", ... */
    char description[160];        /* adapter / API details for the log */
    bool gpu_hires;               /* rasterises the render command list itself */
    bool needs_gl_window;         /* the SDL window must be created with SDL_WINDOW_OPENGL */
    int max_vram_scale;           /* largest supported scale per axis (internal x AA) */
    void *impl;
    /* set by the frontend before present(): the backend then copies the
     * presented image (window contents) into cap_px (RGBA, owned by it) */
    bool capture_next;
    u32 *cap_px;
    int cap_w, cap_h;
    /* window: platform handle (HWND for Direct3D, SDL_Window* for OpenGL and software) */
    bool (*init)(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t errlen);
    /* settings changed; returns false if they cannot be applied (kept old) */
    bool (*configure)(nr_vbackend *b, const nr_gfx_settings *s);
    /* GPU backends: rebuild the high-resolution VRAM from the console VRAM */
    void (*sync)(nr_vbackend *b, const u16 *vram);
    /* GPU backends: replay one frame of commands */
    void (*execute)(nr_vbackend *b, const nr_rlist *l);
    void (*present)(nr_vbackend *b, const nr_vpresent *p);
    void (*shutdown)(nr_vbackend *b);
};

/* destination rectangle of the picture inside the window */
void nr_video_dest_rect(const nr_gfx_settings *s, int win_w, int win_h, int pic_w, int pic_h,
                        int native_w, int native_h, int *x, int *y, int *w, int *h);

bool nr_vb_software_create(nr_vbackend *b);
bool nr_vb_opengl_create(nr_vbackend *b);
#ifdef _WIN32
bool nr_vb_d3d9_create(nr_vbackend *b);
bool nr_vb_d3d10_create(nr_vbackend *b);
bool nr_vb_d3d11_create(nr_vbackend *b);
bool nr_vb_d3d12_create(nr_vbackend *b);
#endif

#endif
