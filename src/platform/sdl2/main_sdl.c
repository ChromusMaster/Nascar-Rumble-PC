/* SDL2 frontend of the engine: window, video backend, audio output and
 * controller input.
 *
 * The engine window is started by the launcher (nrlauncher), where every
 * setting is chosen; it reads them from nrengine.ini. Started directly, it
 * opens the launcher instead. While the game runs only these keys are
 * handled: Esc (pause + "Do you want to close the Engine?"), F11 (switch
 * window/full screen) and F12 (screenshot). The engine core has no SDL
 * dependency; all platform code lives under src/platform. */
#include "../../app/app.h"
#include "../../app/audio_sync.h"
#include "../../common/paths.h"
#include "../../common/proc.h"
#include "../../input/input.h"
#include "../../ui/ui.h"
#include "../video.h"
#include "../sysinfo.h"

#include "../nr_sdl.h"
#ifdef NR_SDL3
#include <SDL3/SDL_main.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTSC_FPS (53693175.0 / (3413.0 * 263.0))   /* 59.817 Hz, same clock as the emulated GPU */

static void show_error(const char *msg)
{
    NR_ERR("app", "%s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NR Engine", msg, NULL);
}

/* ------------------------------------------------------------ backends */

static void *window_handle(SDL_Window *w, const nr_vbackend *b)
{
    if (strcmp(b->name, "software") == 0 || b->needs_gl_window) return w;
    return nr_sdl_native_window(w);
}

static bool create_backend(const char *name, nr_vbackend *b)
{
    if (strcmp(name, "software") == 0) return nr_vb_software_create(b);
    if (strcmp(name, "opengl") == 0) return nr_vb_opengl_create(b);
#ifdef _WIN32
    if (strcmp(name, "d3d9") == 0) return nr_vb_d3d9_create(b);
    if (strcmp(name, "d3d10") == 0) return nr_vb_d3d10_create(b);
    if (strcmp(name, "d3d11") == 0) return nr_vb_d3d11_create(b);
    if (strcmp(name, "d3d12") == 0) return nr_vb_d3d12_create(b);
#endif
    return false;
}

static SDL_Window *make_window(const nr_options *o, bool gl)
{
    int scale = o->scale > 0 ? o->scale : 3;
    return nr_sdl_window("NASCAR Rumble - NR Engine", 320 * scale, 240 * scale, gl, o->fullscreen);
}

/* Tries the chosen renderer, then the fallbacks. The window is recreated
 * when a backend needs an OpenGL window and the current one is not (or the
 * other way round). */
static bool open_backend(SDL_Window **win, const nr_options *o, nr_vbackend *b, const nr_sysinfo *si)
{
    const char *order[8];
    int n = 0, i;
    char err[256];
    if (nr_strcasecmp(o->renderer, "auto") == 0) {
#ifdef _WIN32
        order[n++] = "d3d11"; order[n++] = "opengl"; order[n++] = "d3d10"; order[n++] = "d3d9";
#else
        order[n++] = "opengl";
#endif
    } else if (nr_strcasecmp(o->renderer, "software") != 0) {
        order[n++] = o->renderer;
        if (nr_strcasecmp(o->renderer, "opengl") != 0) order[n++] = "opengl";
    }
    order[n++] = "software";
    for (i = 0; i < n; i++) {
        char lname[16];
        size_t k;
        bool is_gl;
        for (k = 0; k + 1 < sizeof(lname) && order[i][k]; k++) lname[k] = (char)SDL_tolower(order[i][k]);
        lname[k] = 0;
        if (!create_backend(lname, b)) { NR_WARN("video", "renderer '%s' is not available on this platform", lname); continue; }
        is_gl = b->needs_gl_window;
        if (*win && ((SDL_GetWindowFlags(*win) & SDL_WINDOW_OPENGL) != 0) != is_gl) { SDL_DestroyWindow(*win); *win = NULL; }
        if (!*win) {
            *win = make_window(o, is_gl);
            if (!*win) { NR_WARN("video", "cannot create the window: %s", SDL_GetError()); continue; }
        }
        err[0] = 0;
        nr_gfx_set_memory_scale_limit(nr_sysinfo_scale_limit(si, strcmp(lname, "software") == 0));
        if (b->init(b, window_handle(*win, b), &o->gfx, err, sizeof(err))) {
            NR_INFO("video", "renderer: %s%s%s", b->name, b->description[0] ? " - " : "", b->description);
            if (nr_strcasecmp(lname, o->renderer) != 0 && nr_strcasecmp(o->renderer, "auto") != 0)
                NR_WARN("video", "the chosen renderer '%s' could not be used; using '%s'", o->renderer, lname);
            return true;
        }
        NR_WARN("video", "cannot start '%s': %s", lname, err);
        if (b->shutdown && b->impl) b->shutdown(b);
    }
    return false;
}

