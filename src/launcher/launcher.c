/* nrlauncher - the program players start.
 *
 * Every engine setting is chosen here and written to nrengine.ini; "Play!"
 * starts the engine window (nrengine) with a launcher session token, and the
 * engine refuses to run without one (it opens the launcher instead).
 * Interface languages: see l10n.h. No game file is read beyond identifying
 * the user's disc image, and nothing is ever written to it. */
#include "lui.h"
#include "l10n.h"
#include "../app/app.h"
#include "../common/paths.h"
#include "../common/proc.h"
#include "../disc/discsel.h"
#include "../input/input.h"
#include "../render/postfx.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "../platform/win32/winutil.h"
#endif

#ifndef NR_VERSION_STRING
#define NR_VERSION_STRING "dev"
#endif

#define LW 960
#define LH 640

enum { TAB_GENERAL, TAB_GRAPHICS, TAB_EFFECTS, TAB_CONTROLS, TAB_ADVANCED, TAB_COUNT };

typedef struct {
    nr_options o;
    nr_port_config port[2];
    char ini[1100];
} settings;

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tw, th;
    lui u;
    settings st;
    int tab;
    char status[512];
    u32 status_color;
    /* discs */
    nr_disc_list discs;
    char custom_disc[1024];
    /* game languages found in mods/Language */
    char langs[32][64];
    int nlangs;
    /* controllers */
    nr_input_device dev[NR_MAX_INPUT_DEVICES];
    int ndev;
    int ctl_player;
    nr_pad *pad;
    char pad_for[160];
    int capture_mode;          /* 0 none, 1 keyboard, 2 controller */
    int capture_button;
    u32 capture_start;
    bool kb_state[SDL_NUM_SCANCODES];
    /* file browser (non-Windows) */
    bool browsing;
    char bdir[1024];
    char bent[256][256];
    bool bent_dir[256];
    int nbent;
    int bscroll;
    /* preview */
    u32 *test_src, *test_dst;
    /* engine */
    nr_proc *engine;
    bool running;
    bool font_warned;
    int autoplay;              /* test hook: 1 = press Play on this frame */
} launcher;

/* ------------------------------------------------------------ settings */

static void settings_load(settings *s)
{
    const char *argv[3];
    nr_config *c;
    nr_strlcpy(s->ini, nr_default_config_path(), sizeof(s->ini));
    nr_options_default(&s->o);
    argv[0] = "nrlauncher"; argv[1] = "--config"; argv[2] = s->ini;
    nr_options_parse(&s->o, 3, (char **)argv);
    c = nr_config_load(s->ini);
    nr_port_config_load(&s->port[0], c, 0);
    nr_port_config_load(&s->port[1], c, 1);
    nr_config_free(c);
}

static const char *const k_scal[] = { "integer", "nearest", "bilinear", "sharp" };
static const char *const k_ar[] = { "4:3", "stretch", "16:9" };

