/* Geometry Transformation Engine (COP2).
 * Reference: psx-spx "Geometry Transformation Engine (GTE)". Implements the
 * 44-bit MAC overflow checks, IR/colour/SZ/SXY saturation, FLAG register and
 * the hardware's UNR division (needed for bit-exact vertex positions). */
#include "psx.h"
#include "../render/pgxp.h"

#include <string.h>

/* data register indices */
enum {
    VXY0 = 0, VZ0, VXY1, VZ1, VXY2, VZ2, RGBC, OTZ, IR0, IR1, IR2, IR3,
    SXY0, SXY1, SXY2, SXYP, SZ0, SZ1, SZ2, SZ3, RGB0, RGB1, RGB2, RES1,
    MAC0, MAC1, MAC2, MAC3, IRGB, ORGB, LZCS, LZCR
};
/* control register indices */
enum {
    RT11RT12 = 0, RT13RT21, RT22RT23, RT31RT32, RT33, TRX, TRY, TRZ,
    L11L12, L13L21, L22L23, L31L32, L33, RBK, GBK, BBK,
    LR1LR2, LR3LG1, LG2LG3, LB1LB2, LB3, RFC, GFC, BFC,
    OFX, OFY, H, DQA, DQB, ZSF3, ZSF4, FLAG
};

#define F_MAC1_POS (1u << 30)
#define F_MAC2_POS (1u << 29)
#define F_MAC3_POS (1u << 28)
#define F_MAC1_NEG (1u << 27)
#define F_MAC2_NEG (1u << 26)
#define F_MAC3_NEG (1u << 25)
#define F_IR1 (1u << 24)
#define F_IR2 (1u << 23)
#define F_IR3 (1u << 22)
#define F_COLR (1u << 21)
#define F_COLG (1u << 20)
#define F_COLB (1u << 19)
#define F_SZ (1u << 18)
#define F_DIV (1u << 17)
#define F_MAC0_POS (1u << 16)
#define F_MAC0_NEG (1u << 15)
#define F_SX (1u << 14)
#define F_SY (1u << 13)
#define F_IR0 (1u << 12)

static const u8 k_unr_table[257] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6, 0xE4, 0xE3,
    0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8,
    0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0,
    0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4, 0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A,
    0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86,
    0x85, 0x84, 0x83, 0x82, 0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74,
    0x73, 0x72, 0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
    0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57, 0x56, 0x55,
    0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B, 0x4A, 0x49, 0x48, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B,
    0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F,
    0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B, 0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A,
    0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
    0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09, 0x08, 0x08,
    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
    0x00
};

/* Working context for one command */
typedef struct {
    psx_gte *g;
    u32 flag;
    int sf;      /* shift amount 0 or 12 */
    bool lm;
} ctx;

static inline s16 lo16(u32 v) { return (s16)(v & 0xFFFF); }
static inline s16 hi16(u32 v) { return (s16)(v >> 16); }

/* ---------------------------------------------------------------- helpers */

static inline s64 mac_check(ctx *c, int i, s64 v)
{
    if (v > 0x7FFFFFFFFFFLL) c->flag |= (i == 1) ? F_MAC1_POS : (i == 2) ? F_MAC2_POS : F_MAC3_POS;
    else if (v < -0x80000000000LL) c->flag |= (i == 1) ? F_MAC1_NEG : (i == 2) ? F_MAC2_NEG : F_MAC3_NEG;
    /* sign-extend to 44 bits */
    return (s64)((u64)v << 20) >> 20;
}

static inline void set_mac(ctx *c, int i, s64 v, int shift)
{
    mac_check(c, i, v);
    c->g->d[MAC0 + i] = (u32)(s32)(v >> shift);
}

static inline void set_ir(ctx *c, int i, s32 v, bool lm)
{
    s32 lo = lm ? 0 : -0x8000;
    if (v < lo) { v = lo; c->flag |= (i == 1) ? F_IR1 : (i == 2) ? F_IR2 : F_IR3; }
    else if (v > 0x7FFF) { v = 0x7FFF; c->flag |= (i == 1) ? F_IR1 : (i == 2) ? F_IR2 : F_IR3; }
    c->g->d[IR0 + i] = (u32)v;
}

static inline void set_mac_ir(ctx *c, int i, s64 v, bool lm)
{
    set_mac(c, i, v, c->sf);
    set_ir(c, i, (s32)c->g->d[MAC0 + i], lm);
}

