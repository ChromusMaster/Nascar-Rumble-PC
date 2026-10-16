/* Direct3D 11 video backend.
 *
 * Rasterises the render command list into a high-resolution VRAM render
 * target (internal resolution x supersampling), resolves the displayed area
 * with a box filter (the anti-aliasing step) and presents it with the
 * scaling filters / effects / colour grading of the present shader.
 * Direct3D is loaded at run time, so the executable also starts on systems
 * without it (the frontend then falls back to another renderer). */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../video.h"
#include "d3d_common.h"
#include "d3d_shaders.h"

typedef HRESULT(WINAPI *pfn_D3D11CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                                           UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                                           D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

typedef struct {
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv;
    int w, h;
} rt_tex;

typedef struct {
    HWND hwnd;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *sc;
    ID3D11RenderTargetView *bb_rtv;
    int bb_w, bb_h;
    D3D_FEATURE_LEVEL fl;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps_prim, *ps_upload, *ps_copy, *ps_fill, *ps_resolve, *ps_present, *ps_overlay;
    ID3D11InputLayout *layout;
    ID3D11Buffer *vb, *cb;
    UINT vb_size, vb_pos;
    ID3D11BlendState *bs_opaque, *bs_semi[4], *bs_alpha;
    ID3D11RasterizerState *rs;
    ID3D11SamplerState *smp;
    rt_tex vram, vread, internal;
    rt_tex upload, picture, overlay;   /* no rtv */
    int sx, sy, S, ax, ay;
    nr_gfx_settings s;
    nr_gcb cbd;
    nr_gpu_replay_state rp;
    nr_gpu_ops ops;
    ID3D11Texture2D *staging;
    int st_w, st_h;
} d11;

static void rt_free(rt_tex *t)
{
    if (t->rtv) ID3D11RenderTargetView_Release(t->rtv);
    if (t->srv) ID3D11ShaderResourceView_Release(t->srv);
    if (t->tex) ID3D11Texture2D_Release(t->tex);
    memset(t, 0, sizeof(*t));
}

static bool rt_make(d11 *d, rt_tex *t, int w, int h, bool rtv)
{
    D3D11_TEXTURE2D_DESC td;
    if (t->tex && t->w == w && t->h == h) return true;
    rt_free(t);
    memset(&td, 0, sizeof(td));
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (rtv ? D3D11_BIND_RENDER_TARGET : 0);
    if (FAILED(ID3D11Device_CreateTexture2D(d->dev, &td, NULL, &t->tex))) return false;
    if (FAILED(ID3D11Device_CreateShaderResourceView(d->dev, (ID3D11Resource *)t->tex, NULL, &t->srv))) { rt_free(t); return false; }
    if (rtv && FAILED(ID3D11Device_CreateRenderTargetView(d->dev, (ID3D11Resource *)t->tex, NULL, &t->rtv))) { rt_free(t); return false; }
    t->w = w; t->h = h;
    return true;
}

static void set_cb(d11 *d)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(d->ctx, (ID3D11Resource *)d->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &d->cbd, sizeof(d->cbd));
        ID3D11DeviceContext_Unmap(d->ctx, (ID3D11Resource *)d->cb, 0);
    }
}

