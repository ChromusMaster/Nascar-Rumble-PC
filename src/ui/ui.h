/* In-game overlay: short on-screen messages, the optional FPS counter and
 * the quit confirmation dialog shown when Esc is pressed. All settings are
 * chosen in the launcher (nrlauncher), not while the game runs.
 * Platform independent: the frontend feeds abstract keys and composites the
 * RGBA canvas over the picture. */
#ifndef NR_UI_H
#define NR_UI_H

#include "font.h"

typedef enum { UI_KEY_NONE = 0, UI_KEY_UP, UI_KEY_DOWN, UI_KEY_LEFT, UI_KEY_RIGHT, UI_KEY_ACCEPT, UI_KEY_BACK } nr_ui_key;

typedef enum { UI_QUIT_NONE = 0, UI_QUIT_YES, UI_QUIT_NO } nr_ui_quit_result;

typedef struct {
    nr_canvas cv;
    struct { char text[160]; u32 until; } msg[4];
    int nmsg;
    bool show_fps;
    float fps;                /* measured game frames per second */
    float vps;                /* presented frames per second */
    bool quit_open;           /* "Do you want to close the Engine?" */
    int quit_sel;             /* 0 = Yes (default), 1 = No */
} nr_ui;

void nr_ui_init(nr_ui *u);
void nr_ui_free(nr_ui *u);
void nr_ui_message(nr_ui *u, u32 now_ms, const char *fmt, ...);
/* opens the quit dialog with "Yes" selected */
void nr_ui_open_quit(nr_ui *u);
/* key for the quit dialog; returns UI_QUIT_YES/NO when a choice is made */
nr_ui_quit_result nr_ui_quit_key(nr_ui *u, nr_ui_key k);
/* redraws the canvas for the window size; returns false when nothing is visible */
bool nr_ui_render(nr_ui *u, int win_w, int win_h, u32 now_ms);

#endif
