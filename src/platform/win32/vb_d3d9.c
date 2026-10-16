/* Direct3D 9Ex video backend (shader model 3.0).
 *
 * Same pipeline as the Direct3D 10/11 backends (see vb_d3d11.c): the render
 * command list is rasterised into a high-resolution VRAM render target, the
 * displayed area is resolved with a box filter (supersampling AA) and the
 * present shader applies scaling filters, effects and colour grading.
 * Differences imposed by the API: shaders use the SM3 variant
 * (k_hlsl_sm3: float bit arithmetic, tex2Dlod at texel centres), textures
 * are BGRA (A8R8G8B8), uploads go through dynamic textures and region copies
 * use StretchRect. The maximum internal resolution follows the GPU's
 * maximum texture size. Direct3D 9Ex (Windows Vista or later) is required:
 * it never loses the device on resize. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../video.h"
#include "d3d_common.h"
#include "d3d_shaders.h"

typedef HRESULT(WINAPI *pfn_Direct3DCreate9Ex)(UINT, IDirect3D9Ex **);

typedef struct {
    IDirect3DTexture9 *tex;
    IDirect3DSurface9 *surf;
    int w, h;
    bool rt;
} tex9;

typedef struct {
    HWND hwnd;
    IDirect3D9Ex *d3d;
    IDirect3DDevice9Ex *dev;
    D3DPRESENT_PARAMETERS pp;
    D3DCAPS9 caps;
    IDirect3DVertexShader9 *vs;
    IDirect3DPixelShader9 *ps_prim, *ps_upload, *ps_copy, *ps_fill, *ps_resolve, *ps_present, *ps_overlay;
    IDirect3DVertexDeclaration9 *decl;
    IDirect3DSurface9 *bb;
    int bb_w, bb_h;
    tex9 vram, vread, internal, upload, picture, overlay;
    int sx, sy, S, ax, ay;
    nr_gfx_settings s;
    nr_gcb cbd;
    nr_gpu_replay_state rp;
    nr_gpu_ops ops;
    IDirect3DSurface9 *capsurf;
    int cap_w, cap_h;
    bool vsync_applied;
} d9;

static void t_free(tex9 *t)
{
    if (t->surf) IDirect3DSurface9_Release(t->surf);
    if (t->tex) IDirect3DTexture9_Release(t->tex);
    memset(t, 0, sizeof(*t));
}

static bool t_make(d9 *d, tex9 *t, int w, int h, bool rt)
{
    HRESULT hr;
    if (t->tex && t->w == w && t->h == h && t->rt == rt) return true;
    t_free(t);
    hr = IDirect3DDevice9Ex_CreateTexture(d->dev, (UINT)w, (UINT)h, 1, rt ? D3DUSAGE_RENDERTARGET : D3DUSAGE_DYNAMIC,
                                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t->tex, NULL);
    if (FAILED(hr)) return false;
    IDirect3DTexture9_GetSurfaceLevel(t->tex, 0, &t->surf);
    t->w = w; t->h = h; t->rt = rt;
    return true;
}

/* writes RGBA (R in the low byte) into a dynamic BGRA texture */
static bool t_write(tex9 *t, int x, int y, int w, int h, const u32 *px, int pitch_px)
{
    D3DLOCKED_RECT lr;
    RECT r;
    int i, j;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    if (FAILED(IDirect3DTexture9_LockRect(t->tex, 0, &lr, &r, 0))) return false;
    for (j = 0; j < h; j++) {
        u32 *o = (u32 *)((u8 *)lr.pBits + (size_t)j * lr.Pitch);
        const u32 *s = px + (size_t)j * pitch_px;
        for (i = 0; i < w; i++) {
            u32 v = s[i];
            o[i] = (v & 0xFF00FF00u) | ((v & 0xFF) << 16) | ((v >> 16) & 0xFF);
        }
    }
    IDirect3DTexture9_UnlockRect(t->tex, 0);
    return true;
}

static void consts(d9 *d)
{
    IDirect3DDevice9Ex_SetVertexShaderConstantF(d->dev, 0, (const float *)&d->cbd, 11);
    IDirect3DDevice9Ex_SetPixelShaderConstantF(d->dev, 0, (const float *)&d->cbd, 11);
}

