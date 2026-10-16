/* Macroblock decoder (MDEC). Reference: psx-spx "Macroblock Decoder (MDEC)".
 * Decodes RLE macroblock streams (produced by the game's own VLC decoder)
 * into 4/8/15/24-bit pixels. Output is produced in the order DMA1 delivers it
 * (16x16 macroblocks in row-major order for colour modes). */
#include "psx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const u8 k_zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};
static u8 k_zagzig[64];

static const s16 k_default_scale[64] = {
    0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82, 0x5A82,
    0x7D8A, 0x6A6D, 0x471C, 0x18F8, (s16)0xE707, (s16)0xB8E3, (s16)0x9592, (s16)0x8275,
    0x7641, 0x30FB, (s16)0xCF04, (s16)0x89BE, (s16)0x89BE, (s16)0xCF04, 0x30FB, 0x7641,
    0x6A6D, (s16)0xE707, (s16)0x8275, (s16)0xB8E3, 0x471C, 0x7D8A, 0x18F8, (s16)0x9592,
    0x5A82, (s16)0xA57D, (s16)0xA57D, 0x5A82, 0x5A82, (s16)0xA57D, (s16)0xA57D, 0x5A82,
    0x471C, (s16)0x8275, 0x18F8, 0x6A6D, (s16)0x9592, (s16)0xE707, 0x7D8A, (s16)0xB8E3,
    0x30FB, (s16)0x89BE, 0x7641, (s16)0xCF04, (s16)0xCF04, 0x7641, (s16)0x89BE, 0x30FB,
    0x18F8, (s16)0xB8E3, 0x6A6D, (s16)0x8275, 0x7D8A, (s16)0x9592, 0x471C, (s16)0xE707
};

static void init_tables(void)
{
    static int done;
    int i;
    if (done) return;
    for (i = 0; i < 64; i++) k_zagzig[k_zigzag[i]] = (u8)i;
    done = 1;
}

/* ------------------------------------------------------------ core decode */

static inline s32 sext10(u32 v) { return ((s32)(v << 22)) >> 22; }

/* Decodes one 8x8 block from RLE halfwords. Returns number of halfwords
 * consumed or -1 if the input ended early. */
static int rl_decode_block(const u16 *src, u32 avail, s16 blk[64], const u8 qt[64])
{
    u32 pos = 0;
    u32 n, k, q_scale;
    s32 val;
    for (k = 0; k < 64; k++) blk[k] = 0;
    for (;;) {
        if (pos >= avail) return -1;
        n = src[pos++];
        if (n != 0xFE00) break;
    }
    k = 0;
    q_scale = (n >> 10) & 0x3F;
    val = sext10(n & 0x3FF) * qt[0];
    for (;;) {
        if (q_scale == 0) val = sext10(n & 0x3FF) * 2;
        if (val < -0x400) val = -0x400;
        if (val > 0x3FF) val = 0x3FF;
        if (q_scale > 0) blk[k_zagzig[k]] = (s16)val;
        else blk[k] = (s16)val;
        if (pos >= avail) return -1;
        n = src[pos++];
        k = k + ((n >> 10) & 0x3F) + 1;
        if (k > 63) break;
        val = (sext10(n & 0x3FF) * qt[k] * (s32)q_scale + 4) / 8;
    }
    return (int)pos;
}

static void idct(s16 blk[64], const s16 scale[64])
{
    s32 tmp[64];
    int x, y, z;
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            s64 sum = 0;
            for (z = 0; z < 8; z++) sum += (s64)blk[y + z * 8] * (scale[x + z * 8] >> 3);
            tmp[x + y * 8] = (s32)((sum + 0xFFF) >> 13);
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            s64 sum = 0;
            for (z = 0; z < 8; z++) sum += (s64)tmp[y + z * 8] * (scale[x + z * 8] >> 3);
            {
                s32 v = (s32)((sum + 0xFFF) >> 13);
                if (v < -0x8000) v = -0x8000;
                if (v > 0x7FFF) v = 0x7FFF;
                blk[x + y * 8] = (s16)v;
            }
        }
    }
}