static void put(const settings *s, const char *key, const char *fmt, ...)
{
    char b[1100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    nr_config_update_file(s->ini, key, b);
}

static void settings_save(const settings *s)
{
    const nr_options *o = &s->o;
    const nr_gfx_settings *g = &o->gfx;
    put(s, "game.disc", "%s", o->cfg_disc);
    put(s, "game.disc_image", "%s", o->cfg_disc_image);
    put(s, "game.language", "%s", o->language);
    put(s, "game.verify", "%d", o->verify ? 1 : 0);
    put(s, "memcard.slot1", "%s", o->memcard_path[0]);
    put(s, "memcard.slot2", "%s", o->memcard_path[1]);
    put(s, "video.renderer", "%s", o->renderer);
    put(s, "video.fullscreen", "%d", o->fullscreen ? 1 : 0);
    put(s, "video.scale", "%d", o->scale);
    put(s, "video.vsync", "%d", g->vsync ? 1 : 0);
    if (o->fps_mode) put(s, "video.fps", "%d", o->fps_mode); else put(s, "video.fps", "original");
    put(s, "video.show_fps", "%d", o->show_fps ? 1 : 0);
    put(s, "video.internal_resolution", "%d", g->internal_scale);
    put(s, "video.antialiasing", "%d", g->antialias);
    put(s, "video.true_color", "%d", g->true_color ? 1 : 0);
    put(s, "video.dithering", "%d", g->dithering ? 1 : 0);
    put(s, "video.texture_filter", "%d", g->texture_filter);
    put(s, "video.scaling", "%s", k_scal[g->scaling & 3]);
    put(s, "video.aspect", "%s", k_ar[g->aspect % 3]);
    put(s, "video.post_fx", "%s", nr_gfx_fx_name(g->post_fx));
    put(s, "video.post_fx_strength", "%.2f", (double)g->post_fx_strength);
    put(s, "video.tonemap", "%s", nr_gfx_tonemap_name(g->tonemap));
    put(s, "video.brightness", "%.2f", (double)g->brightness);
    put(s, "video.contrast", "%.2f", (double)g->contrast);
    put(s, "video.saturation", "%.2f", (double)g->saturation);
    put(s, "video.gamma", "%.2f", (double)g->gamma);
    put(s, "video.pgxp", "%d", g->pgxp ? 1 : 0);
    put(s, "video.widescreen", "%d", g->widescreen ? 1 : 0);
    put(s, "input.port2", "%d", o->pad2 ? 1 : 0);
    nr_port_config_save(&s->port[0], s->ini, 0);
    nr_port_config_save(&s->port[1], s->ini, 1);
    put(s, "engine.native_overrides", "%d", o->natives ? 1 : 0);
    put(s, "engine.gpu_timing_model", "%d", o->gpu_instant ? 0 : 1);
    put(s, "engine.cpu_overclock", "%d", o->cpu_overclock);
    put(s, "engine.physics_step", "%s", o->physics_step == 60 ? "60" : o->physics_step == 30 ? "30" : o->physics_step == 0 ? "original" : "auto");
    put(s, "engine.threads", "%d", o->threads);
    put(s, "debug.log_level", "%d", o->log_level);
    put(s, "launcher.language", "%s", l10n_get(l10n_current())->code);
}

/* ------------------------------------------------------------ helpers */

static void set_status(launcher *L, u32 color, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(L->status, sizeof(L->status), fmt, ap);
    va_end(ap);
    L->status_color = color;
}

static void open_folder(const char *path)
{
    if (!nr_is_dir(path)) nr_mkdir(path);
#ifdef _WIN32
    nr_win_open_folder(path);
#else
    {
        const char *argv[3];
        char err[256];
        nr_proc *p;
        argv[0] = "/usr/bin/xdg-open"; argv[1] = path; argv[2] = NULL;
        p = nr_proc_spawn(argv, NULL, NULL, err, sizeof(err));
        if (p) nr_proc_free(p);
    }
#endif
}

static void scan_discs(launcher *L)
{
    nr_disc_scan(L->st.o.gamedata_dir, &L->discs);
}

typedef struct { launcher *L; } lang_ctx;
static void lang_cb(void *user, const char *name, bool is_dir)
{
    launcher *L = ((lang_ctx *)user)->L;
    int i;
    if (!is_dir || L->nlangs >= 32) return;
    for (i = 0; i < L->nlangs; i++) if (strcmp(L->langs[i], name) == 0) return;
    nr_strlcpy(L->langs[L->nlangs++], name, sizeof(L->langs[0]));
}

static void scan_languages(launcher *L)
{
    const char *bases[4];
    int i;
    lang_ctx c;
    c.L = L;
    L->nlangs = 0;
    bases[0] = "."; bases[1] = nr_data_dir(); bases[2] = nr_exe_folder(); bases[3] = nr_share_dir();
    for (i = 0; i < 4; i++) {
        char m[1100], d[1200];
        nr_path_join(m, sizeof(m), bases[i], "mods");
        nr_path_join(d, sizeof(d), m, "Language");
        nr_list_dir(d, lang_cb, &c);
    }
}

static void scan_devices(launcher *L)
{
    L->ndev = nr_input_list(L->dev, NR_MAX_INPUT_DEVICES);
}

/* opens the device configured for the shown player (for capture/test) */
static void ensure_pad(launcher *L)
{
    const char *want = L->st.port[L->ctl_player].device;
    if (L->pad && strcmp(L->pad_for, want) == 0) return;
    nr_pad_close(L->pad);
    L->pad = nr_pad_open(want, L->ctl_player);
    nr_strlcpy(L->pad_for, want, sizeof(L->pad_for));
}

/* ------------------------------------------------------------ layout */

#define ROW_H 40
#define COL1 32
#define COL2 496
#define LABEL_W 206
#define CTRL_W 240

static int row_y(int r) { return 150 + r * ROW_H; }

/* draws a label in the space left of the controls: one line at 15 px, or
 * one/two lines at 13 px when it does not fit (works for CJK too: lines are
 * broken at spaces when there are any, otherwise between characters) */
static void label_box(launcher *L, int x, int y, int w, const char *t)
{
    lui *u = &L->u;
    int px15 = lui_px(u, 15), px13 = lui_px(u, 13), W = lui_px(u, w), X = lui_px(u, x), Y = lui_px(u, y);
    char a[256], b[256];
    size_t i, cut = 0, n = strlen(t);
    if (lt_width(px15, t) <= W) { lui_text_px(u, X, Y + lui_px(u, 8), px15, LUI_DIM, t); return; }
    if (lt_width(px13, t) <= W) { lui_text_px(u, X, Y + lui_px(u, 9), px13, LUI_DIM, t); return; }
    /* longest prefix that fits, preferring a break at a space */
    for (i = 1; i <= n && i < sizeof(a); i++) {
        if (i < n && ((unsigned char)t[i] & 0xC0) == 0x80) continue;      /* inside a UTF-8 sequence */
        memcpy(a, t, i);
        a[i] = 0;
        if (lt_width(px13, a) > W) break;
        cut = i;
    }
    if (cut == 0) cut = 1;
    for (i = cut; i > 0; i--) if (t[i] == ' ') { cut = i; break; }
    memcpy(a, t, cut);
    a[cut] = 0;
    nr_strlcpy(b, t + cut + (t[cut] == ' ' ? 1 : 0), sizeof(b));
    lui_text_px(u, X, Y + lui_px(u, 0), px13, LUI_DIM, a);
    lui_text_fit(u, X, Y + lui_px(u, 15), px13, LUI_DIM, b, W);
}

static void label(launcher *L, int col, int r, const char *t) { label_box(L, col, row_y(r), LABEL_W - 10, t); }

static lui_rect ctrl(launcher *L, int col, int r) { return lui_r(&L->u, col + LABEL_W, row_y(r), CTRL_W, 30); }

static int combo_row(launcher *L, int col, int r, const char *lab, const char *help, const char *const *items, int n, int cur)
{
    label(L, col, r, lab);
    lui_help(&L->u, help);
    return lui_combo(&L->u, ctrl(L, col, r), items, n, cur);
}

static bool toggle_row(launcher *L, int col, int r, const char *lab, const char *help, bool v)
{
    label(L, col, r, lab);
    lui_help(&L->u, help);
    return lui_toggle(&L->u, ctrl(L, col, r), v, T(S_YES), T(S_NO));
}

/* ------------------------------------------------------------ tabs */

static int disc_index(launcher *L, int ncands)
{
    const nr_options *o = &L->st.o;
    int i;
    if (o->cfg_disc[0]) {
        for (i = 0; i < ncands; i++) if (strcmp(L->discs.c[i].path, o->cfg_disc) == 0) return 1 + i;
        return 1 + ncands;              /* custom path */
    }
    if (o->cfg_disc_image[0])
        for (i = 0; i < ncands; i++) if (nr_strcasecmp(L->discs.c[i].name, o->cfg_disc_image) == 0) return 1 + i;
    return 0;
}

static void update_disc_status(launcher *L)
{
    char path[1024], how[256];
    nr_disc_candidate c;
    if (!nr_disc_resolve("", L->st.o.cfg_disc, L->st.o.cfg_disc_image, &L->discs, path, sizeof(path), how, sizeof(how))) {
        set_status(L, LUI_RED, "%s", T(S_STATUS_NODISC));
        return;
    }
    if (!nr_disc_probe(path, &c)) { set_status(L, LUI_RED, T(S_STATUS_BAD), T(S_DISC_UNREADABLE)); return; }
    if (c.status == NR_CAND_OK) set_status(L, LUI_GREEN, T(S_STATUS_READY), "SLUS-01068");
    else set_status(L, LUI_RED, T(S_STATUS_BAD), c.status == NR_CAND_OTHER_GAME ? T(S_DISC_OTHER) : T(S_DISC_UNREADABLE));
}

static void browse_open(launcher *L, const char *dir);

static void choose_disc_file(launcher *L)
{
#ifdef _WIN32
    char path[1024];
    SDL_SysWMinfo info;
    void *hwnd = NULL;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(L->win, &info)) hwnd = info.info.win.window;
    if (nr_win_open_file(hwnd, T(S_BROWSE_TITLE), path, sizeof(path))) {
        nr_strlcpy(L->st.o.cfg_disc, path, sizeof(L->st.o.cfg_disc));
        L->st.o.cfg_disc_image[0] = 0;
        update_disc_status(L);
    }
#else
    char d[1100];
    nr_path_join(d, sizeof(d), nr_data_dir(), "gamedata");
    browse_open(L, nr_is_dir(d) ? d : (getenv("HOME") ? getenv("HOME") : "/"));
#endif
}

