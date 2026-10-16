/* BS (MDEC bitstream) decoder: standard Sony v1/v2 Huffman codes and the
 * Electronic Arts variant (custom MDEC values from a VLC0 chunk).
 * Reference: psx-spx "CDROM File Video BS Compression". */
#include "formats.h"

#include <stdlib.h>
#include <string.h>

/* The 224 huffman codes in EA VLC0 index order, as (prefix, xbits, indices). */
typedef struct { const char *prefix; int xbits; u8 idx[32]; } code_group;

static const code_group k_groups[] = {
    { "10", 0, { 0x00 } },
    { "11", 1, { 0x01, 0x02 } },
    { "011", 1, { 0x03, 0x04 } },
    { "010", 2, { 0x05, 0x06, 0x07, 0x08 } },
    { "0011", 2, { 0x0D, 0x0E, 0x0B, 0x0C } },
    { "00101", 1, { 0x09, 0x0A } },
    { "00100", 4, { 0x2E, 0x2F, 0x22, 0x23, 0x2C, 0x2D, 0x2A, 0x2B, 0x26, 0x27, 0x24, 0x25, 0x20, 0x21, 0x28, 0x29 } },
    { "0001", 3, { 0x15, 0x16, 0x13, 0x14, 0x0F, 0x10, 0x11, 0x12 } },
    { "00001", 3, { 0x1A, 0x1B, 0x1E, 0x1F, 0x18, 0x19, 0x1C, 0x1D } },
    { "000001", 0, { 0x17 } },
    { "0000001", 4, { 0x3E, 0x3F, 0x38, 0x39, 0x30, 0x31, 0x34, 0x35, 0x32, 0x33, 0x3C, 0x3D, 0x3A, 0x3B, 0x36, 0x37 } },
    { "00000001", 5, { 0x46, 0x47, 0x54, 0x55, 0x4E, 0x4F, 0x44, 0x45, 0x4A, 0x4B, 0x52, 0x53, 0x5E, 0x5F, 0x5C, 0x5D,
                       0x42, 0x43, 0x5A, 0x5B, 0x58, 0x59, 0x48, 0x49, 0x4C, 0x4D, 0x40, 0x41, 0x50, 0x51, 0x56, 0x57 } },
    { "000000001", 5, { 0x74, 0x75, 0x72, 0x73, 0x70, 0x71, 0x6E, 0x6F, 0x6C, 0x6D, 0x6A, 0x6B, 0x68, 0x69, 0x66, 0x67,
                        0x64, 0x65, 0x62, 0x63, 0x60, 0x61, 0x7E, 0x7F, 0x7C, 0x7D, 0x7A, 0x7B, 0x78, 0x79, 0x76, 0x77 } },
    { "0000000001", 5, { 0x9E, 0x9F, 0x9C, 0x9D, 0x9A, 0x9B, 0x98, 0x99, 0x96, 0x97, 0x94, 0x95, 0x92, 0x93, 0x90, 0x91,
                         0x8E, 0x8F, 0x8C, 0x8D, 0x8A, 0x8B, 0x88, 0x89, 0x86, 0x87, 0x84, 0x85, 0x82, 0x83, 0x80, 0x81 } },
    { "00000000001", 5, { 0xB0, 0xB1, 0xAE, 0xAF, 0xAC, 0xAD, 0xAA, 0xAB, 0xA8, 0xA9, 0xA6, 0xA7, 0xA4, 0xA5, 0xA2, 0xA3,
                          0xA0, 0xA1, 0xBE, 0xBF, 0xBC, 0xBD, 0xBA, 0xBB, 0xB8, 0xB9, 0xB6, 0xB7, 0xB4, 0xB5, 0xB2, 0xB3 } },
    { "000000000001", 5, { 0xC6, 0xC7, 0xC4, 0xC5, 0xC2, 0xC3, 0xC0, 0xC1, 0xC8, 0xC9, 0xD4, 0xD5, 0xD2, 0xD3, 0xD0, 0xD1,
                           0xCE, 0xCF, 0xCC, 0xCD, 0xCA, 0xCB, 0xDE, 0xDF, 0xDC, 0xDD, 0xDA, 0xDB, 0xD8, 0xD9, 0xD6, 0xD7 } },
};

