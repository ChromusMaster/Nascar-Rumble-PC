/* Text rendering for the launcher: TrueType/OpenType fonts installed on the
 * system, rasterised with stb_truetype, with per-glyph fallback across a
 * chain of fonts (a Latin UI font plus a CJK font for Chinese, Japanese and
 * Korean). No font is shipped with the launcher; if nothing usable is found
 * the built-in 8x8 bitmap font is used (Latin only). */
#ifndef NR_LTEXT_H
#define NR_LTEXT_H

#include "../common/common.h"

/* script needed by the UI language (selects the fallback font) */
typedef enum { LT_LATIN = 0, LT_HANS, LT_HANT, LT_HANT_HK, LT_JA, LT_KO } lt_script;

typedef struct {
    u32 *px;
    int w, h;
} lt_surface;

bool lt_init(void);
void lt_shutdown(void);
/* (re)loads the fallback chain for a script; returns false when no font
 * covering that script was found (the caller then stays with Latin). */
bool lt_set_script(lt_script s);
bool lt_have_vector_font(void);
/* draws UTF-8 text with its top-left at (x, y); px = font size in pixels;
 * returns the advance width */
int lt_draw(lt_surface *s, int x, int y, int px, u32 rgba, const char *utf8);
int lt_width(int px, const char *utf8);
/* line height for a size */
int lt_line_height(int px);
/* description of the fonts in use (for the log) */
const char *lt_font_info(void);

#endif
