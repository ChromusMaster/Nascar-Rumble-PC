#include "app.h"
#include "../common/paths.h"
#include "../render/swr.h"
#include "../render/postfx.h"
#include "../render/pgxp.h"
#include "../mods/mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void apply_gte_enhancements(nr_app *a, const nr_gfx_settings *s);

static u16 host_read_pad(void *user, int port)
{
    nr_app *a = (nr_app *)user;
    return (u16)~a->pad_pressed[port & 1];
}

static bool host_pad_connected(void *user, int port)
{
    nr_app *a = (nr_app *)user;
    return a->pad_connected[port & 1];
}

void nr_options_default(nr_options *o)
{
    memset(o, 0, sizeof(*o));
    o->log_level = NR_LOG_INFO;
    o->scale = 3;
    o->natives = true;
    o->cpu_overclock = 0; /* 0 = automatic (depends on fps mode) */
    o->physics_step = -1; /* automatic: steady 30 Hz, or 60 Hz in the 60 fps mode */
    nr_strlcpy(o->renderer, "auto", sizeof(o->renderer));
    nr_gfx_defaults(&o->gfx);
    nr_strlcpy(o->memcard_path[0], "memcard1.mcd", sizeof(o->memcard_path[0]));
}

static void usage(void)
{
    fprintf(stderr,
        "NASCAR Rumble Reimplementation Engine\n"
        "Usage: nrengine_headless [options] [disc image .cue/.bin/.iso]\n"
        "The windowed engine (nrengine) is started by the launcher (nrlauncher), which\n"
        "writes these settings to nrengine.ini. Without an image on the command line\n"
        "game.disc / game.disc_image from nrengine.ini are used, or the gamedata/ folder\n"
        "is searched automatically.\n\n"
        "Disc and configuration:\n"
        "  --config FILE         configuration file (default: nrengine.ini in the user folder)\n"
        "  --gamedata DIR        folder searched for the disc image (default: gamedata/)\n"
        "  --list-discs          list the images found and exit\n"
        "  --verify              check the CRC32 of every file on the disc\n"
        "  --allow-modified      run even if the disc differs from the reference\n"
        "  --memcard1 FILE       memory card in slot 1 (default: memcard1.mcd)\n"
        "  --memcard2 FILE       memory card in slot 2 (default: none)\n"
        "  --pad2                connect a controller to port 2 (2 players)\n"
        "  --language NAME       translation in mods/Language/NAME (default: original)\n"
        "  --mod LIST            other mods (folders under mods/, comma separated)\n"
        "\nVideo (enhancements of the displayed picture only):\n"
        "  --renderer R          auto | opengl | d3d9 | d3d10 | d3d11 | d3d12 | software\n"
        "  --internal-res N      internal resolution 1..8 (1 = console)\n"
        "  --aa N                supersampling anti-aliasing: 1, 2, 4, 8, 16\n"
        "  --true-color          24-bit colour (no 15-bit quantisation)\n"
        "  --no-dither           turn off the console's dithering\n"
        "  --filter N            texture filtering: 0 none, 1 bilinear 3D, 2 bilinear everything\n"
        "  --scaling M           integer | nearest | bilinear | sharp\n"
        "  --postfx M            off | crt | scanlines | vignette | colorgrade | grain | sharpen | cinematic\n"
        "  --tonemap M           off | reinhard | aces | filmic\n"
        "  --pgxp                sub-pixel vertices + perspective-correct textures (experimental)\n"
        "  --widescreen          16:9 projection (experimental; changes what the game computes)\n"
        "  --fps MODE            original (default) | 20 | 30 (limit what is shown) | 60 (unlocked)\n"
        "  --scale N             initial window scale (default 3)\n"
        "  --fullscreen          full screen\n"
        "\nEngine:\n"
        "  --overclock N         emulated CPU multiplier (1 = console)\n"
        "  --physics-step S      auto (default) | original | 30 | 60: steady game step in Hz\n"
        "  --no-native           disable the native reimplementations (original code only)\n"
        "  --verify-natives      compare every native call with the original routine\n"
        "  --gpu-instant         disable the GPU drawing-time model\n"
        "  --threads N           software renderer threads (0 = automatic)\n"
        "\nDiagnostics:\n"
        "  --headless            no window/audio\n"
        "  --frames N            stop after N frames\n"
        "  --screenshot-every N  save a PNG every N frames (into --screenshot-dir)\n"
        "  --screenshot-at A,B   save a PNG at the given frames\n"
        "  --screenshot-dir DIR  folder for PNGs (default: .)\n"
        "  --input-script FILE   scripted input (lines: frame buttons)\n"
        "  --log-level N         0=error 1=warning 2=info 3=debug 4=trace\n"
        "  --log-file FILE       also write the log to a file\n"
        "  --trace               trace every instruction (very slow)\n"
        "  --wav FILE            (headless) write the SPU audio output to a WAV file\n"
        "  --dump-hle            show the HLE kernel state on exit\n"
        "  --vram-at N           save the whole VRAM (1024x512) as PNG at frame N\n"
        "  --ram-at A,B          save the 2 MB main RAM to ram_NNNNNN.bin at the given frames\n"
        "  --save-state-at N:FILE  write a machine snapshot at frame N (local analysis only)\n"
        "  --load-state FILE     start from a snapshot written by --save-state-at\n"
        "  --peek ADDRESS        with --dump-hle: print the 32-bit word at ADDRESS (hex) on exit\n"
        "  --watch-pc ADDRESS    log every execution of ADDRESS (hex)\n"
        "  --watch-write ADDRESS log every write to ADDRESS (hex)\n"
        "  --profile-pc A,B      sample the CPU program counter between frames A and B\n");
}

