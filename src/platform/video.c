#include "video.h"

void nr_video_dest_rect(const nr_gfx_settings *s, int win_w, int win_h, int pic_w, int pic_h,
                        int native_w, int native_h, int *x, int *y, int *w, int *h)
{
    int dw = win_w, dh = win_h;
    (void)pic_w; (void)pic_h;
    if (native_w <= 0) native_w = 320;
    if (native_h <= 0) native_h = 240;
    if (s->scaling == GFX_SCALE_INTEGER) {
        /* whole multiples of the console picture, centred */
        int kx = win_w / native_w, ky = win_h / native_h, k = kx < ky ? kx : ky;
        if (k < 1) k = 1;
        dw = native_w * k;
        dh = native_h * k;
    } else if (s->aspect == GFX_AR_4_3 || s->aspect == GFX_AR_16_9) {
        int ax = s->aspect == GFX_AR_4_3 ? 4 : 16, ay = s->aspect == GFX_AR_4_3 ? 3 : 9;
        if ((long long)win_w * ay > (long long)win_h * ax) { dh = win_h; dw = (int)((long long)win_h * ax / ay); }
        else { dw = win_w; dh = (int)((long long)win_w * ay / ax); }
    }
    *w = dw; *h = dh;
    *x = (win_w - dw) / 2;
    *y = (win_h - dh) / 2;
}
