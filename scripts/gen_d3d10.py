import os, re, sys
# Generates src/platform/win32/vb_d3d10.c from vb_d3d11.c (Direct3D 10.1 has the
# same model without device contexts). Run from the repository root.
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'platform', 'win32'))
s=open('vb_d3d11.c').read()
s=s.replace('/* Direct3D 11 video backend.','/* Direct3D 10.1 video backend: the same pipeline as vb_d3d11.c (shader\n * model 4.0, feature levels 10.1/10.0). Generated from vb_d3d11.c by\n * scripts/gen_d3d10.py - keep both in sync.\n *\n * Description of the shared pipeline:')
s=s.replace('#include <d3d11.h>','#include <d3d10_1.h>')
s=s.replace('''typedef HRESULT(WINAPI *pfn_D3D11CreateDeviceAndSwapChain)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT,
                                                           UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D11Device **,
                                                           D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);''','''typedef HRESULT(WINAPI *pfn_D3D10CreateDeviceAndSwapChain1)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1,
                                                            UINT, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **, ID3D10Device1 **);''')
# device creation block (before renames)
a=s.index('    lib = LoadLibraryA("d3d11.dll");'); b=s.index('    d->bb_w = (int)sd.BufferDesc.Width;')
s=s[:a]+'''    lib = LoadLibraryA("d3d10_1.dll");
    if (!lib) { snprintf(err, n, "d3d10_1.dll not available"); return false; }
    create = (pfn_D3D10CreateDeviceAndSwapChain1)(void *)GetProcAddress(lib, "D3D10CreateDeviceAndSwapChain1");
    if (!create) { snprintf(err, n, "D3D10CreateDeviceAndSwapChain1 missing"); return false; }
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
    {
        static const D3D10_FEATURE_LEVEL1 lv[] = { D3D10_FEATURE_LEVEL_10_1, D3D10_FEATURE_LEVEL_10_0 };
        ID3D10Device1 *dev1 = NULL;
        hr = E_FAIL;
        for (i = 0; i < 2 && FAILED(hr); i++) {
            hr = create(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, lv[i], D3D10_1_SDK_VERSION, &sd, &d->sc, &dev1);
            if (SUCCEEDED(hr)) d->fl = lv[i];
        }
        if (FAILED(hr)) {
            snprintf(err, n, "D3D10CreateDeviceAndSwapChain1 failed (0x%08lX)", (unsigned long)hr);
            return false;
        }
        d->dev = (ID3D11Device *)dev1;
    }
'''+s[b:]
s=s.replace('    pfn_D3D11CreateDeviceAndSwapChain create;','    pfn_D3D10CreateDeviceAndSwapChain1 create;')
s=s.replace('    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };\n','')
s=s.replace('    D3D_FEATURE_LEVEL fl;','    D3D10_FEATURE_LEVEL1 fl;')
s=s.replace('    b->max_vram_scale = d->fl >= D3D_FEATURE_LEVEL_11_0 ? 16 : 8;','    b->max_vram_scale = 8;   /* 8192x8192 textures on feature level 10.x */')
s=s.replace('"Direct3D 11 (feature level %d_%d), %s"','"Direct3D 10.1 (feature level %d_%d), %s"')
s=s.replace('    ID3D11DeviceContext *ctx;\n','')
# context calls -> device
s=re.sub(r'ID3D11DeviceContext_(\w+)\(d->ctx,', r'ID3D11Device_\1(d->dev,', s)
s=s.replace('''    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11Device_Map(d->dev, (ID3D11Resource *)d->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &d->cbd, sizeof(d->cbd));
        ID3D11Device_Unmap(d->dev, (ID3D11Resource *)d->cb, 0);
    }''','''    void *m;
    if (SUCCEEDED(ID3D11Buffer_Map(d->cb, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m, &d->cbd, sizeof(d->cbd));
        ID3D11Buffer_Unmap(d->cb);
    }''')
s=s.replace('''    D3D11_MAPPED_SUBRESOURCE m;
    UINT bytes''','''    void *m;
    UINT bytes''')