/* Standard v1/v2 table: per group, MDEC values for the x-bits (sign is last bit). */
typedef struct { const char *prefix; int xbits; bool sign; u16 v[16]; } std_group;
static const std_group k_std[] = {
    { "10", 0, false, { 0xFE00 } },
    { "11", 0, true, { 0x0001 } },
    { "011", 0, true, { 0x0401 } },
    { "010", 1, true, { 0x0002, 0x0801 } },
    { "0011", 1, true, { 0x1001, 0x0C01 } },
    { "00101", 0, true, { 0x0003 } },
    { "00100", 3, true, { 0x3401, 0x0006, 0x3001, 0x2C01, 0x0C02, 0x0403, 0x0005, 0x2801 } },
    { "0001", 2, true, { 0x1C01, 0x1801, 0x0402, 0x1401 } },
    { "00001", 2, true, { 0x0802, 0x2401, 0x0004, 0x2001 } },
    { "000001", 0, false, { 0x7C1F } },
    { "0000001", 3, true, { 0x4001, 0x1402, 0x0007, 0x0803, 0x0404, 0x3C01, 0x3801, 0x1002 } },
    { "00000001", 4, true, { 0x000B, 0x2002, 0x1003, 0x000A, 0x0804, 0x1C02, 0x5401, 0x5001,
                             0x0009, 0x4C01, 0x4801, 0x0405, 0x0C03, 0x0008, 0x1802, 0x4401 } },
    { "000000001", 4, true, { 0x2802, 0x2402, 0x1403, 0x0C04, 0x0805, 0x0407, 0x0406, 0x000F,
                              0x000E, 0x000D, 0x000C, 0x6801, 0x6401, 0x6001, 0x5C01, 0x5801 } },
    { "0000000001", 4, true, { 0x001F, 0x001E, 0x001D, 0x001C, 0x001B, 0x001A, 0x0019, 0x0018,
                               0x0017, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012, 0x0011, 0x0010 } },
    { "00000000001", 4, true, { 0x0028, 0x0027, 0x0026, 0x0025, 0x0024, 0x0023, 0x0022, 0x0021,
                                0x0020, 0x040E, 0x040D, 0x040C, 0x040B, 0x040A, 0x0409, 0x0408 } },
    { "000000000001", 4, true, { 0x0412, 0x0411, 0x0410, 0x040F, 0x1803, 0x4002, 0x3C02, 0x3802,
                                 0x3402, 0x3002, 0x2C02, 0x7C01, 0x7801, 0x7401, 0x7001, 0x6C01 } },
};

/* Code lookup: for each code length (2..17) a direct table indexed by the
 * code value. Built once from k_groups. */
#define MAXLEN 17
static s16 g_table[(1 << (MAXLEN + 1))];  /* entry index or -1; length L uses [2^L, 2^(L+1)) */
static bool g_built;
#define IDX(len, code) g_table[(1u << (len)) + (code)]

static void build(void)
{
    size_t gi;
    u32 i;
    if (g_built) return;
    for (i = 0; i < NR_ARRAY_COUNT(g_table); i++) g_table[i] = -1;
    for (gi = 0; gi < NR_ARRAY_COUNT(k_groups); gi++) {
        const code_group *g = &k_groups[gi];
        int plen = (int)strlen(g->prefix), n = 1 << g->xbits, k;
        u32 pre = 0;
        for (k = 0; k < plen; k++) pre = (pre << 1) | (u32)(g->prefix[k] - '0');
        for (k = 0; k < n; k++) {
            u32 code = (pre << g->xbits) | (u32)k;
            IDX(plen + g->xbits, code) = g->idx[k];
        }
    }
    g_built = true;
}

void nr_bs_table_ea(nr_bs_table *t, const u8 *vlc0)
{
    int i;
    for (i = 0; i < 0xE0; i++) t->value[i] = nr_rd16(vlc0 + i * 2);
    t->ea = true;
}

void nr_bs_table_standard(nr_bs_table *t)
{
    /* Express the standard table in EA index order: walk both group lists
     * in parallel (same huffman codes, same order of code bits). */
    size_t gi;
    build();
    memset(t, 0, sizeof(*t));
    for (gi = 0; gi < NR_ARRAY_COUNT(k_groups); gi++) {
        const code_group *g = &k_groups[gi];
        const std_group *s = &k_std[gi];
        int n = 1 << g->xbits, k;
        for (k = 0; k < n; k++) {
            u16 v;
            if (s->sign) {
                u16 base = s->v[k >> 1];
                v = (k & 1) ? (u16)((base & 0xFC00) | ((u16)(-(s32)(base & 0x3FF)) & 0x3FF)) : base;
            } else {
                v = s->v[k];
            }
            t->value[g->idx[k]] = v;
        }
    }
    t->ea = false;
}

typedef struct { const u8 *d; u32 nbits; u32 pos; } bitreader;

static inline int getbit(bitreader *b)
{
    u32 hw = b->pos >> 4;
    u16 w;
    if (b->pos >= b->nbits) return -1;
    w = nr_rd16(b->d + hw * 2);
    b->pos++;
    return (w >> (15 - ((b->pos - 1) & 15))) & 1;
}