static void set_target(d9 *d, IDirect3DSurface9 *s, int w, int h)
{
    D3DVIEWPORT9 vp;
    IDirect3DDevice9Ex_SetTexture(d->dev, 0, NULL);
    IDirect3DDevice9Ex_SetRenderTarget(d->dev, 0, s);
    vp.X = 0; vp.Y = 0; vp.Width = (DWORD)w; vp.Height = (DWORD)h; vp.MinZ = 0; vp.MaxZ = 1;
    IDirect3DDevice9Ex_SetViewport(d->dev, &vp);
    d->cbd.target[0] = (float)w; d->cbd.target[1] = (float)h; d->cbd.target[2] = 0.5f;
}

static void scissor(d9 *d, int x0, int y0, int x1, int y1)
{
    RECT r;
    r.left = x0; r.top = y0; r.right = x1; r.bottom = y1;
    IDirect3DDevice9Ex_SetScissorRect(d->dev, &r);
}

/* blend: 0 = opaque, 1..4 = console mode 0..3, 5 = alpha */
static void blend(d9 *d, int b)
{
    IDirect3DDevice9Ex *v = d->dev;
    if (b == 0) { IDirect3DDevice9Ex_SetRenderState(v, D3DRS_ALPHABLENDENABLE, FALSE); return; }
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    switch (b) {
    case 1:
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDFACTOR, 0x80808080u);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLEND, D3DBLEND_BLENDFACTOR);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOP, D3DBLENDOP_ADD);
        break;
    case 2:
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLEND, D3DBLEND_ONE);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLEND, D3DBLEND_ONE);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOP, D3DBLENDOP_ADD);
        break;
    case 3:
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLEND, D3DBLEND_ONE);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLEND, D3DBLEND_ONE);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
        break;
    case 4:
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDFACTOR, 0x40404040u);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLEND, D3DBLEND_ONE);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOP, D3DBLENDOP_ADD);
        break;
    default:
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice9Ex_SetRenderState(v, D3DRS_BLENDOP, D3DBLENDOP_ADD);
        break;
    }
}

static void bind(d9 *d, IDirect3DPixelShader9 *ps, tex9 *t)
{
    IDirect3DDevice9Ex_SetPixelShader(d->dev, ps);
    IDirect3DDevice9Ex_SetTexture(d->dev, 0, t ? (IDirect3DBaseTexture9 *)t->tex : NULL);
    if (t) { d->cbd.tsize[0] = (float)t->w; d->cbd.tsize[1] = (float)t->h; }
}

static void draw(d9 *d, const nr_gvtx *v, int n)
{
    consts(d);
    IDirect3DDevice9Ex_DrawPrimitiveUP(d->dev, D3DPT_TRIANGLELIST, (UINT)(n / 3), v, sizeof(nr_gvtx));
}

static void quad(d9 *d, IDirect3DPixelShader9 *ps, tex9 *t, int b, int x0, int y0, int x1, int y1, float u1, float v1)
{
    nr_gvtx q[6];
    bind(d, ps, t);
    blend(d, b);
    scissor(d, x0, y0, x1, y1);
    nr_gpu_quad(q, (float)x0, (float)y0, (float)x1, (float)y1, 0, 0, u1, v1);
    draw(d, q, 6);
}

/* ------------------------------------------------------------ replay ops */

static void op_draw(void *ctx, const nr_gvtx *v, int n, int pass, int mode, const int sc[4])
{
    d9 *d = (d9 *)ctx;
    d->cbd.pass[0] = (float)pass;
    bind(d, d->ps_prim, &d->vread);
    blend(d, pass ? 1 + (mode & 3) : 0);
    scissor(d, sc[0], sc[1], sc[2], sc[3]);
    draw(d, v, n);
}

