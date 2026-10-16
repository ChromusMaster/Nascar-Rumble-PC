/* Sound Processing Unit. Reference: psx-spx "Sound Processing Unit (SPU)".
 * Implemented: 24 ADPCM voices with 4-point gaussian interpolation, ADSR and
 * volume sweep envelopes, pitch modulation, noise, ENDX, key on/off, SPU IRQ,
 * manual/DMA RAM transfers, capture buffers and the reverb unit.
 * Simplification: the reverb runs at 22.05 kHz without the 39-tap FIR
 * resampling filters (documented in docs/STATUS.md). */
#include "psx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SPU_SAMPLE_CYCLES 768u   /* 33868800 / 44100 */
#define SPU_BATCH 32u

static const s16 k_gauss[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7,
    8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24,
    25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56,
    58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111,
    116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199,
    205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328,
    338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512,
    525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761,
    779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089,
    1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508,
    1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031,
    2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668,
    2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427,
    3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315,
    4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332,
    5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475,
    6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735,
    7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097,
    9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542,
    10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045,
    12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577,
    13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104,
    15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592,
    16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001,
    18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295,
    19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439,
    20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399,
    21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148,
    22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662,
    22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927,
    22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

static const s32 k_pos[5] = { 0, 60, 115, 98, 122 };
static const s32 k_neg[5] = { 0, 0, -52, -55, -60 };

/* Extra per-voice state kept outside psx.h (envelope internals). */
typedef struct {
    s32 counter;
    u8 rate;          /* (shift<<2)|step */
    bool exp, dec, phase_neg, never;
} env_state;

typedef struct {
    env_state adsr;
    env_state sweep[2];
    s16 vol_cur[2];
    u8 block_flags;
    s16 hist4[4];     /* last 3 samples of previous block + pad */
    s16 out;          /* last output (for pitch modulation, after ADSR) */
} voice_ext;

typedef struct {
    voice_ext v[24];
    env_state main_sweep[2];
    s16 main_cur[2];
    u64 last_sync;
    u32 pmon, non, eon;
    u32 rev[32];     /* reverb registers 1DC0..1DFE */
    u32 reverb_base;
    s32 rev_lin, rev_rin;  /* accumulated reverb input for current pair */
    int rev_toggle;
} spu_ext;

static spu_ext *X(psx *p) { return (spu_ext *)p->spu.ext; }

void spu_state_ext(psx *p, void **ptr, size_t *size) { *ptr = p->spu.ext; *size = sizeof(spu_ext); }

void spu_init(psx *p)
{
    psx_spu *s = &p->spu;
    s->ram = (u8 *)calloc(1, PSX_SPU_RAM_SIZE);
    s->out_cap = 44100; /* 1 second ring */
    s->out = (s16 *)calloc(1, s->out_cap * 4);
    s->ext = calloc(1, sizeof(spu_ext));
}

void spu_free(psx *p)
{
    free(p->spu.ram);
    free(p->spu.out);
    free(p->spu.ext);
    p->spu.ext = NULL;
    p->spu.ram = NULL;
    p->spu.out = NULL;
}

static void schedule(psx *p)
{
    psx_schedule_periodic(p, EV_SPU, SPU_SAMPLE_CYCLES * SPU_BATCH);
}

void spu_reset(psx *p)
{
    psx_spu *s = &p->spu;
    spu_ext *x = X(p);
    u8 *ram = s->ram;
    s16 *out = s->out;
    void *ext = s->ext;
    u32 cap = s->out_cap;
    memset(ram, 0, PSX_SPU_RAM_SIZE);
    memset(s, 0, sizeof(*s));
    s->ram = ram; s->out = out; s->out_cap = cap; s->ext = ext;
    memset(x, 0, sizeof(*x));
    x->last_sync = p->cycles;
    schedule(p);
}

/* ------------------------------------------------------------ envelope */

static void env_setup(env_state *e, int shift, int step, bool exp, bool dec, bool phase_neg, bool rate_all_ones)
{
    e->rate = (u8)((shift << 2) | step);
    e->exp = exp; e->dec = dec; e->phase_neg = phase_neg;
    e->never = rate_all_ones;
}

static void env_tick(env_state *e, s32 *level)
{
    int shift = e->rate >> 2, step_val = e->rate & 3;
    s32 step = 7 - step_val;
    s32 inc;
    if (e->never) return;
    if (e->dec ^ e->phase_neg) step = ~step;
    step = (s32)((u32)step << ((11 - shift) > 0 ? (11 - shift) : 0));   /* two's-complement shift without UB */
    inc = 0x8000 >> ((shift - 11) > 0 ? (shift - 11) : 0);
    if (e->exp && !e->dec && *level > 0x6000) {
        if (shift < 10) step >>= 2;
        else if (shift >= 11) inc >>= 2;
        else { step >>= 1; inc >>= 1; }
    } else if (e->exp && e->dec) {
        step = (s32)(((s64)step * *level) >> 15);
    }
    if (inc < 1) inc = 1;
    e->counter += inc;
    if (!(e->counter & 0x8000)) return;
    e->counter = 0;
    *level += step;
    if (!e->dec) {
        if (*level > 0x7FFF) *level = 0x7FFF;
        if (*level < -0x8000) *level = -0x8000;
    } else if (e->phase_neg) {
        if (*level < -0x8000) *level = -0x8000;
        if (*level > 0) *level = 0;
    } else if (*level < 0) *level = 0;
}

static void adsr_phase_setup(psx_spu_voice *v, env_state *e)
{
    u32 a1 = v->adsr_lo, a2 = v->adsr_hi;
    switch (v->adsr_phase) {
    case 1: /* attack */
        env_setup(e, (a1 >> 10) & 0x1F, (a1 >> 8) & 3, (a1 >> 15) & 1, false, false, ((a1 >> 8) & 0x7F) == 0x7F);
        break;
    case 2: /* decay */
        env_setup(e, (a1 >> 4) & 0xF, 0, true, true, false, false);
        break;
    case 3: /* sustain */
        env_setup(e, (a2 >> 8) & 0x1F, (a2 >> 6) & 3, (a2 >> 15) & 1, (a2 >> 14) & 1, false, ((a2 >> 6) & 0x7F) == 0x7F);
        break;
    case 4: /* release */
        env_setup(e, a2 & 0x1F, 0, (a2 >> 5) & 1, true, false, (a2 & 0x1F) == 0x1F);
        break;
    default: break;
    }
    e->counter = 0;
}

static void adsr_tick(psx_spu_voice *v, env_state *e)
{
    if (v->adsr_phase == 0) return;
    env_tick(e, &v->adsr_vol);
    switch (v->adsr_phase) {
    case 1:
        if (v->adsr_vol >= 0x7FFF) { v->adsr_phase = 2; adsr_phase_setup(v, e); }
        break;
    case 2: {
        s32 sl = (s32)(((v->adsr_lo & 0xF) + 1) * 0x800);
        if (v->adsr_vol <= sl) { v->adsr_phase = 3; adsr_phase_setup(v, e); }
        break;
    }
    case 4:
        if (v->adsr_vol <= 0) { v->adsr_vol = 0; v->adsr_phase = 0; }
        break;
    default: break;
    }
}

/* Volume register (direct or sweep) */
static s16 vol_tick(env_state *e, s16 *cur, u16 reg)
{
    if (!(reg & 0x8000)) {
        *cur = (s16)((reg & 0x7FFF) << 1);
        return *cur;
    } else {
        s32 lvl = *cur;
        env_tick(e, &lvl);
        if (lvl > 0x7FFF) lvl = 0x7FFF;
        if (lvl < -0x8000) lvl = -0x8000;
        *cur = (s16)lvl;
        return *cur;
    }
}

static void vol_setup(env_state *e, u16 reg)
{
    if (reg & 0x8000)
        env_setup(e, (reg >> 2) & 0x1F, reg & 3, (reg >> 14) & 1, (reg >> 13) & 1, (reg >> 12) & 1, (reg & 0x7F) == 0x7F);
}

/* ------------------------------------------------------------ voices */

static void check_irq(psx *p, u32 addr, u32 len)
{
    psx_spu *s = &p->spu;
    u32 ia = (u32)s->irq_addr * 8;
    if (!(s->ctrl & 0x40) || !(s->ctrl & 0x8000)) return;
    if (ia >= addr && ia < addr + len) {
        if (!(s->stat & 0x40)) {
            s->stat |= 0x40;
            psx_irq(p, IRQ_SPU);
        }
    }
}

static void decode_block(psx *p, int vi)
{
    psx_spu *s = &p->spu;
    psx_spu_voice *v = &s->v[vi];
    voice_ext *vx = &X(p)->v[vi];
    const u8 *b = s->ram + (v->cur_addr & 0x7FFF0);
    int shift = b[0] & 0xF, filter = (b[0] >> 4) & 7, i;
    s32 f0, f1;
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 4;
    f0 = k_pos[filter]; f1 = k_neg[filter];
    check_irq(p, v->cur_addr & 0x7FFF0, 16);
    vx->block_flags = b[1];
    if ((b[1] & 4) && !v->ignore_loop_addr_write) v->repeat = v->cur_addr & 0x7FFF0;
    for (i = 0; i < 28; i++) {
        s32 nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        s32 smp = ((s32)(s16)(nib << 12)) >> shift;
        smp += (v->hist[0] * f0 + v->hist[1] * f1) >> 6;
        if (smp > 0x7FFF) smp = 0x7FFF;
        if (smp < -0x8000) smp = -0x8000;
        v->hist[1] = v->hist[0];
        v->hist[0] = (s16)smp;
        v->samples[i] = (s16)smp;
    }
}

static void key_on(psx *p, int vi)
{
    psx_spu *s = &p->spu;
    psx_spu_voice *v = &s->v[vi];
    voice_ext *vx = &X(p)->v[vi];
    v->cur_addr = (u32)v->start_addr * 8;
    v->repeat = (u32)v->repeat_addr * 8;
    v->ignore_loop_addr_write = false;
    v->counter = 0;
    v->hist[0] = v->hist[1] = 0;
    memset(vx->hist4, 0, sizeof(vx->hist4));
    v->adsr_vol = 0;
    v->adsr_phase = 1;
    adsr_phase_setup(v, &vx->adsr);
    s->endx &= ~(1u << vi);
    v->key_on = true;
    if (nr_log_get_level() >= NR_LOG_TRACE) {
        char hx[40];
        int k;
        for (k = 0; k < 16; k++) snprintf(hx + k * 2, 3, "%02x", s->ram[(v->cur_addr + 16 + (u32)k) & 0x7FFFF]);
        NR_TRACE("spu", "key on v%d addr=%05X pitch=%04X adsr=%04X/%04X vol=%04X/%04X data=%s", vi, v->cur_addr, v->pitch, v->adsr_lo,
                 v->adsr_hi, (u16)v->vol_l, (u16)v->vol_r, hx);
    }
    decode_block(p, vi);
}

static void key_off(psx *p, int vi)
{
    psx_spu_voice *v = &p->spu.v[vi];
    if (v->adsr_phase == 0) return;
    v->adsr_phase = 4;
    adsr_phase_setup(v, &X(p)->v[vi].adsr);
}

static s16 voice_sample(psx *p, int vi, s32 *outl, s32 *outr)
{
    psx_spu *s = &p->spu;
    spu_ext *x = X(p);
    psx_spu_voice *v = &s->v[vi];
    voice_ext *vx = &x->v[vi];
    s32 smp, i, idx;
    u32 step;
    s32 sm3, sm2, sm1, sm0;

    idx = (s32)(v->counter >> 12);
    /* gather 4 most recent samples */
    sm0 = v->samples[idx];
    sm1 = idx >= 1 ? v->samples[idx - 1] : vx->hist4[2 + idx];
    sm2 = idx >= 2 ? v->samples[idx - 2] : vx->hist4[1 + idx];
    sm3 = idx >= 3 ? v->samples[idx - 3] : vx->hist4[idx];
    i = (s32)((v->counter >> 4) & 0xFF);
    smp = ((k_gauss[0xFF - i] * sm3) >> 15) + ((k_gauss[0x1FF - i] * sm2) >> 15) +
          ((k_gauss[0x100 + i] * sm1) >> 15) + ((k_gauss[i] * sm0) >> 15);
    if (x->non & (1u << vi)) smp = (s16)s->noise_level;

    if (v->adsr_phase == 0 && v->adsr_vol == 0) smp = 0;
    else smp = (smp * v->adsr_vol) >> 15;
    vx->out = (s16)smp;

    {
        s16 vl = vol_tick(&vx->sweep[0], &vx->vol_cur[0], (u16)v->vol_l);
        s16 vr = vol_tick(&vx->sweep[1], &vx->vol_cur[1], (u16)v->vol_r);
        *outl = (smp * vl) >> 15;
        *outr = (smp * vr) >> 15;
    }

    /* advance pitch counter */
    step = v->pitch;
    if (vi > 0 && (x->pmon & (1u << vi))) {
        s32 f = (s32)x->v[vi - 1].out + 0x8000;
        s32 st = (s32)(s16)step;
        st = (st * f) >> 15;
        step = (u32)st & 0xFFFF;
    }
    if (step > 0x3FFF) step = 0x4000;
    v->counter += step;
    while ((v->counter >> 12) >= 28) {
        v->counter -= 28u << 12;
        /* keep last 3 samples for interpolation across blocks */
        vx->hist4[0] = v->samples[25]; vx->hist4[1] = v->samples[26]; vx->hist4[2] = v->samples[27];
        if (vx->block_flags & 1) {
            s->endx |= 1u << vi;
            v->cur_addr = v->repeat;
            if (!(vx->block_flags & 2) && !(x->non & (1u << vi))) {
                v->adsr_phase = 4;
                adsr_phase_setup(v, &vx->adsr);
                v->adsr_vol = 0;
                v->adsr_phase = 0;
            }
        } else {
            v->cur_addr = (v->cur_addr + 16) & 0x7FFFF;
        }
        decode_block(p, vi);
    }
    adsr_tick(v, &vx->adsr);
    return (s16)smp;
}

/* ------------------------------------------------------------ reverb */

static inline u32 rev_addr(psx *p, u32 rel)
{
    spu_ext *x = X(p);
    u32 base = x->reverb_base;
    u32 a = p->spu.reverb_cur + rel;
    u32 size = 0x80000 - base;
    if (size == 0) return base;
    while (a >= 0x80000) a = base + (a - 0x80000);
    if (a < base) a = base;
    return a & 0x7FFFE;
}

static inline s16 rev_rd(psx *p, u32 rel) { u32 a = rev_addr(p, rel); return (s16)nr_rd16(p->spu.ram + a); }
static inline void rev_wr(psx *p, u32 rel, s32 v)
{
    u32 a = rev_addr(p, rel);
    if (v > 0x7FFF) v = 0x7FFF;
    if (v < -0x8000) v = -0x8000;
    if (p->spu.ctrl & 0x80) nr_wr16(p->spu.ram + a, (u16)v);
    check_irq(p, a, 2);
}

#define RV(i) ((s32)(s16)x->rev[i])
#define RA(i) ((u32)(u16)x->rev[i] * 8u)

static void reverb_step(psx *p, s32 lin, s32 rin, s32 *lout, s32 *rout)
{
    spu_ext *x = X(p);
    /* register indices (offset from 1DC0 / 2) */
    enum { dAPF1 = 0, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
           mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
           mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN };
    s32 Lin = (lin * RV(vLIN)) >> 15, Rin = (rin * RV(vRIN)) >> 15;
    s32 l, r;
    if (p->spu.ctrl & 0x80) {
        s32 t;
        t = ((Lin + ((rev_rd(p, RA(dLSAME)) * RV(vWALL)) >> 15) - rev_rd(p, RA(mLSAME) - 2)) * RV(vIIR) >> 15) + rev_rd(p, RA(mLSAME) - 2);
        rev_wr(p, RA(mLSAME), t);
        t = ((Rin + ((rev_rd(p, RA(dRSAME)) * RV(vWALL)) >> 15) - rev_rd(p, RA(mRSAME) - 2)) * RV(vIIR) >> 15) + rev_rd(p, RA(mRSAME) - 2);
        rev_wr(p, RA(mRSAME), t);
        t = ((Lin + ((rev_rd(p, RA(dRDIFF)) * RV(vWALL)) >> 15) - rev_rd(p, RA(mLDIFF) - 2)) * RV(vIIR) >> 15) + rev_rd(p, RA(mLDIFF) - 2);
        rev_wr(p, RA(mLDIFF), t);
        t = ((Rin + ((rev_rd(p, RA(dLDIFF)) * RV(vWALL)) >> 15) - rev_rd(p, RA(mRDIFF) - 2)) * RV(vIIR) >> 15) + rev_rd(p, RA(mRDIFF) - 2);
        rev_wr(p, RA(mRDIFF), t);
    }
    l = ((RV(vCOMB1) * rev_rd(p, RA(mLCOMB1))) >> 15) + ((RV(vCOMB2) * rev_rd(p, RA(mLCOMB2))) >> 15) +
        ((RV(vCOMB3) * rev_rd(p, RA(mLCOMB3))) >> 15) + ((RV(vCOMB4) * rev_rd(p, RA(mLCOMB4))) >> 15);
    r = ((RV(vCOMB1) * rev_rd(p, RA(mRCOMB1))) >> 15) + ((RV(vCOMB2) * rev_rd(p, RA(mRCOMB2))) >> 15) +
        ((RV(vCOMB3) * rev_rd(p, RA(mRCOMB3))) >> 15) + ((RV(vCOMB4) * rev_rd(p, RA(mRCOMB4))) >> 15);
    {
        s32 a = rev_rd(p, RA(mLAPF1) - RA(dAPF1));
        l = l - ((RV(vAPF1) * a) >> 15);
        rev_wr(p, RA(mLAPF1), l);
        l = ((l * RV(vAPF1)) >> 15) + a;
        a = rev_rd(p, RA(mRAPF1) - RA(dAPF1));
        r = r - ((RV(vAPF1) * a) >> 15);
        rev_wr(p, RA(mRAPF1), r);
        r = ((r * RV(vAPF1)) >> 15) + a;
        a = rev_rd(p, RA(mLAPF2) - RA(dAPF2));
        l = l - ((RV(vAPF2) * a) >> 15);
        rev_wr(p, RA(mLAPF2), l);
        l = ((l * RV(vAPF2)) >> 15) + a;
        a = rev_rd(p, RA(mRAPF2) - RA(dAPF2));
        r = r - ((RV(vAPF2) * a) >> 15);
        rev_wr(p, RA(mRAPF2), r);
        r = ((r * RV(vAPF2)) >> 15) + a;
    }
    *lout = (l * p->spu.rev_vol_l) >> 15;
    *rout = (r * p->spu.rev_vol_r) >> 15;
    {
        u32 n = (p->spu.reverb_cur + 2) & 0x7FFFE;
        p->spu.reverb_cur = n < x->reverb_base ? x->reverb_base : n;
    }
}

/* ------------------------------------------------------------ mixing */

static void noise_tick(psx_spu *s)
{
    int shift = (s->ctrl >> 10) & 0xF, step = ((s->ctrl >> 8) & 3) + 4;
    u32 lvl = s->noise_level & 0xFFFF;
    u32 parity = ((lvl >> 15) ^ (lvl >> 12) ^ (lvl >> 11) ^ (lvl >> 10) ^ 1) & 1;
    s->noise_timer -= step;
    if (s->noise_timer < 0) {
        s->noise_level = ((lvl << 1) | parity) & 0xFFFF;
        s->noise_timer += 0x20000 >> shift;
        if (s->noise_timer < 0) s->noise_timer += 0x20000 >> shift;
    }
}

static inline s16 clamp16(s32 v) { return (s16)(v > 0x7FFF ? 0x7FFF : (v < -0x8000 ? -0x8000 : v)); }

static void generate_sample(psx *p)
{
    psx_spu *s = &p->spu;
    spu_ext *x = X(p);
    s32 l = 0, r = 0, rl = 0, rr = 0;
    int i;
    noise_tick(s);
    /* pending key on/off applied at sample boundary */
    if (s->koff_pending) {
        for (i = 0; i < 24; i++) if (s->koff_pending & (1u << i)) key_off(p, i);
        s->koff_pending = 0;
    }
    if (s->kon_pending) {
        for (i = 0; i < 24; i++) if (s->kon_pending & (1u << i)) key_on(p, i);
        s->kon_pending = 0;
    }
    for (i = 0; i < 24; i++) {
        s32 vl, vr;
        s16 raw = voice_sample(p, i, &vl, &vr);
        l += vl; r += vr;
        if (x->eon & (1u << i)) { rl += vl; rr += vr; }
        if (i == 1 || i == 3) {
            u32 off = (i == 1 ? 0x800u : 0xC00u) + s->capture_pos * 2;
            nr_wr16(s->ram + off, (u16)raw);
        }
    }
    /* capture buffers for CD audio (silent: no CD audio on this disc) */
    nr_wr16(s->ram + s->capture_pos * 2, (u16)s->cd_l);
    nr_wr16(s->ram + 0x400 + s->capture_pos * 2, (u16)s->cd_r);
    check_irq(p, s->capture_pos * 2, 2);
    check_irq(p, 0x400 + s->capture_pos * 2, 2);
    check_irq(p, 0x800 + s->capture_pos * 2, 2);
    check_irq(p, 0xC00 + s->capture_pos * 2, 2);
    s->capture_pos = (s->capture_pos + 1) & 0x1FF;
    if (s->capture_pos & 0x100) s->stat |= 0x800; else s->stat &= ~0x800u;

    /* reverb at 22.05 kHz */
    x->rev_lin += rl; x->rev_rin += rr;
    if (x->rev_toggle ^= 1) {
        reverb_step(p, clamp16(x->rev_lin / 2), clamp16(x->rev_rin / 2), &x->rev_lin, &x->rev_rin);
        s->rev_out_l = clamp16(x->rev_lin);
        s->rev_out_r = clamp16(x->rev_rin);
        x->rev_lin = x->rev_rin = 0;
    }
    l += s->rev_out_l; r += s->rev_out_r;

    if (!(s->ctrl & 0x4000)) { l = 0; r = 0; }
    {
        s16 ml = vol_tick(&x->main_sweep[0], &x->main_cur[0], (u16)s->main_vol_l);
        s16 mr = vol_tick(&x->main_sweep[1], &x->main_cur[1], (u16)s->main_vol_r);
        l = (clamp16(l) * ml) >> 15;
        r = (clamp16(r) * mr) >> 15;
    }
    if (!(s->ctrl & 0x8000)) { l = r = 0; }
    s->out[s->out_w * 2] = clamp16(l);
    s->out[s->out_w * 2 + 1] = clamp16(r);
    s->out_w = (s->out_w + 1) % s->out_cap;
    if (s->out_w == s->out_r) s->out_r = (s->out_r + 1) % s->out_cap; /* overrun: drop oldest */
}

static void sync(psx *p)
{
    spu_ext *x = X(p);
    while (p->cycles - x->last_sync >= SPU_SAMPLE_CYCLES) {
        generate_sample(p);
        x->last_sync += SPU_SAMPLE_CYCLES;
    }
}

void spu_event(psx *p)
{
    sync(p);
    schedule(p);
}

u32 spu_pull_audio(psx *p, s16 *out, u32 n)
{
    psx_spu *s = &p->spu;
    u32 c = 0;
    while (c < n && s->out_r != s->out_w) {
        out[c * 2] = s->out[s->out_r * 2];
        out[c * 2 + 1] = s->out[s->out_r * 2 + 1];
        s->out_r = (s->out_r + 1) % s->out_cap;
        c++;
    }
    return c;
}

u32 spu_audio_available(psx *p)
{
    psx_spu *s = &p->spu;
    return (s->out_w + s->out_cap - s->out_r) % s->out_cap;
}

/* ------------------------------------------------------------ registers */

u16 spu_read(psx *p, u32 addr)
{
    psx_spu *s = &p->spu;
    spu_ext *x = X(p);
    u32 off = addr & 0x3FE;
    sync(p);
    if (off < 0x180) {
        int vi = (int)(off >> 4);
        psx_spu_voice *v = &s->v[vi];
        switch (off & 0xE) {
        case 0x0: return (u16)v->vol_l;
        case 0x2: return (u16)v->vol_r;
        case 0x4: return v->pitch;
        case 0x6: return v->start_addr;
        case 0x8: return v->adsr_lo;
        case 0xA: return v->adsr_hi;
        case 0xC: return (u16)v->adsr_vol;
        case 0xE: return (u16)(v->repeat / 8);
        }
    }
    switch (off) {
    case 0x180: return (u16)s->main_vol_l;
    case 0x182: return (u16)s->main_vol_r;
    case 0x184: return (u16)s->rev_vol_l;
    case 0x186: return (u16)s->rev_vol_r;
    case 0x188: return s->regs[0x188 / 2];
    case 0x18A: return s->regs[0x18A / 2];
    case 0x18C: return s->regs[0x18C / 2];
    case 0x18E: return s->regs[0x18E / 2];
    case 0x190: return (u16)x->pmon;
    case 0x192: return (u16)(x->pmon >> 16);
    case 0x194: return (u16)x->non;
    case 0x196: return (u16)(x->non >> 16);
    case 0x198: return (u16)x->eon;
    case 0x19A: return (u16)(x->eon >> 16);
    case 0x19C: return (u16)s->endx;
    case 0x19E: return (u16)(s->endx >> 16);
    case 0x1A2: return (u16)(x->reverb_base / 8);
    case 0x1A4: return s->irq_addr;
    case 0x1A6: return s->regs[0x1A6 / 2];
    case 0x1AA: return s->ctrl;
    case 0x1AC: return s->regs[0x1AC / 2];
    case 0x1AE: {
        u16 st = (u16)((s->stat & 0x0840) | (s->ctrl & 0x3F));
        u32 mode = (s->ctrl >> 4) & 3;
        if (mode == 2) st |= 0x100 | 0x80;
        if (mode == 3) st |= 0x200 | 0x80;
        return st;
    }
    case 0x1B0: return (u16)s->cd_vol_l;
    case 0x1B2: return (u16)s->cd_vol_r;
    case 0x1B4: return (u16)s->ext_vol_l;
    case 0x1B6: return (u16)s->ext_vol_r;
    case 0x1B8: return (u16)x->main_cur[0];
    case 0x1BA: return (u16)x->main_cur[1];
    default: break;
    }
    if (off >= 0x1C0 && off < 0x200) return (u16)x->rev[(off - 0x1C0) / 2];
    if (off >= 0x200 && off < 0x260) {
        int vi = (int)((off - 0x200) / 4);
        return (u16)x->v[vi].vol_cur[(off & 2) ? 1 : 0];
    }
    return s->regs[off / 2];
}

void spu_write(psx *p, u32 addr, u16 val)
{
    psx_spu *s = &p->spu;
    spu_ext *x = X(p);
    u32 off = addr & 0x3FE;
    int i;
    sync(p);
    s->regs[off / 2] = val;
    if (off < 0x180) {
        int vi = (int)(off >> 4);
        psx_spu_voice *v = &s->v[vi];
        switch (off & 0xE) {
        case 0x0: v->vol_l = (s16)val; vol_setup(&x->v[vi].sweep[0], val); break;
        case 0x2: v->vol_r = (s16)val; vol_setup(&x->v[vi].sweep[1], val); break;
        case 0x4: v->pitch = val; break;
        case 0x6: v->start_addr = val; break;
        case 0x8: v->adsr_lo = val; if (v->adsr_phase) adsr_phase_setup(v, &x->v[vi].adsr); break;
        case 0xA: v->adsr_hi = val; if (v->adsr_phase) adsr_phase_setup(v, &x->v[vi].adsr); break;
        case 0xC: v->adsr_vol = (s16)val; break;
        case 0xE: v->repeat_addr = val; v->repeat = (u32)val * 8; v->ignore_loop_addr_write = true; break;
        }
        return;
    }
    switch (off) {
    case 0x180: s->main_vol_l = (s16)val; vol_setup(&x->main_sweep[0], val); break;
    case 0x182: s->main_vol_r = (s16)val; vol_setup(&x->main_sweep[1], val); break;
    case 0x184: s->rev_vol_l = (s16)val; break;
    case 0x186: s->rev_vol_r = (s16)val; break;
    case 0x188: s->kon_pending |= val; break;
    case 0x18A: s->kon_pending |= (u32)(val & 0xFF) << 16; break;
    case 0x18C: s->koff_pending |= val; break;
    case 0x18E: s->koff_pending |= (u32)(val & 0xFF) << 16; break;
    case 0x190: x->pmon = (x->pmon & 0xFF0000) | (val & 0xFFFE); break;
    case 0x192: x->pmon = (x->pmon & 0xFFFF) | ((u32)(val & 0xFF) << 16); break;
    case 0x194: x->non = (x->non & 0xFF0000) | val; break;
    case 0x196: x->non = (x->non & 0xFFFF) | ((u32)(val & 0xFF) << 16); break;
    case 0x198: x->eon = (x->eon & 0xFF0000) | val; break;
    case 0x19A: x->eon = (x->eon & 0xFFFF) | ((u32)(val & 0xFF) << 16); break;
    case 0x19C: case 0x19E: break; /* ENDX read-only */
    case 0x1A2: x->reverb_base = (u32)val * 8; s->reverb_cur = x->reverb_base; break;
    case 0x1A4: s->irq_addr = val; break;
    case 0x1A6: s->transfer_addr = (u32)val * 8; break;
    case 0x1A8: /* manual write FIFO (written straight to RAM at the transfer address) */
        nr_wr16(s->ram + (s->transfer_addr & 0x7FFFE), val);
        check_irq(p, s->transfer_addr & 0x7FFFE, 2);
        s->transfer_addr = (s->transfer_addr + 2) & 0x7FFFF;
        break;
    case 0x1AA:
        s->ctrl = val;
        if (!(val & 0x40)) s->stat &= ~0x40u; /* IRQ acknowledge */
        break;
    case 0x1AC: break;
    case 0x1B0: s->cd_vol_l = (s16)val; break;
    case 0x1B2: s->cd_vol_r = (s16)val; break;
    case 0x1B4: s->ext_vol_l = (s16)val; break;
    case 0x1B6: s->ext_vol_r = (s16)val; break;
    default:
        if (off >= 0x1C0 && off < 0x200) { x->rev[(off - 0x1C0) / 2] = val; break; }
        if (off >= 0x200 && off < 0x260) {
            i = (int)((off - 0x200) / 4);
            x->v[i].vol_cur[(off & 2) ? 1 : 0] = (s16)val;
        }
        break;
    }
}

void spu_dma_write(psx *p, u32 v)
{
    psx_spu *s = &p->spu;
    u32 a = s->transfer_addr & 0x7FFFE;
    nr_wr16(s->ram + a, (u16)v);
    nr_wr16(s->ram + ((a + 2) & 0x7FFFE), (u16)(v >> 16));
    check_irq(p, a, 4);
    s->transfer_addr = (s->transfer_addr + 4) & 0x7FFFF;
}

u32 spu_dma_read(psx *p)
{
    psx_spu *s = &p->spu;
    u32 a = s->transfer_addr & 0x7FFFE;
    u32 v = nr_rd16(s->ram + a) | ((u32)nr_rd16(s->ram + ((a + 2) & 0x7FFFE)) << 16);
    check_irq(p, a, 4);
    s->transfer_addr = (s->transfer_addr + 4) & 0x7FFFF;
    return v;
}