static bool parse_bool(const char *v)
{
    return nr_strcasecmp(v, "1") == 0 || nr_strcasecmp(v, "on") == 0 || nr_strcasecmp(v, "true") == 0 ||
           nr_strcasecmp(v, "yes") == 0 || nr_strcasecmp(v, "sim") == 0;
}

static int lookup(const char *v, const char *const *names, int n, int def)
{
    int i;
    for (i = 0; i < n; i++) if (nr_strcasecmp(v, names[i]) == 0) return i;
    if (v[0] >= '0' && v[0] <= '9') { i = atoi(v); if (i >= 0 && i < n) return i; }
    return def;
}

bool nr_gfx_parse(nr_gfx_settings *s, const char *key, const char *v)
{
    static const char *const scal[] = { "integer", "nearest", "bilinear", "sharp" };
    static const char *const fx[] = { "off", "crt", "scanlines", "vignette", "colorgrade", "grain", "sharpen", "cinematic" };
    static const char *const tm[] = { "off", "reinhard", "aces", "filmic" };
    static const char *const ar[] = { "4:3", "stretch", "16:9" };
    if (strcmp(key, "internal_resolution") == 0) { s->internal_scale = atoi(v); if (s->internal_scale < 1) s->internal_scale = 1; if (s->internal_scale > 8) s->internal_scale = 8; }
    else if (strcmp(key, "antialiasing") == 0) {
        int n = atoi(v);
        s->antialias = (n == 2 || n == 4 || n == 8 || n == 16) ? n : 1;
    }
    else if (strcmp(key, "true_color") == 0) s->true_color = parse_bool(v);
    else if (strcmp(key, "dithering") == 0) s->dithering = parse_bool(v);
    else if (strcmp(key, "texture_filter") == 0) { s->texture_filter = atoi(v); if (s->texture_filter < 0 || s->texture_filter > 2) s->texture_filter = 0; }
    else if (strcmp(key, "scaling") == 0) s->scaling = (nr_gfx_scaling)lookup(v, scal, 4, GFX_SCALE_BILINEAR);
    else if (strcmp(key, "aspect") == 0) s->aspect = (nr_gfx_aspect)lookup(v, ar, 3, GFX_AR_4_3);
    else if (strcmp(key, "post_fx") == 0) s->post_fx = (nr_gfx_fx)lookup(v, fx, GFX_FX_COUNT, GFX_FX_OFF);
    else if (strcmp(key, "post_fx_strength") == 0) s->post_fx_strength = (float)atof(v);
    else if (strcmp(key, "tonemap") == 0) s->tonemap = (nr_gfx_tonemap)lookup(v, tm, GFX_TM_COUNT, GFX_TM_OFF);
    else if (strcmp(key, "brightness") == 0) s->brightness = (float)atof(v);
    else if (strcmp(key, "contrast") == 0) s->contrast = (float)atof(v);
    else if (strcmp(key, "saturation") == 0) s->saturation = (float)atof(v);
    else if (strcmp(key, "gamma") == 0) s->gamma = (float)atof(v);
    else if (strcmp(key, "vsync") == 0) s->vsync = parse_bool(v);
    else if (strcmp(key, "pgxp") == 0) s->pgxp = parse_bool(v);
    else if (strcmp(key, "widescreen") == 0) s->widescreen = parse_bool(v);
    else return false;
    if (s->post_fx_strength < 0) s->post_fx_strength = 0;
    if (s->post_fx_strength > 1) s->post_fx_strength = 1;
    if (s->gamma < 0.3f) s->gamma = 0.3f;
    if (s->gamma > 3.0f) s->gamma = 3.0f;
    return true;
}