static void op_upload(void *ctx, int x, int y, int w, int h, const u32 *rgba)
{
    d9 *d = (d9 *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    if (!t_write(&d->upload, x, y, cw, ch, rgba, w)) return;
    quad(d, d->ps_upload, &d->upload, 0, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy, 0, 0);
}

static void op_fill(void *ctx, int x, int y, int w, int h, const float col[4])
{
    d9 *d = (d9 *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    memcpy(d->cbd.color, col, sizeof(d->cbd.color));
    quad(d, d->ps_fill, NULL, 0, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy, 0, 0);
}

static void op_copy(void *ctx, int sx, int sy, int dx, int dy, int w, int h)
{
    d9 *d = (d9 *)ctx;
    int cw = w, ch = h;
    if (dx + cw > 1024) cw = 1024 - dx;
    if (dy + ch > 512) ch = 512 - dy;
    if (sx + cw > 1024) cw = 1024 - sx;
    if (sy + ch > 512) ch = 512 - sy;
    if (cw <= 0 || ch <= 0) return;
    d->cbd.quad[0] = (float)(dx * d->sx); d->cbd.quad[1] = (float)(dy * d->sy);
    d->cbd.quad[2] = (float)(sx * d->sx); d->cbd.quad[3] = (float)(sy * d->sy);
    quad(d, d->ps_copy, &d->vread, 0, dx * d->sx, dy * d->sy, (dx + cw) * d->sx, (dy + ch) * d->sy, 0, 0);
}

static void op_refresh(void *ctx, int x0, int y0, int x1, int y1)
{
    d9 *d = (d9 *)ctx;
    RECT r;
    r.left = x0 * d->sx; r.top = y0 * d->sy; r.right = x1 * d->sx; r.bottom = y1 * d->sy;
    IDirect3DDevice9Ex_StretchRect(d->dev, d->vram.surf, &r, d->vread.surf, &r, D3DTEXF_POINT);
}

/* ------------------------------------------------------------ setup */

static bool make_ps(d9 *d, const char *entry, IDirect3DPixelShader9 **ps, char *err, size_t n)
{
    void *blob = nr_d3d_compile(k_hlsl_sm3, entry, "ps_3_0", err, n);
    HRESULT hr;
    if (!blob) return false;
    hr = IDirect3DDevice9Ex_CreatePixelShader(d->dev, (const DWORD *)nr_d3d_blob_ptr(blob), ps);
    nr_d3d_blob_release(blob);
    if (FAILED(hr)) { snprintf(err, n, "CreatePixelShader(%s) failed (0x%08lX)", entry, (unsigned long)hr); return false; }
    return true;
}

static void base_state(d9 *d)
{
    IDirect3DDevice9Ex *v = d->dev;
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9Ex_SetRenderState(v, D3DRS_SCISSORTESTENABLE, TRUE);
    IDirect3DDevice9Ex_SetSamplerState(v, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9Ex_SetSamplerState(v, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9Ex_SetSamplerState(v, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9Ex_SetSamplerState(v, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9Ex_SetSamplerState(v, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9Ex_SetVertexDeclaration(v, d->decl);
    IDirect3DDevice9Ex_SetVertexShader(v, d->vs);
}

static bool get_bb(d9 *d)
{
    if (d->bb) { IDirect3DSurface9_Release(d->bb); d->bb = NULL; }
    if (FAILED(IDirect3DDevice9Ex_GetBackBuffer(d->dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &d->bb))) return false;
    d->bb_w = (int)d->pp.BackBufferWidth;
    d->bb_h = (int)d->pp.BackBufferHeight;
    return true;
}

static bool d9_configure(nr_vbackend *b, const nr_gfx_settings *s);

static bool d9_init(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t n)
{
    nr_gfx_settings fit;
    static const D3DVERTEXELEMENT9 el[] = {
        { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 8, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
        { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
        { 0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
        { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
        D3DDECL_END()
    };
    d9 *d;
    HMODULE lib;
    pfn_Direct3DCreate9Ex create;
    RECT rc;
    HRESULT hr;
    void *vsb;
    D3DADAPTER_IDENTIFIER9 id;
    if (!window) { snprintf(err, n, "window has no HWND"); return false; }
    lib = LoadLibraryA("d3d9.dll");
    if (!lib) { snprintf(err, n, "d3d9.dll not available"); return false; }
    create = (pfn_Direct3DCreate9Ex)(void *)GetProcAddress(lib, "Direct3DCreate9Ex");
    if (!create) { snprintf(err, n, "Direct3D 9Ex not available (requires Windows Vista or later)"); return false; }
    d = (d9 *)calloc(1, sizeof(d9));
    if (!d) return false;
    b->impl = d;
    d->hwnd = (HWND)window;
    if (FAILED(create(D3D_SDK_VERSION, &d->d3d))) { snprintf(err, n, "Direct3DCreate9Ex failed"); return false; }
    GetClientRect(d->hwnd, &rc);
    memset(&d->pp, 0, sizeof(d->pp));
    d->pp.BackBufferWidth = (UINT)(rc.right - rc.left > 0 ? rc.right - rc.left : 640);
    d->pp.BackBufferHeight = (UINT)(rc.bottom - rc.top > 0 ? rc.bottom - rc.top : 480);
    d->pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d->pp.BackBufferCount = 1;
    d->pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d->pp.hDeviceWindow = d->hwnd;
    d->pp.Windowed = TRUE;
    d->pp.PresentationInterval = s->vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    hr = IDirect3D9Ex_CreateDeviceEx(d->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d->hwnd,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &d->pp, NULL, &d->dev);
    if (FAILED(hr))
        hr = IDirect3D9Ex_CreateDeviceEx(d->d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d->hwnd,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &d->pp, NULL, &d->dev);
    if (FAILED(hr)) { snprintf(err, n, "CreateDeviceEx failed (0x%08lX)", (unsigned long)hr); return false; }
    d->vsync_applied = s->vsync;
    IDirect3DDevice9Ex_GetDeviceCaps(d->dev, &d->caps);
    if (d->caps.PixelShaderVersion < D3DPS_VERSION(3, 0)) { snprintf(err, n, "the GPU does not support pixel shader 3.0"); return false; }
    if (!(d->caps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR)) { snprintf(err, n, "the GPU does not support D3DBLEND_BLENDFACTOR"); return false; }
    if (!get_bb(d)) { snprintf(err, n, "backbuffer"); return false; }
    if (FAILED(IDirect3DDevice9Ex_CreateVertexDeclaration(d->dev, el, &d->decl))) { snprintf(err, n, "vertex declaration"); return false; }
    vsb = nr_d3d_compile(k_hlsl_sm3, "vs_main", "vs_3_0", err, n);
    if (!vsb) return false;
    hr = IDirect3DDevice9Ex_CreateVertexShader(d->dev, (const DWORD *)nr_d3d_blob_ptr(vsb), &d->vs);
    nr_d3d_blob_release(vsb);
    if (FAILED(hr)) { snprintf(err, n, "vertex shader (0x%08lX)", (unsigned long)hr); return false; }
    if (!make_ps(d, "ps_prim", &d->ps_prim, err, n) || !make_ps(d, "ps_upload", &d->ps_upload, err, n) ||
        !make_ps(d, "ps_copy", &d->ps_copy, err, n) || !make_ps(d, "ps_fill", &d->ps_fill, err, n) ||
        !make_ps(d, "ps_resolve", &d->ps_resolve, err, n) || !make_ps(d, "ps_present", &d->ps_present, err, n) ||
        !make_ps(d, "ps_overlay", &d->ps_overlay, err, n))
        return false;
    if (!t_make(d, &d->upload, 1024, 512, false)) { snprintf(err, n, "upload texture"); return false; }
    base_state(d);
    nr_gpu_replay_init(&d->rp);
    d->ops.ctx = d;
    d->ops.draw = op_draw; d->ops.upload = op_upload; d->ops.fill = op_fill; d->ops.copy = op_copy; d->ops.refresh = op_refresh;
    {
        int mx = (int)d->caps.MaxTextureWidth / 1024, my = (int)d->caps.MaxTextureHeight / 512;
        b->max_vram_scale = mx < my ? mx : my;
        if (b->max_vram_scale > 16) b->max_vram_scale = 16;
        if (b->max_vram_scale < 1) b->max_vram_scale = 1;
    }
    memset(&id, 0, sizeof(id));
    IDirect3D9Ex_GetAdapterIdentifier(d->d3d, D3DADAPTER_DEFAULT, 0, &id);
    snprintf(b->description, sizeof(b->description), "Direct3D 9Ex (SM 3.0), %s, max texture %lux%lu", id.Description,
             (unsigned long)d->caps.MaxTextureWidth, (unsigned long)d->caps.MaxTextureHeight);
    if (!d9_configure(b, nr_gfx_fit(s, b->max_vram_scale, &fit))) { snprintf(err, n, "initial configuration not supported"); return false; }
    return true;
}

static bool d9_configure(nr_vbackend *b, const nr_gfx_settings *s)
{
    d9 *d = (d9 *)b->impl;
    int S = s->internal_scale < 1 ? 1 : s->internal_scale, ax, ay;
    nr_gfx_aa_grid(s->antialias, &ax, &ay);
    if (S * ax > b->max_vram_scale || S * ay > b->max_vram_scale) return false;
    if (!t_make(d, &d->vram, 1024 * S * ax, 512 * S * ay, true) || !t_make(d, &d->vread, 1024 * S * ax, 512 * S * ay, true)) {
        t_free(&d->vram); t_free(&d->vread);
        return false;
    }
    d->S = S; d->ax = ax; d->ay = ay; d->sx = S * ax; d->sy = S * ay;
    d->s = *s;
    d->cbd.vram[0] = (float)d->vram.w; d->cbd.vram[1] = (float)d->vram.h;
    d->cbd.vram[2] = (float)d->sx; d->cbd.vram[3] = (float)d->sy;
    d->cbd.pass[1] = s->true_color ? 1.0f : 0.0f;
    d->cbd.pass[2] = s->dithering ? 1.0f : 0.0f;
    return true;
}

static void d9_sync(nr_vbackend *b, const u16 *vram)
{
    d9 *d = (d9 *)b->impl;
    IDirect3DDevice9Ex_BeginScene(d->dev);
    set_target(d, d->vram.surf, d->vram.w, d->vram.h);
    nr_gpu_sync(&d->rp, vram, &d->ops);
    IDirect3DDevice9Ex_EndScene(d->dev);
}

static void d9_execute(nr_vbackend *b, const nr_rlist *l)
{
    d9 *d = (d9 *)b->impl;
    IDirect3DDevice9Ex_BeginScene(d->dev);
    set_target(d, d->vram.surf, d->vram.w, d->vram.h);
    nr_gpu_replay(&d->rp, l, &d->s, d->sx, d->sy, &d->ops);
    IDirect3DDevice9Ex_EndScene(d->dev);
}

static void capture(nr_vbackend *b, d9 *d)
{
    D3DLOCKED_RECT lr;
    int y, x;
    if (!d->capsurf || d->cap_w != d->bb_w || d->cap_h != d->bb_h) {
        if (d->capsurf) IDirect3DSurface9_Release(d->capsurf);
        d->capsurf = NULL;
        if (FAILED(IDirect3DDevice9Ex_CreateOffscreenPlainSurface(d->dev, (UINT)d->bb_w, (UINT)d->bb_h, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &d->capsurf, NULL)))
            return;
        d->cap_w = d->bb_w; d->cap_h = d->bb_h;
    }
    if (FAILED(IDirect3DDevice9Ex_GetRenderTargetData(d->dev, d->bb, d->capsurf))) return;
    if (FAILED(IDirect3DSurface9_LockRect(d->capsurf, &lr, NULL, D3DLOCK_READONLY))) return;
    {
        u32 *px = (u32 *)realloc(b->cap_px, (size_t)d->cap_w * d->cap_h * 4);
        if (px) {
            b->cap_px = px;
            for (y = 0; y < d->cap_h; y++) {
                const u32 *s = (const u32 *)((const u8 *)lr.pBits + (size_t)y * lr.Pitch);
                for (x = 0; x < d->cap_w; x++) {
                    u32 v = s[x];
                    px[(size_t)y * d->cap_w + x] = 0xFF000000u | ((v & 0xFF) << 16) | (v & 0xFF00u) | ((v >> 16) & 0xFF);
                }
            }
            b->cap_w = d->cap_w; b->cap_h = d->cap_h;
        }
    }
    IDirect3DSurface9_UnlockRect(d->capsurf);
}

static void d9_present(nr_vbackend *b, const nr_vpresent *p)
{
    d9 *d = (d9 *)b->impl;
    tex9 *src = NULL;
    int sw = 0, sh = 0, x, y, w, h;
    if ((p->win_w > 0 && p->win_h > 0 && (p->win_w != d->bb_w || p->win_h != d->bb_h)) || d->vsync_applied != d->s.vsync) {
        if (p->win_w > 0) { d->pp.BackBufferWidth = (UINT)p->win_w; d->pp.BackBufferHeight = (UINT)p->win_h; }
        d->pp.PresentationInterval = d->s.vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
        if (d->bb) { IDirect3DSurface9_Release(d->bb); d->bb = NULL; }
        if (SUCCEEDED(IDirect3DDevice9Ex_ResetEx(d->dev, &d->pp, NULL))) { base_state(d); d->vsync_applied = d->s.vsync; }
        get_bb(d);
    }
    if (!d->bb) return;
    IDirect3DDevice9Ex_BeginScene(d->dev);
    if (p->use_hires && p->display && !p->display->rgb24) {
        int ow = (p->display->width > 0 ? p->display->width : 320) * d->S, oh = (p->display->height > 0 ? p->display->height : 240) * d->S;
        if (p->display->enabled && t_make(d, &d->internal, ow, oh, true)) {
            set_target(d, d->internal.surf, ow, oh);
            d->cbd.resolve[0] = (float)(p->display->disp_x * (u32)d->sx);
            d->cbd.resolve[1] = (float)(p->display->disp_y * (u32)d->sy);
            d->cbd.resolve[2] = (float)d->ax; d->cbd.resolve[3] = (float)d->ay;
            quad(d, d->ps_resolve, &d->vram, 0, 0, 0, ow, oh, 0, 0);
            src = &d->internal; sw = ow; sh = oh;
        }
    } else if (p->picture && p->pic_w > 0 && p->pic_h > 0) {
        if (t_make(d, &d->picture, p->pic_w, p->pic_h, false) && t_write(&d->picture, 0, 0, p->pic_w, p->pic_h, p->picture, p->pic_w)) {
            src = &d->picture; sw = p->pic_w; sh = p->pic_h;
        }
    }
    set_target(d, d->bb, d->bb_w, d->bb_h);
    IDirect3DDevice9Ex_Clear(d->dev, 0, NULL, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
    if (src) {
        nr_video_dest_rect(&d->s, d->bb_w, d->bb_h, sw, sh, p->native_w, p->native_h, &x, &y, &w, &h);
        d->cbd.psize[0] = (float)sw; d->cbd.psize[1] = (float)sh; d->cbd.psize[2] = (float)w; d->cbd.psize[3] = (float)h;
        d->cbd.pfx[0] = (float)(p->native_h > 0 ? p->native_h : sh);
        d->cbd.pfx[1] = (float)d->s.post_fx; d->cbd.pfx[2] = d->s.post_fx_strength; d->cbd.pfx[3] = (float)d->s.tonemap;
        d->cbd.pgrade[0] = d->s.brightness; d->cbd.pgrade[1] = d->s.contrast; d->cbd.pgrade[2] = d->s.saturation; d->cbd.pgrade[3] = d->s.gamma;
        d->cbd.pmisc[0] = (float)(p->frame & 0xFFFF); d->cbd.pmisc[1] = (float)d->s.scaling; d->cbd.pmisc[2] = (float)x; d->cbd.pmisc[3] = (float)y;
        quad(d, d->ps_present, src, 0, x, y, x + w, y + h, 0, 0);
    }
    if (p->overlay && p->overlay->px && t_make(d, &d->overlay, p->overlay->w, p->overlay->h, false) &&
        t_write(&d->overlay, 0, 0, p->overlay->w, p->overlay->h, p->overlay->px, p->overlay->w))
        quad(d, d->ps_overlay, &d->overlay, 5, 0, 0, d->bb_w, d->bb_h, (float)p->overlay->w, (float)p->overlay->h);
    IDirect3DDevice9Ex_EndScene(d->dev);
    if (b->capture_next) { b->capture_next = false; capture(b, d); }
    IDirect3DDevice9Ex_PresentEx(d->dev, NULL, NULL, NULL, NULL, 0);
}

static void d9_shutdown(nr_vbackend *b)
{
    d9 *d = (d9 *)b->impl;
    if (!d) return;
    t_free(&d->vram); t_free(&d->vread); t_free(&d->internal); t_free(&d->upload); t_free(&d->picture); t_free(&d->overlay);
#define REL(x, T) if (x) T##_Release(x)
    REL(d->capsurf, IDirect3DSurface9); REL(d->bb, IDirect3DSurface9);
    REL(d->ps_prim, IDirect3DPixelShader9); REL(d->ps_upload, IDirect3DPixelShader9); REL(d->ps_copy, IDirect3DPixelShader9);
    REL(d->ps_fill, IDirect3DPixelShader9); REL(d->ps_resolve, IDirect3DPixelShader9); REL(d->ps_present, IDirect3DPixelShader9);
    REL(d->ps_overlay, IDirect3DPixelShader9); REL(d->vs, IDirect3DVertexShader9); REL(d->decl, IDirect3DVertexDeclaration9);
    REL(d->dev, IDirect3DDevice9Ex); REL(d->d3d, IDirect3D9Ex);
#undef REL
    nr_gpu_replay_free(&d->rp);
    free(d);
    free(b->cap_px);
    b->cap_px = NULL;
    b->impl = NULL;
}

bool nr_vb_d3d9_create(nr_vbackend *b)
{
    memset(b, 0, sizeof(*b));
    b->name = "d3d9";
    b->gpu_hires = true;
    b->max_vram_scale = 8;
    b->init = d9_init;
    b->configure = d9_configure;
    b->sync = d9_sync;
    b->execute = d9_execute;
    b->present = d9_present;
    b->shutdown = d9_shutdown;
    return true;
}