static void tab_general(launcher *L)
{
    nr_options *o = &L->st.o;
    const char *items[NR_MAX_CANDIDATES + 3];
    char lab[NR_MAX_CANDIDATES + 1][320];
    int n = 0, i, cur, sel;
    /* disc */
    items[n++] = T(S_DISC_AUTO);
    for (i = 0; i < L->discs.n; i++) {
        snprintf(lab[i], sizeof(lab[i]), "%s  (%s)", L->discs.c[i].name,
                 L->discs.c[i].status == NR_CAND_OK ? T(S_DISC_OK) : L->discs.c[i].status == NR_CAND_OTHER_GAME ? T(S_DISC_OTHER) : T(S_DISC_UNREADABLE));
        items[n++] = lab[i];
    }
    cur = disc_index(L, L->discs.n);
    if (cur == 1 + L->discs.n) { nr_strlcpy(lab[NR_MAX_CANDIDATES], nr_path_basename(o->cfg_disc), sizeof(lab[0])); items[n++] = lab[NR_MAX_CANDIDATES]; }
    items[n++] = T(S_DISC_BROWSE);
    label(L, COL1, 0, T(S_DISC));
    lui_help(&L->u, T(S_H_DISC));
    sel = lui_combo(&L->u, lui_r(&L->u, COL1 + LABEL_W, row_y(0), 700, 30), items, n, cur);
    if (sel != cur) {
        if (sel == n - 1) choose_disc_file(L);
        else if (sel == 0) { o->cfg_disc[0] = 0; o->cfg_disc_image[0] = 0; }
        else if (sel <= L->discs.n) { o->cfg_disc[0] = 0; nr_strlcpy(o->cfg_disc_image, L->discs.c[sel - 1].name, sizeof(o->cfg_disc_image)); }
        update_disc_status(L);
    }
    lui_help(&L->u, T(S_H_DISC));
    if (lui_button(&L->u, lui_r(&L->u, COL1 + LABEL_W, row_y(1), 236, 30), T(S_BTN_OPEN_GAMEDATA), false)) {
        char d[1100];
        nr_path_join(d, sizeof(d), nr_data_dir(), "gamedata");
        open_folder(d);
    }
    lui_help(&L->u, T(S_H_DISC));
    if (lui_button(&L->u, lui_r(&L->u, COL1 + LABEL_W + 248, row_y(1), 160, 30), T(S_BTN_RESCAN), false)) {
        scan_discs(L);
        update_disc_status(L);
    }
    /* left column */
    {
        const char *disp[2];
        disp[0] = T(S_WINDOWED); disp[1] = T(S_FULLSCREEN);
        o->fullscreen = combo_row(L, COL1, 3, T(S_DISPLAY), T(S_H_DISPLAY), disp, 2, o->fullscreen ? 1 : 0) == 1;
    }
    {
        static const char *const sizes[] = { "640 x 480 (2x)", "960 x 720 (3x)", "1280 x 960 (4x)", "1600 x 1200 (5x)", "1920 x 1440 (6x)" };
        int c = o->scale - 2;
        if (c < 0) c = 0;
        if (c > 4) c = 4;
        o->scale = 2 + combo_row(L, COL1, 4, T(S_WINDOW_SIZE), T(S_H_WINDOW_SIZE), sizes, 5, c);
    }
    {
        const char *fps[4];
        static const int modes[4] = { 0, 30, 20, 60 };
        int c = 0;
        fps[0] = T(S_FPS_ORIGINAL); fps[1] = T(S_FPS_30); fps[2] = T(S_FPS_20); fps[3] = T(S_FPS_60);
        for (i = 0; i < 4; i++) if (modes[i] == o->fps_mode) c = i;
        o->fps_mode = modes[combo_row(L, COL1, 5, T(S_FPS), T(S_H_FPS), fps, 4, c)];
    }
    o->show_fps = toggle_row(L, COL1, 6, T(S_SHOW_FPS), T(S_H_SHOW_FPS), o->show_fps);
    /* right column */
    {
        const char *gl[33];
        int c = 0, k;
        gl[0] = T(S_GAME_LANG_ORIGINAL);
        for (k = 0; k < L->nlangs; k++) { gl[k + 1] = L->langs[k]; if (strcmp(L->langs[k], o->language) == 0) c = k + 1; }
        c = combo_row(L, COL2, 3, T(S_GAME_LANG), T(S_H_GAME_LANG), gl, L->nlangs + 1, c);
        nr_strlcpy(o->language, c == 0 ? "" : L->langs[c - 1], sizeof(o->language));
    }
    {
        const char *ul[32];
        int k, c = l10n_current(), nc;
        for (k = 0; k < l10n_count() && k < 32; k++) ul[k] = l10n_get(k)->name;
        nc = combo_row(L, COL2, 4, T(S_UI_LANG), T(S_H_UI_LANG), ul, l10n_count(), c);
        if (nc != c) {
            if (lt_set_script(l10n_get(nc)->script)) {
                l10n_set(nc);
                update_disc_status(L);
                SDL_SetWindowTitle(L->win, T(S_WINDOW_TITLE));
            } else {
                lt_set_script(LT_LATIN);
                l10n_set(0);
                set_status(L, LUI_RED, "%s", T(S_STATUS_FONT_MISSING));
            }
        }
    }
    {
        bool m1 = o->memcard_path[0][0] != 0, m2 = o->memcard_path[1][0] != 0;
        label(L, COL2, 5, T(S_MEMCARD1));
        lui_help(&L->u, T(S_H_MEMCARD));
        m1 = lui_toggle(&L->u, ctrl(L, COL2, 5), m1, T(S_INSERTED), T(S_EMPTY_SLOT));
        label(L, COL2, 6, T(S_MEMCARD2));
        lui_help(&L->u, T(S_H_MEMCARD));
        m2 = lui_toggle(&L->u, ctrl(L, COL2, 6), m2, T(S_INSERTED), T(S_EMPTY_SLOT));
        if (m1 && !o->memcard_path[0][0]) nr_strlcpy(o->memcard_path[0], "memcard1.mcd", sizeof(o->memcard_path[0]));
        if (!m1) o->memcard_path[0][0] = 0;
        if (m2 && !o->memcard_path[1][0]) nr_strlcpy(o->memcard_path[1], "memcard2.mcd", sizeof(o->memcard_path[1]));
        if (!m2) o->memcard_path[1][0] = 0;
    }
}

static void tab_graphics(launcher *L)
{
    nr_options *o = &L->st.o;
    nr_gfx_settings *g = &o->gfx;
    int i, c;
    {
#ifdef _WIN32
        static const char *const ids[] = { "auto", "opengl", "d3d9", "d3d10", "d3d11", "d3d12", "software" };
        const char *names[7];
        names[0] = T(S_AUTO); names[1] = "OpenGL 3.3"; names[2] = "Direct3D 9"; names[3] = "Direct3D 10";
        names[4] = "Direct3D 11"; names[5] = "Direct3D 12"; names[6] = T(S_REN_SOFTWARE);
        const int n = 7;
#else
        static const char *const ids[] = { "auto", "opengl", "software" };
        const char *names[3];
        names[0] = T(S_AUTO); names[1] = "OpenGL 3.3"; names[2] = T(S_REN_SOFTWARE);
        const int n = 3;
#endif
        c = 0;
        for (i = 0; i < n; i++) if (nr_strcasecmp(ids[i], o->renderer) == 0) c = i;
        c = combo_row(L, COL1, 0, T(S_RENDERER), T(S_H_RENDERER), names, n, c);
        nr_strlcpy(o->renderer, ids[c], sizeof(o->renderer));
    }
    g->vsync = toggle_row(L, COL1, 1, T(S_VSYNC), T(S_H_VSYNC), g->vsync);
    {
        char lab[8][48];
        const char *items[8];
        for (i = 0; i < 8; i++) {
            if (i == 0) snprintf(lab[i], sizeof(lab[i]), "%s (320 x 240)", T(S_RES_NATIVE));
            else snprintf(lab[i], sizeof(lab[i]), "%dx (%d x %d)", i + 1, 320 * (i + 1), 240 * (i + 1));
            items[i] = lab[i];
        }
        g->internal_scale = 1 + combo_row(L, COL1, 2, T(S_INTERNAL_RES), T(S_H_INTERNAL_RES), items, 8, g->internal_scale - 1);
    }
    {
        static const int aa[5] = { 1, 2, 4, 8, 16 };
        const char *items[5];
        items[0] = T(S_OFF); items[1] = "2x (2 x 1)"; items[2] = "4x (2 x 2)"; items[3] = "8x (4 x 2)"; items[4] = "16x (4 x 4)";
        c = 0;
        for (i = 0; i < 5; i++) if (aa[i] == g->antialias) c = i;
        g->antialias = aa[combo_row(L, COL1, 3, T(S_AA), T(S_H_AA), items, 5, c)];
    }
    {
        const char *items[2];
        items[0] = T(S_COLOR_15); items[1] = T(S_COLOR_24);
        g->true_color = combo_row(L, COL1, 4, T(S_COLOR), T(S_H_COLOR), items, 2, g->true_color ? 1 : 0) == 1;
    }
    g->dithering = toggle_row(L, COL1, 5, T(S_DITHER), T(S_H_DITHER), g->dithering);
    {
        const char *items[3];
        items[0] = T(S_FILTER_NEAREST); items[1] = T(S_FILTER_3D); items[2] = T(S_FILTER_ALL);
        g->texture_filter = combo_row(L, COL2, 0, T(S_TEXFILTER), T(S_H_TEXFILTER), items, 3, g->texture_filter);
    }
    {
        const char *items[4];
        items[0] = T(S_SCALE_INTEGER); items[1] = T(S_SCALE_NEAREST); items[2] = T(S_SCALE_BILINEAR); items[3] = T(S_SCALE_SHARP);
        g->scaling = (nr_gfx_scaling)combo_row(L, COL2, 1, T(S_SCALING), T(S_H_SCALING), items, 4, (int)g->scaling);
    }
    {
        const char *items[3];
        items[0] = "4:3"; items[1] = T(S_ASPECT_STRETCH); items[2] = "16:9";
        g->aspect = (nr_gfx_aspect)combo_row(L, COL2, 2, T(S_ASPECT), T(S_H_ASPECT), items, 3, (int)g->aspect);
    }
    g->pgxp = toggle_row(L, COL2, 3, T(S_PGXP), T(S_H_PGXP), g->pgxp);
    {
        bool w = toggle_row(L, COL2, 4, T(S_WIDESCREEN), T(S_H_WIDESCREEN), g->widescreen);
        if (w && !g->widescreen) g->aspect = GFX_AR_16_9;
        g->widescreen = w;
    }
}

