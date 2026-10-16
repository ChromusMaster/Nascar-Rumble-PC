/* Direct3D 12 video backend.
 *
 * Same pipeline as vb_d3d11.c (high-resolution VRAM render target,
 * supersampling resolve, present shader), expressed with explicit D3D12
 * objects: one root signature (44 root constants = the shader constant
 * buffer + one SRV table), one pipeline state object per blend mode/shader,
 * an upload ring for vertices and texture data, and explicit resource
 * barriers. Every submission is followed by a fence wait, which keeps the
 * resource lifetime rules trivial at a small cost in CPU/GPU overlap.
 * Requires Windows 10 and a feature level 11_0 GPU. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../video.h"
#include "d3d_common.h"
#include "d3d_shaders.h"

typedef HRESULT(WINAPI *pfn_D3D12CreateDevice)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
typedef HRESULT(WINAPI *pfn_D3D12SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
typedef HRESULT(WINAPI *pfn_CreateDXGIFactory1)(REFIID, void **);

enum { SRV_VREAD = 0, SRV_VRAM, SRV_UPLOAD, SRV_INTERNAL, SRV_PICTURE, SRV_OVERLAY, SRV_COUNT };
enum { RTV_VRAM = 0, RTV_INTERNAL, RTV_BB0, RTV_BB1, RTV_COUNT };
enum { PSO_PRIM_OPAQUE = 0, PSO_PRIM_SEMI0, PSO_PRIM_SEMI1, PSO_PRIM_SEMI2, PSO_PRIM_SEMI3, PSO_UPLOAD, PSO_COPY, PSO_FILL,
       PSO_RESOLVE, PSO_PRESENT, PSO_OVERLAY, PSO_COUNT };

#define NBB 2
#define RING_SIZE (64u << 20)

typedef struct {
    ID3D12Resource *res;
    int w, h;
    D3D12_RESOURCE_STATES state;
} tex12;

typedef struct {
    HWND hwnd;
    ID3D12Device *dev;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *cl;
    ID3D12Fence *fence;
    HANDLE fence_ev;
    UINT64 fence_val;
    IDXGISwapChain3 *sc;
    ID3D12Resource *bb[NBB];
    int bb_w, bb_h;
    ID3D12DescriptorHeap *srv_heap, *rtv_heap;
    UINT srv_inc, rtv_inc;
    ID3D12RootSignature *rs;
    ID3D12PipelineState *pso[PSO_COUNT];
    ID3D12Resource *ring;
    u8 *ring_map;
    UINT64 ring_pos;
    ID3D12Resource *readback;
    UINT64 readback_size;
    tex12 vram, vread, internal, upload, picture, overlay;
    int sx, sy, S, ax, ay;
    nr_gfx_settings s;
    nr_gcb cbd;
    nr_gpu_replay_state rp;
    nr_gpu_ops ops;
    bool recording;
    ID3D12PipelineState *cur_pso;
} d12;

/* ------------------------------------------------------------ helpers */

static D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle(ID3D12DescriptorHeap *h, UINT inc, int i)
{
    D3D12_CPU_DESCRIPTOR_HANDLE r;
    h->lpVtbl->GetCPUDescriptorHandleForHeapStart(h, &r);
    r.ptr += (SIZE_T)inc * (SIZE_T)i;
    return r;
}

static D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle(ID3D12DescriptorHeap *h, UINT inc, int i)
{
    D3D12_GPU_DESCRIPTOR_HANDLE r;
    h->lpVtbl->GetGPUDescriptorHandleForHeapStart(h, &r);
    r.ptr += (UINT64)inc * (UINT64)i;
    return r;
}

static void barrier(d12 *d, tex12 *t, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b;
    if (!t->res || t->state == to) return;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t->res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t->state;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(d->cl, 1, &b);
    t->state = to;
}

static void barrier_res(d12 *d, ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(d->cl, 1, &b);
}

static void wait_idle(d12 *d)
{
    d->fence_val++;
    ID3D12CommandQueue_Signal(d->queue, d->fence, d->fence_val);
    if (ID3D12Fence_GetCompletedValue(d->fence) < d->fence_val) {
        ID3D12Fence_SetEventOnCompletion(d->fence, d->fence_val, d->fence_ev);
        WaitForSingleObject(d->fence_ev, INFINITE);
    }
}

