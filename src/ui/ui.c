#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void nr_ui_init(nr_ui *u) { memset(u, 0, sizeof(*u)); }

void nr_ui_free(nr_ui *u) { nr_canvas_free(&u->cv); }

void nr_ui_message(nr_ui *u, u32 now, const char *fmt, ...)
{
    va_list ap;
    int i;
    if (u->nmsg == 4) {
        for (i = 1; i < 4; i++) u->msg[i - 1] = u->msg[i];
        u->nmsg = 3;
    }
    va_start(ap, fmt);
    vsnprintf(u->msg[u->nmsg].text, sizeof(u->msg[0].text), fmt, ap);
    va_end(ap);
    u->msg[u->nmsg].until = now + 2500;
    u->nmsg++;
}

void nr_ui_open_quit(nr_ui *u)
{
    u->quit_open = true;
    u->quit_sel = 0;          /* "Yes" is the default */
}

nr_ui_quit_result nr_ui_quit_key(nr_ui *u, nr_ui_key k)
{
    if (!u->quit_open) return UI_QUIT_NONE;
    switch (k) {
    case UI_KEY_LEFT: case UI_KEY_UP: u->quit_sel = 0; return UI_QUIT_NONE;
    case UI_KEY_RIGHT: case UI_KEY_DOWN: u->quit_sel = 1; return UI_QUIT_NONE;
    case UI_KEY_ACCEPT:
        u->quit_open = false;
        return u->quit_sel == 0 ? UI_QUIT_YES : UI_QUIT_NO;
    case UI_KEY_BACK:         /* Esc again / controller B: keep playing */
        u->quit_open = false;
        return UI_QUIT_NO;
    default: return UI_QUIT_NONE;
    }
}

#define COL_TEXT 0xFFFFFFFFu
#define COL_SEL 0xFF40D0FFu
#define COL_DIM 0xFFA0A0A0u
#define COL_PANEL 0xE0201810u
#define COL_BORDER 0xFF40D0FFu
#define COL_SHADE 0x80000000u

static void draw_quit(nr_ui *u, int cw, int ch)
{
    static const char *const title = "Do you want to close the Engine?";
    int scale = cw >= 640 ? 2 : 1;
    int tw = nr_text_width(title, scale);
    int pw = tw + 40 * scale, ph = 64 * scale;
    int px = (cw - pw) / 2, py = (ch - ph) / 2;
    int by = py + 38 * scale, yes_x = px + pw / 2 - 60 * scale, no_x = px + pw / 2 + 24 * scale;
    nr_canvas_rect(&u->cv, 0, 0, cw, ch, COL_SHADE);               /* dim the paused game */
    nr_canvas_rect(&u->cv, px - scale, py - scale, pw + 2 * scale, ph + 2 * scale, COL_BORDER);
    nr_canvas_rect(&u->cv, px, py, pw, ph, COL_PANEL);
    nr_canvas_text(&u->cv, px + (pw - tw) / 2, py + 12 * scale, scale, COL_TEXT, title);
    /* the arrow marks the current choice ("Yes" by default) */
    nr_canvas_text(&u->cv, yes_x - 12 * scale, by, scale, u->quit_sel == 0 ? COL_SEL : COL_DIM, u->quit_sel == 0 ? ">" : " ");
    nr_canvas_text(&u->cv, yes_x, by, scale, u->quit_sel == 0 ? COL_SEL : COL_TEXT, "Yes");
    nr_canvas_text(&u->cv, no_x - 12 * scale, by, scale, u->quit_sel == 1 ? COL_SEL : COL_DIM, u->quit_sel == 1 ? ">" : " ");
    nr_canvas_text(&u->cv, no_x, by, scale, u->quit_sel == 1 ? COL_SEL : COL_TEXT, "No");
}

bool nr_ui_render(nr_ui *u, int win_w, int win_h, u32 now)
{
    int cw = win_w / 2, ch = win_h / 2, i;
    bool any = false;
    char buf[200];
    if (cw < 320) cw = win_w < 320 ? win_w : 320;
    if (ch < 240) ch = win_h < 240 ? win_h : 240;
    if (cw <= 0 || ch <= 0) return false;
    if (!nr_canvas_resize(&u->cv, cw, ch)) return false;
    nr_canvas_clear(&u->cv);
    for (i = 0; i < u->nmsg;) {
        if ((int)(u->msg[i].until - now) <= 0) { memmove(&u->msg[i], &u->msg[i + 1], sizeof(u->msg[0]) * (size_t)(u->nmsg - i - 1)); u->nmsg--; }
        else i++;
    }
    if (u->show_fps) {
        snprintf(buf, sizeof(buf), "%.0f game fps / %.0f shown", (double)u->fps, (double)u->vps);
        nr_canvas_text(&u->cv, 4, 4, 1, 0xFF80FF80u, buf);
        any = true;
    }
    for (i = 0; i < u->nmsg; i++) {
        nr_canvas_text(&u->cv, 4, ch - 12 - (u->nmsg - 1 - i) * 10, 1, 0xFFFFFFC0u, u->msg[i].text);
        any = true;
    }
    if (u->quit_open) { draw_quit(u, cw, ch); any = true; }
    return any;
}