/* our own test picture for the effects preview (no game content) */
static void make_test_picture(u32 *p, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        u32 r, g, b;
        if (y < h / 2) {                                   /* sky */
            r = 40 + y * 120 / (h / 2); g = 90 + y * 110 / (h / 2); b = 200 + y * 40 / (h / 2);
        } else {                                           /* road in perspective with stripes */
            int yy = y - h / 2, cx = w / 2, half = 10 + yy * 3;
            bool road = x > cx - half && x < cx + half;
            bool stripe = road && abs(x - cx) < 2 + yy / 16 && ((yy * yy / 40) % 8) < 4;
            bool kerb = road && abs(abs(x - cx) - half) < 4 + yy / 12;
            if (stripe) { r = 240; g = 220; b = 60; }
            else if (kerb) { if (((yy * yy / 30) % 6) < 3) { r = 220; g = 40; b = 40; } else { r = 240; g = 240; b = 240; } }
            else if (road) { r = 70 + (x * 7 % 9); g = 72; b = 78; }
            else { r = 60; g = 140 + (x * y % 23); b = 50; }
        }
        if (y >= 8 && y < 24) {                            /* colour bars + grey ramp */
            static const u32 bars[8] = { 0xFFFFFF, 0x00FFFF, 0xFFFF00, 0x00FF00, 0xFF00FF, 0x0000FF, 0xFF0000, 0x000000 };
            if (x < w / 2) { u32 c = bars[x * 8 / (w / 2)]; r = c & 0xFF; g = (c >> 8) & 0xFF; b = c >> 16; }
            else { r = g = b = (u32)((x - w / 2) * 255 / (w / 2)); }
        }
        p[y * w + x] = r | (g << 8) | (b << 16) | 0xFF000000u;
    }
}

static float adjust_row(launcher *L, int r, const char *lab, float v, float lo, float hi, float step, bool percent_delta)
{
    char t[32];
    if (percent_delta) snprintf(t, sizeof(t), "%+d%%", (int)(v * 100.0f + (v < 0 ? -0.5f : 0.5f)));
    else snprintf(t, sizeof(t), "%d%%", (int)(v * 100.0f + 0.5f));
    label(L, COL1, r, lab);
    lui_help(&L->u, T(S_H_ADJUST));
    return lui_slider(&L->u, ctrl(L, COL1, r), v, lo, hi, step, t);
}

static void tab_effects(launcher *L)
{
    nr_gfx_settings *g = &L->st.o.gfx;
    {
        const char *items[GFX_FX_COUNT];
        items[0] = T(S_OFF); items[1] = T(S_FX_CRT); items[2] = T(S_FX_SCANLINES); items[3] = T(S_FX_VIGNETTE);
        items[4] = T(S_FX_GRADE); items[5] = T(S_FX_GRAIN); items[6] = T(S_FX_SHARPEN); items[7] = T(S_FX_CINEMATIC);
        g->post_fx = (nr_gfx_fx)combo_row(L, COL1, 0, T(S_POSTFX), T(S_H_POSTFX), items, GFX_FX_COUNT, (int)g->post_fx);
    }
    {
        char t[16];
        snprintf(t, sizeof(t), "%d%%", (int)(g->post_fx_strength * 100.0f + 0.5f));
        label(L, COL1, 1, T(S_FX_STRENGTH));
        lui_help(&L->u, T(S_H_FX_STRENGTH));
        g->post_fx_strength = lui_slider(&L->u, ctrl(L, COL1, 1), g->post_fx_strength, 0, 1, 0.05f, t);
    }
    {
        const char *items[GFX_TM_COUNT];
        items[0] = T(S_OFF); items[1] = "Reinhard"; items[2] = "ACES"; items[3] = "Filmic";
        g->tonemap = (nr_gfx_tonemap)combo_row(L, COL1, 2, T(S_TONEMAP), T(S_H_TONEMAP), items, GFX_TM_COUNT, (int)g->tonemap);
    }
    g->brightness = adjust_row(L, 3, T(S_BRIGHTNESS), g->brightness, -1, 1, 0.05f, true);
    g->contrast = adjust_row(L, 4, T(S_CONTRAST), g->contrast, 0.5f, 1.5f, 0.05f, false);
    g->saturation = adjust_row(L, 5, T(S_SATURATION), g->saturation, 0, 2, 0.1f, false);
    {
        char t[16];
        snprintf(t, sizeof(t), "%.2f", (double)g->gamma);
        label(L, COL1, 6, T(S_GAMMA));
        lui_help(&L->u, T(S_H_ADJUST));
        g->gamma = lui_slider(&L->u, ctrl(L, COL1, 6), g->gamma, 0.5f, 2.0f, 0.05f, t);
    }
    /* preview: our own test picture through the same post-processing code */
    {
        lui *u = &L->u;
        int pw = 320, ph = 240, dw = lui_px(u, 400), dh = lui_px(u, 300), x0 = lui_px(u, COL2 + 20), y0 = lui_px(u, row_y(0));
        int x, y;
        if (!L->test_src) { L->test_src = (u32 *)malloc((size_t)pw * ph * 4); if (L->test_src) make_test_picture(L->test_src, pw, ph); }
        L->test_dst = (u32 *)realloc(L->test_dst, (size_t)dw * dh * 4);
        if (L->test_src && L->test_dst) {
            nr_gfx_settings pg = *g;
            pg.scaling = GFX_SCALE_BILINEAR;
            nr_postfx_render(&pg, L->test_src, pw, ph, ph, L->test_dst, dw, dh, SDL_GetTicks() / 16, NULL);
            for (y = 0; y < dh && y0 + y < u->s.h; y++)
                for (x = 0; x < dw && x0 + x < u->s.w; x++) u->s.px[(y0 + y) * u->s.w + x0 + x] = L->test_dst[y * dw + x] | 0xFF000000u;
        }
        lui_text(u, COL2 + 20, row_y(0) + 306, 13, LUI_DIM, T(S_PREVIEW));
    }
}

static const int k_ps_label[NR_PS_BUTTONS] = {
    S_PS_UP, S_PS_DOWN, S_PS_LEFT, S_PS_RIGHT, S_PS_CROSS, S_PS_CIRCLE, S_PS_SQUARE, S_PS_TRIANGLE,
    S_PS_L1, S_PS_R1, S_PS_L2, S_PS_R2, S_PS_START, S_PS_SELECT
};