static void begin(d12 *d)
{
    ID3D12DescriptorHeap *heaps[1];
    if (d->recording) return;
    ID3D12CommandAllocator_Reset(d->alloc);
    ID3D12GraphicsCommandList_Reset(d->cl, d->alloc, NULL);
    heaps[0] = d->srv_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(d->cl, 1, heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(d->cl, d->rs);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(d->cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d->recording = true;
    d->cur_pso = NULL;
}

static void submit(d12 *d)
{
    ID3D12CommandList *lists[1];
    if (!d->recording) return;
    ID3D12GraphicsCommandList_Close(d->cl);
    lists[0] = (ID3D12CommandList *)d->cl;
    ID3D12CommandQueue_ExecuteCommandLists(d->queue, 1, lists);
    wait_idle(d);
    d->recording = false;
    d->ring_pos = 0;
}

/* allocation in the upload ring; submits and waits when full */
static UINT64 ring_alloc(d12 *d, UINT64 size, UINT64 align)
{
    UINT64 at = (d->ring_pos + align - 1) & ~(align - 1);
    if (at + size > RING_SIZE) {
        /* re-establish the recording state after the flush */
        D3D12_CPU_DESCRIPTOR_HANDLE rt;
        bool had = d->recording;
        submit(d);
        if (had) {
            begin(d);
            (void)rt;
        }
        at = 0;
        if (size > RING_SIZE) return (UINT64)-1;
    }
    d->ring_pos = at + size;
    return at;
}

static void set_target(d12 *d, int rtv, int w, int h)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h2 = cpu_handle(d->rtv_heap, d->rtv_inc, rtv);
    D3D12_VIEWPORT vp;
    ID3D12GraphicsCommandList_OMSetRenderTargets(d->cl, 1, &h2, FALSE, NULL);
    vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = (float)w; vp.Height = (float)h; vp.MinDepth = 0; vp.MaxDepth = 1;
    ID3D12GraphicsCommandList_RSSetViewports(d->cl, 1, &vp);
    d->cbd.target[0] = (float)w; d->cbd.target[1] = (float)h; d->cbd.target[2] = 0;
}

/* the RTV binding is lost when the ring flushes in the middle of a pass, so
 * every draw re-binds its target */
typedef struct { int rtv, w, h; } target_info;
static target_info g_target_dummy;

static void draw(d12 *d, int pso, int srv, const nr_gvtx *v, int n, const int *sc, float factor, const target_info *t)
{
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_RECT r;
    float bf[4];
    UINT64 bytes = (UINT64)n * sizeof(nr_gvtx);
    UINT64 at = ring_alloc(d, bytes, 16);
    if (at == (UINT64)-1) return;
    if (t) set_target(d, t->rtv, t->w, t->h);
    memcpy(d->ring_map + at, v, (size_t)bytes);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(d->ring) + at;
    vbv.SizeInBytes = (UINT)bytes;
    vbv.StrideInBytes = sizeof(nr_gvtx);
    ID3D12GraphicsCommandList_IASetVertexBuffers(d->cl, 0, 1, &vbv);
    if (d->cur_pso != d->pso[pso]) { ID3D12GraphicsCommandList_SetPipelineState(d->cl, d->pso[pso]); d->cur_pso = d->pso[pso]; }
    bf[0] = bf[1] = bf[2] = bf[3] = factor;
    ID3D12GraphicsCommandList_OMSetBlendFactor(d->cl, bf);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(d->cl, 0, sizeof(nr_gcb) / 4, &d->cbd, 0);
    if (srv >= 0) ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(d->cl, 1, gpu_handle(d->srv_heap, d->srv_inc, srv));
    r.left = sc[0]; r.top = sc[1]; r.right = sc[2]; r.bottom = sc[3];
    ID3D12GraphicsCommandList_RSSetScissorRects(d->cl, 1, &r);
    ID3D12GraphicsCommandList_DrawInstanced(d->cl, (UINT)n, 1, 0, 0);
}

static void quad(d12 *d, int pso, int srv, int x0, int y0, int x1, int y1, float u1, float v1, const target_info *t)
{
    nr_gvtx q[6];
    int sc[4];
    sc[0] = x0; sc[1] = y0; sc[2] = x1; sc[3] = y1;
    nr_gpu_quad(q, (float)x0, (float)y0, (float)x1, (float)y1, 0, 0, u1, v1);
    draw(d, pso, srv, q, 6, sc, 0, t);
}

/* copies RGBA rows into a texture region through the ring */
static void upload_region(d12 *d, tex12 *t, int x, int y, int w, int h, const u32 *px, int pitch_px)
{
    UINT pitch = ((UINT)w * 4 + 255) & ~255u;
    UINT64 at = ring_alloc(d, (UINT64)pitch * (UINT)h, 512);
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_RESOURCE_STATES prev = t->state;
    int j;
    if (at == (UINT64)-1) return;
    for (j = 0; j < h; j++) memcpy(d->ring_map + at + (UINT64)j * pitch, px + (size_t)j * pitch_px, (size_t)w * 4);
    memset(&dst, 0, sizeof(dst));
    memset(&src, 0, sizeof(src));
    dst.pResource = t->res;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    src.pResource = d->ring;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = at;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = (UINT)w;
    src.PlacedFootprint.Footprint.Height = (UINT)h;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;
    barrier(d, t, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(d->cl, &dst, (UINT)x, (UINT)y, 0, &src, NULL);
    barrier(d, t, prev == D3D12_RESOURCE_STATE_COPY_DEST ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : prev);
}

static void t_free(tex12 *t)
{
    if (t->res) ID3D12Resource_Release(t->res);
    memset(t, 0, sizeof(*t));
}

static bool t_make(d12 *d, tex12 *t, int w, int h, int rtv, int srv)
{
    D3D12_HEAP_PROPERTIES hp;
    D3D12_RESOURCE_DESC rd;
    D3D12_CLEAR_VALUE cv;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv;
    D3D12_RESOURCE_STATES st = rtv >= 0 ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (t->res && t->w == w && t->h == h) return true;
    if (t->res) { submit(d); t_free(t); }
    memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)w; rd.Height = (UINT)h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = rtv >= 0 ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    memset(&cv, 0, sizeof(cv));
    cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(ID3D12Device_CreateCommittedResource(d->dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, st, rtv >= 0 ? &cv : NULL,
                                                    &IID_ID3D12Resource, (void **)&t->res)))
        return false;
    t->w = w; t->h = h; t->state = st;
    if (rtv >= 0) ID3D12Device_CreateRenderTargetView(d->dev, t->res, NULL, cpu_handle(d->rtv_heap, d->rtv_inc, rtv));
    memset(&sv, 0, sizeof(sv));
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(d->dev, t->res, &sv, cpu_handle(d->srv_heap, d->srv_inc, srv));
    return true;
}

