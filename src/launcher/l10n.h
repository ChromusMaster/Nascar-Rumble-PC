/* Launcher interface languages. Only the launcher's own texts change; the
 * game's texts come from mods/Language (see docs/MODS.md).
 *
 * Every string has an English (United States) original. Regional variants
 * (en-GB, pt-PT, es-MX, zh-HK, ...) only list the strings that differ from
 * their base language; anything missing falls back to the base language and
 * then to English. */
#ifndef NR_L10N_H
#define NR_L10N_H

#include "ltext.h"

enum {
    S_WINDOW_TITLE, S_SUBTITLE,
    S_TAB_GENERAL, S_TAB_GRAPHICS, S_TAB_EFFECTS, S_TAB_CONTROLS, S_TAB_ADVANCED,
    S_BTN_PLAY, S_BTN_SAVE, S_BTN_QUIT, S_BTN_OPEN_GAMEDATA, S_BTN_RESCAN, S_BTN_DEFAULTS, S_BTN_OPEN_USER, S_BTN_CANCEL, S_BTN_UP,
    S_YES, S_NO, S_ON, S_OFF, S_AUTO, S_NONE,
    /* general */
    S_DISC, S_DISC_AUTO, S_DISC_BROWSE, S_DISPLAY, S_WINDOWED, S_FULLSCREEN, S_WINDOW_SIZE,
    S_FPS, S_FPS_ORIGINAL, S_FPS_30, S_FPS_20, S_FPS_60, S_SHOW_FPS, S_GAME_LANG, S_GAME_LANG_ORIGINAL, S_UI_LANG,
    S_MEMCARD1, S_MEMCARD2, S_INSERTED, S_EMPTY_SLOT,
    S_DISC_OK, S_DISC_OTHER, S_DISC_UNREADABLE,
    S_STATUS_READY, S_STATUS_NODISC, S_STATUS_BAD, S_STATUS_SAVED, S_STATUS_RUNNING, S_STATUS_ENGINE_ERR, S_STATUS_ENGINE_MISSING,
    S_STATUS_FONT_MISSING,
    /* graphics */
    S_RENDERER, S_REN_SOFTWARE, S_VSYNC, S_INTERNAL_RES, S_RES_NATIVE, S_AA, S_COLOR, S_COLOR_15, S_COLOR_24, S_DITHER,
    S_TEXFILTER, S_FILTER_NEAREST, S_FILTER_3D, S_FILTER_ALL, S_SCALING, S_SCALE_INTEGER, S_SCALE_NEAREST, S_SCALE_BILINEAR,
    S_SCALE_SHARP, S_ASPECT, S_ASPECT_STRETCH, S_PGXP, S_WIDESCREEN,
    /* effects */
    S_POSTFX, S_FX_CRT, S_FX_SCANLINES, S_FX_VIGNETTE, S_FX_GRADE, S_FX_GRAIN, S_FX_SHARPEN, S_FX_CINEMATIC, S_FX_STRENGTH,
    S_TONEMAP, S_BRIGHTNESS, S_CONTRAST, S_SATURATION, S_GAMMA, S_PREVIEW,
    /* controls */
    S_PLAYER, S_PLAYER1, S_PLAYER2, S_DEVICE, S_DEV_KEYBOARD_ONLY, S_DEV_AUTO, S_DRV_SYSTEM, S_DRV_DIRECT, S_DRV_NO_ACCESS,
    S_PORT2, S_STICK_DPAD, S_COL_BUTTON, S_COL_KEY, S_COL_PAD,
    S_PS_UP, S_PS_DOWN, S_PS_LEFT, S_PS_RIGHT, S_PS_CROSS, S_PS_CIRCLE, S_PS_SQUARE, S_PS_TRIANGLE,
    S_PS_L1, S_PS_R1, S_PS_L2, S_PS_R2, S_PS_START, S_PS_SELECT,
    S_PRESS_KEY, S_PRESS_BUTTON, S_NOT_SET, S_DEFAULT, S_NO_DEVICE_OPEN,
    /* advanced */
    S_NATIVE, S_GPU_TIMING, S_OVERCLOCK, S_OC_CONSOLE, S_THREADS, S_LOG_LEVEL, S_LOG_ERRORS, S_LOG_WARNINGS, S_LOG_INFO, S_LOG_DEBUG,
    S_VERIFY_DISC, S_FOLDERS, S_PHYS_STEP, S_PHYS_ORIGINAL, S_PHYS_30, S_PHYS_60,
    /* file browser */
    S_BROWSE_TITLE, S_BROWSE_HINT,
    /* help lines */
    S_H_DISC, S_H_DISPLAY, S_H_WINDOW_SIZE, S_H_FPS, S_H_SHOW_FPS, S_H_GAME_LANG, S_H_UI_LANG, S_H_MEMCARD,
    S_H_RENDERER, S_H_VSYNC, S_H_INTERNAL_RES, S_H_AA, S_H_COLOR, S_H_DITHER, S_H_TEXFILTER, S_H_SCALING, S_H_ASPECT,
    S_H_PGXP, S_H_WIDESCREEN, S_H_POSTFX, S_H_FX_STRENGTH, S_H_TONEMAP, S_H_ADJUST,
    S_H_DEVICE, S_H_PORT2, S_H_STICK_DPAD, S_H_BINDING,
    S_H_NATIVE, S_H_GPU_TIMING, S_H_OVERCLOCK, S_H_THREADS, S_H_LOG_LEVEL, S_H_VERIFY, S_H_PHYS_STEP,
    S_H_PLAY, S_H_SAVE,
    S_COUNT
};

typedef struct {
    const char *code;        /* BCP 47: "en-US", "pt-BR", "zh-Hant-HK", ... */
    const char *name;        /* name in the language itself */
    lt_script script;
    const char *const *table;
    int parent;              /* index of the base language, -1 for en-US */
} l10n_lang;

int l10n_count(void);
const l10n_lang *l10n_get(int i);
int l10n_find(const char *code);          /* -1 if unknown */
/* best match for system locale codes such as "pt_BR", "zh-TW", "es" */
int l10n_match_locale(const char *lang, const char *country);
bool l10n_load_catalog(int lang, const char *path);
void l10n_set(int i);
int l10n_current(void);
const char *T(int id);

#endif