/* ------------------------------------------------------------ input */

typedef struct {
    nr_port_config cfg[2];
    nr_pad *pad[2];
    bool port2;
} ports;

static void ports_open(ports *pt)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (pt->pad[i]) { nr_pad_close(pt->pad[i]); pt->pad[i] = NULL; }
        if (i == 1 && !pt->port2) continue;
        pt->pad[i] = nr_pad_open(pt->cfg[i].device, i);
        if (pt->pad[i]) NR_INFO("input", "port %d: %s", i + 1, nr_pad_name(pt->pad[i]));
    }
}

static nr_ui_key map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return UI_KEY_UP;
    case SDLK_DOWN: return UI_KEY_DOWN;
    case SDLK_LEFT: return UI_KEY_LEFT;
    case SDLK_RIGHT: return UI_KEY_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return UI_KEY_ACCEPT;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: return UI_KEY_BACK;
    default: return UI_KEY_NONE;
    }
}

static nr_ui_key map_button(Uint8 b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return UI_KEY_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return UI_KEY_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return UI_KEY_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return UI_KEY_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: return UI_KEY_ACCEPT;
    case SDL_GAMEPAD_BUTTON_EAST: return UI_KEY_BACK;
    default: return UI_KEY_NONE;
    }
}

/* ------------------------------------------------------------ main */

static int start_launcher(void)
{
    char path[1100], err[256];
    const char *argv[2];
    nr_proc *p;
    nr_sibling_exe(path, sizeof(path), "nrlauncher");
    if (!nr_file_exists(path)) {
        show_error("NR Engine must be started from the launcher (nrlauncher), which was not found next to this program.");
        return 2;
    }
    argv[0] = path;
    argv[1] = NULL;
    p = nr_proc_spawn(argv, NULL, NULL, err, sizeof(err));
    if (!p) { show_error(err); return 2; }
    nr_proc_free(p);
    return 0;
}

static void present_frame(nr_app *a, nr_vbackend *vb, SDL_Window *win, nr_ui *ui, u32 now, bool allow_hires_24)
{
    nr_vpresent pr;
    int nh;
    memset(&pr, 0, sizeof(pr));
    SDL_GetWindowSize(win, &pr.win_w, &pr.win_h);
    if (vb->gpu_hires && a->rec && (allow_hires_24 || !a->rec->display.rgb24)) {
        pr.use_hires = true;
        pr.display = &a->rec->display;
        pr.native_w = a->rec->display.width;
        pr.native_h = a->rec->display.height;
        if (a->rec->display.rgb24) {
            /* 24-bit pictures (movies) come from the console framebuffer */
            pr.picture = nr_app_picture(a, &pr.pic_w, &pr.pic_h, &nh);
        }
    } else {
        pr.picture = nr_app_picture(a, &pr.pic_w, &pr.pic_h, &nh);
        pr.native_h = nh;
        pr.native_w = a->swr_active && a->rec ? a->rec->display.width : pr.pic_w;
    }
    pr.frame = (u32)a->frame;
    if (nr_ui_render(ui, pr.win_w, pr.win_h, now)) pr.overlay = &ui->cv;
    vb->present(vb, &pr);
}

