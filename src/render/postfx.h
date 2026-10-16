/* Software presentation pass: scales the rendered picture to the output
 * size with the selected filter and applies the look effects, tone mapping
 * and colour adjustments (same formulas as the Direct3D shaders). */
#ifndef NR_POSTFX_H
#define NR_POSTFX_H

#include "gfx.h"

struct nr_pool;

/* True when the frame needs this pass (otherwise the frontend can let the
 * platform scale the picture directly). */
bool nr_postfx_needed(const nr_gfx_settings *s);
/* src: sw x sh RGBA (R in the low byte). native_h: displayed lines of the
 * console picture (for scanline effects). dst: dw x dh. */
void nr_postfx_render(const nr_gfx_settings *s, const u32 *src, int sw, int sh, int native_h,
                      u32 *dst, int dw, int dh, u32 frame, struct nr_pool *pool);

#endif
