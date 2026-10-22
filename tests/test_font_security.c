#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "../src/launcher/lfont.h"
static long fail_at = -1, allocations;
static void *fault_malloc(size_t n) { return fail_at >= 0 && allocations++ == fail_at ? NULL : malloc(n); }
#define STBTT_malloc(n,u) fault_malloc(n)
#define STBTT_free(p,u) free(p)
#define STB_TRUETYPE_IMPLEMENTATION
#include "../src/third_party/stb_truetype.h"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    stbtt_fontinfo f;
    stbtt_pack_context ctx;
    unsigned char pixels[32], *bmp;
    stbtt_bakedchar chars[1];
    int w, h, x, y, i, glyph;
    size_t bytes;
    REQUIRE(!stbtt__safe_bitmap_size(-1, 1, &bytes));
    REQUIRE(!stbtt__safe_bitmap_size(0, 1, &bytes));
    REQUIRE(!stbtt__safe_bitmap_size(INT_MAX, INT_MAX, &bytes));
    REQUIRE(stbtt__safe_bitmap_size(1, 1, &bytes) && bytes == 1);
    REQUIRE(!stbtt_PackBegin(&ctx, pixels, -1, 4, 0, 0, NULL));
    REQUIRE(!stbtt_PackBegin(&ctx, pixels, 4, 4, 3, 0, NULL));
    REQUIRE(!stbtt_PackBegin(&ctx, pixels, 4, 4, 0, -1, NULL));
    REQUIRE(!stbtt_PackBegin(&ctx, pixels, 4, 4, 0, 4, NULL));
    memset(pixels, 0xA5, sizeof(pixels));
    REQUIRE(stbtt_PackBegin(&ctx, pixels, 4, 4, 8, 1, NULL));
    for (i = 0; i < 32; i++) REQUIRE(pixels[i] == (i % 8 < 4 ? 0 : 0xA5));
    stbtt_PackEnd(&ctx);
    for (i = 0; i < 2; i++) {
        fail_at = i; allocations = 0;
        REQUIRE(!stbtt_PackBegin(&ctx, pixels, 4, 4, 0, 1, NULL));
    }
    fail_at = -1;
    REQUIRE(stbtt_InitFont(&f, lt_builtin_font, 0));
    glyph = stbtt_FindGlyphIndex(&f, 'A');
    REQUIRE(glyph != 0);
    REQUIRE(stbtt_BakeFontBitmap(lt_builtin_font, 0, 16, pixels, -1, 2, 65, 1, chars) == -1);
    REQUIRE(!stbtt_GetGlyphSDF(&f, NAN, glyph, 1, 128, 32, &w, &h, &x, &y));
    REQUIRE(!stbtt_GetGlyphSDF(&f, INFINITY, glyph, 1, 128, 32, &w, &h, &x, &y));
    REQUIRE(!stbtt_GetGlyphSDF(&f, 1, glyph, INT_MAX, 128, 32, &w, &h, &x, &y));
    REQUIRE(!stbtt_GetGlyphBitmap(&f, INFINITY, 1, glyph, &w, &h, &x, &y));
    bmp = stbtt_GetGlyphBitmap(&f, 0.02f, 0.02f, glyph, &w, &h, &x, &y);
    REQUIRE(bmp && w > 0 && h > 0);
    stbtt_FreeBitmap(bmp, NULL);
    for (i = 0; i < 3; i++) {
        fail_at = i; allocations = 0;
        bmp = stbtt_GetGlyphSDF(&f, 0.02f, glyph, 2, 128, 32, &w, &h, &x, &y);
        if (bmp) stbtt_FreeSDF(bmp, NULL);
    }
    fail_at = -1;
    bmp = stbtt_GetGlyphSDF(&f, 0.02f, glyph, 2, 128, 32, &w, &h, &x, &y);
    REQUIRE(bmp);
    stbtt_FreeSDF(bmp, NULL);
    puts("font security: passed");
    return 0;
}
