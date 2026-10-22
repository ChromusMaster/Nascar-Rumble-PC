/* Launcher strings. See l10n.h. Files are UTF-8. */
#include "l10n.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "../common/paths.h"

/* ===================================================================== en-US */
static const char *const k_en_US[S_COUNT] = {
    [S_WINDOW_TITLE] = "NR Engine Launcher",
    [S_SUBTITLE] = "PC launcher - NASCAR Rumble (USA) reimplementation engine",
    [S_TAB_GENERAL] = "General", [S_TAB_GRAPHICS] = "Graphics", [S_TAB_EFFECTS] = "Effects",
    [S_TAB_CONTROLS] = "Controls", [S_TAB_ADVANCED] = "Advanced",
    [S_BTN_PLAY] = "Play!", [S_BTN_SAVE] = "Save", [S_BTN_QUIT] = "Quit",
    [S_BTN_OPEN_GAMEDATA] = "Open gamedata folder", [S_BTN_RESCAN] = "Search again", [S_BTN_DEFAULTS] = "Restore defaults",
    [S_BTN_OPEN_USER] = "Open settings folder", [S_BTN_CANCEL] = "Cancel", [S_BTN_UP] = "Up one folder",
    [S_YES] = "Yes", [S_NO] = "No", [S_ON] = "On", [S_OFF] = "Off", [S_AUTO] = "Automatic", [S_NONE] = "None",
    [S_DISC] = "Disc image", [S_DISC_AUTO] = "Automatic (gamedata folder)", [S_DISC_BROWSE] = "Browse...",
    [S_DISPLAY] = "Display", [S_WINDOWED] = "Window", [S_FULLSCREEN] = "Full screen", [S_WINDOW_SIZE] = "Window size",
    [S_FPS] = "Frame rate", [S_FPS_ORIGINAL] = "Original (console)", [S_FPS_30] = "Limit to 30 fps", [S_FPS_20] = "Limit to 20 fps",
    [S_FPS_60] = "60 fps (unlocked)", [S_SHOW_FPS] = "Show FPS counter", [S_GAME_LANG] = "Game language",
    [S_GAME_LANG_ORIGINAL] = "Original (English)", [S_UI_LANG] = "Launcher language",
    [S_MEMCARD1] = "Memory card 1", [S_MEMCARD2] = "Memory card 2", [S_INSERTED] = "Inserted", [S_EMPTY_SLOT] = "Empty",
    [S_DISC_OK] = "ready", [S_DISC_OTHER] = "another game", [S_DISC_UNREADABLE] = "unreadable",
    [S_STATUS_READY] = "%s - NTSC-U (USA) - ready",
    [S_STATUS_NODISC] = "No disc image found. Put your own NASCAR Rumble (USA) image in the gamedata folder or use Browse.",
    [S_STATUS_BAD] = "This image cannot be used: %s",
    [S_STATUS_SAVED] = "Settings saved.",
    [S_STATUS_RUNNING] = "The game is running...",
    [S_STATUS_ENGINE_ERR] = "The engine stopped with an error (code %d). Details: %s",
    [S_STATUS_ENGINE_MISSING] = "The engine (nrengine) was not found next to the launcher.",
    [S_STATUS_FONT_MISSING] = "No font for this language was found on this system; the launcher stays in English.",
    [S_RENDERER] = "Renderer", [S_REN_SOFTWARE] = "Software", [S_VSYNC] = "VSync",
    [S_INTERNAL_RES] = "Internal resolution", [S_RES_NATIVE] = "Native", [S_AA] = "Anti-aliasing (SSAA)",
    [S_COLOR] = "Color depth", [S_COLOR_15] = "15-bit (console)", [S_COLOR_24] = "24-bit", [S_DITHER] = "Dithering",
    [S_TEXFILTER] = "Texture filtering", [S_FILTER_NEAREST] = "Nearest (console)", [S_FILTER_3D] = "Bilinear (3D only)",
    [S_FILTER_ALL] = "Bilinear (everything)", [S_SCALING] = "Scaling", [S_SCALE_INTEGER] = "Integer", [S_SCALE_NEAREST] = "Nearest",
    [S_SCALE_BILINEAR] = "Bilinear", [S_SCALE_SHARP] = "Sharp bilinear", [S_ASPECT] = "Aspect ratio", [S_ASPECT_STRETCH] = "Stretch",
    [S_PGXP] = "Precise geometry (PGXP, experimental)", [S_WIDESCREEN] = "Widescreen 16:9 (experimental)",
    [S_POSTFX] = "Post-processing", [S_FX_CRT] = "CRT", [S_FX_SCANLINES] = "Scanlines", [S_FX_VIGNETTE] = "Vignette",
    [S_FX_GRADE] = "Color grading", [S_FX_GRAIN] = "Film grain", [S_FX_SHARPEN] = "Sharpen", [S_FX_CINEMATIC] = "Cinematic",
    [S_FX_STRENGTH] = "Effect strength", [S_TONEMAP] = "Tone mapping", [S_BRIGHTNESS] = "Brightness", [S_CONTRAST] = "Contrast",
    [S_SATURATION] = "Saturation", [S_GAMMA] = "Gamma", [S_PREVIEW] = "Preview (test picture)",
    [S_PLAYER] = "Player", [S_PLAYER1] = "Player 1", [S_PLAYER2] = "Player 2", [S_DEVICE] = "Controller",
    [S_DEV_KEYBOARD_ONLY] = "Keyboard only", [S_DEV_AUTO] = "Automatic (first controller found)",
    [S_DRV_SYSTEM] = "system driver", [S_DRV_DIRECT] = "direct USB", [S_DRV_NO_ACCESS] = "no permission",
    [S_PORT2] = "Controller plugged into port 2", [S_STICK_DPAD] = "Left stick also moves the D-pad",
    [S_COL_BUTTON] = "Button", [S_COL_KEY] = "Keyboard", [S_COL_PAD] = "Controller",
    [S_PS_UP] = "Up", [S_PS_DOWN] = "Down", [S_PS_LEFT] = "Left", [S_PS_RIGHT] = "Right",
    [S_PS_CROSS] = "Cross", [S_PS_CIRCLE] = "Circle", [S_PS_SQUARE] = "Square", [S_PS_TRIANGLE] = "Triangle",
    [S_PS_L1] = "L1", [S_PS_R1] = "R1", [S_PS_L2] = "L2", [S_PS_R2] = "R2", [S_PS_START] = "Start", [S_PS_SELECT] = "Select",
    [S_PRESS_KEY] = "Press a key for %s (Esc cancels, Backspace clears)",
    [S_PRESS_BUTTON] = "Press a controller button for %s (Esc cancels, Backspace clears)",
    [S_NOT_SET] = "-", [S_DEFAULT] = "default", [S_NO_DEVICE_OPEN] = "Connect and select a controller to assign its buttons.",
    [S_NATIVE] = "Native routines (C reimplementation)", [S_GPU_TIMING] = "GPU drawing-time model", [S_OVERCLOCK] = "Emulated CPU speed",
    [S_OC_CONSOLE] = "1x (console)", [S_THREADS] = "Software renderer threads", [S_LOG_LEVEL] = "Log detail",
    [S_LOG_ERRORS] = "Errors", [S_LOG_WARNINGS] = "Warnings", [S_LOG_INFO] = "Information", [S_LOG_DEBUG] = "Debug",
    [S_VERIFY_DISC] = "Check every disc file at start (CRC32)", [S_FOLDERS] = "Folders",
    [S_BROWSE_TITLE] = "Choose your disc image (.cue, .bin, .iso, .img)",
    [S_BROWSE_HINT] = "Enter opens a folder or picks the image. Esc cancels.",
    [S_H_DISC] = "Your own copy of NASCAR Rumble (USA). Images in the gamedata folder are found automatically; nothing is copied or changed.",
    [S_H_DISPLAY] = "Play in a window or on the full screen. F11 also switches while playing.",
    [S_H_WINDOW_SIZE] = "Starting size of the game window (multiples of the console's 320x240 picture).",
    [S_H_FPS] = "Original keeps the console's pace. 20/30 only limit what is shown. 60 speeds up the emulated CPU and changes the pace.",
    [S_H_SHOW_FPS] = "Shows the game's frame rate in a corner of the window.",
    [S_H_GAME_LANG] = "Translation of the game's texts installed by you in mods/Language (see docs/MODS.md).",
    [S_H_UI_LANG] = "Language of this launcher only. The game's own texts are changed with Game language.",
    [S_H_MEMCARD] = "Memory card image files (128 KB) kept in your data folder; created automatically.",
    [S_H_RENDERER] = "Graphics API. Automatic tries Direct3D 11, OpenGL, Direct3D 10 and 9, then Software.",
    [S_H_VSYNC] = "Synchronizes the picture with the monitor to avoid tearing.",
    [S_H_INTERNAL_RES] = "Draws the 3D scene at a higher resolution. The game itself is not changed.",
    [S_H_AA] = "Real supersampling: 2x, 4x, 8x or 16x samples per pixel, averaged. Needs a strong GPU at high values.",
    [S_H_COLOR] = "24-bit shading and blending instead of the console's 15-bit colors.",
    [S_H_DITHER] = "The console's dot pattern used to hide color banding. Turn it off with 24-bit color.",
    [S_H_TEXFILTER] = "Smooths textures. Nearest keeps the console's sharp texels.",
    [S_H_SCALING] = "How the picture is enlarged to the window. Sharp bilinear keeps pixels crisp without uneven sizes.",
    [S_H_ASPECT] = "Shape of the picture in the window.",
    [S_H_PGXP] = "Sub-pixel vertex positions and perspective-correct textures. Experimental.",
    [S_H_WIDESCREEN] = "16:9 projection. Experimental: changes what the game computes; objects may pop in at the sides.",
    [S_H_POSTFX] = "Effect applied to the final picture.",
    [S_H_FX_STRENGTH] = "How strong the effect is.",
    [S_H_TONEMAP] = "Color curve applied to the final picture.",
    [S_H_ADJUST] = "Picture adjustment applied after the effects.",
    [S_H_DEVICE] = "System driver uses Windows/Linux gamepad support. Direct USB reads the controller's own reports.",
    [S_H_PORT2] = "Connects a second controller for two-player modes.",
    [S_H_STICK_DPAD] = "The left analog stick works as the D-pad (the game only uses digital buttons).",
    [S_H_BINDING] = "Click a cell (or press Enter) and then the key or controller button to assign.",
    [S_H_NATIVE] = "Uses the C reimplementations of game routines, each verified against the original code.",
    [S_H_GPU_TIMING] = "Models how long the console GPU takes to draw, so the game keeps its original pace.",
    [S_H_OVERCLOCK] = "Multiplier of the emulated CPU. 1x is the console; higher values change the game's pace.",
    [S_PHYS_STEP] = "Physics step", [S_PHYS_ORIGINAL] = "Original (variable)", [S_PHYS_30] = "Steady 30 Hz", [S_PHYS_60] = "Steady 60 Hz",
    [S_H_PHYS_STEP] = "Gives every game frame the same time step, so handling and collisions no longer change with frame-rate drops. Automatic: 30 Hz, or 60 Hz in the 60 fps mode. Original: the console behaviour.",
    [S_H_THREADS] = "CPU threads used by the Software renderer.",
    [S_H_LOG_LEVEL] = "How much is written to nrengine.log.",
    [S_H_VERIFY] = "Compares the CRC32 of all 108 files with the reference before starting (slower start).",
    [S_H_PLAY] = "Saves the settings and starts the game.",
    [S_H_SAVE] = "Writes the settings to nrengine.ini.",
};