static inline void set_mac0(ctx *c, s64 v)
{
    if (v > 0x7FFFFFFFLL) c->flag |= F_MAC0_POS;
    else if (v < -0x80000000LL) c->flag |= F_MAC0_NEG;
    c->g->d[MAC0] = (u32)(s32)v;
}

static inline void check_mac0(ctx *c, s64 v)
{
    if (v > 0x7FFFFFFFLL) c->flag |= F_MAC0_POS;
    else if (v < -0x80000000LL) c->flag |= F_MAC0_NEG;
}

static inline u8 sat_col(ctx *c, s32 v, u32 f)
{
    if (v < 0) { c->flag |= f; return 0; }
    if (v > 255) { c->flag |= f; return 255; }
    return (u8)v;
}

static void push_rgb_from_mac(ctx *c)
{
    psx_gte *g = c->g;
    u8 r = sat_col(c, (s32)g->d[MAC1] >> 4, F_COLR);
    u8 gg = sat_col(c, (s32)g->d[MAC2] >> 4, F_COLG);
    u8 b = sat_col(c, (s32)g->d[MAC3] >> 4, F_COLB);
    u8 code = (u8)(g->d[RGBC] >> 24);
    g->d[RGB0] = g->d[RGB1];
    g->d[RGB1] = g->d[RGB2];
    g->d[RGB2] = (u32)r | ((u32)gg << 8) | ((u32)b << 16) | ((u32)code << 24);
}

static void push_sz(ctx *c, s64 z)
{
    psx_gte *g = c->g;
    if (z < 0) { z = 0; c->flag |= F_SZ; }
    else if (z > 0xFFFF) { z = 0xFFFF; c->flag |= F_SZ; }
    g->d[SZ0] = g->d[SZ1];
    g->d[SZ1] = g->d[SZ2];
    g->d[SZ2] = g->d[SZ3];
    g->d[SZ3] = (u32)z;
}

static void push_sxy(ctx *c, s32 x, s32 y)
{
    psx_gte *g = c->g;
    if (x < -0x400) { x = -0x400; c->flag |= F_SX; }
    else if (x > 0x3FF) { x = 0x3FF; c->flag |= F_SX; }
    if (y < -0x400) { y = -0x400; c->flag |= F_SY; }
    else if (y > 0x3FF) { y = 0x3FF; c->flag |= F_SY; }
    g->d[SXY0] = g->d[SXY1];
    g->d[SXY1] = g->d[SXY2];
    g->d[SXY2] = ((u32)(u16)x) | ((u32)(u16)y << 16);
}

static u32 unr_divide(u32 h, u32 sz3, ctx *c)
{
    if (h < sz3 * 2) {
        int z = 0;
        u32 n, d, u;
        u32 t = sz3 & 0xFFFF;
        while (z < 16 && !(t & 0x8000)) { t <<= 1; z++; }
        n = h << z;
        d = (sz3 << z) & 0xFFFF;
        u = (u32)k_unr_table[(d - 0x7FC0) >> 7] + 0x101;
        d = (0x2000080u - (d * u)) >> 8;
        d = (0x0000080u + (d * u)) >> 8;
        {
            u64 r = (((u64)n * d) + 0x8000) >> 16;
            if (r > 0x1FFFF) r = 0x1FFFF;
            return (u32)r;
        }
    }
    c->flag |= F_DIV;
    return 0x1FFFF;
}

/* Matrix fetch: m = 0 RT, 1 LLM, 2 LCM, 3 garbage */
static void get_matrix(psx_gte *g, int m, s16 out[3][3])
{
    int base;
    if (m == 3) {
        s16 r = (s16)((g->d[RGBC] & 0xFF) << 4);
        out[0][0] = (s16)-r; out[0][1] = r; out[0][2] = (s16)g->d[IR0];
        out[1][0] = out[1][1] = out[1][2] = lo16(g->c[RT13RT21]);
        out[2][0] = out[2][1] = out[2][2] = lo16(g->c[RT22RT23]);
        return;
    }
    base = m * 8;
    out[0][0] = lo16(g->c[base + 0]); out[0][1] = hi16(g->c[base + 0]);
    out[0][2] = lo16(g->c[base + 1]); out[1][0] = hi16(g->c[base + 1]);
    out[1][1] = lo16(g->c[base + 2]); out[1][2] = hi16(g->c[base + 2]);
    out[2][0] = lo16(g->c[base + 3]); out[2][1] = hi16(g->c[base + 3]);
    out[2][2] = lo16(g->c[base + 4]);
}

