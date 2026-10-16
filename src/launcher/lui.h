/* Small immediate-mode widget toolkit for the launcher, drawn in software
 * into an RGBA surface (presented with SDL). Mouse, keyboard and game
 * controller navigation:
 *   Up/Down (D-pad)      move between controls
 *   Left/Right           change the value of the focused control
 *   Enter / A            open a list, press a button
 *   Esc / B              close a list
 *   PageUp/PageDown, LB/RB  previous/next tab */
#ifndef NR_LUI_H
#define NR_LUI_H

#include "ltext.h"

typedef enum { LK_NONE = 0, LK_UP, LK_DOWN, LK_LEFT, LK_RIGHT, LK_ENTER, LK_BACK, LK_PREV_TAB, LK_NEXT_TAB, LK_HOME, LK_END } lui_key;

#define LUI_POPUP_MAX 64
#define LUI_POPUP_TEXT 160

typedef struct { int x, y, w, h; } lui_rect;

typedef struct {
    lt_surface s;
    float k;                    /* layout scale (logical units -> pixels) */
    /* input of the current frame */
    int mx, my;
    bool mdown, mpressed, mreleased;
    int wheel;
    lui_key key;
    bool key_used;
    /* focus */
    int next_id;                /* focusable controls created this frame */
    int focus;                  /* focused control (-1 none) */
    int count_last;             /* controls in the previous frame */
    bool focus_visible;         /* keyboard/controller navigation in use */
    int scroll_to_focus;
    /* open list */
    int popup_id;
    lui_rect popup_anchor;
    /* The item texts are copied: callers often build their item lists in
     * local arrays that no longer exist when the popup is drawn in lui_end. */
    char popup_text[LUI_POPUP_MAX][LUI_POPUP_TEXT];
    int popup_n, popup_sel, popup_scroll;
    int popup_result;           /* chosen item, delivered on the next frame */
    int popup_result_id;
    /* slider drag */
    int drag_id;
    /* help text of the hovered/focused row */
    const char *help;
    bool help_from_focus;
} lui;

/* colours (RGBA, R in the low byte) */
#define LUI_BG 0xFF1E1A17u
#define LUI_PANEL 0xFF2A2522u
#define LUI_FIELD 0xFF38322Eu
#define LUI_FIELD_HOT 0xFF4A423Du
#define LUI_TEXT 0xFFF0F0F0u
#define LUI_DIM 0xFFA8A8A8u
#define LUI_ACCENT 0xFF14C8FFu     /* racing yellow */
#define LUI_RED 0xFF3030E0u
#define LUI_GREEN 0xFF60D060u

void lui_begin(lui *u, int w, int h);
void lui_end(lui *u);                 /* draws the open list on top */
int lui_px(const lui *u, int logical);
lui_rect lui_r(const lui *u, int x, int y, int w, int h);   /* logical -> pixels */

void lui_fill(lui *u, lui_rect r, u32 c);
void lui_frame(lui *u, lui_rect r, u32 c, int thick);
void lui_text(lui *u, int x, int y, int size, u32 c, const char *t);  /* logical coords */
void lui_text_px(lui *u, int x, int y, int px, u32 c, const char *t); /* pixel coords */
int lui_text_w(const lui *u, int size, const char *t);
/* text cut with "..." to fit max_w pixels */
void lui_text_fit(lui *u, int x, int y, int px, u32 c, const char *t, int max_w);

bool lui_hover(const lui *u, lui_rect r);
/* sets the help line when the next control is hovered or focused */
void lui_help(lui *u, const char *help);

/* controls (rectangles in logical units); return the new value */
int lui_combo(lui *u, lui_rect r, const char *const *items, int n, int cur);
bool lui_toggle(lui *u, lui_rect r, bool v, const char *yes, const char *no);
float lui_slider(lui *u, lui_rect r, float v, float lo, float hi, float step, const char *value_text);
bool lui_button(lui *u, lui_rect r, const char *text, bool primary);
bool lui_tab(lui *u, lui_rect r, const char *text, bool selected);
/* a focusable cell that reports activation (used for key binding) */
bool lui_cell(lui *u, lui_rect r, const char *text, bool highlight);

#endif
