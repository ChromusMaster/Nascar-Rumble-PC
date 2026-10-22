#include "audio_sync.h"

#include <string.h>
#include <math.h>
#include <limits.h>

#define MAX_DEVIATION 0.005

void nr_audio_sync_init(nr_audio_sync *s, u32 target_frames)
{
    memset(s, 0, sizeof(*s));
    s->ratio = 1.0;
    s->target = target_frames ? target_frames : 2048;
}

u32 nr_audio_sync_process(nr_audio_sync *s, const s16 *in, u32 n, u32 queued, s16 *out, u32 cap)
{
    double err, want, step;
    u32 o = 0;
    if (!s || !in || (!out && cap) || !n || n > INT_MAX / 2 || !s->target) return 0;
    if (!isfinite(s->pos) || s->pos < -1.0 || s->pos > 1.0) s->pos = 0;
    if (!isfinite(s->ratio) || s->ratio < 1.0 - MAX_DEVIATION || s->ratio > 1.0 + MAX_DEVIATION) s->ratio = 1.0;
    s->consumed += n;
    /* Far too much queued (e.g. after the window was dragged and the
     * emulation caught up): the only remedy is to skip this block. */
    if ((u64)queued > (u64)s->target * 6) { s->dropped += n; return 0; }
    err = ((double)queued - (double)s->target) / (double)s->target;   /* >0: too much queued */
    if (err > 1.0) err = 1.0;
    if (err < -1.0) err = -1.0;
    want = 1.0 + err * MAX_DEVIATION;
    s->ratio += (want - s->ratio) * 0.05;                               /* smooth changes */
    step = s->ratio;
    /* Linear interpolation; input index i refers to in[i], index -1 to prev. */
    while (o < cap) {
        double p = s->pos;
        int i0 = (int)floor(p);
        double f = p - (double)i0;
        s16 a[2], b[2];
        int ch;
        if (i0 + 1 >= (int)n) break;
        if (i0 < 0) { a[0] = s->prev[0]; a[1] = s->prev[1]; }
        else { a[0] = in[i0 * 2]; a[1] = in[i0 * 2 + 1]; }
        b[0] = in[(i0 + 1) * 2]; b[1] = in[(i0 + 1) * 2 + 1];
        for (ch = 0; ch < 2; ch++) {
            double v = (double)a[ch] + ((double)b[ch] - (double)a[ch]) * f;
            out[(size_t)o * 2 + (size_t)ch] = (s16)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
        o++;
        s->pos += step;
    }
    s->pos -= (double)n;      /* now relative to the next block (>= -1) */
    if (s->pos < -1.0) { s->dropped += (u64)(-1.0 - s->pos); s->pos = -1.0; }
    s->prev[0] = in[(n - 1) * 2];
    s->prev[1] = in[(n - 1) * 2 + 1];
    s->produced += o;
    return o;
}