static void get_vector(psx_gte *g, int v, s16 out[3])
{
    if (v == 3) {
        out[0] = (s16)g->d[IR1]; out[1] = (s16)g->d[IR2]; out[2] = (s16)g->d[IR3];
        return;
    }
    out[0] = lo16(g->d[v * 2]); out[1] = hi16(g->d[v * 2]); out[2] = lo16(g->d[v * 2 + 1]);
}

/* MAC = (T*0x1000 + M*V) >> sf, IR = MAC (sat lm) */
static void mat_vec(ctx *c, s16 m[3][3], const s16 v[3], const s32 t[3], bool lm)
{
    int i;
    for (i = 0; i < 3; i++) {
        s64 acc = NR_SHL64(t[i], 12);
        acc = mac_check(c, i + 1, acc + (s64)m[i][0] * v[0]);
        acc = mac_check(c, i + 1, acc + (s64)m[i][1] * v[1]);
        acc = mac_check(c, i + 1, acc + (s64)m[i][2] * v[2]);
        set_mac_ir(c, i + 1, acc, lm);
    }
}

static void interpolate_color(ctx *c, s64 m1, s64 m2, s64 m3)
{
    psx_gte *g = c->g;
    s64 in[3];
    s32 fc[3];
    int i;
    in[0] = m1; in[1] = m2; in[2] = m3;
    fc[0] = (s32)g->c[RFC]; fc[1] = (s32)g->c[GFC]; fc[2] = (s32)g->c[BFC];
    for (i = 0; i < 3; i++) {
        s64 t = NR_SHL64(fc[i], 12) - in[i];
        set_mac(c, i + 1, t, c->sf);
        set_ir(c, i + 1, (s32)g->d[MAC1 + i], false);
    }
    for (i = 0; i < 3; i++) {
        s64 t = (s64)(s16)g->d[IR1 + i] * (s16)g->d[IR0] + in[i];
        set_mac_ir(c, i + 1, t, c->lm);
    }
}

/* ----------------------------------------------------------- operations */

static void rtps(ctx *c, int vi, bool last)
{
    psx_gte *g = c->g;
    s16 m[3][3], v[3];
    s32 tr[3];
    s64 acc[3];
    int i;
    u32 hz;
    get_matrix(g, 0, m);
    get_vector(g, vi, v);
    tr[0] = (s32)g->c[TRX]; tr[1] = (s32)g->c[TRY]; tr[2] = (s32)g->c[TRZ];
    for (i = 0; i < 3; i++) {
        s64 a = NR_SHL64(tr[i], 12);
        a = mac_check(c, i + 1, a + (s64)m[i][0] * v[0]);
        a = mac_check(c, i + 1, a + (s64)m[i][1] * v[1]);
        a = mac_check(c, i + 1, a + (s64)m[i][2] * v[2]);
        acc[i] = a;
    }
    set_mac_ir(c, 1, acc[0], c->lm);
    set_mac_ir(c, 2, acc[1], c->lm);
    set_mac(c, 3, acc[2], c->sf);
    {
        s32 z12 = (s32)(acc[2] >> 12);
        s32 ir3 = (s32)g->d[MAC3];
        s32 lo = c->lm ? 0 : -0x8000;
        if (z12 < -0x8000 || z12 > 0x7FFF) c->flag |= F_IR3;
        if (ir3 < lo) ir3 = lo;
        if (ir3 > 0x7FFF) ir3 = 0x7FFF;
        g->d[IR3] = (u32)ir3;
        push_sz(c, z12);
    }
    hz = unr_divide(g->c[H] & 0xFFFF, g->d[SZ3] & 0xFFFF, c);
    {
        s64 sx = (s64)hz * (s16)g->d[IR1] + (s64)(s32)g->c[OFX];
        s64 sy = (s64)hz * (s16)g->d[IR2] + (s64)(s32)g->c[OFY];
        check_mac0(c, sx);
        check_mac0(c, sy);
        if (NR_UNLIKELY(g->ws_num != 0)) {
            /* widescreen enhancement: narrow the projection around the screen centre */
            s64 ofx = (s64)(s32)g->c[OFX];
            sx = ofx + (sx - ofx) * g->ws_num / g->ws_den;
        }
        push_sxy(c, (s32)(sx >> 16), (s32)(sy >> 16));
        if (NR_UNLIKELY(g->pgxp != NULL)) {
            /* unrounded projection: real division instead of the UNR approximation */
            double z = (double)acc[2] / 4096.0, h = (double)(g->c[H] & 0xFFFF);
            if (z > h / 2.0 && z > 0.0) {
                double div = c->sf ? 4096.0 : 1.0;
                double fx = ((double)(s32)g->c[OFX] / 65536.0) + h * ((double)acc[0] / div) / z;
                double fy = ((double)(s32)g->c[OFY] / 65536.0) + h * ((double)acc[1] / div) / z;
                if (g->ws_num) {
                    double ofx = (double)(s32)g->c[OFX] / 65536.0;
                    fx = ofx + (fx - ofx) * (double)g->ws_num / (double)g->ws_den;
                }
                nr_pgxp_store(g->pgxp, g->d[SXY2], (float)fx, (float)fy, (float)z);
            }
        }
    }
    if (last) {
        s64 dq = (s64)hz * (s16)g->c[DQA] + (s64)(s32)g->c[DQB];
        s32 ir0;
        set_mac0(c, dq);
        ir0 = (s32)(dq >> 12);
        if (ir0 < 0) { ir0 = 0; c->flag |= F_IR0; }
        else if (ir0 > 0x1000) { ir0 = 0x1000; c->flag |= F_IR0; }
        g->d[IR0] = (u32)ir0;
    }
}