/* ===================================================================== en-GB */
static const char *const k_en_GB[S_COUNT] = {
    [S_COLOR] = "Colour depth", [S_H_COLOR] = "24-bit shading and blending instead of the console's 15-bit colours.",
    [S_H_DITHER] = "The console's dot pattern used to hide colour banding. Turn it off with 24-bit colour.",
    [S_FX_GRADE] = "Colour grading", [S_H_TONEMAP] = "Colour curve applied to the final picture.",
    [S_H_VSYNC] = "Synchronises the picture with the monitor to avoid tearing.",
    [S_H_INTERNAL_RES] = "Draws the 3D scene at a higher resolution. The game itself is not altered.",
};

/* ===================================================================== en-CA */
static const char *const k_en_CA[S_COUNT] = {
    [S_COLOR] = "Colour depth", [S_H_COLOR] = "24-bit shading and blending instead of the console's 15-bit colours.",
    [S_H_DITHER] = "The console's dot pattern used to hide colour banding. Turn it off with 24-bit colour.",
    [S_FX_GRADE] = "Colour grading", [S_H_TONEMAP] = "Colour curve applied to the final picture.",
    [S_H_VSYNC] = "Synchronizes the picture with the monitor to avoid tearing.",
};

#include "l10n_tables.inc"

/* ===================================================================== list */
static const l10n_lang k_langs[] = {
    { "en-US", "English (United States)", LT_LATIN, k_en_US, -1 },
    { "en-GB", "English (United Kingdom)", LT_LATIN, k_en_GB, 0 },
    { "en-CA", "English (Canada)", LT_LATIN, k_en_CA, 0 },
    { "pt-BR", "Português (Brasil)", LT_LATIN, k_pt_BR, 0 },
    { "pt-PT", "Português (Portugal)", LT_LATIN, k_pt_PT, 3 },
    { "es-ES", "Español (España)", LT_LATIN, k_es_ES, 0 },
    { "es-MX", "Español (México)", LT_LATIN, k_es_419, 5 },
    { "es-419", "Español (Latinoamérica)", LT_LATIN, k_es_419, 5 },
    { "es-AR", "Español (Argentina)", LT_LATIN, k_es_AR, 7 },
    { "es-US", "Español (Estados Unidos)", LT_LATIN, k_es_US, 7 },
    { "zh-Hans-CN", "简体中文（中国大陆）", LT_HANS, k_zh_Hans, 0 },
    { "zh-Hans-SG", "简体中文（新加坡）", LT_HANS, k_zh_SG, 10 },
    { "zh-Hant-TW", "繁體中文（台灣）", LT_HANT, k_zh_Hant, 0 },
    { "zh-Hant-HK", "繁體中文（香港）", LT_HANT_HK, k_zh_HK, 12 },
    { "zh-Hant-MO", "繁體中文（澳門）", LT_HANT_HK, k_zh_HK, 12 },
    { "ja-JP", "日本語", LT_JA, k_ja, 0 },
    { "ko-KR", "한국어", LT_KO, k_ko, 0 },
};

