#include "ltext.h"
#include "../ui/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define MAX_FONTS 4
#define CACHE_SIZE 4096

typedef struct {
    u8 *data;
    stbtt_fontinfo info;
    char path[512];
    int index;
    bool ok;
} lt_font;

typedef struct {
    u32 cp;
    u16 px;
    s8 font;           /* -1: not found */
    s16 xoff, yoff, w, h, adv;
    u8 *bmp;
    bool used;
} lt_glyph;

static lt_font g_font[MAX_FONTS];
static int g_nfonts;
static lt_glyph g_cache[CACHE_SIZE];
static char g_info[1024];
static lt_script g_script = LT_LATIN;

/* ------------------------------------------------------------ font files */

static bool load_font(lt_font *f, const char *path, int index)
{
    u8 *d;
    long n = nr_read_file(path, &d);
    int off;
    if (n <= 0) return false;
    off = stbtt_GetFontOffsetForIndex(d, index);
    if (off < 0 || !stbtt_InitFont(&f->info, d, off)) { free(d); return false; }
    f->data = d;
    f->index = index;
    nr_strlcpy(f->path, path, sizeof(f->path));
    f->ok = true;
    return true;
}

static void free_fonts(void)
{
    int i;
    for (i = 0; i < g_nfonts; i++) free(g_font[i].data);
    memset(g_font, 0, sizeof(g_font));
    g_nfonts = 0;
    for (i = 0; i < CACHE_SIZE; i++) { free(g_cache[i].bmp); }
    memset(g_cache, 0, sizeof(g_cache));
}

static bool add_font(const char *path, int index)
{
    int i;
    if (g_nfonts >= MAX_FONTS || !path || !*path) return false;
    for (i = 0; i < g_nfonts; i++) if (strcmp(g_font[i].path, path) == 0 && g_font[i].index == index) return true;
    if (!load_font(&g_font[g_nfonts], path, index)) return false;
    g_nfonts++;
    return true;
}

#ifndef _WIN32
/* fontconfig, loaded at run time so the launcher also runs without it */
typedef void FcPattern;
typedef void FcConfig;
typedef int FcBool;
typedef unsigned char FcChar8;
typedef enum { FcMatchPattern = 0 } FcMatchKind;
typedef int FcResult;
static struct {
    void *lib;
    FcBool (*Init)(void);
    FcPattern *(*NameParse)(const FcChar8 *);
    FcBool (*ConfigSubstitute)(FcConfig *, FcPattern *, FcMatchKind);
    void (*DefaultSubstitute)(FcPattern *);
    FcPattern *(*FontMatch)(FcConfig *, FcPattern *, FcResult *);
    FcResult (*PatternGetString)(const FcPattern *, const char *, int, FcChar8 **);
    FcResult (*PatternGetInteger)(const FcPattern *, const char *, int, int *);
    void (*PatternDestroy)(FcPattern *);
} fc;