/* readable form of a binding */
static void binding_text(const char *b, char *out, size_t n)
{
    static const struct { const char *k, *v; } names[] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" }, { "back", "Back" }, { "start", "Start" }, { "guide", "Guide" },
        { "leftshoulder", "LB" }, { "rightshoulder", "RB" }, { "lefttrigger", "LT" }, { "righttrigger", "RT" },
        { "leftstick", "LS" }, { "rightstick", "RS" }, { "dpup", "D-pad Up" }, { "dpdown", "D-pad Down" },
        { "dpleft", "D-pad Left" }, { "dpright", "D-pad Right" }, { "leftx-", "LS Left" }, { "leftx+", "LS Right" },
        { "lefty-", "LS Up" }, { "lefty+", "LS Down" }, { "rightx-", "RS Left" }, { "rightx+", "RS Right" },
        { "righty-", "RS Up" }, { "righty+", "RS Down" } };
    size_t i;
    if (!b || !*b) { nr_strlcpy(out, "-", n); return; }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (strcmp(b, names[i].k) == 0) { nr_strlcpy(out, names[i].v, n); return; }
    if (b[0] == 'b' && b[1] >= '0' && b[1] <= '9') { snprintf(out, n, "#%d", atoi(b + 1) + 1); return; }
    if (b[0] == 'a' && b[1] >= '0' && b[1] <= '9') { snprintf(out, n, "Axis %d%s", atoi(b + 1) + 1, strchr(b, '-') ? "-" : "+"); return; }
    if (b[0] == 'h' && b[1] >= '0' && b[1] <= '9') {
        const char *d = strchr(b, '.');
        snprintf(out, n, "Hat %d %s", atoi(b + 1) + 1, d ? d + 1 : "");
        return;
    }
    nr_strlcpy(out, b, n);
}

static void tab_controls(launcher *L)
{
    lui *u = &L->u;
    nr_port_config *pc;
    int i;
    {
        const char *items[2];
        items[0] = T(S_PLAYER1); items[1] = T(S_PLAYER2);
        label(L, COL1, 0, T(S_PLAYER));
        L->ctl_player = lui_combo(u, lui_r(u, COL1 + 110, row_y(0), 150, 30), items, 2, L->ctl_player);
    }
    pc = &L->st.port[L->ctl_player];
    {
        const char *items[NR_MAX_INPUT_DEVICES + 2];
        char lab[NR_MAX_INPUT_DEVICES][200];
        int c = 0, n;
        items[0] = T(S_DEV_KEYBOARD_ONLY);
        items[1] = T(S_DEV_AUTO);
        if (nr_strcasecmp(pc->device, "auto") == 0) c = 1;
        for (i = 0; i < L->ndev; i++) {
            const nr_input_device *d = &L->dev[i];
            snprintf(lab[i], sizeof(lab[i]), "%s  [%s%s%s]  %04X:%04X", d->name, d->kind == NR_DEV_SDL ? T(S_DRV_SYSTEM) : T(S_DRV_DIRECT),
                     d->accessible ? "" : ", ", d->accessible ? "" : T(S_DRV_NO_ACCESS), d->vid, d->pid);
            items[2 + i] = lab[i];
            if (strcmp(pc->device, d->id) == 0) c = 2 + i;
        }
        n = 2 + L->ndev;
        lui_text(u, 300, row_y(0) + 8, 15, LUI_DIM, T(S_DEVICE));
        lui_help(u, T(S_H_DEVICE));
        c = lui_combo(u, lui_r(u, 410, row_y(0), 518, 30), items, n, c);
        if (c == 0) nr_strlcpy(pc->device, "keyboard", sizeof(pc->device));
        else if (c == 1) nr_strlcpy(pc->device, "auto", sizeof(pc->device));
        else nr_strlcpy(pc->device, L->dev[c - 2].id, sizeof(pc->device));
    }
    if (L->ctl_player == 1) {
        label_box(L, COL1, row_y(1), 250, T(S_PORT2));
        lui_help(u, T(S_H_PORT2));
        L->st.o.pad2 = lui_toggle(u, lui_r(u, COL1 + 260, row_y(1), 172, 30), L->st.o.pad2, T(S_YES), T(S_NO));
    }
    label_box(L, COL2, row_y(1), 250, T(S_STICK_DPAD));
    lui_help(u, T(S_H_STICK_DPAD));
    pc->stick_as_dpad = lui_toggle(u, lui_r(u, COL2 + 260, row_y(1), 172, 30), pc->stick_as_dpad, T(S_YES), T(S_NO));
    ensure_pad(L);
    /* binding table: two halves of 7 buttons */
    {
        int half, k;
        int ty = row_y(2) + 6;
        for (half = 0; half < 2; half++) {
            int x0 = half ? COL2 - 16 : COL1;
            lui_text(u, x0, ty, 13, LUI_DIM, T(S_COL_BUTTON));
            lui_text(u, x0 + 110, ty, 13, LUI_DIM, T(S_COL_KEY));
            lui_text(u, x0 + 260, ty, 13, LUI_DIM, T(S_COL_PAD));
            for (k = 0; k < 7; k++) {
                int b = half * 7 + k, y = ty + 22 + k * 32;
                char kt[64], pt[96], tmp[64];
                bool kon, pon;
                const char *bind = pc->bind[b][0] ? pc->bind[b] : (L->pad ? nr_pad_default_binding(L->pad, b) : "");
                lui_text(u, x0, y + 6, 15, LUI_TEXT, T(k_ps_label[b]));
                if (L->capture_mode == 1 && L->capture_button == b) nr_strlcpy(kt, "...", sizeof(kt));
                else nr_strlcpy(kt, pc->keys[b] ? SDL_GetScancodeName((SDL_Scancode)pc->keys[b]) : "-", sizeof(kt));
                kon = pc->keys[b] && pc->keys[b] < SDL_NUM_SCANCODES && L->kb_state[pc->keys[b]];
                lui_help(u, T(S_H_BINDING));
                if (lui_cell(u, lui_r(u, x0 + 110, y, 140, 28), kt, kon) && !L->capture_mode) {
                    L->capture_mode = 1; L->capture_button = b; L->capture_start = SDL_GetTicks();
                }
                if (L->capture_mode == 2 && L->capture_button == b) nr_strlcpy(pt, "...", sizeof(pt));
                else {
                    binding_text(bind, tmp, sizeof(tmp));
                    if (!pc->bind[b][0] && L->pad) snprintf(pt, sizeof(pt), "%s (%s)", tmp, T(S_DEFAULT));
                    else nr_strlcpy(pt, L->pad || pc->bind[b][0] ? tmp : "-", sizeof(pt));
                }
                pon = L->pad && nr_pad_binding_active(L->pad, bind);
                lui_help(u, T(S_H_BINDING));
                if (lui_cell(u, lui_r(u, x0 + 260, y, 172, 28), pt, pon) && !L->capture_mode && L->pad) {
                    L->capture_mode = 2; L->capture_button = b; L->capture_start = SDL_GetTicks();
                    nr_pad_capture_begin(L->pad);
                }
            }
        }
    }
    {
        int y = row_y(2) + 6 + 22 + 7 * 32 + 6;
        if (L->capture_mode) {
            char t[256];
            snprintf(t, sizeof(t), L->capture_mode == 1 ? T(S_PRESS_KEY) : T(S_PRESS_BUTTON), T(k_ps_label[L->capture_button]));
            lui_text(u, COL1, y + 8, 15, LUI_ACCENT, t);
        } else {
            if (!L->pad) lui_text(u, COL1, y + 8, 14, LUI_DIM, T(S_NO_DEVICE_OPEN));
            if (lui_button(u, lui_r(u, 928 - 220, y, 220, 30), T(S_BTN_DEFAULTS), false)) {
                for (i = 0; i < NR_PS_BUTTONS; i++) {
                    pc->bind[i][0] = 0;
                    pc->keys[i] = (int)SDL_GetScancodeFromName(nr_key_default(L->ctl_player, i));
                    if (!nr_key_default(L->ctl_player, i)[0]) pc->keys[i] = 0;
                }
                pc->stick_as_dpad = true;
            }
        }
    }
}