static inline s32 clamp8s(s32 v) { return v < -128 ? -128 : (v > 127 ? 127 : v); }

/* Colour macroblock -> 16x16 RGB (each as 0x00BBGGRR, signed/unsigned applied) */
static void yuv_to_rgb(const s16 *cr, const s16 *cb, const s16 *y, int xx, int yy, u32 *dst, bool sign)
{
    int x, yv;
    for (yv = 0; yv < 8; yv++) {
        for (x = 0; x < 8; x++) {
            s32 R = cr[((x + xx) >> 1) + ((yv + yy) >> 1) * 8];
            s32 B = cb[((x + xx) >> 1) + ((yv + yy) >> 1) * 8];
            s32 G = ((-22 * B) + (-46 * R)) >> 6;     /* -0.3437*B - 0.7143*R */
            s32 Y = y[x + yv * 8];
            u32 r, g, b;
            R = (R * 90) >> 6;                         /* 1.402 */
            B = (B * 113) >> 6;                        /* 1.772 */
            r = (u32)(clamp8s(Y + R) & 0xFF);
            g = (u32)(clamp8s(Y + G) & 0xFF);
            b = (u32)(clamp8s(Y + B) & 0xFF);
            if (!sign) { r ^= 0x80; g ^= 0x80; b ^= 0x80; }
            dst[(x + xx) + (yv + yy) * 16] = r | (g << 8) | (b << 16);
        }
    }
}

/* Decodes one colour macroblock: returns halfwords used, -1 on underflow. */
static int decode_color_mb(const u16 *src, u32 avail, const u8 *qy, const u8 *quv, const s16 *scale, u32 out[256], bool sign)
{
    s16 cr[64], cb[64], yb[64];
    int used = 0, n, i;
    static const int offs[4][2] = { { 0, 0 }, { 8, 0 }, { 0, 8 }, { 8, 8 } };
    n = rl_decode_block(src + used, avail - (u32)used, cr, quv); if (n < 0) return -1; used += n; idct(cr, scale);
    n = rl_decode_block(src + used, avail - (u32)used, cb, quv); if (n < 0) return -1; used += n; idct(cb, scale);
    for (i = 0; i < 4; i++) {
        n = rl_decode_block(src + used, avail - (u32)used, yb, qy);
        if (n < 0) return -1;
        used += n;
        idct(yb, scale);
        yuv_to_rgb(cr, cb, yb, offs[i][0], offs[i][1], out, sign);
    }
    return used;
}

/* ------------------------------------------------------------ hardware */

void mdec_init(psx *p)
{
    init_tables();
    p->mdec.in_cap = 0x10000;
    p->mdec.in = (u16 *)malloc(p->mdec.in_cap * sizeof(u16));
    p->mdec.out_cap = 0x40000;
    p->mdec.out = (u32 *)malloc(p->mdec.out_cap * sizeof(u32));
}

void mdec_free(psx *p)
{
    free(p->mdec.in);
    free(p->mdec.out);
    p->mdec.in = NULL;
    p->mdec.out = NULL;
}

void mdec_reset(psx *p)
{
    psx_mdec *m = &p->mdec;
    m->status = 0x80040000u;
    m->state = 0;
    m->in_len = 0;
    m->out_len = m->out_pos = 0;
    m->words_left = 0;
    m->dma_in_enabled = m->dma_out_enabled = false;
    memcpy(m->scale, k_default_scale, sizeof(m->scale));
    memset(m->quant_y, 1, 64);
    memset(m->quant_uv, 1, 64);
}

static void push_out(psx_mdec *m, u32 v)
{
    if (m->out_len >= m->out_cap) {
        m->out_cap *= 2;
        m->out = (u32 *)realloc(m->out, m->out_cap * sizeof(u32));
    }
    m->out[m->out_len++] = v;
}

