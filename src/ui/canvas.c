#include "font.h"

#include <stdlib.h>
#include <string.h>

bool nr_canvas_resize(nr_canvas *c, int w, int h)
{
    u32 *p;
    if (c->px && c->w == w && c->h == h) return true;
    p = (u32 *)calloc((size_t)w * h, 4);
    if (!p) return false;
    free(c->px);
    c->px = p; c->w = w; c->h = h;
    return true;
}

void nr_canvas_free(nr_canvas *c) { free(c->px); memset(c, 0, sizeof(*c)); }

void nr_canvas_clear(nr_canvas *c) { if (c->px) memset(c->px, 0, (size_t)c->w * c->h * 4); }

void nr_canvas_rect(nr_canvas *c, int x, int y, int w, int h, u32 rgba)
{
    int i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < c->h; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < c->w; i++) c->px[(size_t)j * c->w + i] = rgba;
}

/* next Latin-1 code point from UTF-8 ('?' for anything outside) */
static unsigned next_cp(const unsigned char **s)
{
    const unsigned char *p = *s;
    unsigned cp;
    if (p[0] < 0x80) { cp = p[0]; *s = p + 1; return cp; }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        *s = p + 2;
        return cp < 256 ? cp : '?';
    }
    /* skip longer sequences */
    p++;
    while ((*p & 0xC0) == 0x80) p++;
    *s = p;
    return '?';
}

static void glyph(nr_canvas *c, int x, int y, int scale, u32 rgba, unsigned cp)
{
    int row, col, i, j;
    for (row = 0; row < 8; row++) {
        unsigned bits = nr_font8x8[cp & 0xFF][row];
        for (col = 0; col < 8; col++) {
            if (!((bits >> col) & 1)) continue;
            for (j = 0; j < scale; j++) {
                int py = y + row * scale + j;
                if (py < 0 || py >= c->h) continue;
                for (i = 0; i < scale; i++) {
                    int px = x + col * scale + i;
                    if (px >= 0 && px < c->w) c->px[(size_t)py * c->w + px] = rgba;
                }
            }
        }
    }
}

int nr_text_width(const char *utf8, int scale)
{
    const unsigned char *s = (const unsigned char *)utf8;
    int n = 0;
    while (*s) { next_cp(&s); n++; }
    return n * 8 * scale;
}

int nr_canvas_text(nr_canvas *c, int x, int y, int scale, u32 rgba, const char *utf8)
{
    const unsigned char *s = (const unsigned char *)utf8;
    int cx = x;
    while (*s) {
        unsigned cp = next_cp(&s);
        glyph(c, cx + 1, y + 1, scale, 0xC0000000u, cp);
        glyph(c, cx, y, scale, rgba, cp);
        cx += 8 * scale;
    }
    return cx - x;
}