static const char *const catalog_keys[S_COUNT] = {
    [S_WINDOW_TITLE] = "S_WINDOW_TITLE",
    [S_SUBTITLE] = "S_SUBTITLE",
    [S_TAB_GENERAL] = "S_TAB_GENERAL",
    [S_TAB_GRAPHICS] = "S_TAB_GRAPHICS",
    [S_TAB_EFFECTS] = "S_TAB_EFFECTS",
    [S_TAB_CONTROLS] = "S_TAB_CONTROLS",
    [S_TAB_ADVANCED] = "S_TAB_ADVANCED",
    [S_BTN_PLAY] = "S_BTN_PLAY",
    [S_BTN_SAVE] = "S_BTN_SAVE",
    [S_BTN_QUIT] = "S_BTN_QUIT",
    [S_BTN_OPEN_GAMEDATA] = "S_BTN_OPEN_GAMEDATA",
    [S_BTN_RESCAN] = "S_BTN_RESCAN",
    [S_BTN_DEFAULTS] = "S_BTN_DEFAULTS",
    [S_BTN_OPEN_USER] = "S_BTN_OPEN_USER",
    [S_BTN_CANCEL] = "S_BTN_CANCEL",
    [S_BTN_UP] = "S_BTN_UP",
    [S_YES] = "S_YES",
    [S_NO] = "S_NO",
    [S_ON] = "S_ON",
    [S_OFF] = "S_OFF",
    [S_AUTO] = "S_AUTO",
    [S_NONE] = "S_NONE",
    [S_DISC] = "S_DISC",
    [S_DISC_AUTO] = "S_DISC_AUTO",
    [S_DISC_BROWSE] = "S_DISC_BROWSE",
    [S_DISPLAY] = "S_DISPLAY",
    [S_WINDOWED] = "S_WINDOWED",
    [S_FULLSCREEN] = "S_FULLSCREEN",
    [S_WINDOW_SIZE] = "S_WINDOW_SIZE",
    [S_FPS] = "S_FPS",
    [S_FPS_ORIGINAL] = "S_FPS_ORIGINAL",
    [S_FPS_30] = "S_FPS_30",
    [S_FPS_20] = "S_FPS_20",
    [S_FPS_60] = "S_FPS_60",
    [S_SHOW_FPS] = "S_SHOW_FPS",
    [S_GAME_LANG] = "S_GAME_LANG",
    [S_GAME_LANG_ORIGINAL] = "S_GAME_LANG_ORIGINAL",
    [S_UI_LANG] = "S_UI_LANG",
    [S_MEMCARD1] = "S_MEMCARD1",
    [S_MEMCARD2] = "S_MEMCARD2",
    [S_INSERTED] = "S_INSERTED",
    [S_EMPTY_SLOT] = "S_EMPTY_SLOT",
    [S_DISC_OK] = "S_DISC_OK",
    [S_DISC_OTHER] = "S_DISC_OTHER",
    [S_DISC_UNREADABLE] = "S_DISC_UNREADABLE",
    [S_STATUS_READY] = "S_STATUS_READY",
    [S_STATUS_NODISC] = "S_STATUS_NODISC",
    [S_STATUS_BAD] = "S_STATUS_BAD",
    [S_STATUS_SAVED] = "S_STATUS_SAVED",
    [S_STATUS_RUNNING] = "S_STATUS_RUNNING",
    [S_STATUS_ENGINE_ERR] = "S_STATUS_ENGINE_ERR",
    [S_STATUS_ENGINE_MISSING] = "S_STATUS_ENGINE_MISSING",
    [S_STATUS_FONT_MISSING] = "S_STATUS_FONT_MISSING",
    [S_RENDERER] = "S_RENDERER",
    [S_REN_SOFTWARE] = "S_REN_SOFTWARE",
    [S_VSYNC] = "S_VSYNC",
    [S_INTERNAL_RES] = "S_INTERNAL_RES",
    [S_RES_NATIVE] = "S_RES_NATIVE",
    [S_AA] = "S_AA",
    [S_COLOR] = "S_COLOR",
    [S_COLOR_15] = "S_COLOR_15",
    [S_COLOR_24] = "S_COLOR_24",
    [S_DITHER] = "S_DITHER",
    [S_TEXFILTER] = "S_TEXFILTER",
    [S_FILTER_NEAREST] = "S_FILTER_NEAREST",
    [S_FILTER_3D] = "S_FILTER_3D",
    [S_FILTER_ALL] = "S_FILTER_ALL",
    [S_SCALING] = "S_SCALING",
    [S_SCALE_INTEGER] = "S_SCALE_INTEGER",
    [S_SCALE_NEAREST] = "S_SCALE_NEAREST",
    [S_SCALE_BILINEAR] = "S_SCALE_BILINEAR",
    [S_SCALE_SHARP] = "S_SCALE_SHARP",
    [S_ASPECT] = "S_ASPECT",
    [S_ASPECT_STRETCH] = "S_ASPECT_STRETCH",
    [S_PGXP] = "S_PGXP",
    [S_WIDESCREEN] = "S_WIDESCREEN",
    [S_POSTFX] = "S_POSTFX",
    [S_FX_CRT] = "S_FX_CRT",
    [S_FX_SCANLINES] = "S_FX_SCANLINES",
    [S_FX_VIGNETTE] = "S_FX_VIGNETTE",
    [S_FX_GRADE] = "S_FX_GRADE",
    [S_FX_GRAIN] = "S_FX_GRAIN",
    [S_FX_SHARPEN] = "S_FX_SHARPEN",
    [S_FX_CINEMATIC] = "S_FX_CINEMATIC",
    [S_FX_STRENGTH] = "S_FX_STRENGTH",
    [S_TONEMAP] = "S_TONEMAP",
    [S_BRIGHTNESS] = "S_BRIGHTNESS",
    [S_CONTRAST] = "S_CONTRAST",
    [S_SATURATION] = "S_SATURATION",
    [S_GAMMA] = "S_GAMMA",
    [S_PREVIEW] = "S_PREVIEW",
    [S_PLAYER] = "S_PLAYER",
    [S_PLAYER1] = "S_PLAYER1",
    [S_PLAYER2] = "S_PLAYER2",
    [S_DEVICE] = "S_DEVICE",
    [S_DEV_KEYBOARD_ONLY] = "S_DEV_KEYBOARD_ONLY",
    [S_DEV_AUTO] = "S_DEV_AUTO",
    [S_DRV_SYSTEM] = "S_DRV_SYSTEM",
    [S_DRV_DIRECT] = "S_DRV_DIRECT",
    [S_DRV_NO_ACCESS] = "S_DRV_NO_ACCESS",
    [S_PORT2] = "S_PORT2",
    [S_STICK_DPAD] = "S_STICK_DPAD",
    [S_COL_BUTTON] = "S_COL_BUTTON",
    [S_COL_KEY] = "S_COL_KEY",
    [S_COL_PAD] = "S_COL_PAD",
    [S_PS_UP] = "S_PS_UP",
    [S_PS_DOWN] = "S_PS_DOWN",
    [S_PS_LEFT] = "S_PS_LEFT",
    [S_PS_RIGHT] = "S_PS_RIGHT",
    [S_PS_CROSS] = "S_PS_CROSS",
    [S_PS_CIRCLE] = "S_PS_CIRCLE",
    [S_PS_SQUARE] = "S_PS_SQUARE",
    [S_PS_TRIANGLE] = "S_PS_TRIANGLE",
    [S_PS_L1] = "S_PS_L1",
    [S_PS_R1] = "S_PS_R1",
    [S_PS_L2] = "S_PS_L2",
    [S_PS_R2] = "S_PS_R2",
    [S_PS_START] = "S_PS_START",
    [S_PS_SELECT] = "S_PS_SELECT",
    [S_PRESS_KEY] = "S_PRESS_KEY",
    [S_PRESS_BUTTON] = "S_PRESS_BUTTON",
    [S_NOT_SET] = "S_NOT_SET",
    [S_DEFAULT] = "S_DEFAULT",
    [S_NO_DEVICE_OPEN] = "S_NO_DEVICE_OPEN",
    [S_NATIVE] = "S_NATIVE",
    [S_GPU_TIMING] = "S_GPU_TIMING",
    [S_OVERCLOCK] = "S_OVERCLOCK",
    [S_OC_CONSOLE] = "S_OC_CONSOLE",
    [S_THREADS] = "S_THREADS",
    [S_LOG_LEVEL] = "S_LOG_LEVEL",
    [S_LOG_ERRORS] = "S_LOG_ERRORS",
    [S_LOG_WARNINGS] = "S_LOG_WARNINGS",
    [S_LOG_INFO] = "S_LOG_INFO",
    [S_LOG_DEBUG] = "S_LOG_DEBUG",
    [S_VERIFY_DISC] = "S_VERIFY_DISC",
    [S_FOLDERS] = "S_FOLDERS",
    [S_PHYS_STEP] = "S_PHYS_STEP",
    [S_PHYS_ORIGINAL] = "S_PHYS_ORIGINAL",
    [S_PHYS_30] = "S_PHYS_30",
    [S_PHYS_60] = "S_PHYS_60",
    [S_BROWSE_TITLE] = "S_BROWSE_TITLE",
    [S_BROWSE_HINT] = "S_BROWSE_HINT",
    [S_H_DISC] = "S_H_DISC",
    [S_H_DISPLAY] = "S_H_DISPLAY",
    [S_H_WINDOW_SIZE] = "S_H_WINDOW_SIZE",
    [S_H_FPS] = "S_H_FPS",
    [S_H_SHOW_FPS] = "S_H_SHOW_FPS",
    [S_H_GAME_LANG] = "S_H_GAME_LANG",
    [S_H_UI_LANG] = "S_H_UI_LANG",
    [S_H_MEMCARD] = "S_H_MEMCARD",
    [S_H_RENDERER] = "S_H_RENDERER",
    [S_H_VSYNC] = "S_H_VSYNC",
    [S_H_INTERNAL_RES] = "S_H_INTERNAL_RES",
    [S_H_AA] = "S_H_AA",
    [S_H_COLOR] = "S_H_COLOR",
    [S_H_DITHER] = "S_H_DITHER",
    [S_H_TEXFILTER] = "S_H_TEXFILTER",
    [S_H_SCALING] = "S_H_SCALING",
    [S_H_ASPECT] = "S_H_ASPECT",
    [S_H_PGXP] = "S_H_PGXP",
    [S_H_WIDESCREEN] = "S_H_WIDESCREEN",
    [S_H_POSTFX] = "S_H_POSTFX",
    [S_H_FX_STRENGTH] = "S_H_FX_STRENGTH",
    [S_H_TONEMAP] = "S_H_TONEMAP",
    [S_H_ADJUST] = "S_H_ADJUST",
    [S_H_DEVICE] = "S_H_DEVICE",
    [S_H_PORT2] = "S_H_PORT2",
    [S_H_STICK_DPAD] = "S_H_STICK_DPAD",
    [S_H_BINDING] = "S_H_BINDING",
    [S_H_NATIVE] = "S_H_NATIVE",
    [S_H_GPU_TIMING] = "S_H_GPU_TIMING",
    [S_H_OVERCLOCK] = "S_H_OVERCLOCK",
    [S_H_THREADS] = "S_H_THREADS",
    [S_H_LOG_LEVEL] = "S_H_LOG_LEVEL",
    [S_H_VERIFY] = "S_H_VERIFY",
    [S_H_PHYS_STEP] = "S_H_PHYS_STEP",
    [S_H_PLAY] = "S_H_PLAY",
    [S_H_SAVE] = "S_H_SAVE",
};

