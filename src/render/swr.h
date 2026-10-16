/* Software enhanced renderer: replays a render command list into a
 * high-resolution copy of VRAM (internal resolution x supersampling) and
 * produces the displayed picture. Portable C, multi-threaded by bands. */
#ifndef NR_SWR_H
#define NR_SWR_H

#include "rlist.h"
#include "gfx.h"

typedef struct nr_swr nr_swr;

nr_swr *nr_swr_new(int threads);
void nr_swr_free(nr_swr *r);
/* Applies settings; reallocates the high-resolution VRAM when the scale
 * changes (then nr_swr_sync must be called). Returns false on allocation
 * failure (the previous configuration is kept). */
bool nr_swr_configure(nr_swr *r, const nr_gfx_settings *s, bool *needs_sync);
/* Rebuilds the high-resolution VRAM from the console VRAM (1024x512). */
void nr_swr_sync(nr_swr *r, const u16 *vram);
void nr_swr_execute(nr_swr *r, const nr_rlist *l);
/* Picture at internal resolution (display size x internal_scale), resolved
 * from the supersampled VRAM. The buffer belongs to the renderer. */
const u32 *nr_swr_output(nr_swr *r, const nr_rlist *l, int *w, int *h);
/* Raw access for tests: high-resolution pixel (R | G<<8 | B<<16 | mask<<24). */
u32 nr_swr_pixel(const nr_swr *r, int hx, int hy);
void nr_swr_scale(const nr_swr *r, int *sx, int *sy);
u64 nr_swr_barriers(const nr_swr *r);

#endif
