#ifndef NR_SYSINFO_H
#define NR_SYSINFO_H

#include "../common/common.h"

typedef struct {
    u64 ram_total;
    int cpu_threads;
    char gpu_name[128];
    u64 gpu_vram;
} nr_sysinfo;

bool nr_sysinfo_query(nr_sysinfo *si);
int nr_sysinfo_scale_limit(const nr_sysinfo *si, bool software_renderer);
void nr_sysinfo_describe(const nr_sysinfo *si, char *out, size_t n);

#endif