#define CATALOG_LANGS (sizeof(k_langs) / sizeof(k_langs[0]))
static char *catalogs[CATALOG_LANGS][S_COUNT];
static bool catalog_loaded[CATALOG_LANGS];

static bool catalog_utf8(const unsigned char *s)
{
    while (*s) {
        unsigned cp, min;
        int n, j;
        if (*s < 0x80) { if (*s < 32 && *s != '\t') return false; s++; continue; }
        if (*s >= 0xC2 && *s <= 0xDF) { n = 2; min = 0x80; cp = *s & 31; }
        else if (*s >= 0xE0 && *s <= 0xEF) { n = 3; min = 0x800; cp = *s & 15; }
        else if (*s >= 0xF0 && *s <= 0xF4) { n = 4; min = 0x10000; cp = *s & 7; }
        else return false;
        for (j = 1; j < n; j++) { if ((s[j] & 0xC0) != 0x80) return false; cp = (cp << 6) | (s[j] & 63); }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        s += n;
    }
    return true;
}

static bool catalog_format(const char *base, const char *value)
{
    for (;;) {
        base = strchr(base, '%'); value = strchr(value, '%');
        if (!base || !value) return base == NULL && value == NULL;
        if (!base[1] || !value[1] || !strchr("sd%", value[1]) || base[1] != value[1]) return false;
        base += 2; value += 2;
    }
}