static void tab_advanced(launcher *L)
{
    nr_options *o = &L->st.o;
    int i, c;
    o->natives = toggle_row(L, COL1, 0, T(S_NATIVE), T(S_H_NATIVE), o->natives);
    o->gpu_instant = !toggle_row(L, COL1, 1, T(S_GPU_TIMING), T(S_H_GPU_TIMING), !o->gpu_instant);
    {
        const char *items[5];
        items[0] = T(S_AUTO); items[1] = T(S_OC_CONSOLE); items[2] = "2x"; items[3] = "3x"; items[4] = "4x";
        c = o->cpu_overclock < 0 ? 0 : o->cpu_overclock > 4 ? 4 : o->cpu_overclock;
        o->cpu_overclock = combo_row(L, COL1, 2, T(S_OVERCLOCK), T(S_H_OVERCLOCK), items, 5, c);
    }
    {
        static const int th[6] = { 0, 1, 2, 4, 8, 16 };
        const char *items[6];
        items[0] = T(S_AUTO); items[1] = "1"; items[2] = "2"; items[3] = "4"; items[4] = "8"; items[5] = "16";
        c = 0;
        for (i = 0; i < 6; i++) if (th[i] == o->threads) c = i;
        o->threads = th[combo_row(L, COL1, 3, T(S_THREADS), T(S_H_THREADS), items, 6, c)];
    }
    {
        const char *items[4];
        items[0] = T(S_LOG_ERRORS); items[1] = T(S_LOG_WARNINGS); items[2] = T(S_LOG_INFO); items[3] = T(S_LOG_DEBUG);
        c = o->log_level < 0 ? 0 : o->log_level > 3 ? 3 : o->log_level;
        o->log_level = combo_row(L, COL1, 4, T(S_LOG_LEVEL), T(S_H_LOG_LEVEL), items, 4, c);
    }
    o->verify = toggle_row(L, COL1, 5, T(S_VERIFY_DISC), T(S_H_VERIFY), o->verify);
    {
        const char *items[4];
        items[0] = T(S_AUTO); items[1] = T(S_PHYS_ORIGINAL); items[2] = T(S_PHYS_30); items[3] = T(S_PHYS_60);
        c = o->physics_step == 60 ? 3 : o->physics_step == 30 ? 2 : o->physics_step == 0 ? 1 : 0;
        c = combo_row(L, COL1, 6, T(S_PHYS_STEP), T(S_H_PHYS_STEP), items, 4, c);
        o->physics_step = c == 3 ? 60 : c == 2 ? 30 : c == 1 ? 0 : -1;
    }
    {
        lui *u = &L->u;
        char t[1200];
        lui_text(u, COL2, row_y(0) + 8, 15, LUI_DIM, T(S_FOLDERS));
        snprintf(t, sizeof(t), "nrengine.ini: %s", L->st.ini);
        lui_text_fit(u, lui_px(u, COL2), lui_px(u, row_y(1)), lui_px(u, 13), LUI_TEXT, t, lui_px(u, 430));
        snprintf(t, sizeof(t), "gamedata, mods: %s", nr_data_dir());
        lui_text_fit(u, lui_px(u, COL2), lui_px(u, row_y(1) + 22), lui_px(u, 13), LUI_TEXT, t, lui_px(u, 430));
        snprintf(t, sizeof(t), "fonts: %s", lt_font_info());
        lui_text_fit(u, lui_px(u, COL2), lui_px(u, row_y(1) + 44), lui_px(u, 13), LUI_DIM, t, lui_px(u, 430));
        if (lui_button(u, lui_r(u, COL2, row_y(3), 240, 30), T(S_BTN_OPEN_USER), false)) open_folder(nr_user_dir());
    }
}

/* ------------------------------------------------------------ file browser */

typedef struct { launcher *L; } br_ctx;

static bool image_ext(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && (nr_strcasecmp(n + l - 4, ".cue") == 0 || nr_strcasecmp(n + l - 4, ".bin") == 0 ||
                     nr_strcasecmp(n + l - 4, ".iso") == 0 || nr_strcasecmp(n + l - 4, ".img") == 0);
}

static void br_cb(void *user, const char *name, bool is_dir)
{
    launcher *L = ((br_ctx *)user)->L;
    if (L->nbent >= 256 || name[0] == '.') return;
    if (!is_dir && !image_ext(name)) return;
    nr_strlcpy(L->bent[L->nbent], name, sizeof(L->bent[0]));
    L->bent_dir[L->nbent] = is_dir;
    L->nbent++;
}

static int br_cmp_launcher_index;
static launcher *g_sort_L;
static int br_cmp(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    if (g_sort_L->bent_dir[ia] != g_sort_L->bent_dir[ib]) return g_sort_L->bent_dir[ia] ? -1 : 1;
    return nr_strcasecmp(g_sort_L->bent[ia], g_sort_L->bent[ib]);
}

static void browse_open(launcher *L, const char *dir)
{
    br_ctx c;
    int idx[256], i;
    char tmp[256][256];
    bool tdir[256];
    c.L = L;
    nr_strlcpy(L->bdir, dir, sizeof(L->bdir));
    L->nbent = 0;
    nr_list_dir(dir, br_cb, &c);
    for (i = 0; i < L->nbent; i++) idx[i] = i;
    g_sort_L = L;
    (void)br_cmp_launcher_index;
    qsort(idx, (size_t)L->nbent, sizeof(int), br_cmp);
    for (i = 0; i < L->nbent; i++) { memcpy(tmp[i], L->bent[idx[i]], sizeof(tmp[0])); tdir[i] = L->bent_dir[idx[i]]; }
    for (i = 0; i < L->nbent; i++) { memcpy(L->bent[i], tmp[i], sizeof(tmp[0])); L->bent_dir[i] = tdir[i]; }
    L->bscroll = 0;
    L->browsing = true;
    L->u.focus = 0;
}

static void browse_up(launcher *L)
{
    char d[1024];
    char *s;
    nr_strlcpy(d, L->bdir, sizeof(d));
    s = strrchr(d, '/');
    if (s && s != d) *s = 0;
    else nr_strlcpy(d, "/", sizeof(d));
    browse_open(L, d);
}

static void draw_browser(launcher *L)
{
    lui *u = &L->u;
    int vis = 11, i;
    lui_fill(u, lui_r(u, 16, 104, 928, 470), LUI_PANEL);
    lui_text(u, 32, 112, 17, LUI_ACCENT, T(S_BROWSE_TITLE));
    lui_text_fit(u, lui_px(u, 32), lui_px(u, 138), lui_px(u, 14), LUI_DIM, L->bdir, lui_px(u, 890));
    if (u->wheel) { L->bscroll -= u->wheel * 3; }
    if (u->focus >= 0 && u->focus < L->nbent) {
        if (u->focus < L->bscroll) L->bscroll = u->focus;
        if (u->focus >= L->bscroll + vis) L->bscroll = u->focus - vis + 1;
    }
    if (L->bscroll > L->nbent - vis) L->bscroll = L->nbent - vis;
    if (L->bscroll < 0) L->bscroll = 0;
    /* hidden ids for entries above the window keep focus numbering stable */
    u->next_id = L->bscroll;
    for (i = L->bscroll; i < L->nbent && i < L->bscroll + vis; i++) {
        char t[300];
        snprintf(t, sizeof(t), "%s%s", L->bent[i], L->bent_dir[i] ? "/" : "");
        if (lui_cell(u, lui_r(u, 32, 162 + (i - L->bscroll) * 32, 896, 28), t, false)) {
            char p[1300];
            nr_path_join(p, sizeof(p), L->bdir, L->bent[i]);
            if (L->bent_dir[i]) { browse_open(L, p); return; }
            nr_strlcpy(L->st.o.cfg_disc, p, sizeof(L->st.o.cfg_disc));
            L->st.o.cfg_disc_image[0] = 0;
            L->browsing = false;
            update_disc_status(L);
            return;
        }
    }
    u->next_id = L->nbent > u->next_id ? L->nbent : u->next_id;
    if (lui_button(u, lui_r(u, 32, 530, 200, 32), T(S_BTN_UP), false)) { browse_up(L); return; }
    if (lui_button(u, lui_r(u, 248, 530, 160, 32), T(S_BTN_CANCEL), false)) L->browsing = false;
    lui_text(u, 430, 538, 13, LUI_DIM, T(S_BROWSE_HINT));
}