/* ------------------------------------------------------------ replay ops */

static const target_info *vram_target(d12 *d)
{
    static target_info t;
    t.rtv = RTV_VRAM; t.w = d->vram.w; t.h = d->vram.h;
    return &t;
}

static void op_draw(void *ctx, const nr_gvtx *v, int n, int pass, int mode, const int sc[4])
{
    d12 *d = (d12 *)ctx;
    d->cbd.pass[0] = (float)pass;
    draw(d, pass ? PSO_PRIM_SEMI0 + (mode & 3) : PSO_PRIM_OPAQUE, SRV_VREAD, v, n, sc, (mode & 3) == 3 ? 0.25f : 0.5f, vram_target(d));
}

static void op_upload(void *ctx, int x, int y, int w, int h, const u32 *rgba)
{
    d12 *d = (d12 *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    upload_region(d, &d->upload, x, y, cw, ch, rgba, w);
    quad(d, PSO_UPLOAD, SRV_UPLOAD, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy, 0, 0, vram_target(d));
}

static void op_fill(void *ctx, int x, int y, int w, int h, const float col[4])
{
    d12 *d = (d12 *)ctx;
    int cw = x + w > 1024 ? 1024 - x : w, ch = y + h > 512 ? 512 - y : h;
    if (cw <= 0 || ch <= 0) return;
    memcpy(d->cbd.color, col, sizeof(d->cbd.color));
    quad(d, PSO_FILL, -1, x * d->sx, y * d->sy, (x + cw) * d->sx, (y + ch) * d->sy, 0, 0, vram_target(d));
}

static void op_copy(void *ctx, int sx, int sy, int dx, int dy, int w, int h)
{
    d12 *d = (d12 *)ctx;
    int cw = w, ch = h;
    if (dx + cw > 1024) cw = 1024 - dx;
    if (dy + ch > 512) ch = 512 - dy;
    if (sx + cw > 1024) cw = 1024 - sx;
    if (sy + ch > 512) ch = 512 - sy;
    if (cw <= 0 || ch <= 0) return;
    d->cbd.quad[0] = (float)(dx * d->sx); d->cbd.quad[1] = (float)(dy * d->sy);
    d->cbd.quad[2] = (float)(sx * d->sx); d->cbd.quad[3] = (float)(sy * d->sy);
    quad(d, PSO_COPY, SRV_VREAD, dx * d->sx, dy * d->sy, (dx + cw) * d->sx, (dy + ch) * d->sy, 0, 0, vram_target(d));
}

static void op_refresh(void *ctx, int x0, int y0, int x1, int y1)
{
    d12 *d = (d12 *)ctx;
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    D3D12_BOX b;
    memset(&dst, 0, sizeof(dst));
    memset(&src, 0, sizeof(src));
    dst.pResource = d->vread.res; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = d->vram.res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    b.left = (UINT)(x0 * d->sx); b.top = (UINT)(y0 * d->sy); b.front = 0;
    b.right = (UINT)(x1 * d->sx); b.bottom = (UINT)(y1 * d->sy); b.back = 1;
    barrier(d, &d->vram, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(d, &d->vread, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(d->cl, &dst, b.left, b.top, 0, &src, &b);
    barrier(d, &d->vread, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barrier(d, &d->vram, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

/* ------------------------------------------------------------ setup */

static bool make_pso(d12 *d, int idx, void *vs, const char *entry, int blend_kind, char *err, size_t n)
{
    static const D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    D3D12_RENDER_TARGET_BLEND_DESC *bt;
    void *ps = nr_d3d_compile(k_hlsl_sm4, entry, "ps_5_0", err, n);
    HRESULT hr;
    if (!ps) return false;
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = d->rs;
    pd.VS.pShaderBytecode = nr_d3d_blob_ptr(vs); pd.VS.BytecodeLength = nr_d3d_blob_size(vs);
    pd.PS.pShaderBytecode = nr_d3d_blob_ptr(ps); pd.PS.BytecodeLength = nr_d3d_blob_size(ps);
    bt = &pd.BlendState.RenderTarget[0];
    bt->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    bt->LogicOp = D3D12_LOGIC_OP_NOOP;
    bt->SrcBlendAlpha = D3D12_BLEND_ONE; bt->DestBlendAlpha = D3D12_BLEND_ZERO; bt->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    bt->SrcBlend = D3D12_BLEND_ONE; bt->DestBlend = D3D12_BLEND_ZERO; bt->BlendOp = D3D12_BLEND_OP_ADD;
    switch (blend_kind) {
    case 1: bt->BlendEnable = TRUE; bt->SrcBlend = D3D12_BLEND_BLEND_FACTOR; bt->DestBlend = D3D12_BLEND_BLEND_FACTOR; break;
    case 2: bt->BlendEnable = TRUE; bt->DestBlend = D3D12_BLEND_ONE; break;
    case 3: bt->BlendEnable = TRUE; bt->DestBlend = D3D12_BLEND_ONE; bt->BlendOp = D3D12_BLEND_OP_REV_SUBTRACT; break;
    case 4: bt->BlendEnable = TRUE; bt->SrcBlend = D3D12_BLEND_BLEND_FACTOR; bt->DestBlend = D3D12_BLEND_ONE; break;
    case 5: bt->BlendEnable = TRUE; bt->SrcBlend = D3D12_BLEND_SRC_ALPHA; bt->DestBlend = D3D12_BLEND_INV_SRC_ALPHA; break;
    default: break;
    }
    pd.SampleMask = 0xFFFFFFFFu;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.InputLayout.pInputElementDescs = il;
    pd.InputLayout.NumElements = 7;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(d->dev, &pd, &IID_ID3D12PipelineState, (void **)&d->pso[idx]);
    nr_d3d_blob_release(ps);
    if (FAILED(hr)) { snprintf(err, n, "CreateGraphicsPipelineState(%s) failed (0x%08lX)", entry, (unsigned long)hr); return false; }
    return true;
}

static bool get_backbuffers(d12 *d)
{
    int i;
    for (i = 0; i < NBB; i++) {
        if (FAILED(IDXGISwapChain3_GetBuffer(d->sc, (UINT)i, &IID_ID3D12Resource, (void **)&d->bb[i]))) return false;
        ID3D12Device_CreateRenderTargetView(d->dev, d->bb[i], NULL, cpu_handle(d->rtv_heap, d->rtv_inc, RTV_BB0 + i));
    }
    return true;
}

static bool d12_configure(nr_vbackend *b, const nr_gfx_settings *s);

static bool d12_init(nr_vbackend *b, void *window, const nr_gfx_settings *s, char *err, size_t n)
{
    nr_gfx_settings fit;
    d12 *d;
    HMODULE lib, dxgi;
    pfn_D3D12CreateDevice create;
    pfn_D3D12SerializeRootSignature serialize;
    pfn_CreateDXGIFactory1 mkfactory;
    IDXGIFactory4 *fac = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    DXGI_SWAP_CHAIN_DESC1 sd;
    D3D12_COMMAND_QUEUE_DESC qd;
    D3D12_DESCRIPTOR_HEAP_DESC hd;
    D3D12_ROOT_PARAMETER rp[2];
    D3D12_DESCRIPTOR_RANGE range;
    D3D12_ROOT_SIGNATURE_DESC rsd;
    ID3DBlob *sig = NULL, *sig_err = NULL;
    D3D12_HEAP_PROPERTIES hp;
    D3D12_RESOURCE_DESC rd;
    RECT rc;
    HRESULT hr;
    void *vs;
    int i;
    if (!window) { snprintf(err, n, "window has no HWND"); return false; }
    lib = LoadLibraryA("d3d12.dll");
    dxgi = LoadLibraryA("dxgi.dll");
    if (!lib || !dxgi) { snprintf(err, n, "d3d12.dll not available (requires Windows 10 or later)"); return false; }
    create = (pfn_D3D12CreateDevice)(void *)GetProcAddress(lib, "D3D12CreateDevice");
    serialize = (pfn_D3D12SerializeRootSignature)(void *)GetProcAddress(lib, "D3D12SerializeRootSignature");
    mkfactory = (pfn_CreateDXGIFactory1)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!create || !serialize || !mkfactory) { snprintf(err, n, "Direct3D 12 functions missing"); return false; }
    d = (d12 *)calloc(1, sizeof(d12));
    if (!d) return false;
    b->impl = d;
    d->hwnd = (HWND)window;
    if (FAILED(mkfactory(&IID_IDXGIFactory4, (void **)&fac))) { snprintf(err, n, "IDXGIFactory4 not available"); return false; }
    hr = create(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&d->dev);
    if (FAILED(hr)) { IDXGIFactory4_Release(fac); snprintf(err, n, "D3D12CreateDevice failed (0x%08lX)", (unsigned long)hr); return false; }
    memset(&qd, 0, sizeof(qd));
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(ID3D12Device_CreateCommandQueue(d->dev, &qd, &IID_ID3D12CommandQueue, (void **)&d->queue))) { snprintf(err, n, "command queue"); return false; }
    GetClientRect(d->hwnd, &rc);
    memset(&sd, 0, sizeof(sd));
    sd.Width = (UINT)(rc.right - rc.left > 0 ? rc.right - rc.left : 640);
    sd.Height = (UINT)(rc.bottom - rc.top > 0 ? rc.bottom - rc.top : 480);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = NBB;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)d->queue, d->hwnd, &sd, NULL, NULL, &sc1);
    if (SUCCEEDED(hr)) IDXGIFactory4_MakeWindowAssociation(fac, d->hwnd, DXGI_MWA_NO_ALT_ENTER);
    IDXGIFactory4_Release(fac);
    if (FAILED(hr)) { snprintf(err, n, "CreateSwapChainForHwnd failed (0x%08lX)", (unsigned long)hr); return false; }
    hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&d->sc);
    IDXGISwapChain1_Release(sc1);
    if (FAILED(hr)) { snprintf(err, n, "IDXGISwapChain3 not available"); return false; }
    d->bb_w = (int)sd.Width; d->bb_h = (int)sd.Height;
    if (FAILED(ID3D12Device_CreateCommandAllocator(d->dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&d->alloc)) ||
        FAILED(ID3D12Device_CreateCommandList(d->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, d->alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&d->cl)) ||
        FAILED(ID3D12Device_CreateFence(d->dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&d->fence))) {
        snprintf(err, n, "command list/fence"); return false;
    }
    ID3D12GraphicsCommandList_Close(d->cl);
    d->fence_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    memset(&hd, 0, sizeof(hd));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = SRV_COUNT; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(d->dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&d->srv_heap))) { snprintf(err, n, "descriptor heap"); return false; }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = RTV_COUNT; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(d->dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&d->rtv_heap))) { snprintf(err, n, "rtv heap"); return false; }
    d->srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(d->dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    d->rtv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(d->dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (!get_backbuffers(d)) { snprintf(err, n, "backbuffers"); return false; }
    /* root signature: b0 = 44 constants, t0 = SRV table */
    memset(rp, 0, sizeof(rp));
    memset(&range, 0, sizeof(range));
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.ShaderRegister = 0;
    rp[0].Constants.Num32BitValues = sizeof(nr_gcb) / 4;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = 0;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    memset(&rsd, 0, sizeof(rsd));
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sig_err);
    if (FAILED(hr)) { snprintf(err, n, "root signature: %s", sig_err ? (const char *)ID3D10Blob_GetBufferPointer(sig_err) : "?"); return false; }
    hr = ID3D12Device_CreateRootSignature(d->dev, 0, ID3D10Blob_GetBufferPointer(sig), ID3D10Blob_GetBufferSize(sig), &IID_ID3D12RootSignature, (void **)&d->rs);
    ID3D10Blob_Release(sig);
    if (sig_err) ID3D10Blob_Release(sig_err);
    if (FAILED(hr)) { snprintf(err, n, "CreateRootSignature (0x%08lX)", (unsigned long)hr); return false; }
    vs = nr_d3d_compile(k_hlsl_sm4, "vs_main", "vs_5_0", err, n);
    if (!vs) return false;
    {
        static const struct { int idx; const char *ps; int blend; } list[] = {
            { PSO_PRIM_OPAQUE, "ps_prim", 0 }, { PSO_PRIM_SEMI0, "ps_prim", 1 }, { PSO_PRIM_SEMI1, "ps_prim", 2 },
            { PSO_PRIM_SEMI2, "ps_prim", 3 }, { PSO_PRIM_SEMI3, "ps_prim", 4 }, { PSO_UPLOAD, "ps_upload", 0 },
            { PSO_COPY, "ps_copy", 0 }, { PSO_FILL, "ps_fill", 0 }, { PSO_RESOLVE, "ps_resolve", 0 },
            { PSO_PRESENT, "ps_present", 0 }, { PSO_OVERLAY, "ps_overlay", 5 },
        };
        for (i = 0; i < (int)(sizeof(list) / sizeof(list[0])); i++)
            if (!make_pso(d, list[i].idx, vs, list[i].ps, list[i].blend, err, n)) { nr_d3d_blob_release(vs); return false; }
    }
    nr_d3d_blob_release(vs);
    /* upload ring (persistently mapped) */
    memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = RING_SIZE; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(d->dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                    &IID_ID3D12Resource, (void **)&d->ring)) ||
        FAILED(ID3D12Resource_Map(d->ring, 0, NULL, (void **)&d->ring_map))) {
        snprintf(err, n, "upload ring"); return false;
    }
    if (!t_make(d, &d->upload, 1024, 512, -1, SRV_UPLOAD)) { snprintf(err, n, "upload texture"); return false; }
    nr_gpu_replay_init(&d->rp);
    d->ops.ctx = d;
    d->ops.draw = op_draw; d->ops.upload = op_upload; d->ops.fill = op_fill; d->ops.copy = op_copy; d->ops.refresh = op_refresh;
    b->max_vram_scale = 16;
    {
        LUID luid;
        d->dev->lpVtbl->GetAdapterLuid(d->dev, &luid);
        IDXGIFactory4 *f2 = NULL;
        char name[128] = "?";
        if (SUCCEEDED(mkfactory(&IID_IDXGIFactory4, (void **)&f2))) {
            IDXGIAdapter1 *ad = NULL;
            UINT k;
            for (k = 0; IDXGIFactory4_EnumAdapters1(f2, k, &ad) == S_OK; k++) {
                DXGI_ADAPTER_DESC1 ds;
                if (SUCCEEDED(IDXGIAdapter1_GetDesc1(ad, &ds)) && ds.AdapterLuid.LowPart == luid.LowPart && ds.AdapterLuid.HighPart == luid.HighPart)
                    WideCharToMultiByte(CP_UTF8, 0, ds.Description, -1, name, sizeof(name), NULL, NULL);
                IDXGIAdapter1_Release(ad);
            }
            IDXGIFactory4_Release(f2);
        }
        snprintf(b->description, sizeof(b->description), "Direct3D 12 (feature level 11_0), %s", name);
    }
    if (!d12_configure(b, nr_gfx_fit(s, b->max_vram_scale, &fit))) { snprintf(err, n, "initial configuration not supported"); return false; }
    return true;
}

