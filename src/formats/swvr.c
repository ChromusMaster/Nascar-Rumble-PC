/* EA SWVR audio streams (.AV files, also embedded in .TRK/GLBLDATA.PSX).
 * Reverse engineered from the user's disc (see docs/FORMATS.md):
 *
 *  The file is a sequence of 0x6000-byte aligned groups:
 *    SWVR  (0x24 bytes)  +0x14: stream file name, e.g. "Showboat.stream"
 *    VAGB  mono clip:    +0x08 u32 2, +0x10 FourCC "VAGB", +0x14 u16 1,u16 1,
 *                        data from +0x18: SPU-ADPCM 16-byte blocks, the last
 *                        block carries the loop-end flag (1)
 *    VAGM  music block:  +0x14 u16 total chunk count, u16 chunk index,
 *                        +0x18 u32 bytes per channel; data from +0x1C:
 *                        left channel bytes then right channel bytes
 *    FILL  padding to the next 0x6000 boundary (4-byte tag only if <8 left)
 *
 *  The playback rate is chosen by the game when it programs the SPU; it is
 *  not stored in these headers. nrtool writes 22050 Hz by default (the rate
 *  the game uses for its WVE movie audio, SPU pitch 0800h); see docs/STATUS.md. */
#include "formats.h"

#include <stdlib.h>
#include <string.h>

static void fourcc(const u8 *p, char out[5])
{
    out[0] = (char)p[3]; out[1] = (char)p[2]; out[2] = (char)p[1]; out[3] = (char)p[0]; out[4] = 0;
}

typedef void (*swvr_cb)(void *user, const char *stream, const char *tag, const u8 *chunk, u32 size);

static int walk(const u8 *d, u32 len, swvr_cb cb, void *user, FILE *log)
{
    u32 off = 0;
    char tag[5], name[64] = "";
    while (off + 4 <= len) {
        u32 sz;
        fourcc(d + off, tag);
        if (strcmp(tag, "FILL") == 0) {
            u32 tb = 0x6000 - (off % 0x6000);
            sz = tb < 8 ? tb : nr_rd32(d + off + 4);
            if (sz < 4 || off + sz > len) break;
            off += sz;
            continue;
        }
        if (off + 8 > len) break;
        sz = nr_rd32(d + off + 4);
        if (sz < 8 || off + sz > len) { if (log) fprintf(log, "%08X invalid chunk '%s'\n", off, tag); return 1; }
        if (strcmp(tag, "SWVR") == 0) {
            u32 k;
            memset(name, 0, sizeof(name));
            for (k = 0; k < 63 && 0x14 + k < sz && d[off + 0x14 + k]; k++) name[k] = (char)d[off + 0x14 + k];
        }
        cb(user, name, tag, d + off, sz);
        off += sz;
    }
    return 0;
}

static void dump_cb(void *user, const char *stream, const char *tag, const u8 *c, u32 sz)
{
    FILE *out = (FILE *)user;
    if (strcmp(tag, "SWVR") == 0) fprintf(out, "SWVR  %s\n", stream);
    else if (strcmp(tag, "VAGB") == 0) fprintf(out, "  VAGB mono  %6u bytes ADPCM (%u samples)\n", sz - 0x18, (sz - 0x18) / 16 * 28);
    else if (strcmp(tag, "VAGM") == 0)
        fprintf(out, "  VAGM block %u/%u, %u bytes per channel\n", nr_rd16(c + 0x16) + 1, nr_rd16(c + 0x14), nr_rd32(c + 0x18));
    else fprintf(out, "  %-4s %u bytes\n", tag, sz);
}

int nr_swvr_dump(const u8 *data, u32 len, FILE *out)
{
    return walk(data, len, dump_cb, out, out);
}

typedef struct {
    FILE *f;
    u32 frames;
    nr_adpcm_state st[2];
    char cur[64];
} wav_ctx;

static void wav_cb(void *user, const char *stream, const char *tag, const u8 *c, u32 sz)
{
    wav_ctx *w = (wav_ctx *)user;
    s16 a[28], b[28];
    u32 i;
    if (strcmp(tag, "SWVR") == 0) {
        if (strcmp(stream, w->cur) != 0) { memset(w->st, 0, sizeof(w->st)); nr_strlcpy(w->cur, stream, sizeof(w->cur)); }
        return;
    }
    if (strcmp(tag, "VAGB") == 0) {
        for (i = 0x18; i + 16 <= sz; i += 16) {
            int k;
            s16 st[56];
            nr_adpcm_decode_block(c + i, true, &w->st[0], a);
            for (k = 0; k < 28; k++) { st[k * 2] = a[k]; st[k * 2 + 1] = a[k]; }
            fwrite(st, 2, 56, w->f);
            w->frames += 28;
        }
    } else if (strcmp(tag, "VAGM") == 0) {
        u32 per = nr_rd32(c + 0x18);
        const u8 *l = c + 0x1C, *r = c + 0x1C + per;
        if (0x1C + per * 2 > sz) return;
        for (i = 0; i + 16 <= per; i += 16) {
            int k;
            s16 st[56];
            nr_adpcm_decode_block(l + i, true, &w->st[0], a);
            nr_adpcm_decode_block(r + i, true, &w->st[1], b);
            for (k = 0; k < 28; k++) { st[k * 2] = a[k]; st[k * 2 + 1] = b[k]; }
            fwrite(st, 2, 56, w->f);
            w->frames += 28;
        }
    }
}

int nr_swvr_to_wav(const u8 *data, u32 len, const char *wav_path, FILE *log)
{
    wav_ctx w;
    u8 h[44];
    u32 rate = 22050;
    memset(&w, 0, sizeof(w));
    w.f = nr_fopen(wav_path, "wb");
    if (!w.f) { if (log) fprintf(log, "ERROR: could not create %s\n", wav_path); return 1; }
    memset(h, 0, sizeof(h));
    fwrite(h, 1, 44, w.f);
    walk(data, len, wav_cb, &w, log);
    memcpy(h, "RIFF", 4); nr_wr32(h + 4, 36 + w.frames * 4); memcpy(h + 8, "WAVEfmt ", 8);
    nr_wr32(h + 16, 16); nr_wr16(h + 20, 1); nr_wr16(h + 22, 2); nr_wr32(h + 24, rate);
    nr_wr32(h + 28, rate * 4); nr_wr16(h + 32, 4); nr_wr16(h + 34, 16);
    memcpy(h + 36, "data", 4); nr_wr32(h + 40, w.frames * 4);
    fseek(w.f, 0, SEEK_SET);
    fwrite(h, 1, 44, w.f);
    fclose(w.f);
    if (log) fprintf(log, "%u stereo samples written to %s (%u Hz)\n", w.frames, wav_path, rate);
    return 0;
}