bool l10n_load_catalog(int lang, const char *path)
{
    u8 *data;
    long size;
    char *line, *next;
    bool seen[S_COUNT] = {false}, any = false;
    if (lang < 0 || (size_t)lang >= CATALOG_LANGS || catalog_loaded[lang]) return false;
    size = nr_read_file_limited(path, &data, 256u * 1024u);
    if (size < 0) return false;
    if (memchr(data, 0, (size_t)size)) { free(data); return false; }
    line = (char *)data;
    if (size >= 3 && memcmp(data, "\xEF\xBB\xBF", 3) == 0) line += 3;
    for (; line && *line; line = next) {
        char *eq, *end, *value;
        int id;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        end = line + strlen(line);
        while (end > line && isspace((unsigned char)end[-1])) *--end = 0;
        if (!*line || *line == ';' || *line == '#' || strcmp(line, "[strings]") == 0) continue;
        eq = strchr(line, '=');
        if (!eq) { NR_WARN("l10n", "invalid catalog line ignored"); continue; }
        *eq = 0;
        end = eq;
        while (end > line && isspace((unsigned char)end[-1])) *--end = 0;
        value = eq+1;
        while (*value == ' ' || *value == '\t') value++;
        for (id = 0; id < S_COUNT; id++) if (strcmp(line, catalog_keys[id]) == 0) break;
        if (id == S_COUNT || seen[id] || strlen(value) > 4096 || !catalog_utf8((unsigned char *)value) ||
            !catalog_format(k_en_US[id] ? k_en_US[id] : "", value)) {
            NR_WARN("l10n", "unknown, duplicate or invalid catalog entry ignored"); continue;
        }
        seen[id] = true;
        catalogs[lang][id] = (char *)malloc(strlen(value)+1);
        if (!catalogs[lang][id]) { NR_ERR("l10n", "catalog allocation failed"); break; }
        strcpy(catalogs[lang][id], value);
        any = true;
    }
    free(data);
    catalog_loaded[lang] = true;
    return any;
}