static bool d12_configure(nr_vbackend *b, const nr_gfx_settings *s)
{
    d12 *d = (d12 *)b->impl;
    int S = s->internal_scale < 1 ? 1 : s->internal_scale, ax, ay;
    nr_gfx_aa_grid(s->antialias, &ax, &ay);
    if (S * ax > b->max_vram_scale || S * ay > b->max_vram_scale) return false;
    submit(d);
    if (!t_make(d, &d->vram, 1024 * S * ax, 512 * S * ay, RTV_VRAM, SRV_VRAM) ||
        !t_make(d, &d->vread, 1024 * S * ax, 512 * S * ay, -1, SRV_VREAD)) {
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

static void d12_sync(nr_vbackend *b, const u16 *vram)
{
    d12 *d = (d12 *)b->impl;
    begin(d);
    nr_gpu_sync(&d->rp, vram, &d->ops);
    submit(d);
}

static void d12_execute(nr_vbackend *b, const nr_rlist *l)
{
    d12 *d = (d12 *)b->impl;
    begin(d);
    nr_gpu_replay(&d->rp, l, &d->s, d->sx, d->sy, &d->ops);
    submit(d);
}

static bool resize(d12 *d, int w, int h)
{
    int i;
    submit(d);
    for (i = 0; i < NBB; i++) if (d->bb[i]) { ID3D12Resource_Release(d->bb[i]); d->bb[i] = NULL; }
    if (FAILED(IDXGISwapChain3_ResizeBuffers(d->sc, NBB, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0))) return false;
    d->bb_w = w; d->bb_h = h;
    return get_backbuffers(d);
}

static void d12_present(nr_vbackend *b, const nr_vpresent *p)
{
    d12 *d = (d12 *)b->impl;
    int src = -1, sw = 0, sh = 0, x, y, w, h, cur;
    target_info tbb, tint;
    float black[4] = { 0, 0, 0, 1 };
    bool cap = b->capture_next;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    if (p->win_w > 0 && p->win_h > 0 && (p->win_w != d->bb_w || p->win_h != d->bb_h)) resize(d, p->win_w, p->win_h);
    cur = (int)IDXGISwapChain3_GetCurrentBackBufferIndex(d->sc);
    if (!d->bb[cur]) return;
    begin(d);
    if (p->use_hires && p->display && !p->display->rgb24) {
        int ow = (p->display->width > 0 ? p->display->width : 320) * d->S, oh = (p->display->height > 0 ? p->display->height : 240) * d->S;
        if (p->display->enabled && t_make(d, &d->internal, ow, oh, RTV_INTERNAL, SRV_INTERNAL)) {
            if (!d->recording) begin(d);
            barrier(d, &d->vram, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            barrier(d, &d->internal, D3D12_RESOURCE_STATE_RENDER_TARGET);
            tint.rtv = RTV_INTERNAL; tint.w = ow; tint.h = oh;
            d->cbd.resolve[0] = (float)(p->display->disp_x * (u32)d->sx);
            d->cbd.resolve[1] = (float)(p->display->disp_y * (u32)d->sy);
            d->cbd.resolve[2] = (float)d->ax; d->cbd.resolve[3] = (float)d->ay;
            set_target(d, RTV_INTERNAL, ow, oh);
            quad(d, PSO_RESOLVE, SRV_VRAM, 0, 0, ow, oh, 0, 0, &tint);
            barrier(d, &d->internal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            barrier(d, &d->vram, D3D12_RESOURCE_STATE_RENDER_TARGET);
            src = SRV_INTERNAL; sw = ow; sh = oh;
        }
    } else if (p->picture && p->pic_w > 0 && p->pic_h > 0) {
        if (t_make(d, &d->picture, p->pic_w, p->pic_h, -1, SRV_PICTURE)) {
            if (!d->recording) begin(d);
            upload_region(d, &d->picture, 0, 0, p->pic_w, p->pic_h, p->picture, p->pic_w);
            src = SRV_PICTURE; sw = p->pic_w; sh = p->pic_h;
        }
    }
    if (p->overlay && p->overlay->px && t_make(d, &d->overlay, p->overlay->w, p->overlay->h, -1, SRV_OVERLAY)) {
        if (!d->recording) begin(d);
        upload_region(d, &d->overlay, 0, 0, p->overlay->w, p->overlay->h, p->overlay->px, p->overlay->w);
    }
    if (!d->recording) begin(d);
    barrier_res(d, d->bb[cur], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    tbb.rtv = RTV_BB0 + cur; tbb.w = d->bb_w; tbb.h = d->bb_h;
    set_target(d, tbb.rtv, tbb.w, tbb.h);
    ID3D12GraphicsCommandList_ClearRenderTargetView(d->cl, cpu_handle(d->rtv_heap, d->rtv_inc, tbb.rtv), black, 0, NULL);
    if (src >= 0) {
        nr_video_dest_rect(&d->s, d->bb_w, d->bb_h, sw, sh, p->native_w, p->native_h, &x, &y, &w, &h);
        d->cbd.psize[0] = (float)sw; d->cbd.psize[1] = (float)sh; d->cbd.psize[2] = (float)w; d->cbd.psize[3] = (float)h;
        d->cbd.pfx[0] = (float)(p->native_h > 0 ? p->native_h : sh);
        d->cbd.pfx[1] = (float)d->s.post_fx; d->cbd.pfx[2] = d->s.post_fx_strength; d->cbd.pfx[3] = (float)d->s.tonemap;
        d->cbd.pgrade[0] = d->s.brightness; d->cbd.pgrade[1] = d->s.contrast; d->cbd.pgrade[2] = d->s.saturation; d->cbd.pgrade[3] = d->s.gamma;
        d->cbd.pmisc[0] = (float)(p->frame & 0xFFFF); d->cbd.pmisc[1] = (float)d->s.scaling; d->cbd.pmisc[2] = (float)x; d->cbd.pmisc[3] = (float)y;
        quad(d, PSO_PRESENT, src, x, y, x + w, y + h, 0, 0, &tbb);
    }
    if (p->overlay && p->overlay->px && d->overlay.res)
        quad(d, PSO_OVERLAY, SRV_OVERLAY, 0, 0, d->bb_w, d->bb_h, (float)p->overlay->w, (float)p->overlay->h, &tbb);
    if (cap) {
        D3D12_RESOURCE_DESC bd;
        UINT64 total = 0;
        D3D12_TEXTURE_COPY_LOCATION dst, srcl;
        d->bb[cur]->lpVtbl->GetDesc(d->bb[cur], &bd);
        ID3D12Device_GetCopyableFootprints(d->dev, &bd, 0, 1, 0, &fp, NULL, NULL, &total);
        if (!d->readback || d->readback_size < total) {
            D3D12_HEAP_PROPERTIES hp;
            D3D12_RESOURCE_DESC rd;
            if (d->readback) ID3D12Resource_Release(d->readback);
            d->readback = NULL;
            memset(&hp, 0, sizeof(hp)); hp.Type = D3D12_HEAP_TYPE_READBACK;
            memset(&rd, 0, sizeof(rd));
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = total; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(ID3D12Device_CreateCommittedResource(d->dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                            &IID_ID3D12Resource, (void **)&d->readback)))
                cap = false;
            else d->readback_size = total;
        }
        if (cap) {
            memset(&dst, 0, sizeof(dst)); memset(&srcl, 0, sizeof(srcl));
            dst.pResource = d->readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
            srcl.pResource = d->bb[cur]; srcl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            barrier_res(d, d->bb[cur], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(d->cl, &dst, 0, 0, 0, &srcl, NULL);
            barrier_res(d, d->bb[cur], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        }
    }
    if (!cap) barrier_res(d, d->bb[cur], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    submit(d);
    if (cap) {
        u8 *m = NULL;
        D3D12_RANGE r;
        r.Begin = 0; r.End = (SIZE_T)d->readback_size;
        b->capture_next = false;
        if (SUCCEEDED(ID3D12Resource_Map(d->readback, 0, &r, (void **)&m))) {
            int yy, xx;
            u32 *px = (u32 *)realloc(b->cap_px, (size_t)d->bb_w * d->bb_h * 4);
            if (px) {
                b->cap_px = px;
                for (yy = 0; yy < d->bb_h; yy++) {
                    const u32 *s = (const u32 *)(m + fp.Offset + (size_t)yy * fp.Footprint.RowPitch);
                    for (xx = 0; xx < d->bb_w; xx++) px[(size_t)yy * d->bb_w + xx] = s[xx] | 0xFF000000u;
                }
                b->cap_w = d->bb_w; b->cap_h = d->bb_h;
            }
            r.End = 0;
            ID3D12Resource_Unmap(d->readback, 0, &r);
        }
    }
    IDXGISwapChain3_Present(d->sc, d->s.vsync ? 1 : 0, 0);
}

static void d12_shutdown(nr_vbackend *b)
{
    d12 *d = (d12 *)b->impl;
    int i;
    if (!d) return;
    if (d->queue && d->fence) { submit(d); wait_idle(d); }
    t_free(&d->vram); t_free(&d->vread); t_free(&d->internal); t_free(&d->upload); t_free(&d->picture); t_free(&d->overlay);
#define REL(x, T) if (x) T##_Release(x)
    for (i = 0; i < PSO_COUNT; i++) REL(d->pso[i], ID3D12PipelineState);
    for (i = 0; i < NBB; i++) REL(d->bb[i], ID3D12Resource);
    if (d->ring && d->ring_map) ID3D12Resource_Unmap(d->ring, 0, NULL);
    REL(d->ring, ID3D12Resource); REL(d->readback, ID3D12Resource);
    REL(d->rs, ID3D12RootSignature); REL(d->srv_heap, ID3D12DescriptorHeap); REL(d->rtv_heap, ID3D12DescriptorHeap);
    REL(d->cl, ID3D12GraphicsCommandList); REL(d->alloc, ID3D12CommandAllocator); REL(d->fence, ID3D12Fence);
    REL(d->sc, IDXGISwapChain3); REL(d->queue, ID3D12CommandQueue); REL(d->dev, ID3D12Device);
#undef REL
    if (d->fence_ev) CloseHandle(d->fence_ev);
    nr_gpu_replay_free(&d->rp);
    free(d);
    free(b->cap_px);
    b->cap_px = NULL;
    b->impl = NULL;
    (void)g_target_dummy;
}

bool nr_vb_d3d12_create(nr_vbackend *b)
{
    memset(b, 0, sizeof(*b));
    b->name = "d3d12";
    b->gpu_hires = true;
    b->max_vram_scale = 16;
    b->init = d12_init;
    b->configure = d12_configure;
    b->sync = d12_sync;
    b->execute = d12_execute;
    b->present = d12_present;
    b->shutdown = d12_shutdown;
    return true;
}