static void apply_config(nr_options *o, const nr_config *c)
{
    const char *v;
    static const char *const gfx_keys[] = { "internal_resolution", "antialiasing", "true_color", "dithering", "texture_filter",
                                            "scaling", "aspect", "post_fx", "post_fx_strength", "tonemap", "brightness",
                                            "contrast", "saturation", "gamma", "vsync", "pgxp", "widescreen" };
    size_t k;
    if ((v = nr_config_get(c, "game.disc", NULL))) nr_strlcpy(o->cfg_disc, v, sizeof(o->cfg_disc));
    if ((v = nr_config_get(c, "game.disc_image", NULL))) nr_strlcpy(o->cfg_disc_image, v, sizeof(o->cfg_disc_image));
    if ((v = nr_config_get(c, "game.gamedata", NULL))) nr_strlcpy(o->gamedata_dir, v, sizeof(o->gamedata_dir));
    if ((v = nr_config_get(c, "game.language", NULL))) nr_strlcpy(o->language, v, sizeof(o->language));
    if ((v = nr_config_get(c, "mods.load", NULL))) nr_strlcpy(o->mods_extra, v, sizeof(o->mods_extra));
    if ((v = nr_config_get(c, "video.renderer", NULL))) nr_strlcpy(o->renderer, v, sizeof(o->renderer));
    for (k = 0; k < NR_ARRAY_COUNT(gfx_keys); k++) {
        char key[64];
        snprintf(key, sizeof(key), "video.%s", gfx_keys[k]);
        if ((v = nr_config_get(c, key, NULL))) nr_gfx_parse(&o->gfx, gfx_keys[k], v);
    }
    o->threads = nr_config_get_int(c, "engine.threads", o->threads);
    if ((v = nr_config_get(c, "memcard.slot1", NULL))) nr_strlcpy(o->memcard_path[0], v, sizeof(o->memcard_path[0]));
    if ((v = nr_config_get(c, "memcard.slot2", NULL))) nr_strlcpy(o->memcard_path[1], v, sizeof(o->memcard_path[1]));
    o->scale = nr_config_get_int(c, "video.scale", o->scale);
    o->fullscreen = nr_config_get_int(c, "video.fullscreen", o->fullscreen) != 0;
    o->log_level = nr_config_get_int(c, "debug.log_level", o->log_level);
    o->natives = nr_config_get_int(c, "engine.native_overrides", o->natives) != 0;
    o->gpu_instant = nr_config_get_int(c, "engine.gpu_timing_model", !o->gpu_instant) == 0;
    o->pad2 = nr_config_get_int(c, "input.port2", o->pad2) != 0;
    if ((v = nr_config_get(c, "video.fps", NULL))) o->fps_mode = nr_strcasecmp(v, "original") == 0 ? 0 : atoi(v);
    o->cpu_overclock = nr_config_get_int(c, "engine.cpu_overclock", o->cpu_overclock);
    if ((v = nr_config_get(c, "engine.physics_step", NULL))) o->physics_step = nr_physics_step_parse(v);
    o->show_fps = nr_config_get_int(c, "video.show_fps", o->show_fps) != 0;
    if ((v = nr_config_get(c, "debug.log_file", NULL)) && *v) nr_strlcpy(o->log_file, v, sizeof(o->log_file));
    o->verify = nr_config_get_int(c, "game.verify", o->verify) != 0;
}