/* ------------------------------------------------------------ frame */

static void draw_banner(launcher *L)
{
    lui *u = &L->u;
    int x, y, bh = lui_px(u, 92), sq = lui_px(u, 8);
    for (y = 0; y < bh && y < u->s.h; y++) {
        u32 r = 30 + 90 * (u32)(bh - y) / (u32)bh, g = 12 + 10 * (u32)(bh - y) / (u32)bh, b = 12;
        for (x = 0; x < u->s.w; x++) {
            u32 rr = r + (u32)(x * 40 / (u->s.w ? u->s.w : 1));
            u->s.px[y * u->s.w + x] = (rr > 255 ? 255 : rr) | (g << 8) | (b << 16) | 0xFF000000u;
        }
    }
    /* checkered flag strip */
    for (y = bh - 2 * sq; y < bh; y++)
        for (x = u->s.w - lui_px(u, 300); x < u->s.w; x++) {
            bool w = (((x / sq) + (y / sq)) & 1) != 0;
            if (x >= 0 && y >= 0) u->s.px[y * u->s.w + x] = w ? 0xFFF0F0F0u : 0xFF141414u;
        }
    {
        lui_rect stripe = lui_r(u, 0, 88, LW, 4);
        lui_fill(u, stripe, LUI_ACCENT);
    }
    lui_text(u, 30, 10, 40, 0xFF101010u, "NR ENGINE");
    lui_text(u, 28, 8, 40, LUI_TEXT, "NR ENGINE");
    lui_text(u, 31, 9, 40, LUI_TEXT, "NR ENGINE");
    lui_text(u, 30, 62, 14, 0xFFD8D8D8u, T(S_SUBTITLE));
}

static void frame(launcher *L)
{
    lui *u = &L->u;
    int i;
    static const int tab_label[TAB_COUNT] = { S_TAB_GENERAL, S_TAB_GRAPHICS, S_TAB_EFFECTS, S_TAB_CONTROLS, S_TAB_ADVANCED };
    for (i = 0; i < u->s.w * u->s.h; i++) u->s.px[i] = LUI_BG;
    lui_begin(u, u->s.w, u->s.h);
    draw_banner(L);
    if (L->browsing) {
        draw_browser(L);
        lui_end(u);
        return;
    }
    if (u->key == LK_PREV_TAB) { L->tab = (L->tab + TAB_COUNT - 1) % TAB_COUNT; u->focus = 0; u->key = LK_NONE; }
    if (u->key == LK_NEXT_TAB) { L->tab = (L->tab + 1) % TAB_COUNT; u->focus = 0; u->key = LK_NONE; }
    for (i = 0; i < TAB_COUNT; i++)
        if (lui_tab(u, lui_r(u, 16 + i * 160, 96, 156, 36), T(tab_label[i]), L->tab == i) && L->tab != i) { L->tab = i; u->focus = 0; }
    lui_fill(u, lui_r(u, 16, 132, 928, 410), LUI_PANEL);
    switch (L->tab) {
    case TAB_GENERAL: tab_general(L); break;
    case TAB_GRAPHICS: tab_graphics(L); break;
    case TAB_EFFECTS: tab_effects(L); break;
    case TAB_CONTROLS: tab_controls(L); break;
    default: tab_advanced(L); break;
    }
    /* footer */
    lui_help(u, T(S_H_SAVE));
    if (lui_button(u, lui_r(u, 566, 590, 120, 38), T(S_BTN_SAVE), false)) {
        settings_save(&L->st);
        set_status(L, LUI_GREEN, "%s", T(S_STATUS_SAVED));
    }
    lui_help(u, T(S_H_PLAY));
    if (lui_button(u, lui_r(u, 696, 590, 140, 38), T(S_BTN_PLAY), true) || L->autoplay == 1) {
        L->autoplay = 2;
        char exe[1100], err[256], token[33], logf[1100];
        const char *argv[8];
        settings_save(&L->st);
        nr_sibling_exe(exe, sizeof(exe), "nrengine");
        if (!nr_file_exists(exe)) set_status(L, LUI_RED, "%s", T(S_STATUS_ENGINE_MISSING));
        else {
            nr_session_token(token);
            nr_path_join(logf, sizeof(logf), nr_user_dir(), "nrengine.log");
            argv[0] = exe; argv[1] = "--config"; argv[2] = L->st.ini; argv[3] = "--launcher-session"; argv[4] = token;
            argv[5] = "--log-file"; argv[6] = logf; argv[7] = NULL;
            remove(logf);
            L->engine = nr_proc_spawn(argv, NR_SESSION_ENV, token, err, sizeof(err));
            if (!L->engine) set_status(L, LUI_RED, "%s", err);
            else {
                L->running = true;
                set_status(L, LUI_TEXT, "%s", T(S_STATUS_RUNNING));
                SDL_HideWindow(L->win);
            }
        }
    }
    if (lui_button(u, lui_r(u, 846, 590, 98, 38), T(S_BTN_QUIT), false)) {
        SDL_Event q;
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }
    if (u->help) lui_text_fit(u, lui_px(u, 20), lui_px(u, 548), lui_px(u, 13), 0xFFC8C8C8u, u->help, lui_px(u, 920));
    {
        int px = lt_width(lui_px(u, 14), L->status) <= lui_px(u, 920) ? lui_px(u, 14) : lui_px(u, 12);
        lui_text_fit(u, lui_px(u, 20), lui_px(u, 570), px, L->status_color, L->status, lui_px(u, 920));
    }
    lui_text(u, 20, 600, 13, LUI_DIM, "NR Engine " NR_VERSION_STRING);
    lui_end(u);
}

/* ------------------------------------------------------------ main */

static lui_key map_key(SDL_Keycode k, Uint16 mod)
{
    switch (k) {
    case SDLK_UP: return LK_UP;
    case SDLK_DOWN: return LK_DOWN;
    case SDLK_LEFT: return LK_LEFT;
    case SDLK_RIGHT: return LK_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return LK_ENTER;
    case SDLK_ESCAPE: return LK_BACK;
    case SDLK_PAGEUP: return LK_PREV_TAB;
    case SDLK_PAGEDOWN: return LK_NEXT_TAB;
    case SDLK_HOME: return LK_HOME;
    case SDLK_END: return LK_END;
    case SDLK_TAB: return (mod & KMOD_SHIFT) ? LK_UP : LK_DOWN;
    default: return LK_NONE;
    }
}

static lui_key map_button(Uint8 b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return LK_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return LK_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return LK_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return LK_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return LK_ENTER;
    case SDL_CONTROLLER_BUTTON_B: return LK_BACK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return LK_PREV_TAB;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return LK_NEXT_TAB;
    default: return LK_NONE;
    }
}

static void choose_ui_language(launcher *L)
{
    nr_config *c = nr_config_load(L->st.ini);
    const char *code = nr_config_get(c, "launcher.language", "");
    int idx = l10n_find(code);
    if (idx < 0) idx = 0;   /* English (US) unless the player chose another language */
    nr_config_free(c);
    if (idx != 0 && !lt_set_script(l10n_get(idx)->script)) { idx = 0; lt_set_script(LT_LATIN); }
    l10n_set(idx);
}