static void load_catalog_for(int lang)
{
    const char *bases[4];
    char rel[128], path[2048];
    int i;
    if (catalog_loaded[lang]) return;
    bases[0] = "."; bases[1] = nr_data_dir(); bases[2] = nr_exe_folder(); bases[3] = nr_share_dir();
    snprintf(rel, sizeof(rel), "mods/Language/%s/launcher.ini", k_langs[lang].code);
    for (i = 0; i < 4; i++) {
        if (!nr_safe_relative_path(bases[i], rel)) continue;
        nr_path_join(path, sizeof(path), bases[i], rel);
        if (!nr_file_exists(path)) continue;
        l10n_load_catalog(lang, path);
        break;
    }
    catalog_loaded[lang] = true;
}

static int g_cur;

int l10n_count(void) { return (int)(sizeof(k_langs) / sizeof(k_langs[0])); }
const l10n_lang *l10n_get(int i) { return (i >= 0 && i < l10n_count()) ? &k_langs[i] : &k_langs[0]; }

int l10n_find(const char *code)
{
    int i;
    if (!code) return -1;
    for (i = 0; i < l10n_count(); i++) if (nr_strcasecmp(k_langs[i].code, code) == 0) return i;
    return -1;
}

int l10n_match_locale(const char *lang, const char *country)
{
    char c[3] = { 0, 0, 0 };
    if (!lang || !*lang) return 0;
    if (country) { c[0] = (char)(country[0] & ~0x20); if (country[0]) c[1] = (char)(country[1] & ~0x20); }
#define IS(x) (nr_strcasecmp(lang, x) == 0)
#define CC(x) (strcmp(c, x) == 0)
    if (IS("en")) return CC("GB") || CC("IE") || CC("AU") || CC("NZ") ? 1 : CC("CA") ? 2 : 0;
    if (IS("pt")) return CC("BR") || !c[0] ? 3 : 4;
    if (IS("es")) {
        if (CC("ES") || !c[0]) return 5;
        if (CC("MX")) return 6;
        if (CC("AR") || CC("UY") || CC("PY")) return 8;
        if (CC("US")) return 9;
        return 7;
    }
    if (IS("zh")) {
        if (CC("TW")) return 12;
        if (CC("HK")) return 13;
        if (CC("MO")) return 14;
        if (CC("SG")) return 11;
        return 10;
    }
    if (IS("ja")) return 15;
    if (IS("ko")) return 16;
#undef IS
#undef CC
    return 0;
}

void l10n_set(int i)
{
    int l;
    g_cur = (i >= 0 && i < l10n_count()) ? i : 0;
    for (l = g_cur; l >= 0; l = k_langs[l].parent) load_catalog_for(l);
}
int l10n_current(void) { return g_cur; }

const char *T(int id)
{
    int l = g_cur;
    if (id < 0 || id >= S_COUNT) return "";
    while (l >= 0) {
        const char *s = catalogs[l][id] ? catalogs[l][id] : k_langs[l].table[id];
        if (s) return s;
        l = k_langs[l].parent;
    }
    return k_en_US[id] ? k_en_US[id] : "";
}