static void ncs_core(ctx *c, int vi, int mode /*0 NC,1 NCC,2 NCD*/)
{
    psx_gte *g = c->g;
    s16 llm[3][3], lcm[3][3], v[3], ir[3];
    s32 bk[3], zero[3] = { 0, 0, 0 };
    get_matrix(g, 1, llm);
    get_matrix(g, 2, lcm);
    get_vector(g, vi, v);
    mat_vec(c, llm, v, zero, c->lm);
    ir[0] = (s16)g->d[IR1]; ir[1] = (s16)g->d[IR2]; ir[2] = (s16)g->d[IR3];
    bk[0] = (s32)g->c[RBK]; bk[1] = (s32)g->c[GBK]; bk[2] = (s32)g->c[BBK];
    mat_vec(c, lcm, ir, bk, c->lm);
    if (mode != 0) {
        u32 rgbc = g->d[RGBC];
        s64 m1 = NR_SHL64((s64)(rgbc & 0xFF) * (s16)g->d[IR1], 4);
        s64 m2 = NR_SHL64((s64)((rgbc >> 8) & 0xFF) * (s16)g->d[IR2], 4);
        s64 m3 = NR_SHL64((s64)((rgbc >> 16) & 0xFF) * (s16)g->d[IR3], 4);
        if (mode == 2) interpolate_color(c, m1, m2, m3);
        else { set_mac_ir(c, 1, m1, c->lm); set_mac_ir(c, 2, m2, c->lm); set_mac_ir(c, 3, m3, c->lm); }
    }
    push_rgb_from_mac(c);
}

static void cc_cdp(ctx *c, bool depth)
{
    psx_gte *g = c->g;
    s16 lcm[3][3], ir[3];
    s32 bk[3];
    u32 rgbc = g->d[RGBC];
    s64 m1, m2, m3;
    get_matrix(g, 2, lcm);
    ir[0] = (s16)g->d[IR1]; ir[1] = (s16)g->d[IR2]; ir[2] = (s16)g->d[IR3];
    bk[0] = (s32)g->c[RBK]; bk[1] = (s32)g->c[GBK]; bk[2] = (s32)g->c[BBK];
    mat_vec(c, lcm, ir, bk, c->lm);
    m1 = NR_SHL64((s64)(rgbc & 0xFF) * (s16)g->d[IR1], 4);
    m2 = NR_SHL64((s64)((rgbc >> 8) & 0xFF) * (s16)g->d[IR2], 4);
    m3 = NR_SHL64((s64)((rgbc >> 16) & 0xFF) * (s16)g->d[IR3], 4);
    if (depth) interpolate_color(c, m1, m2, m3);
    else { set_mac_ir(c, 1, m1, c->lm); set_mac_ir(c, 2, m2, c->lm); set_mac_ir(c, 3, m3, c->lm); }
    push_rgb_from_mac(c);
}

