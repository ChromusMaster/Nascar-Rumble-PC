#include "../src/render/swr.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void)
{
    static u16 native[1024 * 512];
    int scale, aa, sx, sy;
    const int modes[] = {1, 2, 4, 8, 16};
    nr_gfx_settings settings;
    nr_rlist *list = nr_rlist_new();
    nr_swr *r = nr_swr_new(1);
    nr_rcmd *c;
    bool sync;
    REQUIRE(list && r);
    REQUIRE(nr_rlist_alloc_data(list, UINT_MAX) == UINT_MAX && list->overflow);
    for (scale = 1; scale <= 8; scale++) {
        for (aa = 0; aa < 5; aa++) {
            nr_gfx_defaults(&settings);
            settings.internal_scale = scale; settings.antialias = modes[aa];
            REQUIRE(nr_swr_configure(r, &settings, &sync));
            nr_swr_scale(r, &sx, &sy);
            REQUIRE(sx > 0 && sx <= 16 && sy > 0 && sy <= 16);
            memset(native, 0, sizeof(native));
            native[511 * 1024 + 1023] = 31;
            native[511 * 1024] = 31 << 5;
            native[0] = 0x8000;
            nr_swr_sync(r, native);
            nr_rlist_clear(list);
            c = nr_rlist_push(list);
            REQUIRE(c);
            c->type = R_COPY; c->sx = 1023; c->sy = 511; c->x = 0; c->y = 0; c->w = 2; c->h = 1;
            c->flags = RF_CHECK_MASK;
            nr_swr_execute(r, list);
            REQUIRE(nr_swr_pixel(r, 0, 0) == 0x01000000);
            REQUIRE(nr_swr_pixel(r, sx, 0) == 0x0000FF00);
            c->flags = RF_SET_MASK;
            nr_swr_execute(r, list);
            REQUIRE(nr_swr_pixel(r, 0, 0) == 0x010000FF);
            REQUIRE(nr_swr_pixel(r, sx, 0) == 0x0100FF00);
            c->sx = 0; c->sy = 0; c->x = 1; c->w = 2; c->flags = 0;
            nr_swr_execute(r, list);
            REQUIRE(nr_swr_pixel(r, sx, 0) == 0x010000FF);
            REQUIRE(nr_swr_pixel(r, 2*sx, 0) == 0x0100FF00);
            c->x = 0; c->y = 511; c->w = 1; c->h = 2;
            nr_swr_execute(r, list);
            REQUIRE(nr_swr_pixel(r, 0, 511*sy) == 0x010000FF);
            c->w = 1025;
            nr_swr_execute(r, list);
            REQUIRE(nr_swr_pixel(r, -1, 0) == 0);
        }
    }
    nr_rlist_free(list); nr_swr_free(r);
    puts("render security: 40 configurations passed");
    return 0;
}