s=s.replace('''    if (FAILED(ID3D11Device_Map(d->dev, (ID3D11Resource *)d->vb, 0, d->vb_pos ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    memcpy((u8 *)m.pData + d->vb_pos, v, bytes);
    ID3D11Device_Unmap(d->dev, (ID3D11Resource *)d->vb, 0);''','''    if (FAILED(ID3D11Buffer_Map(d->vb, d->vb_pos ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    memcpy((u8 *)m + d->vb_pos, v, bytes);
    ID3D11Buffer_Unmap(d->vb);''')
s=s.replace('ID3D11Device_PSSetShader(d->dev, ps, NULL, 0);','ID3D11Device_PSSetShader(d->dev, ps);')
s=s.replace('ID3D11Device_VSSetShader(d->dev, d->vs, NULL, 0);','ID3D11Device_VSSetShader(d->dev, d->vs);')
s=s.replace('hr = ID3D11Device_CreatePixelShader(d->dev, nr_d3d_blob_ptr(blob), nr_d3d_blob_size(blob), NULL, ps);','hr = ID3D11Device_CreatePixelShader(d->dev, nr_d3d_blob_ptr(blob), nr_d3d_blob_size(blob), ps);')
s=s.replace('hr = ID3D11Device_CreateVertexShader(d->dev, nr_d3d_blob_ptr(vsb), nr_d3d_blob_size(vsb), NULL, &d->vs);','hr = ID3D11Device_CreateVertexShader(d->dev, nr_d3d_blob_ptr(vsb), nr_d3d_blob_size(vsb), &d->vs);')
s=s.replace('''    bd.RenderTarget[0].BlendEnable = on;
    bd.RenderTarget[0].SrcBlend = src;
    bd.RenderTarget[0].DestBlend = dst;
    bd.RenderTarget[0].BlendOp = op;
    bd.RenderTarget[0].SrcBlendAlpha = srca;
    bd.RenderTarget[0].DestBlendAlpha = dsta;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;''','''    bd.BlendEnable[0] = on;
    bd.SrcBlend = src;
    bd.DestBlend = dst;
    bd.BlendOp = op;
    bd.SrcBlendAlpha = srca;
    bd.DestBlendAlpha = dsta;
    bd.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTargetWriteMask[0] = D3D11_COLOR_WRITE_ENABLE_ALL;''')
s=s.replace('''    D3D11_MAPPED_SUBRESOURCE m;
    int y;''','''    D3D10_MAPPED_TEXTURE2D m;
    int y;''')
s=s.replace('''    if (FAILED(ID3D11Device_Map(d->dev, (ID3D11Resource *)d->staging, 0, D3D11_MAP_READ, 0, &m))) return;''','''    if (FAILED(ID3D11Texture2D_Map(d->staging, 0, D3D11_MAP_READ, 0, &m))) return;''')
s=s.replace('''    ID3D11Device_Unmap(d->dev, (ID3D11Resource *)d->staging, 0);''','''    ID3D11Texture2D_Unmap(d->staging, 0);''')
s=s.replace('    if (d->ctx) ID3D11DeviceContext_ClearState(d->ctx);','    if (d->dev) ID3D11Device_ClearState(d->dev);')
s=s.replace(' REL(d->ctx, ID3D11DeviceContext);','')
s=s.replace('rd.DepthClipEnable = TRUE;','rd.DepthClipEnable = TRUE;')
# generic renames
for a,b in [('ID3D11','ID3D10'),('D3D11_','D3D10_'),('IID_ID3D11','IID_ID3D10')]:
    s=s.replace(a,b)
s=s.replace('d11','d10')
s=s.replace('D3D10_SDK_VERSION','D3D10_1_SDK_VERSION')
s=s.replace('nr_vb_d3d10_create','nr_vb_d3d10_create')
s=s.replace('b->name = "d3d10";','b->name = "d3d10";')
s=s.replace('the same pipeline as vb_d3d10.c','the same pipeline as vb_d3d11.c').replace('Generated from vb_d3d10.c','Generated from vb_d3d11.c')
open('vb_d3d10.c','w').write(s)