int main(int argc, char **argv)
{
    nr_app a;
    SDL_Window *win = NULL;
    nr_sdl_audio audio = 0;
    nr_vbackend vb;
    nr_sysinfo si;
    bool vb_ok = false, running = true, hires_synced = false, paused = false;
    ports pt;
    nr_config *cfg;
    nr_ui ui;
    nr_audio_sync as;
    s16 *abuf = NULL, *rbuf = NULL;
    u64 perf_freq, next_tick, fps_t0;
    u64 flips0 = 0;
    u32 presents = 0;
    double frame_ticks;
    int rc = 0, shot = 0;
    char shot_dir[1100];

    nr_utf8_args(&argc, &argv);
    nr_paths_init();
    memset(&a, 0, sizeof(a));
    memset(&pt, 0, sizeof(pt));
    nr_options_default(&a.opt);
    if (!nr_options_parse(&a.opt, argc, argv)) return 2;
    nr_log_set_level((nr_log_level)a.opt.log_level);
    if (a.opt.log_file[0]) nr_log_set_file(a.opt.log_file);

    if (a.opt.list_discs) {
        nr_disc_list dl;
        int i;
        nr_disc_scan(a.opt.gamedata_dir, &dl);
        for (i = 0; i < dl.ndirs; i++) printf("folder: %s\n", dl.dirs[i]);
        for (i = 0; i < dl.n; i++) printf("  %-40s %s (boot %s, volume %s)\n", dl.c[i].name, nr_cand_status_text(dl.c[i].status), dl.c[i].boot, dl.c[i].volume);
        if (!dl.n) printf("  no images found\n");
        return 0;
    }

    if (!nr_sdl_init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        NR_ERR("sdl", "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    /* The engine only runs from the launcher, where the settings are chosen. */
    if (!nr_session_valid(a.opt.launcher_session)) {
        SDL_Quit();
        return start_launcher();
    }
    if (!nr_sdl_init(SDL_INIT_AUDIO)) NR_WARN("sdl", "audio is not available: %s", SDL_GetError());

    cfg = nr_config_load(a.opt.config_path);
    nr_port_config_load(&pt.cfg[0], cfg, 0);
    nr_port_config_load(&pt.cfg[1], cfg, 1);
    nr_config_free(cfg);
    pt.port2 = a.opt.pad2;

    if (!nr_app_start(&a)) {
        show_error("The game could not be started. The reason is written in nrengine.log; "
                   "open the launcher to choose another disc image or other settings.");
        nr_app_shutdown(&a);
        SDL_Quit();
        return 2;
    }

    {
        char hw[256];
        nr_sysinfo_query(&si);
        nr_sysinfo_describe(&si, hw, sizeof(hw));
        NR_INFO("system", "%s", hw);
    }
    vb_ok = open_backend(&win, &a.opt, &vb, &si);
    if (!vb_ok) {
        show_error("No renderer could be started (see nrengine.log).");
        if (win) SDL_DestroyWindow(win);
        nr_app_shutdown(&a);
        SDL_Quit();
        return 1;
    }
    a.gpu_backend = vb.gpu_hires;
    nr_app_set_gfx(&a, &a.opt.gfx);
    ports_open(&pt);

    nr_ui_init(&ui);
    ui.show_fps = a.opt.show_fps;
    nr_path_join(shot_dir, sizeof(shot_dir), nr_data_dir(), "screenshots");

    {
        if (SDL_WasInit(SDL_INIT_AUDIO)) audio = nr_sdl_audio_open();
        if (!audio) NR_WARN("sdl", "no audio output: %s", SDL_GetError());
        else {
            const char *drv = SDL_GetCurrentAudioDriver();
            NR_INFO("audio", "output: %s (driver %s, 44100 Hz stereo)", nr_sdl_audio_device_name(audio), drv ? drv : "unknown");
            nr_sdl_audio_pause(audio, 0);
        }
    }
    nr_audio_sync_init(&as, 2205);    /* ~50 ms of queued audio */
    abuf = (s16 *)SDL_malloc(44100 * 4);
    rbuf = (s16 *)SDL_malloc(44100 * 4 + 64);

    perf_freq = SDL_GetPerformanceFrequency();
    frame_ticks = (double)perf_freq / NTSC_FPS;
    next_tick = SDL_GetPerformanceCounter();
    fps_t0 = next_tick;

    while (running && !a.p->halted) {
        SDL_Event ev;
        int i;
        u32 now = nr_sdl_ticks();
        /* test hook: NRENGINE_TEST_ESC_AT=N presses Esc at frame N, saves the
         * dialog to quit_dialog.png and then confirms the default answer */
        {
            static int esc_at = -2, step;
            if (esc_at == -2) { const char *e = getenv("NRENGINE_TEST_ESC_AT"); esc_at = e ? atoi(e) : -1; }
            if (esc_at >= 0 && (int)a.frame == esc_at && step == 0) {
                SDL_Event k;
                memset(&k, 0, sizeof(k));
                k.type = SDL_EVENT_KEY_DOWN; NR_SDL_KEY(k) = SDLK_ESCAPE; NR_SDL_SCAN(k) = SDL_SCANCODE_ESCAPE;
                SDL_PushEvent(&k);
                step = 1;
            } else if (step >= 1 && step < 30 && paused) {
                if (++step == 10) vb.capture_next = true;
                if (vb.cap_w > 0) { nr_write_png("quit_dialog.png", vb.cap_px, vb.cap_w, vb.cap_h); vb.cap_w = 0; }
                if (step == 29) {
                    SDL_Event k;
                    memset(&k, 0, sizeof(k));
                    k.type = SDL_EVENT_KEY_DOWN; NR_SDL_KEY(k) = SDLK_RETURN; NR_SDL_SCAN(k) = SDL_SCANCODE_RETURN;
                    SDL_PushEvent(&k);
                }
            }
        }
        while (SDL_PollEvent(&ev)) {
            nr_ui_key uk = UI_KEY_NONE;
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN: {
                SDL_Keycode k = NR_SDL_KEY(ev);
                if (ev.key.repeat) break;
                if (k == SDLK_F11) {
                    nr_sdl_toggle_fullscreen(win);
                } else if (k == SDLK_F12) {
                    char path[1300];
                    if (!nr_is_dir(shot_dir)) nr_mkdir(shot_dir);
                    for (;;) {
                        char name[64];
                        snprintf(name, sizeof(name), "screenshot_%03d.png", shot++);
                        nr_path_join(path, sizeof(path), shot_dir, name);
                        if (!nr_file_exists(path) || shot > 999) break;
                    }
                    if (nr_app_screenshot(&a, path)) nr_ui_message(&ui, now, "screenshot saved: %s", nr_path_basename(path));
                } else if (paused) {
                    uk = map_key(k);
                } else if (k == SDLK_ESCAPE) {
                    /* pause the simulation and ask */
                    nr_ui_open_quit(&ui);
                    paused = true;
                    if (audio) { nr_sdl_audio_pause(audio, 1); nr_sdl_audio_clear(audio); }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (paused) uk = map_button(NR_SDL_PAD_BUTTON(ev));
                break;
            case SDL_EVENT_JOYSTICK_ADDED:
            case SDL_EVENT_JOYSTICK_REMOVED:
                ports_open(&pt);
                break;
            default: break;
            }
            if (paused && uk != UI_KEY_NONE) {
                nr_ui_quit_result r = nr_ui_quit_key(&ui, uk);
                if (r == UI_QUIT_YES) running = false;
                else if (r == UI_QUIT_NO) {
                    /* resume: restart pacing and audio from now */
                    paused = false;
                    next_tick = SDL_GetPerformanceCounter();
                    if (audio) nr_sdl_audio_pause(audio, 0);
                }
            }
        }
        if (!running) break;

        if (paused) {
            /* simulation stopped: keep showing the last frame under the dialog */
            present_frame(&a, &vb, win, &ui, now, false);
            SDL_Delay(16);
            continue;
        }

        if (!a.nscript) {
            u8 ks[SDL_SCANCODE_COUNT];
            nr_sdl_keyboard(ks, sizeof(ks));
            for (i = 0; i < 2; i++) {
                nr_pad_update(pt.pad[i]);
                a.pad_pressed[i] = nr_port_read(&pt.cfg[i], pt.pad[i], ks);
            }
        }

        nr_app_frame(&a);
        if (vb.gpu_hires && a.rec) {
            if (!hires_synced) { vb.sync(&vb, a.p->gpu.vram); hires_synced = true; }
            else vb.execute(&vb, a.rec);
        } else {
            hires_synced = false;
        }

        /* audio: resampled by a tiny ratio to follow the device clock */
        if (audio) {
            u32 n = spu_pull_audio(a.p, abuf, 44100);
            u32 queued = nr_sdl_audio_queued(audio) / 4;
            u32 m = nr_audio_sync_process(&as, abuf, n, queued, rbuf, 44100);
            if (m) nr_sdl_audio_queue(audio, rbuf, m * 4);
        } else {
            spu_pull_audio(a.p, abuf, 44100);
        }

        /* video (optionally capped at 20/30 presentations per second) */
        {
            bool show = a.opt.fps_mode == 20 ? (a.frame % 3) == 0 : a.opt.fps_mode == 30 ? (a.frame % 2) == 0 : true;
            if (show) {
                for (i = 0; i < a.opt.n_screenshot_at; i++) if ((u64)a.opt.screenshot_at[i] == a.frame) vb.capture_next = true;
                vb.cap_w = 0;
                present_frame(&a, &vb, win, &ui, now, true);
                if (vb.cap_w > 0) {
                    char path[1200];
                    snprintf(path, sizeof(path), "%s%swindow_%06llu.png", a.opt.screenshot_dir, a.opt.screenshot_dir[0] ? "/" : "", (unsigned long long)a.frame);
                    if (nr_write_png(path, vb.cap_px, vb.cap_w, vb.cap_h)) NR_INFO("video", "window captured: %s (%dx%d)", path, vb.cap_w, vb.cap_h);
                    vb.cap_w = 0;
                }
                presents++;
            }
        }

        /* statistics once per second */
        {
            u64 t = SDL_GetPerformanceCounter();
            if (t - fps_t0 >= perf_freq) {
                double secs = (double)(t - fps_t0) / (double)perf_freq;
                ui.fps = (float)((double)(a.p->gpu.flips - flips0) / secs);
                ui.vps = (float)((double)presents / secs);
                flips0 = a.p->gpu.flips;
                presents = 0;
                fps_t0 = t;
            }
        }

        /* pacing at the NTSC field rate */
        next_tick += (u64)frame_ticks;
        {
            u64 t = SDL_GetPerformanceCounter();
            if (t < next_tick) {
                u64 ms = (next_tick - t) * 1000 / perf_freq;
                if (ms > 1) SDL_Delay((Uint32)(ms - 1));
                while (SDL_GetPerformanceCounter() < next_tick) { }
            } else if (t - next_tick > perf_freq / 4) {
                next_tick = t; /* too far behind: resync */
            }
        }
        /* The audio device is the reference clock: when the emulation got
         * ahead of it (e.g. while catching up after a stall), wait for the
         * queue to drain instead of discarding sound. */
        if (audio) {
            int guard = 0;
            while (nr_sdl_audio_queued(audio) / 4 > as.target * 2 && guard++ < 100) SDL_Delay(1);
            if (guard > 0) next_tick = SDL_GetPerformanceCounter();
        }
        if (a.opt.frames && (int)a.frame >= a.opt.frames) running = false;
    }
    if (a.p && a.p->halted) rc = 3;
    if (audio) NR_INFO("audio", "audio sync: %llu emulated samples, %llu sent, %llu dropped, final ratio %.4f",
                       (unsigned long long)as.consumed, (unsigned long long)as.produced, (unsigned long long)as.dropped, as.ratio);
    SDL_free(abuf);
    SDL_free(rbuf);
    for (shot = 0; shot < 2; shot++) nr_pad_close(pt.pad[shot]);
    if (audio) nr_sdl_audio_close(audio);
    if (vb_ok) vb.shutdown(&vb);
    nr_ui_free(&ui);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    nr_app_shutdown(&a);
    return rc;
}