bool nr_options_parse(nr_options *o, int argc, char **argv)
{
    int i;
    const char *cfg_path = nr_default_config_path();
    nr_config *cfg;
    /* first pass: config path */
    for (i = 1; i < argc; i++) if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) cfg_path = argv[i + 1];
    nr_strlcpy(o->config_path, cfg_path, sizeof(o->config_path));
    cfg = nr_config_load(cfg_path);
    apply_config(o, cfg);
    nr_config_free(cfg);
    for (i = 1; i < argc; i++) {
        const char *s = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), (char *)NULL))
        if (strcmp(s, "--config") == 0) { i++; }
        else if (strcmp(s, "--headless") == 0) o->headless = true;
        else if (strcmp(s, "--frames") == 0) { char *v = NEXT(); if (!v) return false; o->frames = atoi(v); }
        else if (strcmp(s, "--screenshot-every") == 0) { char *v = NEXT(); if (!v) return false; o->screenshot_every = atoi(v); }
        else if (strcmp(s, "--screenshot-at") == 0) {
            char *v = NEXT(), *t;
            if (!v) return false;
            for (t = v; *t && o->n_screenshot_at < 64;) {
                o->screenshot_at[o->n_screenshot_at++] = atoi(t);
                t = strchr(t, ',');
                if (!t) break;
                t++;
            }
        }
        else if (strcmp(s, "--save-state-at") == 0) {
            char *v = NEXT(), *c;
            if (!v) return false;
            o->state_save_at = atoi(v);
            c = strchr(v, ':');
            nr_strlcpy(o->state_save_path, c ? c + 1 : "state.nrs", sizeof(o->state_save_path));
        }
        else if (strcmp(s, "--load-state") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->state_load_path, v, sizeof(o->state_load_path)); }
        else if (strcmp(s, "--ram-at") == 0) {
            char *v = NEXT(), *t;
            if (!v) return false;
            for (t = v; *t && o->n_ram_at < 64;) { o->ram_at[o->n_ram_at++] = atoi(t); t = strchr(t, ','); if (!t) break; t++; }
        }
        else if (strcmp(s, "--vram-at") == 0) { char *v = NEXT(); if (!v) return false; if (o->n_vram_at < 16) o->vram_at[o->n_vram_at++] = atoi(v); }
        else if (strcmp(s, "--screenshot-dir") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->screenshot_dir, v, sizeof(o->screenshot_dir)); }
        else if (strcmp(s, "--input-script") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->input_script, v, sizeof(o->input_script)); }
        else if (strcmp(s, "--memcard1") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->memcard_path[0], v, sizeof(o->memcard_path[0])); }
        else if (strcmp(s, "--memcard2") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->memcard_path[1], v, sizeof(o->memcard_path[1])); }
        else if (strcmp(s, "--verify") == 0) o->verify = true;
        else if (strcmp(s, "--allow-modified") == 0) o->allow_modified = true;
        else if (strcmp(s, "--no-native") == 0) o->natives = false;
        else if (strcmp(s, "--verify-natives") == 0) o->verify_natives = true;
        else if (strcmp(s, "--gpu-instant") == 0) o->gpu_instant = true;
        else if (strcmp(s, "--log-level") == 0) { char *v = NEXT(); if (!v) return false; o->log_level = atoi(v); }
        else if (strcmp(s, "--log-file") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->log_file, v, sizeof(o->log_file)); }
        else if (strcmp(s, "--trace") == 0) o->trace = true;
        else if (strcmp(s, "--wav") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->wav_path, v, sizeof(o->wav_path)); }
        else if (strcmp(s, "--dump-hle") == 0) o->dump_hle_at_exit = true;
        else if (strcmp(s, "--peek") == 0) { char *v = NEXT(); if (!v) return false; if (o->npeek < 32) o->peek[o->npeek++] = (u32)strtoul(v, NULL, 16); }
        else if (strcmp(s, "--watch-write") == 0) { char *v = NEXT(); if (!v) return false; o->watch_write = (u32)strtoul(v, NULL, 16); }
        else if (strcmp(s, "--profile-pc") == 0) { char *v = NEXT(), *c; if (!v) return false; o->prof_from = atoi(v); c = strchr(v, ','); o->prof_to = c ? atoi(c + 1) : o->prof_from + 600; }
        else if (strcmp(s, "--watch-pc") == 0) { char *v = NEXT(); if (!v) return false; o->watch_pc = (u32)strtoul(v, NULL, 16); }
        else if (strcmp(s, "--scale") == 0) { char *v = NEXT(); if (!v) return false; o->scale = atoi(v); }
        else if (strcmp(s, "--fullscreen") == 0) o->fullscreen = true;
        else if (strcmp(s, "--pad2") == 0) o->pad2 = true;
        else if (strcmp(s, "--gamedata") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->gamedata_dir, v, sizeof(o->gamedata_dir)); }
        else if (strcmp(s, "--list-discs") == 0) o->list_discs = true;
        else if (strcmp(s, "--language") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->language, v, sizeof(o->language)); }
        else if (strcmp(s, "--mod") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->mods_extra, v, sizeof(o->mods_extra)); }
        else if (strcmp(s, "--renderer") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->renderer, v, sizeof(o->renderer)); }
        else if (strcmp(s, "--internal-res") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "internal_resolution", v); }
        else if (strcmp(s, "--aa") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "antialiasing", v); }
        else if (strcmp(s, "--true-color") == 0) o->gfx.true_color = true;
        else if (strcmp(s, "--no-dither") == 0) o->gfx.dithering = false;
        else if (strcmp(s, "--filter") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "texture_filter", v); }
        else if (strcmp(s, "--scaling") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "scaling", v); }
        else if (strcmp(s, "--postfx") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "post_fx", v); }
        else if (strcmp(s, "--tonemap") == 0) { char *v = NEXT(); if (!v) return false; nr_gfx_parse(&o->gfx, "tonemap", v); }
        else if (strcmp(s, "--pgxp") == 0) o->gfx.pgxp = true;
        else if (strcmp(s, "--widescreen") == 0) { o->gfx.widescreen = true; o->gfx.aspect = GFX_AR_16_9; }
        else if (strcmp(s, "--threads") == 0) { char *v = NEXT(); if (!v) return false; o->threads = atoi(v); }
        else if (strcmp(s, "--fps") == 0) { char *v = NEXT(); if (!v) return false; o->fps_mode = nr_strcasecmp(v, "original") == 0 ? 0 : atoi(v); }
        else if (strcmp(s, "--physics-step") == 0) { char *v = NEXT(); if (!v) return false; o->physics_step = nr_physics_step_parse(v); }
        else if (strcmp(s, "--overclock") == 0) { char *v = NEXT(); if (!v) return false; o->cpu_overclock = atoi(v); }
        else if (strcmp(s, "--launcher-session") == 0) { char *v = NEXT(); if (!v) return false; nr_strlcpy(o->launcher_session, v, sizeof(o->launcher_session)); }
        else if (strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0) { usage(); return false; }
        else if (s[0] == '-') { fprintf(stderr, "Unknown option: %s\n\n", s); usage(); return false; }
        else nr_strlcpy(o->disc_path, s, sizeof(o->disc_path));
#undef NEXT
    }
    return true;
}

static bool load_script(nr_app *a, const char *path)
{
    u8 *txt;
    long n = nr_read_file(path, &txt);
    char *line, *next;
    if (n < 0) { NR_ERR("app", "cannot read the input script %s", path); return false; }
    for (line = (char *)txt; line && *line; line = next) {
        int frame;
        char btns[256] = { 0 };
        next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; while (*next == '\r' || *next == '\n') next++; }
        if (line[0] == '#' || !line[0]) continue;
        if (sscanf(line, "%d %255[^\n]", &frame, btns) >= 1) {
            static const struct { const char *n; u16 b; } map[] = {
                { "select", PAD_SELECT }, { "start", PAD_START }, { "up", PAD_UP }, { "right", PAD_RIGHT },
                { "down", PAD_DOWN }, { "left", PAD_LEFT }, { "l2", PAD_L2 }, { "r2", PAD_R2 }, { "l1", PAD_L1 },
                { "r1", PAD_R1 }, { "triangle", PAD_TRIANGLE }, { "circle", PAD_CIRCLE }, { "cross", PAD_CROSS },
                { "square", PAD_SQUARE } };
            /* "port 1 buttons | port 2 buttons" (the port 2 part is optional) */
            u16 b[2] = { 0, 0 };
            size_t k;
            char *bar = strchr(btns, '|');
            if (bar) {
                *bar = 0;
                for (k = 0; k < NR_ARRAY_COUNT(map); k++) if (strstr(bar + 1, map[k].n)) b[1] |= map[k].b;
            }
            for (k = 0; k < NR_ARRAY_COUNT(map); k++) if (strstr(btns, map[k].n)) b[0] |= map[k].b;
            a->script = (nr_input_event *)realloc(a->script, sizeof(nr_input_event) * (size_t)(a->nscript + 1));
            a->script[a->nscript].frame = frame;
            a->script[a->nscript].buttons[0] = b[0];
            a->script[a->nscript].buttons[1] = b[1];
            a->nscript++;
        }
    }
    free(txt);
    NR_INFO("app", "input script: %d events", a->nscript);
    return true;
}