static void dpcs(ctx *c, u32 rgb)
{
    s64 m1 = NR_SHL64(rgb & 0xFF, 16), m2 = NR_SHL64((rgb >> 8) & 0xFF, 16), m3 = NR_SHL64((rgb >> 16) & 0xFF, 16);
    interpolate_color(c, m1, m2, m3);
    push_rgb_from_mac(c);
}

/* ------------------------------------------------------------- interface */

void gte_reset(psx_gte *g) { memset(g, 0, sizeof(*g)); }

u32 gte_read_data(psx_gte *g, int r)
{
    switch (r) {
    case VZ0: case VZ1: case VZ2: case IR0: case IR1: case IR2: case IR3:
        return (u32)(s32)(s16)g->d[r];
    case OTZ: case SZ0: case SZ1: case SZ2: case SZ3:
        return g->d[r] & 0xFFFF;
    case SXYP: return g->d[SXY2];
    case IRGB: case ORGB: {
        s32 r1 = (s16)g->d[IR1] >> 7, g1 = (s16)g->d[IR2] >> 7, b1 = (s16)g->d[IR3] >> 7;
        if (r1 < 0) r1 = 0; if (r1 > 0x1F) r1 = 0x1F;
        if (g1 < 0) g1 = 0; if (g1 > 0x1F) g1 = 0x1F;
        if (b1 < 0) b1 = 0; if (b1 > 0x1F) b1 = 0x1F;
        return (u32)r1 | ((u32)g1 << 5) | ((u32)b1 << 10);
    }
    default:
        return g->d[r];
    }
}

void gte_write_data(psx_gte *g, int r, u32 v)
{
    switch (r) {
    case VZ0: case VZ1: case VZ2: case IR0: case IR1: case IR2: case IR3:
        g->d[r] = (u32)(s32)(s16)v;
        break;
    case OTZ: case SZ0: case SZ1: case SZ2: case SZ3:
        g->d[r] = v & 0xFFFF;
        break;
    case SXYP:
        g->d[SXY0] = g->d[SXY1];
        g->d[SXY1] = g->d[SXY2];
        g->d[SXY2] = v;
        break;
    case IRGB:
        g->d[IR1] = (v & 0x1F) << 7;
        g->d[IR2] = ((v >> 5) & 0x1F) << 7;
        g->d[IR3] = ((v >> 10) & 0x1F) << 7;
        g->d[IRGB] = v & 0x7FFF;
        break;
    case ORGB: case LZCR:
        break;
    case LZCS: {
        u32 x = (v & 0x80000000u) ? ~v : v;
        u32 n = 0;
        while (n < 32 && !(x & 0x80000000u)) { x <<= 1; n++; }
        g->d[LZCS] = v;
        g->d[LZCR] = n;
        break;
    }
    default:
        g->d[r] = v;
        break;
    }
}

u32 gte_read_ctrl(psx_gte *g, int r)
{
    switch (r) {
    case RT33: case L33: case LB3: case DQA: case ZSF3: case ZSF4: case H:
        return (u32)(s32)(s16)g->c[r];
    case FLAG: {
        u32 f = g->c[FLAG] & 0x7FFFF000u;
        if (f & 0x7F87E000u) f |= 0x80000000u;
        return f;
    }
    default:
        return g->c[r];
    }
}

void gte_write_ctrl(psx_gte *g, int r, u32 v)
{
    switch (r) {
    case RT33: case L33: case LB3: case DQA: case ZSF3: case ZSF4:
        g->c[r] = (u32)(s32)(s16)v;
        break;
    case H:
        g->c[r] = v & 0xFFFF;
        break;
    case FLAG:
        g->c[FLAG] = v & 0x7FFFF000u;
        break;
    default:
        g->c[r] = v;
        break;
    }
}

