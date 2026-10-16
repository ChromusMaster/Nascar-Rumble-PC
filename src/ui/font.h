/* Bitmap text and a small RGBA canvas for on-screen messages and menus. */
#ifndef NR_FONT_H
#define NR_FONT_H

#include "../common/common.h"

extern const unsigned char nr_font8x8[256][8];

typedef struct {
    u32 *px;        /* RGBA (R low byte), A = coverage */
    int w, h;
} nr_canvas;

bool nr_canvas_resize(nr_canvas *c, int w, int h);
void nr_canvas_free(nr_canvas *c);
void nr_canvas_clear(nr_canvas *c);
void nr_canvas_rect(nr_canvas *c, int x, int y, int w, int h, u32 rgba);
/* Draws UTF-8 text (Latin-1 range) at (x, y) with integer scale; returns the
 * width in pixels. A 1-pixel dark shadow is added for legibility. */
int nr_canvas_text(nr_canvas *c, int x, int y, int scale, u32 rgba, const char *utf8);
int nr_text_width(const char *utf8, int scale);

#endif