static bool fc_load(void)
{
    if (fc.lib) return true;
    fc.lib = dlopen("libfontconfig.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!fc.lib) return false;
#define FCS(n) *(void **)&fc.n = dlsym(fc.lib, "Fc" #n); if (!fc.n) { dlclose(fc.lib); fc.lib = NULL; return false; }
    FCS(Init) FCS(NameParse) FCS(ConfigSubstitute) FCS(DefaultSubstitute) FCS(FontMatch)
    FCS(PatternGetString) FCS(PatternGetInteger) FCS(PatternDestroy)
#undef FCS
    fc.Init();
    return true;
}

/* best font file for a fontconfig pattern such as "sans-serif:lang=ja" */
static bool fc_find(const char *pattern, char *path, size_t n, int *index)
{
    FcPattern *p, *m;
    FcResult r;
    FcChar8 *file = NULL;
    bool ok = false;
    if (!fc_load()) return false;
    p = fc.NameParse((const FcChar8 *)pattern);
    if (!p) return false;
    fc.ConfigSubstitute(NULL, p, FcMatchPattern);
    fc.DefaultSubstitute(p);
    m = fc.FontMatch(NULL, p, &r);
    if (m) {
        if (fc.PatternGetString(m, "file", 0, &file) == 0 && file) {
            nr_strlcpy(path, (const char *)file, n);
            *index = 0;
            fc.PatternGetInteger(m, "index", 0, index);
            ok = true;
        }
        fc.PatternDestroy(m);
    }
    fc.PatternDestroy(p);
    return ok;
}
#endif

/* tries a list of "path#index" candidates */
static bool add_first(const char *const *cands, const char *dir)
{
    int i;
    for (i = 0; cands[i]; i++) {
        char path[600];
        const char *hash = strchr(cands[i], '#');
        int idx = hash ? atoi(hash + 1) : 0;
        size_t l = hash ? (size_t)(hash - cands[i]) : strlen(cands[i]);
        char name[256];
        if (l >= sizeof(name)) continue;
        memcpy(name, cands[i], l);
        name[l] = 0;
        if (dir) nr_path_join(path, sizeof(path), dir, name);
        else nr_strlcpy(path, name, sizeof(path));
        if (nr_file_exists(path) && add_font(path, idx)) return true;
    }
    return false;
}

#ifdef _WIN32
static void fonts_dir(char *out, size_t n)
{
    wchar_t w[MAX_PATH];
    UINT l = GetWindowsDirectoryW(w, MAX_PATH);
    char win[MAX_PATH * 3] = "C:\\Windows";
    if (l && l < MAX_PATH) WideCharToMultiByte(CP_UTF8, 0, w, -1, win, sizeof(win), NULL, NULL);
    nr_path_join(out, n, win, "Fonts");
}
#endif

static void load_latin(void)
{
#ifdef _WIN32
    static const char *const c[] = { "segoeui.ttf", "tahoma.ttf", "arial.ttf", "verdana.ttf", NULL };
    char dir[600];
    fonts_dir(dir, sizeof(dir));
    add_first(c, dir);
#else
    static const char *const c[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf", NULL };
    char path[600];
    int idx = 0;
    if (fc_find("sans-serif:lang=en:style=Regular", path, sizeof(path), &idx) && add_font(path, idx)) return;
    add_first(c, NULL);
#endif
}

static bool load_cjk(lt_script s)
{
#ifdef _WIN32
    static const char *const hans[] = { "msyh.ttc#0", "msyh.ttf", "simsun.ttc#0", "simhei.ttf", NULL };
    static const char *const hant[] = { "msjh.ttc#0", "msjh.ttf", "mingliu.ttc#0", NULL };
    static const char *const hk[] = { "msjh.ttc#0", "mingliu.ttc#2", "mingliu.ttc#0", NULL };
    static const char *const ja[] = { "YuGothM.ttc#0", "meiryo.ttc#0", "msgothic.ttc#0", NULL };
    static const char *const ko[] = { "malgun.ttf", "gulim.ttc#0", NULL };
    char dir[600];
    fonts_dir(dir, sizeof(dir));
    switch (s) {
    case LT_HANS: return add_first(hans, dir);
    case LT_HANT: return add_first(hant, dir);
    case LT_HANT_HK: return add_first(hk, dir);
    case LT_JA: return add_first(ja, dir);
    case LT_KO: return add_first(ko, dir);
    default: return true;
    }
#else
    /* Noto Sans CJK collections: 0 JP, 1 KR, 2 SC, 3 TC, 4 HK */
    static const char *const hans[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#2", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc#2",
                                        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc#0", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", NULL };
    static const char *const hant[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#3", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc#3",
                                        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc#0", NULL };
    static const char *const hk[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#4", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc#4",
                                      "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc#0", NULL };
    static const char *const ja[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#0", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc#0",
                                      "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf", "/usr/share/fonts/truetype/takao-gothic/TakaoPGothic.ttf", NULL };
    static const char *const ko[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#1", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc#1",
                                      "/usr/share/fonts/truetype/nanum/NanumGothic.ttf", NULL };
    static const char *const pat[] = { "", "sans-serif:lang=zh-cn", "sans-serif:lang=zh-tw", "sans-serif:lang=zh-hk", "sans-serif:lang=ja", "sans-serif:lang=ko" };
    const char *const *list = s == LT_HANS ? hans : s == LT_HANT ? hant : s == LT_HANT_HK ? hk : s == LT_JA ? ja : ko;
    char path[600];
    int idx = 0;
    if (s == LT_LATIN) return true;
    if (add_first(list, NULL)) return true;
    if (fc_find(pat[s], path, sizeof(path), &idx) && add_font(path, idx)) {
        /* accept it only if it really covers the script */
        static const u32 probe[] = { 0, 0x4E2D, 0x4E2D, 0x4E2D, 0x3042, 0xD55C };
        if (stbtt_FindGlyphIndex(&g_font[g_nfonts - 1].info, (int)probe[s])) return true;
        free(g_font[--g_nfonts].data);
        memset(&g_font[g_nfonts], 0, sizeof(g_font[0]));
    }
    return false;
#endif
}

static void update_info(void)
{
    int i;
    g_info[0] = 0;
    for (i = 0; i < g_nfonts; i++) {
        char t[600];
        snprintf(t, sizeof(t), "%s%s#%d", i ? ", " : "", g_font[i].path, g_font[i].index);
        strncat(g_info, t, sizeof(g_info) - strlen(g_info) - 1);
    }
    if (!g_nfonts) nr_strlcpy(g_info, "built-in 8x8 bitmap font", sizeof(g_info));
}

bool lt_init(void)
{
    free_fonts();
    load_latin();
    update_info();
    return g_nfonts > 0;
}

void lt_shutdown(void)
{
    free_fonts();
#ifndef _WIN32
    if (fc.lib) { dlclose(fc.lib); memset(&fc, 0, sizeof(fc)); }
#endif
}

bool lt_set_script(lt_script s)
{
    bool ok;
    free_fonts();
    load_latin();
    ok = load_cjk(s);
    g_script = ok ? s : LT_LATIN;
    update_info();
    return ok;
}

bool lt_have_vector_font(void) { return g_nfonts > 0; }
const char *lt_font_info(void) { return g_info; }

/* ------------------------------------------------------------ glyphs */

static u32 next_cp(const char **ps)
{
    const u8 *s = (const u8 *)*ps;
    u32 c = *s;
    int n = 0;
    if (c < 0x80) { *ps += 1; return c; }
    if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { *ps += 1; return '?'; }
    s++;
    while (n-- > 0) {
        if ((*s & 0xC0) != 0x80) { *ps = (const char *)s; return '?'; }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *ps = (const char *)s;
    return c;
}

static lt_glyph *glyph(u32 cp, int px)
{
    u32 h = (cp * 2654435761u ^ (u32)px * 40503u) & (CACHE_SIZE - 1);
    int probe, f;
    for (probe = 0; probe < 16; probe++) {
        lt_glyph *g = &g_cache[(h + (u32)probe) & (CACHE_SIZE - 1)];
        if (g->used && g->cp == cp && g->px == px) return g;
        if (!g->used) {
            int gi = 0, x0, y0, x1, y1, adv, lsb;
            float sc;
            g->used = true;
            g->cp = cp;
            g->px = (u16)px;
            g->font = -1;
            for (f = 0; f < g_nfonts; f++) {
                gi = stbtt_FindGlyphIndex(&g_font[f].info, (int)cp);
                if (gi) { g->font = (s8)f; break; }
            }
            if (g->font < 0) return g;
            sc = stbtt_ScaleForMappingEmToPixels(&g_font[f].info, (float)px);
            stbtt_GetGlyphHMetrics(&g_font[f].info, gi, &adv, &lsb);
            stbtt_GetGlyphBitmapBox(&g_font[f].info, gi, sc, sc, &x0, &y0, &x1, &y1);
            g->adv = (s16)(adv * sc + 0.5f);
            g->xoff = (s16)x0;
            g->yoff = (s16)y0;
            g->w = (s16)(x1 - x0);
            g->h = (s16)(y1 - y0);
            if (g->w > 0 && g->h > 0) {
                g->bmp = (u8 *)calloc((size_t)g->w * g->h, 1);
                if (g->bmp) stbtt_MakeGlyphBitmap(&g_font[f].info, g->bmp, g->w, g->h, g->w, sc, sc, gi);
            }
            return g;
        }
    }
    /* cache full around this slot: evict the first probe */
    {
        lt_glyph *g = &g_cache[h];
        free(g->bmp);
        memset(g, 0, sizeof(*g));
        return glyph(cp, px);
    }
}

static int ascent_px(int px)
{
    int a, d, l;
    float sc;
    if (!g_nfonts) return px;
    stbtt_GetFontVMetrics(&g_font[0].info, &a, &d, &l);
    sc = stbtt_ScaleForMappingEmToPixels(&g_font[0].info, (float)px);
    return (int)(a * sc + 0.5f);
}

int lt_line_height(int px) { return g_nfonts ? (px * 13) / 10 : 10 * ((px + 7) / 10); }

static void blend(u32 *d, u32 rgba, u32 cov)
{
    u32 a = ((rgba >> 24) * cov + 127) / 255, ia = 255 - a;
    u32 s = *d;
    u32 r = ((rgba & 0xFF) * a + (s & 0xFF) * ia) / 255;
    u32 g = (((rgba >> 8) & 0xFF) * a + ((s >> 8) & 0xFF) * ia) / 255;
    u32 b = (((rgba >> 16) & 0xFF) * a + ((s >> 16) & 0xFF) * ia) / 255;
    *d = r | (g << 8) | (b << 16) | 0xFF000000u;
}

/* fallback: scaled 8x8 bitmap font (Latin-1) */
static int draw_bitmap(lt_surface *s, int x, int y, int px, u32 rgba, const char *t)
{
    int sc = px >= 14 ? px / 8 : 1, x0 = x;
    while (*t) {
        u32 cp = next_cp(&t);
        const unsigned char *gl = nr_font8x8[cp < 256 ? cp : '?'];
        int gx, gy, i, j;
        if (s) for (gy = 0; gy < 8; gy++) for (gx = 0; gx < 8; gx++) if (gl[gy] & (1 << gx))
            for (j = 0; j < sc; j++) for (i = 0; i < sc; i++) {
                int X = x + gx * sc + i, Y = y + gy * sc + j;
                if (X >= 0 && Y >= 0 && X < s->w && Y < s->h) blend(&s->px[Y * s->w + X], rgba, 255);
            }
        x += 8 * sc;
    }
    return x - x0;
}

int lt_draw(lt_surface *s, int x, int y, int px, u32 rgba, const char *t)
{
    int x0 = x, base;
    if (!g_nfonts) return draw_bitmap(s, x, y, px, rgba, t);
    base = y + ascent_px(px);
    while (*t) {
        u32 cp = next_cp(&t);
        lt_glyph *g;
        if (cp == '\n' || cp == '\r') continue;
        g = glyph(cp, px);
        if (g->font < 0) g = glyph('?', px);
        if (s && g->bmp) {
            int i, j;
            for (j = 0; j < g->h; j++) {
                int Y = base + g->yoff + j;
                if (Y < 0 || Y >= s->h) continue;
                for (i = 0; i < g->w; i++) {
                    int X = x + g->xoff + i;
                    u8 c = g->bmp[j * g->w + i];
                    if (c && X >= 0 && X < s->w) blend(&s->px[Y * s->w + X], rgba, c);
                }
            }
        }
        x += g->adv;
    }
    return x - x0;
}

int lt_width(int px, const char *t) { return lt_draw(NULL, 0, 0, px, 0, t); }