static bool ensure_target(launcher *L)
{
    int w, h;
    SDL_GetRendererOutputSize(L->ren, &w, &h);
    if (w <= 0 || h <= 0) return false;
    if (!L->tex || w != L->tw || h != L->th) {
        if (L->tex) SDL_DestroyTexture(L->tex);
        L->tex = SDL_CreateTexture(L->ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        free(L->u.s.px);
        L->u.s.px = (u32 *)malloc((size_t)w * h * 4);
        L->u.s.w = w; L->u.s.h = h;
        L->tw = w; L->th = h;
    }
    {
        float kx = (float)w / LW, ky = (float)h / LH;
        L->u.k = kx < ky ? kx : ky;
    }
    return L->tex && L->u.s.px;
}

int main(int argc, char **argv)
{
    static launcher Ls;
    launcher *L = &Ls;
    bool quit = false;
    float scale = 1.0f;
    (void)argc; (void)argv;
    nr_utf8_args(&argc, &argv);
    nr_paths_init();
    nr_log_set_level(NR_LOG_WARN);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    lt_init();
    settings_load(&L->st);
    choose_ui_language(L);
    {
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(0, &r) == 0) {
            float k = (float)r.h * 0.85f / LH;
            if (k > 1.6f) k = 1.6f;
            if (k < 1.0f) k = 1.0f;
            if (LW * k > r.w * 0.95f) k = r.w * 0.95f / LW;
            scale = k < 0.6f ? 0.6f : k;
        }
    }
    L->win = SDL_CreateWindow(T(S_WINDOW_TITLE), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(LW * scale), (int)(LH * scale),
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!L->win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_SetWindowMinimumSize(L->win, LW / 2, LH / 2);
    L->ren = SDL_CreateRenderer(L->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!L->ren) L->ren = SDL_CreateRenderer(L->win, -1, SDL_RENDERER_SOFTWARE);
    if (!L->ren) { fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    L->u.focus = -1;
    L->u.popup_id = -1;
    L->u.popup_result_id = -1;
    L->u.drag_id = -1;
    L->status_color = LUI_TEXT;
    scan_discs(L);
    scan_languages(L);
    scan_devices(L);
    update_disc_status(L);

    /* test hooks (used by the automated screenshots of every language/tab) */
    {
        const char *t = getenv("NRLAUNCHER_TAB"), *lg = getenv("NRLAUNCHER_LANG");
        if (t) L->tab = atoi(t) % TAB_COUNT;
        if (lg && l10n_find(lg) >= 0) {
            int i = l10n_find(lg);
            if (lt_set_script(l10n_get(i)->script)) l10n_set(i);
            update_disc_status(L);
        }
    }
    while (!quit) {
        SDL_Event ev;
        Uint32 mstate;
        int mx, my;
        if (L->running) {
            int code = 0;
            while (SDL_PollEvent(&ev)) { }
            if (nr_proc_done(L->engine, &code)) {
                char logf[1100];
                nr_proc_free(L->engine);
                L->engine = NULL;
                L->running = false;
                if (code == 0) break;               /* normal exit: the launcher closes too */
                nr_path_join(logf, sizeof(logf), nr_user_dir(), "nrengine.log");
                set_status(L, LUI_RED, T(S_STATUS_ENGINE_ERR), code, logf);
                SDL_ShowWindow(L->win);
                SDL_RaiseWindow(L->win);
            } else {
                SDL_Delay(100);
                continue;
            }
        }
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: quit = true; break;
            case SDL_KEYDOWN:
                L->kb_state[ev.key.keysym.scancode] = true;
                if (L->capture_mode) {
                    nr_port_config *pc = &L->st.port[L->ctl_player];
                    SDL_Scancode sc = ev.key.keysym.scancode;
                    if (sc == SDL_SCANCODE_ESCAPE) L->capture_mode = 0;
                    else if (sc == SDL_SCANCODE_BACKSPACE) {
                        if (L->capture_mode == 1) pc->keys[L->capture_button] = 0;
                        else pc->bind[L->capture_button][0] = 0;
                        L->capture_mode = 0;
                    } else if (L->capture_mode == 1 && SDL_GetTicks() - L->capture_start > 150) {
                        pc->keys[L->capture_button] = (int)sc;
                        L->capture_mode = 0;
                    }
                    break;
                }
                if (L->browsing && ev.key.keysym.sym == SDLK_ESCAPE) { L->browsing = false; break; }
                if (L->browsing && ev.key.keysym.sym == SDLK_BACKSPACE) { browse_up(L); break; }
                L->u.key = map_key(ev.key.keysym.sym, ev.key.keysym.mod);
                break;
            case SDL_KEYUP:
                L->kb_state[ev.key.keysym.scancode] = false;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (!L->capture_mode) L->u.key = map_button(ev.cbutton.button);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) L->u.mpressed = true;
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) L->u.mreleased = true;
                break;
            case SDL_MOUSEWHEEL:
                L->u.wheel += ev.wheel.y;
                break;
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                scan_devices(L);
                L->pad_for[0] = 0;          /* reopen */
                nr_pad_close(L->pad);
                L->pad = NULL;
                break;
            default: break;
            }
        }
        if (quit) break;
        /* controller capture */
        if (L->capture_mode == 2) {
            char b[32];
            nr_pad_update(L->pad);
            if (nr_pad_capture(L->pad, b, sizeof(b))) {
                nr_strlcpy(L->st.port[L->ctl_player].bind[L->capture_button], b, sizeof(L->st.port[0].bind[0]));
                L->capture_mode = 0;
            } else if (SDL_GetTicks() - L->capture_start > 10000) L->capture_mode = 0;
        } else if (L->pad) {
            nr_pad_update(L->pad);
        }
        if (!ensure_target(L)) { SDL_Delay(50); continue; }
        mstate = SDL_GetMouseState(&mx, &my);
        {
            int ww, wh;
            SDL_GetWindowSize(L->win, &ww, &wh);
            L->u.mx = ww > 0 ? mx * L->tw / ww : mx;
            L->u.my = wh > 0 ? my * L->th / wh : my;
        }
        L->u.mdown = (mstate & SDL_BUTTON_LMASK) != 0;
        if (!L->autoplay && getenv("NRLAUNCHER_AUTOPLAY") && SDL_GetTicks() > 1500) L->autoplay = 1;
        {   /* test hook: NRLAUNCHER_TEST_CLICKS="x,y;x,y;..." clicks at window pixel
             * positions, one every 8 frames (used to reproduce UI problems) */
            static int tframe;
            const char *clicks = getenv("NRLAUNCHER_TEST_CLICKS");
            if (clicks) {
                int k = tframe / 8, ph = tframe % 8, i = 0, cx, cy;
                const char *c = clicks;
                while (c && i < k) { c = strchr(c, ';'); if (c) c++; i++; }
                if (c && *c && sscanf(c, "%d,%d", &cx, &cy) == 2 && (ph == 4 || ph == 5)) {
                    L->u.mx = cx; L->u.my = cy;
                    if (ph == 4) { L->u.mpressed = true; L->u.mdown = true; }
                    else L->u.mreleased = true;
                }
            }
            tframe++;
        }
        frame(L);
        {
            static int nframes;
            const char *shot = getenv("NRLAUNCHER_SCREENSHOT"), *sf = getenv("NRLAUNCHER_SCREENSHOT_FRAME");
            if (shot && ++nframes == (sf ? atoi(sf) : 10)) { nr_write_png(shot, L->u.s.px, L->u.s.w, L->u.s.h); quit = true; }
        }
        SDL_UpdateTexture(L->tex, NULL, L->u.s.px, L->tw * 4);
        SDL_RenderClear(L->ren);
        SDL_RenderCopy(L->ren, L->tex, NULL, NULL);
        SDL_RenderPresent(L->ren);
    }
    nr_pad_close(L->pad);
    free(L->test_src);
    free(L->test_dst);
    free(L->u.s.px);
    if (L->tex) SDL_DestroyTexture(L->tex);
    SDL_DestroyRenderer(L->ren);
    SDL_DestroyWindow(L->win);
    lt_shutdown();
    SDL_Quit();
    return 0;
}
