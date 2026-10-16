#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d3d_common.h"

/* ------------------------------------------------------------ compiler */

typedef HRESULT(WINAPI *pfn_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                        UINT, UINT, ID3DBlob **, ID3DBlob **);
static pfn_D3DCompile g_compile;

void *nr_d3d_compile(const char *src, const char *entry, const char *target, char *err, size_t errlen)
{
    ID3DBlob *code = NULL, *msg = NULL;
    HRESULT hr;
    if (!g_compile) {
        static const char *dlls[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
        size_t i;
        for (i = 0; i < sizeof(dlls) / sizeof(dlls[0]) && !g_compile; i++) {
            HMODULE m = LoadLibraryA(dlls[i]);
            if (m) g_compile = (pfn_D3DCompile)(void *)GetProcAddress(m, "D3DCompile");
        }
        if (!g_compile) {
            snprintf(err, errlen, "d3dcompiler_47.dll not found (install the DirectX runtime or update Windows)");
            return NULL;
        }
    }
    hr = g_compile(src, strlen(src), "nrengine", NULL, NULL, entry, target, (1 << 15) /* OPTIMIZATION_LEVEL3 */, 0, &code, &msg);
    if (FAILED(hr)) {
        snprintf(err, errlen, "failed to compile %s (%s): %.400s", entry, target, msg ? (const char *)ID3D10Blob_GetBufferPointer(msg) : "?");
        if (msg) ID3D10Blob_Release(msg);
        if (code) ID3D10Blob_Release(code);
        return NULL;
    }
    if (msg) ID3D10Blob_Release(msg);
    return code;
}

const void *nr_d3d_blob_ptr(void *b) { return ID3D10Blob_GetBufferPointer((ID3DBlob *)b); }
size_t nr_d3d_blob_size(void *b) { return ID3D10Blob_GetBufferSize((ID3DBlob *)b); }
void nr_d3d_blob_release(void *b) { if (b) ID3D10Blob_Release((ID3DBlob *)b); }

