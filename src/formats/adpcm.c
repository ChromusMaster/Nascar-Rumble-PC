/* SPU-ADPCM block decoder (same algorithm as the SPU voices, without pitch
 * interpolation). Used to decode EA audio streams natively. */
#include "formats.h"

static const s32 k_pos[5] = { 0, 60, 115, 98, 122 };
static const s32 k_neg[5] = { 0, 0, -52, -55, -60 };

u8 nr_adpcm_decode_block(const u8 *blk, bool has_flags, nr_adpcm_state *st, s16 out[28])
{
    int shift = blk[0] & 0xF, filter = (blk[0] >> 4) & 7, i;
    const u8 *d = blk + (has_flags ? 2 : 1);
    u8 flags = has_flags ? blk[1] : 0;
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 4;
    for (i = 0; i < 28; i++) {
        s32 nib = (d[i / 2] >> ((i & 1) * 4)) & 0xF;
        s32 s = ((s32)(s16)(nib << 12)) >> shift;
        s += (st->h1 * k_pos[filter] + st->h2 * k_neg[filter]) >> 6;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        st->h2 = st->h1;
        st->h1 = s;
        out[i] = (s16)s;
    }
    return flags;
}