static void run_decode(psx *p)
{
    psx_mdec *m = &p->mdec;
    u32 depth = (m->cmd >> 27) & 3;
    bool sign = (m->cmd >> 26) & 1;
    u32 bit15 = ((m->cmd >> 25) & 1) ? 0x8000u : 0;
    u32 pos = 0;
    m->out_len = m->out_pos = 0;
    if (nr_log_get_level() >= NR_LOG_TRACE) {
        static int dumpn;
        char path[64];
        snprintf(path, sizeof(path), "mdec_in_%03d.bin", dumpn++);
        nr_write_file(path, m->in, m->in_len * 2);
        NR_TRACE("mdec", "decode cmd=%08X depth=%u words=%u -> %s", m->cmd, depth, m->in_len / 2, path);
    }
    while (pos < m->in_len) {
        /* skip padding */
        while (pos < m->in_len && m->in[pos] == 0xFE00) pos++;
        if (pos >= m->in_len) break;
        if (depth >= 2) {
            u32 px[256];
            int n = decode_color_mb(m->in + pos, m->in_len - pos, m->quant_y, m->quant_uv, m->scale, px, sign);
            int i;
            if (n < 0) break;
            pos += (u32)n;
            if (depth == 3) {
                for (i = 0; i < 256; i += 2) {
                    u32 a = px[i], b = px[i + 1];
                    u32 c0 = ((a & 0xFF) >> 3) | (((a >> 8) & 0xFF) >> 3) << 5 | (((a >> 16) & 0xFF) >> 3) << 10 | bit15;
                    u32 c1 = ((b & 0xFF) >> 3) | (((b >> 8) & 0xFF) >> 3) << 5 | (((b >> 16) & 0xFF) >> 3) << 10 | bit15;
                    push_out(m, c0 | (c1 << 16));
                }
            } else {
                /* 24-bit: pack 3 bytes per pixel */
                u8 bytes[768];
                for (i = 0; i < 256; i++) {
                    bytes[i * 3] = (u8)px[i];
                    bytes[i * 3 + 1] = (u8)(px[i] >> 8);
                    bytes[i * 3 + 2] = (u8)(px[i] >> 16);
                }
                for (i = 0; i < 768; i += 4) push_out(m, nr_rd32(bytes + i));
            }
        } else {
            s16 yb[64];
            int n = rl_decode_block(m->in + pos, m->in_len - pos, yb, m->quant_y), i;
            u8 px[64];
            if (n < 0) break;
            pos += (u32)n;
            idct(yb, m->scale);
            for (i = 0; i < 64; i++) {
                s32 y = ((s32)((u32)yb[i] << 23)) >> 23; /* 9-bit */
                y = clamp8s(y);
                px[i] = (u8)(sign ? (y & 0xFF) : ((y & 0xFF) ^ 0x80));
            }
            if (depth == 1) {
                for (i = 0; i < 64; i += 4) push_out(m, nr_rd32(px + i));
            } else {
                for (i = 0; i < 64; i += 8) {
                    u32 w = 0; int j;
                    for (j = 0; j < 8; j++) w |= (u32)(px[i + j] >> 4) << (j * 4);
                    push_out(m, w);
                }
            }
        }
    }
    m->in_len = 0;
}