static inline s32 getbits(bitreader *b, int n)
{
    s32 v = 0;
    while (n--) {
        int x = getbit(b);
        if (x < 0) return -1;
        v = (v << 1) | x;
    }
    return v;
}

int nr_bs_decode(const u8 *bs, u32 len, const nr_bs_table *t, u16 *out, u32 out_cap, int *blocks, char *err, size_t errlen)
{
    bitreader br;
    u32 q, n = 0;
    int nb = 0;
    build();
    if (len < 8 || nr_rd16(bs + 2) != 0x3800) { snprintf(err, errlen, "invalid BS header"); return -1; }
    if (nr_rd16(bs + 6) > 2) { snprintf(err, errlen, "BS version %u not supported", nr_rd16(bs + 6)); return -1; }
    q = nr_rd16(bs + 4) & 0x3F;
    br.d = bs + 8;
    br.nbits = ((len - 8) / 2) * 16;
    br.pos = 0;
    for (;;) {
        s32 dc = getbits(&br, 10);
        if (dc < 0) break;
        if (dc == 0x1FF && (nb % 6) == 0) break; /* end of frame */
        if (n >= out_cap) { snprintf(err, errlen, "output buffer too small"); return -1; }
        out[n++] = (u16)((q << 10) | (u32)dc);
        for (;;) {
            u32 code = 0;
            int l, idx = -1;
            u16 v;
            for (l = 1; l <= MAXLEN; l++) {
                int bit = getbit(&br);
                if (bit < 0) { snprintf(err, errlen, "unexpected end of bitstream"); return -1; }
                code = (code << 1) | (u32)bit;
                if (l >= 2 && (idx = IDX(l, code)) >= 0) break;
            }
            if (idx < 0) { snprintf(err, errlen, "invalid huffman code at bit %u", br.pos); return -1; }
            v = t->value[idx];
            if (v == 0x7C1F && (t->ea || idx == 0x17)) {
                s32 raw = getbits(&br, 16);
                if (raw < 0) { snprintf(err, errlen, "truncated escape"); return -1; }
                v = (u16)raw;
            }
            if (n >= out_cap) { snprintf(err, errlen, "output buffer too small"); return -1; }
            out[n++] = v;
            if (v == 0xFE00) break;
        }
        nb++;
    }
    if (blocks) *blocks = nb;
    return (int)n;
}

/* Game-compatible variant: reproduces the exact output contract of the BS
 * decoder routine in SLUS-01068 (see docs/NATIVE.md):
 *   out[0], out[1] = first two header halfwords (MDEC size, 3800h)
 *   DC words = (header quant << 10) | dc  (16-bit, quant not masked)
 *   a DC of 1FFh in ANY block ends the frame
 *   the stream is terminated by 64 x FE00h
 * Returns the number of halfwords written or -1 if an invalid code is met
 * (the caller then lets the original routine handle the data). */
int nr_bs_decode_game(const u8 *frame, u32 avail, const nr_bs_table *t, u16 *out, u32 cap)
{
    bitreader br;
    u32 n = 0, i;
    u16 q;
    build();
    if (avail < 12 || cap < 2 + 64) return -1;
    out[n++] = nr_rd16(frame);
    out[n++] = nr_rd16(frame + 2);
    q = (u16)(nr_rd16(frame + 4) << 10);
    br.d = frame + 8;
    br.nbits = ((avail - 8) / 2) * 16;
    br.pos = 0;
    for (;;) {
        s32 dc = getbits(&br, 10);
        if (dc < 0) return -1;
        if (dc == 0x1FF) break;
        if (n + 1 > cap) return -1;
        out[n++] = (u16)((u32)dc | q);
        for (;;) {
            u32 code = 0;
            int l, idx = -1;
            u16 v;
            for (l = 1; l <= MAXLEN; l++) {
                int bit = getbit(&br);
                if (bit < 0) return -1;
                code = (code << 1) | (u32)bit;
                if (l >= 2 && (idx = IDX(l, code)) >= 0) break;
            }
            if (idx < 0) return -1;
            v = t->value[idx];
            if (v == 0x7C1F) {
                s32 raw = getbits(&br, 16);
                if (raw < 0) return -1;
                v = (u16)raw;
            }
            if (n + 1 > cap) return -1;
            out[n++] = v;
            if (v == 0xFE00) break;
        }
    }
    if (n + 64 > cap) return -1;
    for (i = 0; i < 64; i++) out[n++] = 0xFE00;
    return (int)n;
}
