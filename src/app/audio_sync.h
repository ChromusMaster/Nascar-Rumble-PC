/* Audio/video synchronisation for real-time frontends.
 *
 * The emulated machine produces 44100 Hz stereo samples per emulated second
 * and the frontend paces video at the console's refresh rate (59.817 Hz).
 * The host audio clock never matches that exactly, so the output queue slowly
 * grows or drains. Dropping or repeating whole blocks is audible ("fast" or
 * stuttering audio, A/V drift in movies). Instead the stream is resampled by
 * a tiny, smoothly varying ratio (at most +/-0.5 %, inaudible) that keeps
 * the queue near a target latency ("dynamic rate control"). Nothing about the
 * emulated machine changes. */
#ifndef NR_AUDIO_SYNC_H
#define NR_AUDIO_SYNC_H

#include "../common/common.h"

typedef struct {
    double pos;          /* fractional read position into the pending input */
    s16 prev[2];         /* last input frame of the previous block */
    double ratio;        /* current input/output ratio */
    u32 target;          /* target queued output frames */
    u64 dropped, produced, consumed;
} nr_audio_sync;

void nr_audio_sync_init(nr_audio_sync *s, u32 target_frames);
/* Resamples `n` stereo frames from `in` given the number of frames currently
 * queued at the device. Returns frames written to `out` (capacity `cap`). */
u32 nr_audio_sync_process(nr_audio_sync *s, const s16 *in, u32 n, u32 queued, s16 *out, u32 cap);

#endif