bool nr_app_start(nr_app *a)
{
    char err[512];
    nr_game_status st;
    nr_log_set_level((nr_log_level)a->opt.log_level);
    if (a->opt.log_file[0]) nr_log_set_file(a->opt.log_file);

    {
        nr_disc_list *dl = (nr_disc_list *)calloc(1, sizeof(nr_disc_list));
        char how[256];
        int i;
        if (!dl) return false;
        nr_disc_scan(a->opt.gamedata_dir, dl);
        if (!nr_disc_resolve(a->opt.disc_path, a->opt.cfg_disc, a->opt.cfg_disc_image, dl, a->disc_resolved,
                             sizeof(a->disc_resolved), how, sizeof(how))) {
            NR_ERR("app", "No NASCAR Rumble (USA) disc image found.");
            NR_ERR("app", "Choose it in the launcher, set game.disc in nrengine.ini,");
            NR_ERR("app", "or put your own image (.cue/.bin/.iso) in one of these folders:");
            for (i = 0; i < dl->ndirs; i++) NR_ERR("app", "  %s", dl->dirs[i]);
            for (i = 0; i < dl->n; i++)
                NR_ERR("app", "  (found but not usable: %s - %s, boot %s)", dl->c[i].name, nr_cand_status_text(dl->c[i].status),
                       dl->c[i].boot[0] ? dl->c[i].boot : "-");
            free(dl);
            return false;
        }
        NR_INFO("app", "Disc chosen by %s: %s", how, a->disc_resolved);
        free(dl);
    }
    a->disc = nr_disc_open(a->disc_resolved, err, sizeof(err));
    if (!a->disc) { NR_ERR("app", "%s", err); return false; }
    NR_INFO("app", "Image: %s (%u sectors, %s)", nr_disc_image_path(a->disc), nr_disc_sector_count(a->disc),
            nr_disc_is_raw(a->disc) ? "2352 bytes/sector" : "2048 bytes/sector");
    st = nr_game_identify(a->disc, a->opt.verify, &a->report);
    if (st == NR_GAME_OK) NR_INFO("app", "%s", a->report.message);
    else NR_WARN("app", "%s", a->report.message);
    if (st == NR_GAME_UNREADABLE || st == NR_GAME_NOT_NASCAR) {
        NR_ERR("app", "Unsupported disc. This engine needs a copy of NASCAR Rumble (USA) - SLUS-01068.");
        return false;
    }
    if (st == NR_GAME_MODIFIED && !a->opt.allow_modified) {
        NR_ERR("app", "The disc differs from the reference version. Use --allow-modified to run it anyway.");
        return false;
    }

    /* mods: replacement files are served by the virtual CD layer */
    a->mods = nr_mods_open(a->opt.language, a->opt.mods_extra, a->disc);

    a->p = psx_create();
    if (!a->p) { NR_ERR("app", "out of memory"); return false; }
    a->p->host.user = a;
    a->p->host.read_pad = host_read_pad;
    a->p->host.pad_connected = host_pad_connected;
    a->pad_connected[0] = true;
    a->pad_connected[1] = a->opt.pad2;
    a->p->trace = a->opt.trace;
    a->p->watch_pc = a->opt.watch_pc;
    a->p->watch_write = a->opt.watch_write;
    if (a->opt.fps_mode != 0 && a->opt.fps_mode != 20 && a->opt.fps_mode != 30 && a->opt.fps_mode != 60) {
        NR_WARN("app", "video.fps=%d is invalid; using original", a->opt.fps_mode);
        a->opt.fps_mode = 0;
    }
    nr_app_set_fps_mode(a, a->opt.fps_mode);
    psx_attach_disc(a->p, a->disc);
    {
        int k;
        for (k = 0; k < 2; k++) {
            char mc[1100];
            if (!a->opt.memcard_path[k][0]) continue;
            nr_data_path(mc, sizeof(mc), a->opt.memcard_path[k]);   /* relative names live in the data folder */
            hle_set_memcard(a->p, k, mc);
        }
    }
    psx_reset(a->p);
    if (!hle_boot_disc(a->p, err, sizeof(err))) { NR_ERR("app", "%s", err); return false; }
    if (a->opt.natives) {
        if (a->report.exe_sha1_match) nr_native_register_all(a->p);
        else NR_WARN("app", "executable differs from the reference: native reimplementations disabled");
        a->p->ovr_verify = a->opt.verify_natives;
    }
    if (a->opt.physics_step) {
        int hz = a->opt.physics_step < 0 ? (a->opt.fps_mode == 60 ? 60 : 30) : a->opt.physics_step;
        if (hz == 60 && a->opt.fps_mode != 60) {
            NR_WARN("app", "the 60 Hz physics step needs the 60 fps mode (video.fps = 60); using 30 Hz");
            hz = 30;
        }
        if (a->report.exe_sha1_match) nr_pace_register(a->p, 300 / hz);
        else NR_WARN("app", "executable differs from the reference: steady physics step disabled");
    }
    if (a->opt.input_script[0] && !load_script(a, a->opt.input_script)) return false;
    a->fb = (u32 *)calloc(1024 * 512, 4);
    a->frame = 0;
    if (a->mods) {
        if (a->opt.verify_natives) NR_WARN("mods", "--verify-natives is on: translated texts are disabled for this run");
        else nr_mods_attach(a->mods, a->p);
    }
    if (a->opt.headless) {
        if (nr_gfx_needs_hires(&a->opt.gfx)) nr_app_set_gfx(a, &a->opt.gfx);
        else apply_gte_enhancements(a, &a->opt.gfx);
    }
    if (a->opt.state_load_path[0] && !nr_app_load_state(a, a->opt.state_load_path)) {
        NR_ERR("app", "cannot load the snapshot %s", a->opt.state_load_path);
        return false;
    }
    if (a->opt.wav_path[0]) {
        static const u8 hdr[44] = { 0 };
        a->wav = nr_fopen(a->opt.wav_path, "wb");
        if (!a->wav) { NR_ERR("app", "cannot create %s", a->opt.wav_path); return false; }
        fwrite(hdr, 1, 44, (FILE *)a->wav);
    }
    return true;
}

