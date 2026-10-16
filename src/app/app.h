/* Application layer shared by the SDL2 frontend and the headless runner. */
#ifndef NR_APP_H
#define NR_APP_H

#include "../common/common.h"
#include "../psx/psx.h"
#include "../disc/gameid.h"
#include "../disc/discsel.h"
#include "../render/rlist.h"
#include "../render/gfx.h"

/* PlayStation digital pad bits (active low in the wire format) */
enum {
    PAD_SELECT = 0x0001, PAD_L3 = 0x0002, PAD_R3 = 0x0004, PAD_START = 0x0008,
    PAD_UP = 0x0010, PAD_RIGHT = 0x0020, PAD_DOWN = 0x0040, PAD_LEFT = 0x0080,
    PAD_L2 = 0x0100, PAD_R2 = 0x0200, PAD_L1 = 0x0400, PAD_R1 = 0x0800,
    PAD_TRIANGLE = 0x1000, PAD_CIRCLE = 0x2000, PAD_CROSS = 0x4000, PAD_SQUARE = 0x8000
};

typedef struct {
    char disc_path[1024];          /* from the command line */
    char cfg_disc[1024];           /* game.disc */
    char cfg_disc_image[256];      /* game.disc_image (name inside gamedata/) */
    char gamedata_dir[1024];       /* game.gamedata */
    char config_path[1024];
    char renderer[16];             /* auto | opengl | d3d9 | d3d10 | d3d11 | d3d12 | software */
    char language[32];             /* mods/Language/<language> ("" = original) */
    char mods_extra[512];          /* mods.load: other mod folders under mods/ */
    nr_gfx_settings gfx;
    int threads;                   /* software renderer threads (0 = auto) */
    bool list_discs;
    char memcard_path[2][1024];
    char screenshot_dir[1024];
    char input_script[1024];
    char log_file[1024];
    char wav_path[1024];           /* headless: write SPU output to WAV */
    int frames;                    /* 0 = unlimited */
    int screenshot_every;          /* 0 = off */
    int screenshot_at[64]; int n_screenshot_at;
    int vram_at[16]; int n_vram_at;     /* debug: dump full VRAM PNG at these frames */
    int ram_at[64]; int n_ram_at;       /* debug: dump the 2 MB main RAM at these frames */
    int state_save_at;                  /* debug: write a snapshot at this frame (0 = off) */
    char state_save_path[1024];
    char state_load_path[1024];         /* debug: start from a snapshot */
    bool headless;
    bool verify;                   /* full CRC verification of the disc */
    bool trace;
    bool dump_hle_at_exit;
    bool allow_modified;           /* run even if the disc does not match */
    int log_level;
    int scale;
    bool fullscreen;
    bool natives;                  /* enable native overrides */
    bool verify_natives;           /* compare natives against the original code */
    bool gpu_instant;              /* disable the GPU drawing-time model */
    bool pad2;                     /* controller connected to port 2 */
    int fps_mode;                  /* 0 = original, 20/30 = presentation cap, 60 = unlocked */
    int cpu_overclock;             /* 1 = console; >1 enhancement (60 fps mode) */
    int physics_step;              /* -1 auto, 0 = original variable step, 30 or 60 = steady step (Hz) */
    u32 peek[32]; int npeek;       /* debug: RAM words printed at exit */
    u32 watch_pc;                  /* debug: log when PC hits this address */
    int prof_from, prof_to;        /* debug: PC sampling profile between these frames (0 = off) */
    u32 watch_write;               /* debug: log writes to this address */
    char launcher_session[64];     /* token given by the launcher (nrengine only runs from it) */
    bool show_fps;                 /* on-screen FPS counter */
} nr_options;

typedef struct {
    int frame;
    u16 buttons[2];                /* bitmask of PAD_* pressed, per port */
} nr_input_event;

typedef struct {
    nr_options opt;
    nr_disc *disc;
    psx *p;
    nr_game_report report;
    u16 pad_pressed[2];            /* PAD_* pressed bits (positive logic) */
    bool pad_connected[2];
    nr_input_event *script; int nscript, script_pos;
    u32 *fb;                       /* display buffer 1024x512 RGBA */
    int fb_w, fb_h;
    u64 frame;
    void *wav;                     /* FILE* when recording audio */
    u32 wav_frames;
    char disc_resolved[1024];
    /* enhanced rendering (software path; Direct3D backends use rec directly) */
    nr_rlist *rec;
    struct nr_swr *swr;
    bool swr_active;
    bool gpu_backend;              /* a GPU backend consumes rec itself (no software renderer) */
    u32 *post;                     /* screenshot buffer */
    size_t post_cap;
    struct nr_mods *mods;
    struct nr_pgxp_s *pgxp;
} nr_app;

void nr_options_default(nr_options *o);
/* Parses command line + optional config file. Returns false and prints usage on error. */
bool nr_options_parse(nr_options *o, int argc, char **argv);
/* Opens the disc, validates it, creates the machine and boots. On failure
 * prints a clear error message and returns false. */
bool nr_app_start(nr_app *a);
void nr_app_frame(nr_app *a);
/* Snapshot of the running machine (local analysis only: it contains the
 * console RAM, i.e. game data - never distribute it). */
bool nr_app_save_state(nr_app *a, const char *path);
bool nr_app_load_state(nr_app *a, const char *path);
void nr_app_shutdown(nr_app *a);
bool nr_app_screenshot(nr_app *a, const char *path);
/* Graphics settings for the software path (internal resolution, AA, ...).
 * Takes effect from the next frame. */
void nr_app_set_gfx(nr_app *a, const nr_gfx_settings *s);
/* 0 = console timing, 20/30 = presentation cap (frontend), 60 = unlocked
 * (GPU drawing time not modelled + CPU overclock x3: an enhancement). */
void nr_app_set_fps_mode(nr_app *a, int mode);
/* True when the settings need the high-resolution renderer (otherwise the
 * console framebuffer is shown exactly as the hardware would). */
bool nr_gfx_needs_hires(const nr_gfx_settings *s);
/* Starts/stops recording render commands into a->rec (Direct3D backends). */
void nr_app_enable_recording(nr_app *a, bool on);
/* Current picture: console framebuffer or, with the software enhanced
 * renderer active, the high-resolution image. native_h = console lines. */
const u32 *nr_app_picture(nr_app *a, int *w, int *h, int *native_h);
/* Parses one graphics option value (shared by INI, command line and menus). */
bool nr_gfx_parse(nr_gfx_settings *s, const char *key, const char *value);

/* Native reimplementations (src/native) */
#ifndef NR_NATIVE_BS_CYCLES_PER_HALFWORD
#define NR_NATIVE_BS_CYCLES_PER_HALFWORD 33
#endif
void nr_native_register_all(psx *p);
void nr_pace_register(psx *p, int step_ticks);
int nr_physics_step_parse(const char *v);

#endif
