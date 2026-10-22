#include "sysinfo.h"
#include "../render/gfx.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>

static void query_gpu(nr_sysinfo *si)
{
    typedef HRESULT(WINAPI *pfn_create)(REFIID, void **);
    static const GUID iid_factory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    HMODULE lib = LoadLibraryA("dxgi.dll");
    pfn_create create;
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *ad = NULL;
    DXGI_ADAPTER_DESC1 desc;
    UINT i;
    if (!lib) return;
    create = (pfn_create)(void *)GetProcAddress(lib, "CreateDXGIFactory1");
    if (create && SUCCEEDED(create(&iid_factory1, (void **)&fac)) && fac) {
        for (i = 0; IDXGIFactory1_EnumAdapters1(fac, i, &ad) != DXGI_ERROR_NOT_FOUND; i++) {
            if (SUCCEEDED(IDXGIAdapter1_GetDesc1(ad, &desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, si->gpu_name, (int)sizeof(si->gpu_name), NULL, NULL);
                si->gpu_vram = (u64)desc.DedicatedVideoMemory;
                IDXGIAdapter1_Release(ad);
                break;
            }
            IDXGIAdapter1_Release(ad);
        }
        IDXGIFactory1_Release(fac);
    }
    FreeLibrary(lib);
}

bool nr_sysinfo_query(nr_sysinfo *si)
{
    MEMORYSTATUSEX ms;
    SYSTEM_INFO sys;
    memset(si, 0, sizeof(*si));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) si->ram_total = (u64)ms.ullTotalPhys;
    GetSystemInfo(&sys);
    si->cpu_threads = (int)sys.dwNumberOfProcessors;
    query_gpu(si);
    return si->ram_total != 0;
}
#else
#include <unistd.h>

static u64 read_u64_file(const char *path)
{
    FILE *f = fopen(path, "r");
    unsigned long long v = 0;
    if (!f) return 0;
    if (fscanf(f, "%llu", &v) != 1) v = 0;
    fclose(f);
    return (u64)v;
}

bool nr_sysinfo_query(nr_sysinfo *si)
{
    long pages = sysconf(_SC_PHYS_PAGES), psize = sysconf(_SC_PAGESIZE), cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int card;
    memset(si, 0, sizeof(*si));
    if (pages > 0 && psize > 0) si->ram_total = (u64)pages * (u64)psize;
    si->cpu_threads = cpus > 0 ? (int)cpus : 1;
    for (card = 0; card < 8 && !si->gpu_vram; card++) {
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_total", card);
        si->gpu_vram = read_u64_file(path);
    }
    return si->ram_total != 0;
}
#endif

int nr_sysinfo_scale_limit(const nr_sysinfo *si, bool software_renderer)
{
    return nr_gfx_memory_scale_limit(nr_gfx_render_budget(si->ram_total, si->gpu_vram, !software_renderer), software_renderer ? 2u : 3u);
}

void nr_sysinfo_describe(const nr_sysinfo *si, char *out, size_t n)
{
    unsigned long long ram = (unsigned long long)(si->ram_total >> 20), vram = (unsigned long long)(si->gpu_vram >> 20);
    if (si->gpu_vram)
        snprintf(out, n, "%d CPU threads, %llu MiB RAM, GPU %s (%llu MiB)", si->cpu_threads, ram, si->gpu_name[0] ? si->gpu_name : "unknown", vram);
    else
        snprintf(out, n, "%d CPU threads, %llu MiB RAM, GPU memory unknown", si->cpu_threads, ram);
}