static void accept_word(psx *p, u32 v)
{
    psx_mdec *m = &p->mdec;
    if (m->state == 0) {
        u32 c = v >> 29;
        NR_TRACE("mdec", "command %08X", v);
        m->cmd = v;
        m->status = (m->status & ~0x07800000u) | (((v >> 25) & 0xF) << 23);
        if (c == 1) {
            m->state = 1;
            m->words_left = v & 0xFFFF;
            m->in_len = 0;
            if (m->words_left == 0) m->state = 0;
        } else if (c == 2) {
            m->state = 2;
            m->param_pos = 0;
            m->param_need = (v & 1) ? 32 : 16;
        } else if (c == 3) {
            m->state = 3;
            m->param_pos = 0;
            m->param_need = 32;
        } else {
            m->status = (m->status & ~0xFFFFu) | (v & 0xFFFF);
        }
        return;
    }
    if (m->state == 1) {
        if (m->in_len + 2 > m->in_cap) {
            m->in_cap *= 2;
            m->in = (u16 *)realloc(m->in, m->in_cap * sizeof(u16));
        }
        m->in[m->in_len++] = (u16)v;
        m->in[m->in_len++] = (u16)(v >> 16);
        if (--m->words_left == 0) {
            run_decode(p);
            m->state = 0;
        }
        return;
    }
    /* parameter tables */
    m->param_buf[m->param_pos++] = v;
    if (m->param_pos >= m->param_need) {
        if (m->state == 2) {
            memcpy(m->quant_y, m->param_buf, 64);
            if (m->param_need == 32) memcpy(m->quant_uv, (u8 *)m->param_buf + 64, 64);
            if (nr_log_get_level() >= NR_LOG_DEBUG) {
                char b[400] = "";
                int i;
                for (i = 0; i < 64; i++) snprintf(b + strlen(b), sizeof(b) - strlen(b), "%u,", m->quant_y[i]);
                NR_DBG("mdec", "quant Y: %s", b);
                b[0] = 0;
                for (i = 0; i < 64; i++) snprintf(b + strlen(b), sizeof(b) - strlen(b), "%u,", m->quant_uv[i]);
                NR_DBG("mdec", "quant UV: %s", b);
            }
        } else {
            int i;
            for (i = 0; i < 64; i++) m->scale[i] = (s16)(((const u16 *)m->param_buf)[i]);
            NR_DBG("mdec", "scale table loaded (%s)", memcmp(m->scale, k_default_scale, sizeof(m->scale)) ? "custom" : "standard");
        }
        m->state = 0;
    }
}

void mdec_write(psx *p, u32 reg, u32 v)
{
    psx_mdec *m = &p->mdec;
    if (reg == 0) { accept_word(p, v); return; }
    if (v & 0x80000000u) {
        m->state = 0;
        m->in_len = m->out_len = m->out_pos = 0;
        m->status = 0x80040000u;
    }
    m->dma_in_enabled = (v >> 30) & 1;
    m->dma_out_enabled = (v >> 29) & 1;
}

u32 mdec_read(psx *p, u32 reg)
{
    psx_mdec *m = &p->mdec;
    if (reg == 0) return mdec_dma_read(p);
    {
        u32 s = m->status & 0x07800000u;
        if (m->out_pos >= m->out_len) s |= 0x80000000u;
        if (m->state != 0) s |= 0x20000000u;
        if (m->dma_in_enabled && m->state != 0) s |= 0x10000000u;
        if (m->dma_out_enabled && m->out_pos < m->out_len) s |= 0x08000000u;
        if (m->state == 1) s |= (m->words_left - 1) & 0xFFFF; else s |= 0xFFFF;
        s |= 4u << 16;
        return s;
    }
}

void mdec_dma_write(psx *p, u32 v) { accept_word(p, v); }

u32 mdec_dma_read(psx *p)
{
    psx_mdec *m = &p->mdec;
    if (m->out_pos < m->out_len) return m->out[m->out_pos++];
    return 0;
}

/* ------------------------------------------------- standalone (tools) API */
/* Decodes an already RLE-expanded stream (u16 halfwords) of colour
 * macroblocks to an RGBA image (macroblocks ordered column-major like the PS1
 * BS format: top-to-bottom, then left-to-right). */
int mdec_decode_rle_image(const u16 *rle, u32 nhalf, int width, int height, const u8 *qy, const u8 *quv, u32 *rgba)
{
    u32 pos = 0;
    int mbx, mby, decoded = 0;
    init_tables();
    for (mbx = 0; mbx < (width + 15) / 16; mbx++) {
        for (mby = 0; mby < (height + 15) / 16; mby++) {
            u32 px[256];
            int n, x, y;
            while (pos < nhalf && rle[pos] == 0xFE00) pos++;
            n = decode_color_mb(rle + pos, nhalf - pos, qy, quv, k_default_scale, px, false);
            if (n < 0) return decoded;
            pos += (u32)n;
            for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) {
                int ox = mbx * 16 + x, oy = mby * 16 + y;
                if (ox < width && oy < height) rgba[oy * width + ox] = 0xFF000000u | px[y * 16 + x];
            }
            decoded++;
        }
    }
    return decoded;
}
