/* Shared part of the Direct3D backends: shader compilation (D3DCompile from
 * d3dcompiler_47.dll, loaded at run time). The command-list replay lives in
 * src/render/gpu_replay.c (also used by OpenGL). */
#ifndef NR_D3D_COMMON_H
#define NR_D3D_COMMON_H

#include "../../render/gpu_replay.h"

/* Shader compiler. Returns a blob pointer (ID3DBlob*) or NULL with a message. */
void *nr_d3d_compile(const char *src, const char *entry, const char *target, char *err, size_t errlen);
const void *nr_d3d_blob_ptr(void *blob);
size_t nr_d3d_blob_size(void *blob);
void nr_d3d_blob_release(void *blob);

#endif