int nr_physics_step_parse(const char *v)
{
    int n;
    if (!v || !*v || nr_strcasecmp(v, "auto") == 0) return -1;
    if (nr_strcasecmp(v, "original") == 0) return 0;
    n = atoi(v);
    if (n == 30 || n == 60) return n;
    NR_WARN("app", "engine.physics_step=%s is invalid (use auto, original, 30 or 60); using auto", v);
    return -1;
}

void nr_app_set_fps_mode(nr_app *a, int mode)
{
    if (mode != 0 && mode != 20 && mode != 30 && mode != 60) mode = 0;
    a->opt.fps_mode = mode;
    a->p->gpu.timing = !a->opt.gpu_instant && mode != 60;
    a->p->cpu_overclock = a->opt.cpu_overclock > 0 ? (u32)a->opt.cpu_overclock : (mode == 60 ? (a->opt.physics_step != 0 ? 4u : 3u) : 1u);
    if (a->p->cpu_overclock > 8) a->p->cpu_overclock = 8;
    if (mode == 60 || a->p->cpu_overclock > 1 || !a->p->gpu.timing)
        NR_INFO("app", "performance mode: GPU %s, CPU x%u (enhancement; changes the pace compared with the console)",
                a->p->gpu.timing ? "with drawing time" : "instant", a->p->cpu_overclock);
}

bool nr_gfx_needs_hires(const nr_gfx_settings *s)
{
    return s->internal_scale > 1 || s->antialias > 1 || s->true_color || !s->dithering || s->texture_filter > 0 || s->pgxp;
}

static void apply_gte_enhancements(nr_app *a, const nr_gfx_settings *s)
{
    if (s->pgxp && !a->pgxp) {
        a->pgxp = (struct nr_pgxp_s *)calloc(1, sizeof(nr_pgxp));
        NR_INFO("render", "PGXP (sub-pixel vertices) enabled - experimental");
    }
    a->p->gte.pgxp = s->pgxp ? a->pgxp : NULL;
    a->p->gpu.pgxp = s->pgxp ? a->pgxp : NULL;
    if (s->widescreen != (a->p->gte.ws_num != 0)) {
        a->p->gte.ws_num = s->widescreen ? 3 : 0;
        a->p->gte.ws_den = s->widescreen ? 4 : 1;
        NR_INFO("render", "widescreen %s (experimental enhancement: changes the projection computed by the game)", s->widescreen ? "enabled" : "disabled");
    }
}

void nr_app_enable_recording(nr_app *a, bool on)
{
    if (on && !a->rec) {
        a->rec = nr_rlist_new();
        a->p->gpu.rec = a->rec;
    } else if (!on && a->rec) {
        a->p->gpu.rec = NULL;
        nr_rlist_free(a->rec);
        a->rec = NULL;
    }
}

void nr_app_set_gfx(nr_app *a, const nr_gfx_settings *s)
{
    bool want = nr_gfx_needs_hires(s), sync = false;
    a->opt.gfx = *s;
    apply_gte_enhancements(a, s);
    if (a->gpu_backend) {
        nr_app_enable_recording(a, want);
        a->swr_active = false;
        return;
    }
    if (!want) {
        if (a->swr_active) NR_INFO("render", "console picture (no resolution/AA enhancements)");
        a->swr_active = false;
        return;
    }
    if (!a->swr) {
        a->swr = nr_swr_new(a->opt.threads);
        if (!a->swr) return;
    }
    if (!nr_swr_configure(a->swr, s, &sync)) return;
    nr_app_enable_recording(a, true);
    if (sync || !a->swr_active) {
        nr_swr_sync(a->swr, a->p->gpu.vram);
        nr_rlist_clear(a->rec);
    }
    if (!a->swr_active) {
        int sx, sy;
        nr_swr_scale(a->swr, &sx, &sy);
        NR_INFO("render", "software renderer: internal resolution %dx, AA %dx (%dx%d samples per console pixel), "
                "colour %s, filter %d", s->internal_scale, s->antialias, sx, sy, s->true_color ? "24-bit" : "15-bit", s->texture_filter);
    }
    a->swr_active = true;
}