static void draw_verts(d11 *d, const nr_gvtx *v, int n)
{
    D3D11_MAPPED_SUBRESOURCE m;
    UINT bytes = (UINT)n * sizeof(nr_gvtx), stride = sizeof(nr_gvtx), off;
    if (bytes > d->vb_size) {
        /* larger than the ring: split */
        int chunk = (int)(d->vb_size / sizeof(nr_gvtx)) / 3 * 3;
        while (n > 0) { int k = n < chunk ? n : chunk; draw_verts(d, v, k); v += k; n -= k; }
        return;
    }
    if (d->vb_pos + bytes > d->vb_size) d->vb_pos = 0;
    if (FAILED(ID3D11DeviceContext_Map(d->ctx, (ID3D11Resource *)d->vb, 0, d->vb_pos ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    memcpy((u8 *)m.pData + d->vb_pos, v, bytes);
    ID3D11DeviceContext_Unmap(d->ctx, (ID3D11Resource *)d->vb, 0);
    off = d->vb_pos;
    ID3D11DeviceContext_IASetVertexBuffers(d->ctx, 0, 1, &d->vb, &stride, &off);
    ID3D11DeviceContext_Draw(d->ctx, (UINT)n, 0);
    d->vb_pos += bytes;
}

static void set_target(d11 *d, ID3D11RenderTargetView *rtv, int w, int h)
{
    D3D11_VIEWPORT vp;
    ID3D11ShaderResourceView *null_srv = NULL;
    ID3D11DeviceContext_PSSetShaderResources(d->ctx, 0, 1, &null_srv);
    ID3D11DeviceContext_OMSetRenderTargets(d->ctx, 1, &rtv, NULL);
    vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (float)w; vp.Height = (float)h; vp.MinDepth = 0; vp.MaxDepth = 1;
    ID3D11DeviceContext_RSSetViewports(d->ctx, 1, &vp);
    d->cbd.target[0] = (float)w; d->cbd.target[1] = (float)h; d->cbd.target[2] = 0;
}

static void scissor(d11 *d, int x0, int y0, int x1, int y1)
{
    D3D11_RECT r;
    r.left = x0; r.top = y0; r.right = x1; r.bottom = y1;
    ID3D11DeviceContext_RSSetScissorRects(d->ctx, 1, &r);
}

static void bind(d11 *d, ID3D11PixelShader *ps, ID3D11ShaderResourceView *srv, ID3D11BlendState *bs, float factor)
{
    float bf[4] = { factor, factor, factor, factor };
    ID3D11DeviceContext_PSSetShader(d->ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(d->ctx, 0, 1, &srv);
    ID3D11DeviceContext_OMSetBlendState(d->ctx, bs, bf, 0xFFFFFFFFu);
}

/* ------------------------------------------------------------ replay ops */

static void op_draw(void *ctx, const nr_gvtx *v, int n, int pass, int mode, const int sc[4])
{
    d11 *d = (d11 *)ctx;
    d->cbd.pass[0] = (float)pass;
    set_cb(d);
    bind(d, d->ps_prim, d->vread.srv, pass ? d->bs_semi[mode & 3] : d->bs_opaque, (mode & 3) == 3 ? 0.25f : 0.5f);
    scissor(d, sc[0], sc[1], sc[2], sc[3]);
    draw_verts(d, v, n);
}

static void quad_draw(d11 *d, ID3D11PixelShader *ps, ID3D11ShaderResourceView *srv, int x0, int y0, int x1, int y1)
{
    nr_gvtx q[6];
    set_cb(d);
    bind(d, ps, srv, d->bs_opaque, 0);
    scissor(d, x0, y0, x1, y1);
    nr_gpu_quad(q, (float)x0, (float)y0, (float)x1, (float)y1, 0, 0, 0, 0);
    draw_verts(d, q, 6);
}

static void op_upload(void *ctx, int x, int y, int w, int h, const u32 *rgba)
{
    d11 *d = (d11 *)ctx;
    D3D11_BOX box;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    box.left = (UINT)x; box.top = (UINT)y; box.front = 0; box.right = (UINT)(x + cw); box.bottom = (UINT)(y + ch); box.back = 1;
    ID3D11DeviceContext_UpdateSubresource(d->ctx, (ID3D11Resource *)d->upload.tex, 0, &box, rgba, (UINT)w * 4, 0);
    quad_draw(d, d->ps_upload, d->upload.srv, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy);
}

static void op_fill(void *ctx, int x, int y, int w, int h, const float col[4])
{
    d11 *d = (d11 *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    memcpy(d->cbd.color, col, sizeof(d->cbd.color));
    quad_draw(d, d->ps_fill, NULL, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy);
}

static void op_copy(void *ctx, int sx, int sy, int dx, int dy, int w, int h)
{
    d11 *d = (d11 *)ctx;
    int cw = w, ch = h;
    if (dx + cw > 1024) cw = 1024 - dx;
    if (dy + ch > 512) ch = 512 - dy;
    if (sx + cw > 1024) cw = 1024 - sx;
    if (sy + ch > 512) ch = 512 - sy;
    if (cw <= 0 || ch <= 0) return;
    d->cbd.quad[0] = (float)(dx * d->sx); d->cbd.quad[1] = (float)(dy * d->sy);
    d->cbd.quad[2] = (float)(sx * d->sx); d->cbd.quad[3] = (float)(sy * d->sy);
    quad_draw(d, d->ps_copy, d->vread.srv, dx * d->sx, dy * d->sy, (dx + cw) * d->sx, (dy + ch) * d->sy);
}

static void op_refresh(void *ctx, int x0, int y0, int x1, int y1)
{
    d11 *d = (d11 *)ctx;
    D3D11_BOX b;
    b.left = (UINT)(x0 * d->sx); b.top = (UINT)(y0 * d->sy); b.front = 0;
    b.right = (UINT)(x1 * d->sx); b.bottom = (UINT)(y1 * d->sy); b.back = 1;
    ID3D11DeviceContext_CopySubresourceRegion(d->ctx, (ID3D11Resource *)d->vread.tex, 0, b.left, b.top, 0,
                                              (ID3D11Resource *)d->vram.tex, 0, &b);
}

/* ------------------------------------------------------------ setup */

static bool make_ps(d11 *d, const char *entry, const char *target, ID3D11PixelShader **ps, char *err, size_t n)
{
    void *blob = nr_d3d_compile(k_hlsl_sm4, entry, target, err, n);
    HRESULT hr;
    if (!blob) return false;
    hr = ID3D11Device_CreatePixelShader(d->dev, nr_d3d_blob_ptr(blob), nr_d3d_blob_size(blob), NULL, ps);
    nr_d3d_blob_release(blob);
    if (FAILED(hr)) { snprintf(err, n, "CreatePixelShader(%s) failed (0x%08lX)", entry, (unsigned long)hr); return false; }
    return true;
}

static ID3D11BlendState *blend(d11 *d, BOOL on, D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND_OP op, D3D11_BLEND srca, D3D11_BLEND dsta)
{
    D3D11_BLEND_DESC bd;
    ID3D11BlendState *s = NULL;
    memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].BlendEnable = on;
    bd.RenderTarget[0].SrcBlend = src;
    bd.RenderTarget[0].DestBlend = dst;
    bd.RenderTarget[0].BlendOp = op;
    bd.RenderTarget[0].SrcBlendAlpha = srca;
    bd.RenderTarget[0].DestBlendAlpha = dsta;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(d->dev, &bd, &s);
    return s;
}

static bool make_backbuffer(d11 *d, int w, int h)
{
    ID3D11Texture2D *bb = NULL;
    HRESULT hr;
    if (d->bb_rtv) { ID3D11RenderTargetView_Release(d->bb_rtv); d->bb_rtv = NULL; }
    if (d->bb_w != w || d->bb_h != h) {
        ID3D11DeviceContext_OMSetRenderTargets(d->ctx, 0, NULL, NULL);
        hr = IDXGISwapChain_ResizeBuffers(d->sc, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return false;
    }
    if (FAILED(IDXGISwapChain_GetBuffer(d->sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return false;
    hr = ID3D11Device_CreateRenderTargetView(d->dev, (ID3D11Resource *)bb, NULL, &d->bb_rtv);
    ID3D11Texture2D_Release(bb);
    if (FAILED(hr)) return false;
    d->bb_w = w; d->bb_h = h;
    return true;
}

static bool d11_configure(nr_vbackend *b, const nr_gfx_settings *s);

static bool d11_init(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t n)
{
    nr_gfx_settings fit;
    d11 *d;
    HMODULE lib;
    pfn_D3D11CreateDeviceAndSwapChain create;
    DXGI_SWAP_CHAIN_DESC sd;
    RECT rc;
    HRESULT hr;
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    void *vsb;
    int i;
    static const D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (!window) { snprintf(err, n, "window has no HWND"); return false; }
    lib = LoadLibraryA("d3d11.dll");
    if (!lib) { snprintf(err, n, "d3d11.dll not available"); return false; }
    create = (pfn_D3D11CreateDeviceAndSwapChain)(void *)GetProcAddress(lib, "D3D11CreateDeviceAndSwapChain");
    if (!create) { snprintf(err, n, "D3D11CreateDeviceAndSwapChain missing"); return false; }
    d = (d11 *)calloc(1, sizeof(d11));
    if (!d) return false;
    b->impl = d;
    d->hwnd = (HWND)window;
    GetClientRect(d->hwnd, &rc);
    memset(&sd, 0, sizeof(sd));
    sd.BufferDesc.Width = (UINT)(rc.right - rc.left > 0 ? rc.right - rc.left : 640);
    sd.BufferDesc.Height = (UINT)(rc.bottom - rc.top > 0 ? rc.bottom - rc.top : 480);
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = d->hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 3, D3D11_SDK_VERSION, &sd, &d->sc, &d->dev, &d->fl, &d->ctx);
    if (FAILED(hr)) {
        snprintf(err, n, "D3D11CreateDeviceAndSwapChain failed (0x%08lX)", (unsigned long)hr);
        return false;
    }
    d->bb_w = (int)sd.BufferDesc.Width; d->bb_h = (int)sd.BufferDesc.Height;
    if (!make_backbuffer(d, d->bb_w, d->bb_h)) { snprintf(err, n, "backbuffer"); return false; }
    vsb = nr_d3d_compile(k_hlsl_sm4, "vs_main", "vs_4_0", err, n);
    if (!vsb) return false;
    hr = ID3D11Device_CreateVertexShader(d->dev, nr_d3d_blob_ptr(vsb), nr_d3d_blob_size(vsb), NULL, &d->vs);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateInputLayout(d->dev, il, 7, nr_d3d_blob_ptr(vsb), nr_d3d_blob_size(vsb), &d->layout);
    nr_d3d_blob_release(vsb);
    if (FAILED(hr)) { snprintf(err, n, "vertex shader/input layout (0x%08lX)", (unsigned long)hr); return false; }
    if (!make_ps(d, "ps_prim", "ps_4_0", &d->ps_prim, err, n) || !make_ps(d, "ps_upload", "ps_4_0", &d->ps_upload, err, n) ||
        !make_ps(d, "ps_copy", "ps_4_0", &d->ps_copy, err, n) || !make_ps(d, "ps_fill", "ps_4_0", &d->ps_fill, err, n) ||
        !make_ps(d, "ps_resolve", "ps_4_0", &d->ps_resolve, err, n) || !make_ps(d, "ps_present", "ps_4_0", &d->ps_present, err, n) ||
        !make_ps(d, "ps_overlay", "ps_4_0", &d->ps_overlay, err, n))
        return false;
    memset(&bd, 0, sizeof(bd));
    d->vb_size = 4u << 20;
    bd.ByteWidth = d->vb_size; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(d->dev, &bd, NULL, &d->vb))) { snprintf(err, n, "vertex buffer"); return false; }
    bd.ByteWidth = (UINT)((sizeof(nr_gcb) + 15) & ~15u); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(d->dev, &bd, NULL, &d->cb))) { snprintf(err, n, "constant buffer"); return false; }
    d->bs_opaque = blend(d, FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    d->bs_semi[0] = blend(d, TRUE, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    d->bs_semi[1] = blend(d, TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    d->bs_semi[2] = blend(d, TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    d->bs_semi[3] = blend(d, TRUE, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    d->bs_alpha = blend(d, TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO);
    for (i = 0; i < 4; i++) if (!d->bs_semi[i]) { snprintf(err, n, "blend state"); return false; }
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
    if (FAILED(ID3D11Device_CreateRasterizerState(d->dev, &rd, &d->rs))) { snprintf(err, n, "rasterizer state"); return false; }
    if (!rt_make(d, &d->upload, 1024, 512, false)) { snprintf(err, n, "upload texture"); return false; }
    ID3D11DeviceContext_IASetInputLayout(d->ctx, d->layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(d->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(d->ctx, d->vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(d->ctx, 0, 1, &d->cb);
    ID3D11DeviceContext_PSSetConstantBuffers(d->ctx, 0, 1, &d->cb);
    ID3D11DeviceContext_RSSetState(d->ctx, d->rs);
    nr_gpu_replay_init(&d->rp);
    d->ops.ctx = d;
    d->ops.draw = op_draw; d->ops.upload = op_upload; d->ops.fill = op_fill; d->ops.copy = op_copy; d->ops.refresh = op_refresh;
    b->max_vram_scale = d->fl >= D3D_FEATURE_LEVEL_11_0 ? 16 : 8;
    {
        IDXGIDevice *xd = NULL;
        IDXGIAdapter *ad = NULL;
        DXGI_ADAPTER_DESC desc;
        char name[128] = "?";
        if (SUCCEEDED(ID3D11Device_QueryInterface(d->dev, &IID_IDXGIDevice, (void **)&xd))) {
            if (SUCCEEDED(IDXGIDevice_GetAdapter(xd, &ad))) {
                if (SUCCEEDED(IDXGIAdapter_GetDesc(ad, &desc))) WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), NULL, NULL);
                IDXGIAdapter_Release(ad);
            }
            IDXGIDevice_Release(xd);
        }
        snprintf(b->description, sizeof(b->description), "Direct3D 11 (feature level %d_%d), %s", (d->fl >> 12) & 0xF, (d->fl >> 8) & 0xF, name);
    }
    if (!d11_configure(b, nr_gfx_fit(s, b->max_vram_scale, &fit))) { snprintf(err, n, "initial configuration not supported"); return false; }
    return true;
}

static bool d11_configure(nr_vbackend *b, const nr_gfx_settings *s)
{
    d11 *d = (d11 *)b->impl;
    int S = s->internal_scale < 1 ? 1 : s->internal_scale, ax, ay;
    nr_gfx_aa_grid(s->antialias, &ax, &ay);
    if (S * ax > b->max_vram_scale || S * ay > b->max_vram_scale) return false;
    if (!rt_make(d, &d->vram, 1024 * S * ax, 512 * S * ay, true) || !rt_make(d, &d->vread, 1024 * S * ax, 512 * S * ay, false)) {
        rt_free(&d->vram); rt_free(&d->vread);
        S = d->S ? d->S : 1;
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

static void d11_sync(nr_vbackend *b, const u16 *vram)
{
    d11 *d = (d11 *)b->impl;
    set_target(d, d->vram.rtv, d->vram.w, d->vram.h);
    nr_gpu_sync(&d->rp, vram, &d->ops);
}

static void d11_execute(nr_vbackend *b, const nr_rlist *l)
{
    d11 *d = (d11 *)b->impl;
    set_target(d, d->vram.rtv, d->vram.w, d->vram.h);
    nr_gpu_replay(&d->rp, l, &d->s, d->sx, d->sy, &d->ops);
}

static bool upload_rgba(d11 *d, rt_tex *t, const u32 *px, int w, int h)
{
    if (!rt_make(d, t, w, h, false)) return false;
    ID3D11DeviceContext_UpdateSubresource(d->ctx, (ID3D11Resource *)t->tex, 0, NULL, px, (UINT)w * 4, 0);
    return true;
}

static void capture(nr_vbackend *b, d11 *d)
{
    ID3D11Texture2D *bb = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE m;
    int y;
    if (FAILED(IDXGISwapChain_GetBuffer(d->sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return;
    ID3D11Texture2D_GetDesc(bb, &td);
    if (!d->staging || d->st_w != (int)td.Width || d->st_h != (int)td.Height) {
        if (d->staging) ID3D11Texture2D_Release(d->staging);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        d->staging = NULL;
        if (FAILED(ID3D11Device_CreateTexture2D(d->dev, &td, NULL, &d->staging))) { ID3D11Texture2D_Release(bb); return; }
        d->st_w = (int)td.Width; d->st_h = (int)td.Height;
    }
    ID3D11DeviceContext_CopyResource(d->ctx, (ID3D11Resource *)d->staging, (ID3D11Resource *)bb);
    ID3D11Texture2D_Release(bb);
    if (FAILED(ID3D11DeviceContext_Map(d->ctx, (ID3D11Resource *)d->staging, 0, D3D11_MAP_READ, 0, &m))) return;
    {
        u32 *px = (u32 *)realloc(b->cap_px, (size_t)d->st_w * d->st_h * 4);
        if (px) {
            b->cap_px = px;
            for (y = 0; y < d->st_h; y++) {
                int x;
                const u32 *src = (const u32 *)((const u8 *)m.pData + (size_t)y * m.RowPitch);
                for (x = 0; x < d->st_w; x++) px[(size_t)y * d->st_w + x] = src[x] | 0xFF000000u;
            }
            b->cap_w = d->st_w; b->cap_h = d->st_h;
        }
    }
    ID3D11DeviceContext_Unmap(d->ctx, (ID3D11Resource *)d->staging, 0);
}

static void d11_present(nr_vbackend *b, const nr_vpresent *p)
{
    d11 *d = (d11 *)b->impl;
    float black[4] = { 0, 0, 0, 1 };
    ID3D11ShaderResourceView *src = NULL;
    int sw = 0, sh = 0, x, y, w, h;
    if (p->win_w > 0 && p->win_h > 0 && (p->win_w != d->bb_w || p->win_h != d->bb_h)) make_backbuffer(d, p->win_w, p->win_h);
    if (!d->bb_rtv) return;
    if (p->use_hires && p->display && !p->display->rgb24) {
        int ow = (p->display->width > 0 ? p->display->width : 320) * d->S, oh = (p->display->height > 0 ? p->display->height : 240) * d->S;
        if (p->display->enabled && rt_make(d, &d->internal, ow, oh, true)) {
            set_target(d, d->internal.rtv, ow, oh);
            d->cbd.resolve[0] = (float)(p->display->disp_x * (u32)d->sx);
            d->cbd.resolve[1] = (float)(p->display->disp_y * (u32)d->sy);
            d->cbd.resolve[2] = (float)d->ax; d->cbd.resolve[3] = (float)d->ay;
            quad_draw(d, d->ps_resolve, d->vram.srv, 0, 0, ow, oh);
            src = d->internal.srv; sw = ow; sh = oh;
        }
    } else if (p->picture && p->pic_w > 0 && p->pic_h > 0) {
        if (upload_rgba(d, &d->picture, p->picture, p->pic_w, p->pic_h)) { src = d->picture.srv; sw = p->pic_w; sh = p->pic_h; }
    }
    set_target(d, d->bb_rtv, d->bb_w, d->bb_h);
    ID3D11DeviceContext_ClearRenderTargetView(d->ctx, d->bb_rtv, black);
    if (src) {
        nr_video_dest_rect(&d->s, d->bb_w, d->bb_h, sw, sh, p->native_w, p->native_h, &x, &y, &w, &h);
        d->cbd.psize[0] = (float)sw; d->cbd.psize[1] = (float)sh; d->cbd.psize[2] = (float)w; d->cbd.psize[3] = (float)h;
        d->cbd.pfx[0] = (float)(p->native_h > 0 ? p->native_h : sh);
        d->cbd.pfx[1] = (float)d->s.post_fx; d->cbd.pfx[2] = d->s.post_fx_strength; d->cbd.pfx[3] = (float)d->s.tonemap;
        d->cbd.pgrade[0] = d->s.brightness; d->cbd.pgrade[1] = d->s.contrast; d->cbd.pgrade[2] = d->s.saturation; d->cbd.pgrade[3] = d->s.gamma;
        d->cbd.pmisc[0] = (float)(p->frame & 0xFFFF); d->cbd.pmisc[1] = (float)d->s.scaling; d->cbd.pmisc[2] = (float)x; d->cbd.pmisc[3] = (float)y;
        quad_draw(d, d->ps_present, src, x, y, x + w, y + h);
    }
    if (p->overlay && p->overlay->px && upload_rgba(d, &d->overlay, p->overlay->px, p->overlay->w, p->overlay->h)) {
        nr_gvtx q[6];
        set_cb(d);
        bind(d, d->ps_overlay, d->overlay.srv, d->bs_alpha, 0);
        scissor(d, 0, 0, d->bb_w, d->bb_h);
        nr_gpu_quad(q, 0, 0, (float)d->bb_w, (float)d->bb_h, 0, 0, (float)p->overlay->w, (float)p->overlay->h);
        draw_verts(d, q, 6);
    }
    if (b->capture_next) { b->capture_next = false; capture(b, d); }
    IDXGISwapChain_Present(d->sc, d->s.vsync ? 1 : 0, 0);
}

static void d11_shutdown(nr_vbackend *b)
{
    d11 *d = (d11 *)b->impl;
    int i;
    if (!d) return;
    if (d->ctx) ID3D11DeviceContext_ClearState(d->ctx);
    rt_free(&d->vram); rt_free(&d->vread); rt_free(&d->internal); rt_free(&d->upload); rt_free(&d->picture); rt_free(&d->overlay);
    if (d->staging) ID3D11Texture2D_Release(d->staging);
#define REL(x, T) if (x) T##_Release(x)
    REL(d->ps_prim, ID3D11PixelShader); REL(d->ps_upload, ID3D11PixelShader); REL(d->ps_copy, ID3D11PixelShader);
    REL(d->ps_fill, ID3D11PixelShader); REL(d->ps_resolve, ID3D11PixelShader); REL(d->ps_present, ID3D11PixelShader);
    REL(d->ps_overlay, ID3D11PixelShader); REL(d->vs, ID3D11VertexShader); REL(d->layout, ID3D11InputLayout);
    REL(d->vb, ID3D11Buffer); REL(d->cb, ID3D11Buffer);
    for (i = 0; i < 4; i++) REL(d->bs_semi[i], ID3D11BlendState);
    REL(d->bs_opaque, ID3D11BlendState); REL(d->bs_alpha, ID3D11BlendState); REL(d->rs, ID3D11RasterizerState);
    REL(d->bb_rtv, ID3D11RenderTargetView); REL(d->sc, IDXGISwapChain); REL(d->ctx, ID3D11DeviceContext); REL(d->dev, ID3D11Device);
#undef REL
    nr_gpu_replay_free(&d->rp);
    free(d);
    free(b->cap_px);
    b->cap_px = NULL;
    b->impl = NULL;
}

bool nr_vb_d3d11_create(nr_vbackend *b)
{
    memset(b, 0, sizeof(*b));
    b->name = "d3d11";
    b->gpu_hires = true;
    b->max_vram_scale = 16;
    b->init = d11_init;
    b->configure = d11_configure;
    b->sync = d11_sync;
    b->execute = d11_execute;
    b->present = d11_present;
    b->shutdown = d11_shutdown;
    return true;
}