int gte_command(psx_gte *g, u32 cmd)
{
    ctx c;
    int cycles = 8;
    u32 op = cmd & 0x3F;
    c.g = g;
    c.flag = 0;
    c.sf = (cmd & (1u << 19)) ? 12 : 0;
    c.lm = (cmd >> 10) & 1;

    switch (op) {
    case 0x01: rtps(&c, 0, true); cycles = 15; break;
    case 0x06: { /* NCLIP */
        s64 sx0 = lo16(g->d[SXY0]), sy0 = hi16(g->d[SXY0]);
        s64 sx1 = lo16(g->d[SXY1]), sy1 = hi16(g->d[SXY1]);
        s64 sx2 = lo16(g->d[SXY2]), sy2 = hi16(g->d[SXY2]);
        set_mac0(&c, sx0 * sy1 + sx1 * sy2 + sx2 * sy0 - sx0 * sy2 - sx1 * sy0 - sx2 * sy1);
        cycles = 8;
        break;
    }
    case 0x0C: { /* OP */
        s64 d1 = lo16(g->c[RT11RT12]), d2 = lo16(g->c[RT22RT23]), d3 = lo16(g->c[RT33]);
        s64 i1 = (s16)g->d[IR1], i2 = (s16)g->d[IR2], i3 = (s16)g->d[IR3];
        set_mac_ir(&c, 1, i3 * d2 - i2 * d3, c.lm);
        set_mac_ir(&c, 2, i1 * d3 - i3 * d1, c.lm);
        set_mac_ir(&c, 3, i2 * d1 - i1 * d2, c.lm);
        cycles = 6;
        break;
    }
    case 0x10: dpcs(&c, g->d[RGBC]); cycles = 8; break;
    case 0x11: { /* INTPL */
        s64 m1 = NR_SHL64((s16)g->d[IR1], 12), m2 = NR_SHL64((s16)g->d[IR2], 12), m3 = NR_SHL64((s16)g->d[IR3], 12);
        interpolate_color(&c, m1, m2, m3);
        push_rgb_from_mac(&c);
        cycles = 8;
        break;
    }
    case 0x12: { /* MVMVA */
        int mx = (int)((cmd >> 17) & 3), vx = (int)((cmd >> 15) & 3), cv = (int)((cmd >> 13) & 3);
        s16 m[3][3], v[3];
        s32 t[3] = { 0, 0, 0 };
        get_matrix(g, mx, m);
        get_vector(g, vx, v);
        if (cv == 0) { t[0] = (s32)g->c[TRX]; t[1] = (s32)g->c[TRY]; t[2] = (s32)g->c[TRZ]; }
        else if (cv == 1) { t[0] = (s32)g->c[RBK]; t[1] = (s32)g->c[GBK]; t[2] = (s32)g->c[BBK]; }
        else if (cv == 2) { t[0] = (s32)g->c[RFC]; t[1] = (s32)g->c[GFC]; t[2] = (s32)g->c[BFC]; }
        if (cv == 2) {
            int i;
            for (i = 0; i < 3; i++) {
                s64 tmp = mac_check(&c, i + 1, NR_SHL64(t[i], 12) + (s64)m[i][0] * v[0]);
                s64 acc;
                /* the FC term only affects flags (hardware bug) */
                {
                    s32 iv = (s32)(tmp >> c.sf);
                    if (iv < -0x8000 || iv > 0x7FFF)
                        c.flag |= (i == 0) ? F_IR1 : (i == 1) ? F_IR2 : F_IR3;
                }
                acc = mac_check(&c, i + 1, (s64)m[i][1] * v[1]);
                acc = mac_check(&c, i + 1, acc + (s64)m[i][2] * v[2]);
                set_mac_ir(&c, i + 1, acc, c.lm);
            }
        } else {
            mat_vec(&c, m, v, t, c.lm);
        }
        cycles = 8;
        break;
    }
    case 0x13: ncs_core(&c, 0, 2); cycles = 19; break;               /* NCDS */
    case 0x14: cc_cdp(&c, true); cycles = 13; break;                 /* CDP */
    case 0x16: ncs_core(&c, 0, 2); ncs_core(&c, 1, 2); ncs_core(&c, 2, 2); cycles = 44; break; /* NCDT */
    case 0x1B: ncs_core(&c, 0, 1); cycles = 17; break;               /* NCCS */
    case 0x1C: cc_cdp(&c, false); cycles = 11; break;                /* CC */
    case 0x1E: ncs_core(&c, 0, 0); cycles = 14; break;               /* NCS */
    case 0x20: ncs_core(&c, 0, 0); ncs_core(&c, 1, 0); ncs_core(&c, 2, 0); cycles = 30; break; /* NCT */
    case 0x28: { /* SQR */
        s64 i1 = (s16)g->d[IR1], i2 = (s16)g->d[IR2], i3 = (s16)g->d[IR3];
        set_mac_ir(&c, 1, i1 * i1, c.lm);
        set_mac_ir(&c, 2, i2 * i2, c.lm);
        set_mac_ir(&c, 3, i3 * i3, c.lm);
        cycles = 5;
        break;
    }
    case 0x29: { /* DCPL */
        u32 rgbc = g->d[RGBC];
        s64 m1 = NR_SHL64((s64)(rgbc & 0xFF) * (s16)g->d[IR1], 4);
        s64 m2 = NR_SHL64((s64)((rgbc >> 8) & 0xFF) * (s16)g->d[IR2], 4);
        s64 m3 = NR_SHL64((s64)((rgbc >> 16) & 0xFF) * (s16)g->d[IR3], 4);
        interpolate_color(&c, m1, m2, m3);
        push_rgb_from_mac(&c);
        cycles = 8;
        break;
    }
    case 0x2A: /* DPCT */
        dpcs(&c, g->d[RGB0]);
        dpcs(&c, g->d[RGB0]);
        dpcs(&c, g->d[RGB0]);
        cycles = 17;
        break;
    case 0x2D: { /* AVSZ3 */
        s64 v = (s64)(s16)g->c[ZSF3] * (s64)((g->d[SZ1] & 0xFFFF) + (g->d[SZ2] & 0xFFFF) + (g->d[SZ3] & 0xFFFF));
        s64 o;
        set_mac0(&c, v);
        o = v >> 12;
        if (o < 0) { o = 0; c.flag |= F_SZ; } else if (o > 0xFFFF) { o = 0xFFFF; c.flag |= F_SZ; }
        g->d[OTZ] = (u32)o;
        cycles = 5;
        break;
    }
    case 0x2E: { /* AVSZ4 */
        s64 v = (s64)(s16)g->c[ZSF4] * (s64)((g->d[SZ0] & 0xFFFF) + (g->d[SZ1] & 0xFFFF) + (g->d[SZ2] & 0xFFFF) + (g->d[SZ3] & 0xFFFF));
        s64 o;
        set_mac0(&c, v);
        o = v >> 12;
        if (o < 0) { o = 0; c.flag |= F_SZ; } else if (o > 0xFFFF) { o = 0xFFFF; c.flag |= F_SZ; }
        g->d[OTZ] = (u32)o;
        cycles = 6;
        break;
    }
    case 0x30: rtps(&c, 0, false); rtps(&c, 1, false); rtps(&c, 2, true); cycles = 23; break;
    case 0x3D: { /* GPF */
        s64 ir0 = (s16)g->d[IR0];
        set_mac_ir(&c, 1, (s64)(s16)g->d[IR1] * ir0, c.lm);
        set_mac_ir(&c, 2, (s64)(s16)g->d[IR2] * ir0, c.lm);
        set_mac_ir(&c, 3, (s64)(s16)g->d[IR3] * ir0, c.lm);
        push_rgb_from_mac(&c);
        cycles = 5;
        break;
    }
    case 0x3E: { /* GPL */
        s64 ir0 = (s16)g->d[IR0];
        s64 m1 = NR_SHL64((s32)g->d[MAC1], c.sf), m2 = NR_SHL64((s32)g->d[MAC2], c.sf), m3 = NR_SHL64((s32)g->d[MAC3], c.sf);
        set_mac_ir(&c, 1, mac_check(&c, 1, m1) + (s64)(s16)g->d[IR1] * ir0, c.lm);
        set_mac_ir(&c, 2, mac_check(&c, 2, m2) + (s64)(s16)g->d[IR2] * ir0, c.lm);
        set_mac_ir(&c, 3, mac_check(&c, 3, m3) + (s64)(s16)g->d[IR3] * ir0, c.lm);
        push_rgb_from_mac(&c);
        cycles = 5;
        break;
    }
    case 0x3F: ncs_core(&c, 0, 1); ncs_core(&c, 1, 1); ncs_core(&c, 2, 1); cycles = 39; break; /* NCCT */
    default:
        nr_unimplemented("gte", "undefined GTE opcode");
        break;
    }
    g->c[FLAG] = c.flag;
    return cycles;
}