const u32 *nr_app_picture(nr_app *a, int *w, int *h, int *native_h)
{
    if (a->swr_active && a->rec) {
        const u32 *o = nr_swr_output(a->swr, a->rec, w, h);
        if (native_h) *native_h = a->rec->display.height;
        if (o) return o;
    }
    gpu_get_display(a->p, a->fb, w, h);
    if (native_h) *native_h = *h;
    return a->fb;
}

bool nr_app_screenshot(nr_app *a, const char *path)
{
    int w, h, nh;
    const u32 *img = nr_app_picture(a, &w, &h, &nh);
    if (nr_postfx_needed(&a->opt.gfx) && a->swr_active) {
        size_t need = (size_t)w * h;
        if (need > a->post_cap) {
            u32 *n = (u32 *)realloc(a->post, need * 4);
            if (!n) return false;
            a->post = n; a->post_cap = need;
        }
        nr_postfx_render(&a->opt.gfx, img, w, h, nh, a->post, w, h, (u32)a->frame, NULL);
        img = a->post;
    }
    return nr_write_png(path, img, w, h);
}

/* Prints the most sampled code addresses (diagnostic, --profile-pc). */
static void profile_report(nr_app *a)
{
    u32 *h = a->p->pcprof, i, k, total = 0;
    enum { TOP = 40 };
    u32 best[TOP], bi[TOP];
    memset(best, 0, sizeof(best)); memset(bi, 0, sizeof(bi));
    for (i = 0; i < PSX_RAM_SIZE / 4; i++) total += h[i];
    for (i = 0; i < PSX_RAM_SIZE / 4; i++) {
        u32 v = h[i];
        if (v <= best[TOP - 1]) continue;
        for (k = TOP - 1; k > 0 && best[k - 1] < v; k--) { best[k] = best[k - 1]; bi[k] = bi[k - 1]; }
        best[k] = v; bi[k] = i;
    }
    NR_INFO("prof", "PC samples, frames %d-%d: %u", a->opt.prof_from, a->opt.prof_to, total);
    for (k = 0; k < TOP && best[k]; k++)
        NR_INFO("prof", "  %08X  %6u  %5.1f%%", 0x80000000u | (bi[k] << 2), best[k], 100.0 * best[k] / (total ? total : 1));
}

void nr_app_frame(nr_app *a)
{
    int i;
    while (a->script_pos < a->nscript && a->script[a->script_pos].frame <= (int)a->frame) {
        a->pad_pressed[0] = a->script[a->script_pos].buttons[0];
        a->pad_pressed[1] = a->script[a->script_pos].buttons[1];
        a->script_pos++;
    }
    if (a->rec) nr_rlist_clear(a->rec);
    if (a->mods) nr_mods_frame(a->mods, a->p);
    if (a->opt.prof_to > 0) {
        if ((int)a->frame == a->opt.prof_from && !a->p->pcprof) a->p->pcprof = (u32 *)calloc(PSX_RAM_SIZE / 4, sizeof(u32));
        if ((int)a->frame == a->opt.prof_to && a->p->pcprof) { profile_report(a); free(a->p->pcprof); a->p->pcprof = NULL; }
    }
    psx_run_frame(a->p);
    a->frame++;
    if (a->swr_active && a->rec) nr_swr_execute(a->swr, a->rec);
    if (a->wav) {
        s16 buf[2048 * 2];
        u32 n;
        while ((n = spu_pull_audio(a->p, buf, 2048)) > 0) {
            fwrite(buf, 4, n, (FILE *)a->wav);
            a->wav_frames += n;
        }
    } else if (a->opt.headless) {
        s16 buf[2048 * 2];
        while (spu_pull_audio(a->p, buf, 2048) > 0) { }
    }
    if (a->opt.screenshot_every && (a->frame % (u64)a->opt.screenshot_every) == 0) {
        char path[1200];
        snprintf(path, sizeof(path), "%s%sframe_%06llu.png", a->opt.screenshot_dir, a->opt.screenshot_dir[0] ? "/" : "", (unsigned long long)a->frame);
        nr_app_screenshot(a, path);
    }
    for (i = 0; i < a->opt.n_vram_at; i++) {
        if ((u64)a->opt.vram_at[i] == a->frame) {
            char path[1200];
            u32 *v = (u32 *)malloc(1024 * 512 * 4);
            snprintf(path, sizeof(path), "%s%svram_%06llu.png", a->opt.screenshot_dir, a->opt.screenshot_dir[0] ? "/" : "", (unsigned long long)a->frame);
            gpu_get_vram_rgba(a->p, v);
            nr_write_png(path, v, 1024, 512);
            free(v);
        }
    }
    if (a->opt.state_save_at > 0 && (u64)a->opt.state_save_at == a->frame) {
        if (nr_app_save_state(a, a->opt.state_save_path)) NR_INFO("app", "snapshot written: %s (frame %llu)", a->opt.state_save_path, (unsigned long long)a->frame);
    }
    for (i = 0; i < a->opt.n_ram_at; i++) {
        if ((u64)a->opt.ram_at[i] == a->frame) {
            char path[1200];
            snprintf(path, sizeof(path), "%s%sram_%06llu.bin", a->opt.screenshot_dir, a->opt.screenshot_dir[0] ? "/" : "", (unsigned long long)a->frame);
            nr_write_file(path, a->p->ram, PSX_RAM_SIZE);
        }
    }
    for (i = 0; i < a->opt.n_screenshot_at; i++) {
        if ((u64)a->opt.screenshot_at[i] == a->frame) {
            char path[1200];
            snprintf(path, sizeof(path), "%s%sframe_%06llu.png", a->opt.screenshot_dir, a->opt.screenshot_dir[0] ? "/" : "", (unsigned long long)a->frame);
            nr_app_screenshot(a, path);
        }
    }
}

