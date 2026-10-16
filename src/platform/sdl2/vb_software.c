/* Software video backend: pictures come from the console framebuffer or the
 * software enhanced renderer (src/render/swr.c); presentation through
 * SDL_Renderer (which itself may use Direct3D/OpenGL/Metal to blit). */
#include "../video.h"
#include "../../render/postfx.h"
#include "../../common/thread.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *pic, *ovl;
    int pic_w, pic_h, ovl_w, ovl_h;
    nr_gfx_settings s;
    u32 *post;
    size_t post_cap;
    nr_pool *pool;
} sw_impl;

static bool sw_init(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t n)
{
    sw_impl *im = (sw_impl *)calloc(1, sizeof(sw_impl));
    SDL_RendererInfo info;
    if (!im) return false;
    im->win = (SDL_Window *)window;
    im->s = *s;
    im->ren = SDL_CreateRenderer(im->win, -1, SDL_RENDERER_ACCELERATED | (s->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!im->ren) im->ren = SDL_CreateRenderer(im->win, -1, 0);
    if (!im->ren) { SDL_snprintf(err, n, "SDL_CreateRenderer: %s", SDL_GetError()); free(im); return false; }
    im->pool = nr_pool_new(0);
    b->impl = im;
    if (SDL_GetRendererInfo(im->ren, &info) == 0)
        SDL_snprintf(b->description, sizeof(b->description), "software (presented through SDL %s)", info.name);
    return true;
}

static bool sw_configure(nr_vbackend *b, const nr_gfx_settings *s)
{
    sw_impl *im = (sw_impl *)b->impl;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (s->vsync != im->s.vsync) SDL_RenderSetVSync(im->ren, s->vsync ? 1 : 0);
#endif
    im->s = *s;
    return true;
}

static bool ensure_tex(SDL_Renderer *ren, SDL_Texture **t, int *tw, int *th, int w, int h, bool alpha)
{
    if (*t && *tw == w && *th == h) return true;
    if (*t) SDL_DestroyTexture(*t);
    *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!*t) return false;
    if (alpha) SDL_SetTextureBlendMode(*t, SDL_BLENDMODE_BLEND);
    *tw = w; *th = h;
    return true;
}

static void upload(SDL_Texture *t, const u32 *px, int w, int h)
{
    void *dst;
    int pitch, y;
    if (SDL_LockTexture(t, NULL, &dst, &pitch) != 0) return;
    for (y = 0; y < h; y++) memcpy((u8 *)dst + (size_t)y * pitch, px + (size_t)y * w, (size_t)w * 4);
    SDL_UnlockTexture(t);
}

static void sw_present(nr_vbackend *b, const nr_vpresent *p)
{
    sw_impl *im = (sw_impl *)b->impl;
    SDL_Rect dst;
    int x, y, w, h;
    SDL_SetRenderDrawColor(im->ren, 0, 0, 0, 255);
    SDL_RenderClear(im->ren);
    if (p->picture && p->pic_w > 0 && p->pic_h > 0) {
        nr_video_dest_rect(&im->s, p->win_w, p->win_h, p->pic_w, p->pic_h, p->native_w, p->native_h, &x, &y, &w, &h);
        dst.x = x; dst.y = y; dst.w = w; dst.h = h;
        if (nr_postfx_needed(&im->s) && w > 0 && h > 0) {
            size_t need = (size_t)w * h;
            if (need > im->post_cap) {
                u32 *nb = (u32 *)realloc(im->post, need * 4);
                if (nb) { im->post = nb; im->post_cap = need; }
            }
            if (need <= im->post_cap && ensure_tex(im->ren, &im->pic, &im->pic_w, &im->pic_h, w, h, false)) {
                nr_postfx_render(&im->s, p->picture, p->pic_w, p->pic_h, p->native_h, im->post, w, h, p->frame, im->pool);
                upload(im->pic, im->post, w, h);
#if SDL_VERSION_ATLEAST(2, 0, 12)
                SDL_SetTextureScaleMode(im->pic, SDL_ScaleModeNearest);
#endif
                SDL_RenderCopy(im->ren, im->pic, NULL, &dst);
            }
        } else if (ensure_tex(im->ren, &im->pic, &im->pic_w, &im->pic_h, p->pic_w, p->pic_h, false)) {
            upload(im->pic, p->picture, p->pic_w, p->pic_h);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(im->pic, (im->s.scaling == GFX_SCALE_BILINEAR) ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
            SDL_RenderCopy(im->ren, im->pic, NULL, &dst);
        }
    }
    if (p->overlay && p->overlay->px && ensure_tex(im->ren, &im->ovl, &im->ovl_w, &im->ovl_h, p->overlay->w, p->overlay->h, true)) {
        upload(im->ovl, p->overlay->px, p->overlay->w, p->overlay->h);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(im->ovl, SDL_ScaleModeNearest);
#endif
        SDL_RenderCopy(im->ren, im->ovl, NULL, NULL);
    }
    if (b->capture_next) {
        int w2, h2;
        b->capture_next = false;
        if (SDL_GetRendererOutputSize(im->ren, &w2, &h2) == 0) {
            u32 *n = (u32 *)realloc(b->cap_px, (size_t)w2 * h2 * 4);
            if (n) {
                b->cap_px = n;
                if (SDL_RenderReadPixels(im->ren, NULL, SDL_PIXELFORMAT_ABGR8888, n, w2 * 4) == 0) { b->cap_w = w2; b->cap_h = h2; }
            }
        }
    }
    SDL_RenderPresent(im->ren);
}

static void sw_shutdown(nr_vbackend *b)
{
    sw_impl *im = (sw_impl *)b->impl;
    if (!im) return;
    if (im->pic) SDL_DestroyTexture(im->pic);
    if (im->ovl) SDL_DestroyTexture(im->ovl);
    if (im->ren) SDL_DestroyRenderer(im->ren);
    nr_pool_free(im->pool);
    free(im->post);
    free(im);
    free(b->cap_px);
    b->cap_px = NULL;
    b->impl = NULL;
}

bool nr_vb_software_create(nr_vbackend *b)
{
    memset(b, 0, sizeof(*b));
    b->name = "software";
    b->gpu_hires = false;
    b->max_vram_scale = 16;
    b->init = sw_init;
    b->configure = sw_configure;
    b->present = sw_present;
    b->shutdown = sw_shutdown;
    return true;
}