void nr_app_shutdown(nr_app *a)
{
    if (a->p) {
        NR_INFO("app", "frames=%llu instructions=%llu cycles=%llu CD sectors read=%u GPU primitives=%llu display flips=%llu", (unsigned long long)a->frame,
                (unsigned long long)a->p->instr_count, (unsigned long long)a->p->cycles, a->p->cd.sectors_read,
                (unsigned long long)a->p->gpu.prims, (unsigned long long)a->p->gpu.flips);
        if (a->p->halted) NR_ERR("app", "execution stopped: %s", a->p->halt_reason);
        nr_unimplemented_report();
        if (a->opt.dump_hle_at_exit) {
            u32 pc = a->p->cpu.pc, k;
            int pi;
            for (pi = 0; pi < a->opt.npeek; pi++)
                NR_INFO("app", "peek %08X = %08X", a->opt.peek[pi], psx_ram_r32(a->p, a->opt.peek[pi]));
            NR_INFO("app", "CPU: pc=%08X ra=%08X sp=%08X SR=%08X CAUSE=%08X I_STAT=%03X I_MASK=%03X", pc, a->p->cpu.r[31], a->p->cpu.r[29],
                    a->p->cpu.cop0[C0_SR], a->p->cpu.cop0[C0_CAUSE], a->p->i_stat, a->p->i_mask);
            for (k = pc - 16; k < pc + 16; k += 4) {
                char d[96];
                cpu_disasm(k, bus_read32(a->p, k), d, sizeof(d));
                NR_INFO("app", "  %s%08X: %s", k == pc ? ">" : " ", k, d);
            }
        }
        if (a->opt.dump_hle_at_exit) hle_dump_state(a->p);
        if (a->opt.dump_hle_at_exit || a->opt.verify_natives) psx_override_report(a->p);
        hle_flush_memcards(a->p);
        a->p->gpu.rec = NULL;
        psx_destroy(a->p);
    }
    if (a->wav) {
        u8 h[44];
        u32 data = a->wav_frames * 4;
        memcpy(h, "RIFF", 4); nr_wr32(h + 4, 36 + data); memcpy(h + 8, "WAVEfmt ", 8);
        nr_wr32(h + 16, 16); nr_wr16(h + 20, 1); nr_wr16(h + 22, 2); nr_wr32(h + 24, 44100);
        nr_wr32(h + 28, 44100 * 4); nr_wr16(h + 32, 4); nr_wr16(h + 34, 16);
        memcpy(h + 36, "data", 4); nr_wr32(h + 40, data);
        fseek((FILE *)a->wav, 0, SEEK_SET);
        fwrite(h, 1, 44, (FILE *)a->wav);
        fclose((FILE *)a->wav);
    }
    if (a->disc) nr_disc_close(a->disc);
    nr_swr_free(a->swr);
    nr_rlist_free(a->rec);
    nr_mods_close(a->mods);
    if (a->pgxp) NR_INFO("render", "PGXP: %llu hits, %llu misses, %llu depth conflicts, %llu expired",
                         (unsigned long long)a->pgxp->hits, (unsigned long long)a->pgxp->misses,
                         (unsigned long long)a->pgxp->conflicts, (unsigned long long)a->pgxp->expired);
    free(a->pgxp);
    free(a->post);
    free(a->script);
    free(a->fb);
    memset(a, 0, sizeof(*a));
}

/* ------------------------------------------------------------ snapshots */

bool nr_app_save_state(nr_app *a, const char *path)
{
    u8 *blob;
    size_t n;
    FILE *f;
    u64 hdr[2];
    if (!psx_state_save(a->p, &blob, &n)) return false;
    f = nr_fopen(path, "wb");
    if (!f) { free(blob); return false; }
    hdr[0] = a->frame;
    hdr[1] = ((u64)a->pad_pressed[1] << 16) | a->pad_pressed[0];
    fwrite(hdr, sizeof(hdr), 1, f);
    fwrite(blob, 1, n, f);
    fclose(f);
    free(blob);
    return true;
}

bool nr_app_load_state(nr_app *a, const char *path)
{
    u8 *d;
    long n = nr_read_file(path, &d);
    u64 hdr[2];
    bool ok;
    if (n < 16) { free(d); return false; }
    memcpy(hdr, d, sizeof(hdr));
    ok = psx_state_load(a->p, d + 16, (size_t)n - 16);
    free(d);
    if (!ok) return false;
    a->frame = hdr[0];
    a->pad_pressed[0] = (u16)hdr[1];
    a->pad_pressed[1] = (u16)(hdr[1] >> 16);
    while (a->script_pos < a->nscript && a->script[a->script_pos].frame < (int)a->frame) {
        a->pad_pressed[0] = a->script[a->script_pos].buttons[0];
        a->pad_pressed[1] = a->script[a->script_pos].buttons[1];
        a->script_pos++;
    }
    if (a->rec) nr_rlist_clear(a->rec);
    if (a->swr_active && a->swr) nr_swr_sync(a->swr, a->p->gpu.vram);
    NR_INFO("app", "snapshot loaded: %s (frame %llu)", path, (unsigned long long)a->frame);
    return true;
}
