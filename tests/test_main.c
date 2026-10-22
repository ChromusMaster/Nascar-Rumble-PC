/* Unit and integration tests.
 *
 *  Unit tests use only synthetic data created by the tests themselves.
 *  Data tests (game disc) run only when the environment variable NR_DISC
 *  points to the user's own NASCAR Rumble (USA) image; otherwise they are
 *  reported as SKIPPED. Golden values for data tests are hashes only. */
#include "../src/common/common.h"
#include "../src/disc/disc.h"
#include "../src/disc/gameid.h"
#include "../src/psx/psx.h"
#include "../src/formats/formats.h"
#include "../src/app/app.h"
#include "../src/app/audio_sync.h"
#include "../src/render/swr.h"
#include "../src/disc/discsel.h"
#include "../src/mods/mods.h"
#include "../src/input/hidparse.h"
#include "../src/common/proc.h"
#include "../src/launcher/ltext.h"
#include "../src/launcher/l10n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define rmdir_compat(d) _rmdir(d)
#else
#include <unistd.h>
#define rmdir_compat(d) rmdir(d)
#endif

#ifndef NR_GOLDEN_FILE
#define NR_GOLDEN_FILE "golden_slus01068.txt"
#endif

static int g_pass, g_fail, g_skip;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
    if (_a == _b) g_pass++; else { g_fail++; fprintf(stderr, "  FAILED %s:%d: %s == %s (%llX != %llX)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

/* ---------------------------------------------------------------- helpers */

/* tiny MIPS encoders for test programs */
#define R_(op, rs, rt, rd, sa, fn) ((u32)(((u32)(op) << 26) | ((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(rd) << 11) | ((u32)(sa) << 6) | (fn)))
#define I_(op, rs, rt, imm) ((u32)(((u32)(op) << 26) | ((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(imm) & 0xFFFF)))
#define ADDIU(rt, rs, imm) I_(9, rs, rt, imm)
#define ORI(rt, rs, imm) I_(13, rs, rt, imm)
#define LUI(rt, imm) I_(15, 0, rt, imm)
#define LW(rt, off, base) I_(0x23, base, rt, off)
#define SW(rt, off, base) I_(0x2B, base, rt, off)
#define LWL(rt, off, base) I_(0x22, base, rt, off)
#define LWR(rt, off, base) I_(0x26, base, rt, off)
#define ADDU(rd, rs, rt) R_(0, rs, rt, rd, 0, 0x21)
#define ADD(rd, rs, rt) R_(0, rs, rt, rd, 0, 0x20)
#define DIV(rs, rt) R_(0, rs, rt, 0, 0, 0x1A)
#define DIVU(rs, rt) R_(0, rs, rt, 0, 0, 0x1B)
#define MFLO(rd) R_(0, 0, 0, rd, 0, 0x12)
#define MFHI(rd) R_(0, 0, 0, rd, 0, 0x10)
#define BEQ(rs, rt, off) I_(4, rs, rt, off)
#define BNE(rs, rt, off) I_(5, rs, rt, off)
#define JAL(t) ((u32)((3u << 26) | (((t) >> 2) & 0x3FFFFFF)))
#define JR(rs) R_(0, rs, 0, 0, 0, 8)
#define NOP 0u
#define BREAKI 0x0000000Du

static psx *new_machine(void)
{
    psx *p = psx_create();
    return p;
}

/* Loads a program at 0x80010000 and runs until it reaches a "jr ra" to the
 * sentinel return address or a cycle limit. */
static void run_prog(psx *p, const u32 *code, int n, u64 max_cycles)
{
    int i;
    for (i = 0; i < n; i++) psx_ram_w32(p, 0x80010000u + (u32)i * 4, code[i]);
    p->cpu.r[31] = 0x80020000u;   /* sentinel: an infinite loop "beq zero,zero,-1" */
    psx_ram_w32(p, 0x80020000u, BEQ(0, 0, -1));
    psx_ram_w32(p, 0x80020004u, NOP);
    p->cpu.cop0[C0_SR] = 0x40000000u; /* COP2 on, IRQs off */
    cpu_set_pc(p, 0x80010000u);
    {
        u64 end = p->cycles + max_cycles;
        while (p->cycles < end && (p->cpu.pc & 0x1FFFFFFF) != 0x00020000u) cpu_run(p, p->cycles + 1);
    }
}

/* ---------------------------------------------------------------- CPU */

static void test_cpu(void)
{
    psx *p = new_machine();
    fprintf(stderr, "[cpu]\n");
    {
        /* load delay slot: the instruction after a load sees the old value */
        u32 prog[] = {
            LUI(8, 0x8001), ORI(8, 8, 0x0100),       /* t0 = 0x80010100 */
            ADDIU(9, 0, 7),                            /* t1 = 7 */
            LW(9, 0, 8),                               /* t1 = [t0] (=0x1234) */
            ADDU(10, 9, 0),                            /* t2 = t1 (old value 7) */
            ADDU(11, 9, 0),                            /* t3 = t1 (new value)   */
            JR(31), NOP };
        psx_ram_w32(p, 0x80010100u, 0x1234);
        run_prog(p, prog, (int)NR_ARRAY_COUNT(prog), 1000);
        CHECK_EQ(p->cpu.r[10], 7);
        CHECK_EQ(p->cpu.r[11], 0x1234);
    }
    {
        /* branch delay slot executes; LWL/LWR unaligned load */
        u32 prog[] = {
            LUI(8, 0x8001), ORI(8, 8, 0x0101),
            ADDIU(12, 0, 0),
            BEQ(0, 0, 2), ADDIU(12, 12, 1),            /* delay slot runs */
            ADDIU(12, 12, 100),                        /* skipped */
            LWL(13, 3, 8), LWR(13, 0, 8),              /* unaligned word at 0x80010101 */
            NOP, JR(31), NOP };
        psx_ram_w32(p, 0x80010100u, 0x44332211u);
        psx_ram_w32(p, 0x80010104u, 0x88776655u);
        run_prog(p, prog, (int)NR_ARRAY_COUNT(prog), 1000);
        CHECK_EQ(p->cpu.r[12], 1);
        CHECK_EQ(p->cpu.r[13], 0x55443322u);
    }
    {
        /* DIV edge cases documented for the R3000A */
        u32 prog[] = {
            ADDIU(8, 0, -7), ADDIU(9, 0, 0), DIV(8, 9), MFLO(10), MFHI(11),
            LUI(8, 0x8000), ADDIU(9, 0, -1), DIV(8, 9), MFLO(12), MFHI(13),
            ADDIU(8, 0, 5), DIVU(8, 0), MFLO(14), MFHI(15),
            JR(31), NOP };
        run_prog(p, prog, (int)NR_ARRAY_COUNT(prog), 2000);
        CHECK_EQ(p->cpu.r[10], 1);              /* negative / 0 -> +1 */
        CHECK_EQ(p->cpu.r[11], (u32)-7);
        CHECK_EQ(p->cpu.r[12], 0x80000000u);    /* overflow case */
        CHECK_EQ(p->cpu.r[13], 0);
        CHECK_EQ(p->cpu.r[14], 0xFFFFFFFFu);
        CHECK_EQ(p->cpu.r[15], 5);
    }
    {
        /* ADD overflow raises exception 12 with EPC at the instruction */
        u32 prog[] = { LUI(8, 0x7FFF), ORI(8, 8, 0xFFFF), ADDIU(9, 0, 1), ADD(10, 8, 9), JR(31), NOP };
        int i;
        for (i = 0; i < 6; i++) psx_ram_w32(p, 0x80010000u + (u32)i * 4, prog[i]);
        p->cpu.cop0[C0_SR] = 0;
        cpu_set_pc(p, 0x80010000u);
        while ((p->cpu.pc & 0xFFFF) != 0x0080 && p->cycles < 1000000) cpu_run(p, p->cycles + 1);
        CHECK_EQ(p->cpu.pc, 0x80000080u);
        CHECK_EQ((p->cpu.cop0[C0_CAUSE] >> 2) & 0x1F, EXC_OV);
        CHECK_EQ(p->cpu.cop0[C0_EPC], 0x8001000Cu);
    }
    {
        char buf[96];
        cpu_disasm(0x80010000u, ADDIU(4, 29, 16), buf, sizeof(buf));
        CHECK(strcmp(buf, "addiu a0,sp,16") == 0);
    }
    psx_destroy(p);
}

/* ---------------------------------------------------------------- GTE */

static void test_gte(void)
{
    psx_gte g;
    fprintf(stderr, "[gte]\n");
    gte_reset(&g);
    /* identity rotation (1.0 = 0x1000), translation Z = 1000 */
    gte_write_ctrl(&g, 0, 0x00001000);  /* RT11=1000h RT12=0 */
    gte_write_ctrl(&g, 1, 0x00000000);
    gte_write_ctrl(&g, 2, 0x00001000);  /* RT22 */
    gte_write_ctrl(&g, 3, 0x00000000);
    gte_write_ctrl(&g, 4, 0x00001000);  /* RT33 */
    gte_write_ctrl(&g, 5, 0); gte_write_ctrl(&g, 6, 0); gte_write_ctrl(&g, 7, 1000);
    gte_write_ctrl(&g, 24, 160u << 16); gte_write_ctrl(&g, 25, 120u << 16);
    gte_write_ctrl(&g, 26, 1000);      /* H */
    gte_write_ctrl(&g, 27, 0); gte_write_ctrl(&g, 28, 0);
    gte_write_data(&g, 0, (50u << 16) | 100u);  /* VX=100 VY=50 */
    gte_write_data(&g, 1, 0);                   /* VZ=0 */
    gte_command(&g, 0x00180001);                /* RTPS sf=1 */
    CHECK_EQ(gte_read_data(&g, 14), (170u << 16) | 260u);   /* SXY2 */
    CHECK_EQ(gte_read_data(&g, 19), 1000);                  /* SZ3 */
    CHECK_EQ(gte_read_ctrl(&g, 31), 0);                     /* FLAG */
    /* divide overflow when SZ3 <= H/2 */
    gte_write_ctrl(&g, 7, 400);
    gte_command(&g, 0x00180001);
    CHECK((gte_read_ctrl(&g, 31) & (1u << 17)) != 0);
    CHECK((gte_read_ctrl(&g, 31) & 0x80000000u) != 0);
    /* NCLIP */
    gte_write_data(&g, 12, 0); gte_write_data(&g, 13, 10); gte_write_data(&g, 14, 10u << 16);
    gte_command(&g, 0x01400006);
    CHECK_EQ(gte_read_data(&g, 24), 100);
    /* LZCS/LZCR */
    gte_write_data(&g, 30, 0x00F00000);
    CHECK_EQ(gte_read_data(&g, 31), 8);
    gte_write_data(&g, 30, 0xFF000000);
    CHECK_EQ(gte_read_data(&g, 31), 8);
    /* AVSZ3 */
    gte_write_data(&g, 17, 300); gte_write_data(&g, 18, 600); gte_write_data(&g, 19, 900);
    gte_write_ctrl(&g, 29, 0x555);     /* ~1/3 */
    gte_command(&g, 0x0158002D);
    CHECK_EQ(gte_read_data(&g, 7), (0x555u * 1800u) >> 12);
    /* IRGB/ORGB */
    gte_write_data(&g, 28, 0x7FFF);
    CHECK_EQ(gte_read_data(&g, 9), 0x1F << 7);
    CHECK_EQ(gte_read_data(&g, 29), 0x7FFF);
}

/* ---------------------------------------------------------------- GPU */

static int count_color(psx *p, u16 c, int x0, int y0, int w, int h)
{
    int x, y, n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (p->gpu.vram[y * 1024 + x] == c) n++;
    return n;
}

static void test_gpu(void)
{
    psx *p = new_machine();
    fprintf(stderr, "[gpu]\n");
    gpu_write_gp1(p, 0);
    gpu_write_gp0(p, 0xE3000000);                       /* draw area 0,0 */
    gpu_write_gp0(p, 0xE4000000 | (511u << 10) | 1023u);
    gpu_write_gp0(p, 0xE5000000);
    /* quick fill: 16x8 at (32,4) red */
    gpu_write_gp0(p, 0x020000FF); gpu_write_gp0(p, (4u << 16) | 32u); gpu_write_gp0(p, (8u << 16) | 16u);
    CHECK_EQ(count_color(p, 0x001F, 32, 4, 16, 8), 16 * 8);
    CHECK_EQ(count_color(p, 0x001F, 0, 0, 1024, 512), 16 * 8);
    /* flat quad 0,0..10,10: lower/right edges excluded -> 100 pixels */
    gpu_write_gp0(p, 0x2800FF00);
    gpu_write_gp0(p, (100u << 16) | 100u); gpu_write_gp0(p, (100u << 16) | 110u);
    gpu_write_gp0(p, (110u << 16) | 100u); gpu_write_gp0(p, (110u << 16) | 110u);
    CHECK_EQ(count_color(p, 0x03E0, 90, 90, 40, 40), 100);
    /* flat triangle (0,0),(4,0),(0,4): 10 pixels with the top-left rule */
    gpu_write_gp0(p, 0x200000FF);
    gpu_write_gp0(p, (200u << 16) | 200u); gpu_write_gp0(p, (200u << 16) | 204u); gpu_write_gp0(p, (204u << 16) | 200u);
    CHECK_EQ(count_color(p, 0x001F, 196, 196, 16, 16), 10);
    /* CPU -> VRAM -> CPU round trip */
    gpu_write_gp0(p, 0xA0000000); gpu_write_gp0(p, (300u << 16) | 500u); gpu_write_gp0(p, (1u << 16) | 4u);
    gpu_write_gp0(p, 0x22221111); gpu_write_gp0(p, 0x44443333);
    gpu_write_gp0(p, 0xC0000000); gpu_write_gp0(p, (300u << 16) | 500u); gpu_write_gp0(p, (1u << 16) | 4u);
    CHECK_EQ(gpu_read(p, 0), 0x22221111u);
    CHECK_EQ(gpu_read(p, 0), 0x44443333u);
    /* semi-transparent rectangle B+F over red (mode 1) */
    gpu_write_gp0(p, 0xE1000020);                      /* ABR=1 */
    gpu_write_gp0(p, 0x620000F8);                      /* semi rect, blue-ish? -> red channel F8 */
    gpu_write_gp0(p, (4u << 16) | 32u); gpu_write_gp0(p, (1u << 16) | 1u);
    CHECK_EQ(p->gpu.vram[4 * 1024 + 32], 0x001F);      /* saturates at 31 */
    psx_destroy(p);
}

/* ---------------------------------------------------------------- DMA/OTC */

static void test_dma(void)
{
    psx *p = new_machine();
    u32 i;
    fprintf(stderr, "[dma]\n");
    dma_write(p, 0x1F8010F0, 0x0F654321);   /* enable ch6 */
    dma_write(p, 0x1F8010E0, 0x00100010);   /* MADR = 0x100010 */
    dma_write(p, 0x1F8010E4, 4);            /* 4 entries */
    dma_write(p, 0x1F8010E8, 0x11000002);   /* start + trigger, backwards */
    CHECK_EQ(psx_ram_r32(p, 0x80100010u), 0x0010000Cu);
    CHECK_EQ(psx_ram_r32(p, 0x80100008u), 0x00100004u);
    CHECK_EQ(psx_ram_r32(p, 0x80100004u), 0x00FFFFFFu);
    /* completion clears the busy bit after the modelled delay */
    for (i = 0; i < 100 && (dma_read(p, 0x1F8010E8) & 0x01000000u); i++) { p->cycles += 64; psx_dispatch(p); }
    CHECK((dma_read(p, 0x1F8010E8) & 0x01000000u) == 0);
    psx_destroy(p);
}

/* ---------------------------------------------------------------- timers */

static void test_timers(void)
{
    psx *p = new_machine();
    fprintf(stderr, "[timers]\n");
    p->i_mask = 0x7FF;
    timers_write(p, 0x1F801128, 100);           /* timer2 target */
    timers_write(p, 0x1F801124, 0x0058);        /* reset at target, IRQ at target, repeat */
    p->cycles += 150;
    psx_dispatch(p);
    CHECK((p->i_stat & (1u << IRQ_TMR2)) != 0);
    CHECK_EQ(timers_read(p, 0x1F801120), 49);   /* 150 ticks, period 101 */
    psx_destroy(p);
}

/* ---------------------------------------------------------------- BS/MDEC */

static void put_bits(u8 *buf, u32 *bitpos, u32 value, int n)
{
    while (n--) {
        u32 hw = *bitpos >> 4, bit = 15 - (*bitpos & 15);
        u16 w = nr_rd16(buf + hw * 2);
        if ((value >> n) & 1) w |= (u16)(1u << bit);
        nr_wr16(buf + hw * 2, w);
        (*bitpos)++;
    }
}

static void test_bs(void)
{
    u8 bs[64];
    u32 bp = 0;
    u16 out[64];
    int n, blocks, i;
    char err[128];
    nr_bs_table t;
    fprintf(stderr, "[bs]\n");
    memset(bs, 0, sizeof(bs));
    nr_wr16(bs, 0x10); nr_wr16(bs + 2, 0x3800); nr_wr16(bs + 4, 1); nr_wr16(bs + 6, 2);
    /* 6 blocks: DC=5, AC "11 0" (=0001h), EOB "10"; then end-of-frame DC 1FFh */
    for (i = 0; i < 6; i++) {
        u32 b2 = 64 + bp;
        put_bits(bs + 8, &bp, 5, 10);
        put_bits(bs + 8, &bp, 0x6, 3);
        put_bits(bs + 8, &bp, 0x2, 2);
        (void)b2;
    }
    put_bits(bs + 8, &bp, 0x1FF, 10);
    nr_bs_table_standard(&t);
    n = nr_bs_decode(bs, 8 + (bp + 15) / 16 * 2 + 2, &t, out, 64, &blocks, err, sizeof(err));
    CHECK_EQ(blocks, 6);
    CHECK_EQ(n, 18);
    CHECK_EQ(out[0], (1u << 10) | 5u);
    CHECK_EQ(out[1], 0x0001);
    CHECK_EQ(out[2], 0xFE00);
    /* sign bit set -> negative level */
    memset(bs + 8, 0, sizeof(bs) - 8); bp = 0;
    for (i = 0; i < 6; i++) { put_bits(bs + 8, &bp, 0, 10); put_bits(bs + 8, &bp, 0x7, 3); put_bits(bs + 8, &bp, 0x2, 2); }
    put_bits(bs + 8, &bp, 0x1FF, 10);
    n = nr_bs_decode(bs, 8 + (bp + 15) / 16 * 2 + 2, &t, out, 64, &blocks, err, sizeof(err));
    CHECK_EQ(out[1], 0x03FF);
    /* a flat grey macroblock: all DC=0 -> mid grey 0x80 after unsigned conversion */
    {
        u16 rle[12];
        u32 rgba[256];
        for (i = 0; i < 6; i++) { rle[i * 2] = (u16)(1u << 10); rle[i * 2 + 1] = 0xFE00; }
        CHECK_EQ(mdec_decode_rle_image(rle, 12, 16, 16, nr_mdec_default_quant, nr_mdec_default_quant, rgba), 1);
        CHECK_EQ(rgba[0] & 0xFFFFFF, 0x808080u);
        CHECK_EQ(rgba[255] & 0xFFFFFF, 0x808080u);
    }
}

/* ---------------------------------------------------------------- ADPCM */

static void test_adpcm(void)
{
    u8 blk[16];
    s16 out[28];
    nr_adpcm_state st = { 0, 0 };
    fprintf(stderr, "[adpcm]\n");
    memset(blk, 0, sizeof(blk));
    blk[0] = 0x0C;              /* shift 12 -> nibble value as is */
    blk[2] = 0x71;              /* samples 1, 7 */
    nr_adpcm_decode_block(blk, true, &st, out);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 7);
    CHECK_EQ(out[2], 0);
    memset(blk, 0, sizeof(blk));
    blk[0] = 0x00; blk[2] = 0x08; /* shift 0 -> -8 << 12 = -32768 */
    st.h1 = st.h2 = 0;
    nr_adpcm_decode_block(blk, true, &st, out);
    CHECK_EQ((u16)out[0], 0x8000);
    /* filter 1: s = new + old*60/64 */
    memset(blk, 0, sizeof(blk));
    blk[0] = 0x1C; blk[2] = 0x00;
    st.h1 = 64; st.h2 = 0;
    nr_adpcm_decode_block(blk, true, &st, out);
    CHECK_EQ(out[0], 60);
}

/* ---------------------------------------------------------------- disc */

static void make_iso(const char *path)
{
    /* 40 sectors of 2048: PVD at 16, root directory at 18, file at 19 */
    u8 *img = (u8 *)calloc(40, 2048);
    u8 *pvd = img + 16 * 2048, *root = img + 18 * 2048, *rec;
    const char *name = "SYSTEM.CNF;1";
    const char *cnf = "BOOT = cdrom:\\SLUS_010.68;1\r\n";
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    memcpy(pvd + 40, "TESTVOL                         ", 32);
    rec = pvd + 156; rec[0] = 34; nr_wr32(rec + 2, 18); nr_wr32(rec + 10, 2048); rec[25] = 2; rec[32] = 1;
    /* root: ".", "..", SYSTEM.CNF */
    rec = root; rec[0] = 34; nr_wr32(rec + 2, 18); nr_wr32(rec + 10, 2048); rec[25] = 2; rec[32] = 1; rec[33] = 0;
    rec += 34; rec[0] = 34; nr_wr32(rec + 2, 18); nr_wr32(rec + 10, 2048); rec[25] = 2; rec[32] = 1; rec[33] = 1;
    rec += 34; rec[0] = (u8)(33 + strlen(name) + 1); nr_wr32(rec + 2, 19); nr_wr32(rec + 10, (u32)strlen(cnf));
    rec[32] = (u8)strlen(name); memcpy(rec + 33, name, strlen(name));
    memcpy(img + 19 * 2048, cnf, strlen(cnf));
    nr_write_file(path, img, 40 * 2048);
    free(img);
}

static void test_disc(void)
{
    char err[512];
    nr_disc *d;
    const nr_iso_entry *e;
    u8 *buf;
    u32 sz;
    nr_game_report rep;
    fprintf(stderr, "[disc]\n");
    make_iso("nrtest.iso");
    d = nr_disc_open("nrtest.iso", err, sizeof(err));
    CHECK(d != NULL);
    if (!d) return;
    CHECK(nr_iso_mount(d, err, sizeof(err)));
    CHECK(strcmp(nr_iso_volume_id(d), "TESTVOL") == 0);
    e = nr_iso_find(d, "cdrom:\\system.cnf;1");
    CHECK(e != NULL);
    CHECK(nr_iso_read_file(d, "SYSTEM.CNF", &buf, &sz));
    CHECK_EQ(sz, 29);
    if (buf) free(buf);
    {
        u8 raw[2352];
        CHECK(nr_disc_read_raw(d, 16, raw));
        CHECK_EQ(raw[12], 0x00); CHECK_EQ(raw[13], 0x02); CHECK_EQ(raw[14], 0x16); /* MSF 00:02:16 BCD */
        CHECK_EQ(raw[18], 0x08);
    }
    /* identification: boots SLUS_010.68 but the executable is missing */
    CHECK_EQ(nr_game_identify(d, false, &rep), NR_GAME_UNREADABLE);
    nr_disc_close(d);
    /* clear error messages */
    d = nr_disc_open("does_not_exist.cue", err, sizeof(err));
    CHECK(d == NULL && strstr(err, "not found") != NULL);
    nr_write_file("nrtest.cue", "FILE \"missing.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n", 70);
    d = nr_disc_open("nrtest.cue", err, sizeof(err));
    CHECK(d == NULL && strstr(err, "BIN that does not exist") != NULL);
    nr_write_file("nrtest2.cue", "FILE \"x.bin\" BINARY\n  TRACK 01 AUDIO\n", 38);
    d = nr_disc_open("nrtest2.cue", err, sizeof(err));
    CHECK(d == NULL && strstr(err, "not supported") != NULL);
    remove("nrtest.iso"); remove("nrtest.cue"); remove("nrtest2.cue");
}

/* ---------------------------------------------------------------- misc */

static void test_misc(void)
{
    u8 dig[20];
    char hex[41];
    nr_config *c;
    fprintf(stderr, "[misc]\n");
    CHECK_EQ(nr_crc32(0, "123456789", 9), 0xCBF43926u);
    nr_sha1("abc", 3, dig);
    nr_sha1_hex(dig, hex);
    CHECK(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    nr_write_file("nrtest.ini", "# c\n[game]\ndisc = \"C:\\Games\\NR.cue\"\n[video]\nscale=4\n", 52);
    c = nr_config_load("nrtest.ini");
    CHECK(strcmp(nr_config_get(c, "game.disc", ""), "C:\\Games\\NR.cue") == 0);
    CHECK_EQ(nr_config_get_int(c, "video.scale", 0), 4);
    CHECK_EQ(nr_config_get_int(c, "video.none", 7), 7);
    nr_config_free(c);
    remove("nrtest.ini");
    {
        u32 px[4] = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFFFFFFF };
        u8 *png;
        long n;
        CHECK(nr_write_png("nrtest.png", px, 2, 2));
        n = nr_read_file("nrtest.png", &png);
        CHECK(n > 50 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
        free(png);
        remove("nrtest.png");
    }
}

static void test_launcher_font(void)
{
    char latin1[192 * 2 + 1];
    int i, n = 0, l, id;
    fprintf(stderr, "[launcher font]\n");
    for (i = 0x20; i < 0x7F; i++) latin1[n++] = (char)i;
    for (i = 0xA0; i < 0x100; i++) { latin1[n++] = (char)(0xC0 | (i >> 6)); latin1[n++] = (char)(0x80 | (i & 0x3F)); }
    latin1[n] = 0;
    CHECK(lt_builtin_covers(latin1));
    CHECK(lt_builtin_covers("\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D\xE2\x80\xA6\xE2\x80\x93\xE2\x80\x94\xE2\x82\xAC"));
    CHECK(!lt_builtin_covers("\xE6\x97\xA5"));
    for (l = 0; l < l10n_count(); l++) {
        const l10n_lang *lang = l10n_get(l);
        if (lang->script != LT_LATIN) continue;
        l10n_set(l);
        for (id = 0; id < S_COUNT; id++)
            if (!lt_builtin_covers(T(id))) { fprintf(stderr, "  %s string %d not covered\n", lang->code, id); CHECK(0); }
    }
    l10n_set(0);
    CHECK(lt_init());
    CHECK(strncmp(lt_font_info(), "built-in NREngine Sans", 22) == 0);
    CHECK(lt_width(20, "Configura\xC3\xA7\xC3\xB5" "es") > lt_width(20, "Configura"));
    CHECK_EQ(lt_line_height(20), 26);
    lt_shutdown();
}

/* ---------------------------------------------------------------- audio sync */

static void test_audio_sync(void)
{
    static s16 in[2 * 735], out[2 * 1600];
    nr_audio_sync s;
    int i, k;
    u64 p0;
    fprintf(stderr, "[audio_sync]\n");
    for (i = 0; i < 735; i++) { in[i * 2] = (s16)(i * 40); in[i * 2 + 1] = (s16)(-i * 40); }
    nr_audio_sync_init(&s, 2048);
    for (k = 0; k < 200; k++) nr_audio_sync_process(&s, in, 735, 2048, out, 1600);
    /* queue at target: ratio 1, output frames == input frames (+-1) */
    CHECK(s.produced + 2 >= s.consumed && s.produced <= s.consumed + 2);
    CHECK_EQ(s.dropped, 0);
    /* queue too full: plays slightly faster (fewer frames), by at most 0.5 % */
    p0 = s.produced;
    for (k = 0; k < 400; k++) nr_audio_sync_process(&s, in, 735, 4096, out, 1600);
    CHECK(s.produced - p0 < 400u * 735u);
    CHECK(s.produced - p0 > (u64)(400.0 * 735.0 * 0.994));
    /* queue way too full: the block is skipped, never stretched */
    CHECK_EQ(nr_audio_sync_process(&s, in, 735, 2048 * 7, out, 1600), 0);
    CHECK_EQ(s.dropped, 735);
    /* linear interpolation keeps a ramp a ramp */
    nr_audio_sync_init(&s, 2048);
    k = (int)nr_audio_sync_process(&s, in, 735, 2048, out, 1600);
    CHECK(k > 700);
    CHECK(out[200] >= out[198] && out[201] <= out[199]);
    nr_audio_sync_init(&s, 2048);
    CHECK_EQ(nr_audio_sync_process(&s, in, 735, 2048, out, 1), 1);
    CHECK(s.pos >= -1.0 && s.dropped > 0);
    CHECK(nr_audio_sync_process(&s, in, 735, 2048, out, 1600) > 700);
    CHECK_EQ(nr_audio_sync_process(&s, in, 735, 2048, NULL, 0), 0);
    CHECK(s.pos >= -1.0);
}

/* ------------------------------------------------- enhanced renderer (swr) */

static void draw_test_scene(psx *p)
{
    gpu_write_gp1(p, 0);
    gpu_write_gp0(p, 0xE3000000);
    gpu_write_gp0(p, 0xE4000000 | (511u << 10) | 1023u);
    gpu_write_gp0(p, 0xE5000000);
    gpu_write_gp0(p, 0x02102030); gpu_write_gp0(p, (16u << 16) | 16u); gpu_write_gp0(p, (32u << 16) | 64u);   /* fill */
    gpu_write_gp0(p, 0x2800FF00);                                                                                /* flat quad */
    gpu_write_gp0(p, (100u << 16) | 100u); gpu_write_gp0(p, (100u << 16) | 130u);
    gpu_write_gp0(p, (125u << 16) | 100u); gpu_write_gp0(p, (125u << 16) | 130u);
    gpu_write_gp0(p, 0x200000FF);                                                                                /* flat triangle */
    gpu_write_gp0(p, (200u << 16) | 200u); gpu_write_gp0(p, (203u << 16) | 260u); gpu_write_gp0(p, (240u << 16) | 205u);
    gpu_write_gp0(p, 0xA0000000); gpu_write_gp0(p, (0u << 16) | 512u); gpu_write_gp0(p, (1u << 16) | 4u);      /* texels */
    gpu_write_gp0(p, 0x03E07C1F); gpu_write_gp0(p, 0x7FFF0421);
    gpu_write_gp0(p, 0xE1000000 | 0x108u);                                                                       /* tpage 512,0 15-bit */
    gpu_write_gp0(p, 0x65000000); gpu_write_gp0(p, (300u << 16) | 40u); gpu_write_gp0(p, 0); gpu_write_gp0(p, (1u << 16) | 4u);
    gpu_write_gp0(p, 0xE1000020);                                                                                /* ABR=1 (B+F) */
    gpu_write_gp0(p, 0x62000040); gpu_write_gp0(p, (20u << 16) | 30u); gpu_write_gp0(p, (8u << 16) | 8u);       /* semi rect */
    gpu_write_gp0(p, 0x80000000); gpu_write_gp0(p, (16u << 16) | 16u); gpu_write_gp0(p, (60u << 16) | 700u);   /* VRAM copy */
    gpu_write_gp0(p, (32u << 16) | 64u);
}

static u32 vram_crc_any(psx *p) { return nr_crc32(0, p->gpu.vram, PSX_VRAM_W * PSX_VRAM_H * 2); }

static void test_swr(void)
{
    psx *p = new_machine(), *q = new_machine();
    nr_rlist *l = nr_rlist_new();
    nr_swr *r = nr_swr_new(2);
    nr_gfx_settings gs;
    bool sync;
    int x, y, sx, sy, diff = 0;
    fprintf(stderr, "[swr]\n");
    /* console VRAM is identical with and without the recording side channel */
    p->gpu.rec = l;
    draw_test_scene(p);
    draw_test_scene(q);
    CHECK_EQ(vram_crc_any(p), vram_crc_any(q));
    CHECK(l->n > 0);
    /* 1x, no AA, 15-bit: the replay must reproduce the console VRAM exactly */
    nr_gfx_defaults(&gs);
    CHECK(nr_swr_configure(r, &gs, &sync));
    {
        static u16 zero[1024 * 512];
        nr_swr_sync(r, zero);
    }
    nr_swr_execute(r, l);
    for (y = 0; y < 512; y++)
        for (x = 0; x < 1024; x++) {
            u32 v = nr_swr_pixel(r, x, y);
            u16 c = (u16)(((v >> 3) & 31) | (((v >> 11) & 31) << 5) | (((v >> 19) & 31) << 10) | (((v >> 24) & 1) << 15));
            if (c != p->gpu.vram[y * 1024 + x]) diff++;
        }
    CHECK_EQ(diff, 0);
    /* 4x internal resolution + 16x SSAA (4x4 grid) */
    gs.internal_scale = 4; gs.antialias = 16;
    CHECK(nr_swr_configure(r, &gs, &sync));
    CHECK(sync);
    nr_swr_scale(r, &sx, &sy);
    CHECK_EQ(sx, 16); CHECK_EQ(sy, 16);
    {
        static u16 zero[1024 * 512];
        nr_swr_sync(r, zero);
    }
    nr_swr_execute(r, l);
    /* inside the fill and the copied block every sample has the exact colour */
    CHECK_EQ(nr_swr_pixel(r, 20 * sx + 3, 20 * sy + 7) & 0xFFFFFF, nr_swr_pixel(r, 710 * sx + 15, 70 * sy) & 0xFFFFFF);
    {
        u32 v = nr_swr_pixel(r, 70 * sx, 40 * sy);
        CHECK_EQ(v & 0xF8F8F8u, 0x102030u & 0xF8F8F8u);
    }
    /* triangle edge pixels get partial coverage between samples (real AA) */
    {
        int inside = 0, total = 0, i, j;
        for (j = 0; j < sy; j++) for (i = 0; i < sx; i++) {
            u32 v = nr_swr_pixel(r, 204 * sx + i, 230 * sy + j);
            total++;
            if ((v & 0xFF) == 0xFF) inside++;
        }
        CHECK(inside > 0 && inside < total);
    }
    nr_swr_free(r);
    nr_rlist_free(l);
    p->gpu.rec = NULL;
    psx_destroy(p);
    psx_destroy(q);
}

/* ------------------------------------------- disc selection / mod overlay */

static void test_discsel(void)
{
    nr_disc_list l;
    nr_disc_candidate c;
    char path[1024], how[256], err[256];
    u8 *before, *after, *buf;
    long nb, na;
    u32 sz;
    nr_disc *d;
    fprintf(stderr, "[discsel/overlay]\n");
    nr_mkdir("nrtest_gamedata");
    make_iso("nrtest_gamedata/any_name.iso");
    nr_write_file("nrtest_gamedata/readme.txt", "x", 1);
    CHECK(nr_disc_probe("nrtest_gamedata/any_name.iso", &c));
    CHECK_EQ(c.status, NR_CAND_OK);
    nr_disc_scan("nrtest_gamedata", &l);
    CHECK_EQ(l.n, 1);
    /* automatic detection by content */
    CHECK(nr_disc_resolve("", "", "", &l, path, sizeof(path), how, sizeof(how)));
    CHECK(strstr(path, "any_name.iso") != NULL);
    /* explicit choices win, in order: command line, game.disc, game.disc_image */
    CHECK(nr_disc_resolve("cli.cue", "cfg.cue", "img.cue", &l, path, sizeof(path), how, sizeof(how)));
    CHECK(strcmp(path, "cli.cue") == 0);
    CHECK(nr_disc_resolve("", "cfg.cue", "img.cue", &l, path, sizeof(path), how, sizeof(how)));
    CHECK(strcmp(path, "cfg.cue") == 0);
    CHECK(nr_disc_resolve("", "", "any_name.iso", &l, path, sizeof(path), how, sizeof(how)));
    CHECK(strstr(path, "nrtest_gamedata") != NULL);

    /* overlay: the drive serves the replacement, the image file never changes */
    nb = nr_read_file("nrtest_gamedata/any_name.iso", &before);
    d = nr_disc_open("nrtest_gamedata/any_name.iso", err, sizeof(err));
    CHECK(d != NULL);
    if (d) {
        static u8 big[5000];
        memset(big, 'M', sizeof(big));
        CHECK(nr_iso_mount(d, err, sizeof(err)));
        CHECK(nr_disc_overlay_file(d, "SYSTEM.CNF", big, sizeof(big), err, sizeof(err)));
        CHECK_EQ(nr_disc_overlay_count(d), 1);
        CHECK(!nr_disc_overlay_file(d, "NOTFOUND.BIN", big, 10, err, sizeof(err)));
        {
            /* re-read through the raw sector path, as the emulated drive does */
            u8 raw[2352];
            CHECK(nr_disc_read_raw(d, 18, raw));
            CHECK(nr_disc_read_raw(d, 40 + 150, raw));    /* first virtual sector after the disc */
            CHECK_EQ(raw[24], 'M');
        }
        nr_disc_close(d);
    }
    na = nr_read_file("nrtest_gamedata/any_name.iso", &after);
    CHECK(nb == na && nb > 0 && memcmp(before, after, (size_t)nb) == 0);
    free(before); free(after);
    (void)buf; (void)sz;
    remove("nrtest_gamedata/any_name.iso");
    remove("nrtest_gamedata/readme.txt");
    rmdir_compat("nrtest_gamedata");

    /* ini updater keeps comments and other keys */
    { const char *ini = "# comment\n[game]\n; disc = \n[video]\nscale = 2\n"; nr_write_file("nrtest.ini", ini, strlen(ini)); }
    CHECK(nr_config_update_file("nrtest.ini", "game.disc", "C:\\x.cue"));
    CHECK(nr_config_update_file("nrtest.ini", "video.scale", "4"));
    CHECK(nr_config_update_file("nrtest.ini", "audio.volume", "80"));
    {
        nr_config *cf = nr_config_load("nrtest.ini");
        u8 *t;
        CHECK(strcmp(nr_config_get(cf, "game.disc", ""), "C:\\x.cue") == 0);
        CHECK_EQ(nr_config_get_int(cf, "video.scale", 0), 4);
        CHECK_EQ(nr_config_get_int(cf, "audio.volume", 0), 80);
        nr_config_free(cf);
        if (nr_read_file("nrtest.ini", &t) > 0) { CHECK(strstr((char *)t, "# comment") != NULL); free(t); }
    }
    remove("nrtest.ini");
}


/* ------------------------------------------------------------ HID parser */

static void test_hid(void)
{
    /* a typical generic USB gamepad ("PC Twin Shock" style): 5 axes, a hat,
     * 12 buttons and vendor padding */
    static const u8 desc[] = {
        0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,
        0xA1, 0x02,
        0x75, 0x08, 0x95, 0x05, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x35, 0x00, 0x46, 0xFF, 0x00,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
        0x75, 0x04, 0x95, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
        0x65, 0x00, 0x75, 0x01, 0x95, 0x0C, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x81, 0x02,
        0x06, 0x00, 0xFF, 0x75, 0x01, 0x95, 0x08, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01, 0x81, 0x02,
        0xC0,
        0xA1, 0x02, 0x75, 0x08, 0x95, 0x07, 0x46, 0xFF, 0x00, 0x26, 0xFF, 0x00, 0x09, 0x02, 0x91, 0x02, 0xC0,
        0xC0 };
    /* the same with a report ID and signed 16-bit axes */
    static const u8 desc2[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x03,
        0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x02,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
        0xC0 };
    nr_hid_layout l;
    nr_hid_state st;
    u8 rep[8] = { 0x00, 0xFF, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00 };
    u8 rep2[6] = { 0x03, 0x00, 0x80, 0xFF, 0x7F, 0x81 };
    fprintf(stderr, "[hid]\n");
    CHECK(nr_hid_parse(desc, sizeof(desc), &l));
    CHECK_EQ(l.naxes, 5);
    CHECK_EQ(l.nhats, 1);
    CHECK_EQ(l.nbuttons, 12);
    CHECK(!l.uses_report_ids);
    CHECK_EQ(l.top_usage, 0x04);
    /* hat East (2), button 3 (bit 2 of the 12 that follow the 4-bit hat) */
    rep[5] = (u8)(2 | (1u << (4 + 2)));
    nr_hid_state_reset(&st);
    nr_hid_decode(&l, rep, sizeof(rep), &st);
    CHECK_EQ((u16)st.axis[0], (u16)-32768);
    CHECK_EQ(st.axis[1], 32767);
    CHECK(st.axis[2] > -200 && st.axis[2] < 200);
    CHECK_EQ(st.hat[0], 2);
    CHECK_EQ(st.buttons, 1u << 2);
    /* null hat value (outside 0..7) means centred; button 12 */
    rep[5] = 0x0F; rep[6] = 0x80;
    nr_hid_decode(&l, rep, sizeof(rep), &st);
    CHECK_EQ(st.hat[0], -1);
    CHECK((st.buttons & (1u << 11)) != 0);
    /* report IDs and signed axes */
    CHECK(nr_hid_parse(desc2, sizeof(desc2), &l));
    CHECK(l.uses_report_ids);
    CHECK_EQ(l.naxes, 2);
    CHECK_EQ(l.nbuttons, 8);
    nr_hid_state_reset(&st);
    nr_hid_decode(&l, rep2, sizeof(rep2), &st);
    CHECK_EQ((u16)st.axis[0], (u16)-32768);
    CHECK_EQ(st.axis[1], 32767);
    CHECK_EQ(st.buttons, 0x81);
    rep2[0] = 0x04;                       /* another report ID: ignored */
    rep2[5] = 0x00;
    nr_hid_decode(&l, rep2, sizeof(rep2), &st);
    CHECK_EQ(st.buttons, 0x81);
    /* malformed / non-gamepad descriptors are rejected */
    CHECK(!nr_hid_parse(desc, 10, &l));
    {
        static const u8 kbd[] = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
                                  0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0 };
        CHECK(!nr_hid_parse(kbd, sizeof(kbd), &l));
    }
}

/* ------------------------------------------------------------ launcher session */

static void test_session(void)
{
    char t[33], u[33];
    fprintf(stderr, "[session]\n");
    nr_session_token(t);
    nr_session_token(u);
    CHECK_EQ(strlen(t), 32);
    CHECK(strcmp(t, u) != 0);
#ifdef _WIN32
    _putenv_s(NR_SESSION_ENV, t);
#else
    setenv(NR_SESSION_ENV, t, 1);
#endif
    CHECK(nr_session_valid(t));
    CHECK(!nr_session_valid(u));
    CHECK(!nr_session_valid(""));
    CHECK(!nr_session_valid(NULL));
}

/* ---------------------------------------------------------------- HLE */

static void test_hle(void)
{
    psx *p = new_machine();
    fprintf(stderr, "[hle]\n");
    /* A0 table entry 0x1B (strlen) dispatches through the kernel vector */
    {
        u32 prog[] = { ADDIU(10, 0, 0xA0), LUI(4, 0x8001), ORI(4, 4, 0x0200), JAL(0xA0), ADDIU(9, 0, 0x1B), ADDU(16, 2, 0), JR(31), NOP };
        int i;
        for (i = 0; i < 6; i++) psx_ram_w8(p, 0x80010200u + (u32)i, (u8)"NASCAR"[i]);
        psx_ram_w8(p, 0x80010206u, 0);
        /* jal to 0xA0 (KUSEG) */
        prog[3] = JAL(0x000000A0);
        run_prog(p, prog, (int)NR_ARRAY_COUNT(prog), 10000);
        CHECK_EQ(p->cpu.r[16], 6);
    }
    psx_destroy(p);
}

/* ---------------------------------------------------------------- data tests */

static u32 vram_crc(psx *p) { return nr_crc32(0, p->gpu.vram, PSX_VRAM_W * PSX_VRAM_H * 2); }

/* Scripted input used by the race regression test: confirm through the
 * menus (1 Player > Single Race > car > track) and hold the accelerator. */
static const char *k_race_script =
    "3610 cross\n3618 none\n3730 cross\n3738 none\n3850 cross\n3858 none\n3970 cross\n3978 none\n"
    "4090 cross\n4098 none\n4210 cross\n4218 none\n4330 cross\n4338 none\n4450 cross\n4458 none\n4700 cross\n";

static const nr_gfx_settings *g_test_gfx;   /* enhanced rendering for the next run (NULL = console) */
static const char *g_test_memcard;          /* memory card for the next run (NULL = none) */

static bool start_game(nr_app *a, const char *path, const char *script, bool verify)
{
    memset(a, 0, sizeof(*a));
    nr_options_default(&a->opt);
    a->opt.physics_step = 0;   /* regression values follow the console behaviour */
    nr_strlcpy(a->opt.disc_path, path, sizeof(a->opt.disc_path));
    a->opt.headless = true;
    a->opt.log_level = NR_LOG_ERROR;
    a->opt.memcard_path[0][0] = 0;   /* no memory card: deterministic */
    if (g_test_memcard) nr_strlcpy(a->opt.memcard_path[0], g_test_memcard, sizeof(a->opt.memcard_path[0]));
    a->opt.verify_natives = verify;
    if (script) nr_strlcpy(a->opt.input_script, script, sizeof(a->opt.input_script));
    if (g_test_gfx) a->opt.gfx = *g_test_gfx;
    return nr_app_start(a);
}

static u32 run_to(const char *path, const char *script, int frames, bool verify, nr_app *keep)
{
    nr_app a;
    u32 crc;
    if (!start_game(&a, path, script, verify)) { CHECK(!"failed to start the game"); nr_app_shutdown(&a); return 0; }
    while ((int)a.frame < frames && !a.p->halted) nr_app_frame(&a);
    CHECK(!a.p->halted);
    crc = vram_crc(a.p);
    if (keep) { *keep = a; return crc; }
    nr_app_shutdown(&a);
    return crc;
}

static u32 golden_get(const char *key)
{
    u8 *txt;
    long n = nr_read_file(NR_GOLDEN_FILE, &txt);
    char *k;
    u32 v = 0;
    if (n < 0) return 0;
    k = strstr((char *)txt, key);
    if (k) v = (u32)strtoul(k + strlen(key), NULL, 16);
    free(txt);
    return v;
}

static void test_data(bool update_golden)
{
    const char *path = getenv("NR_DISC");
    nr_app a;
    u32 boot1, boot2, race;
    int i, ok = 0, total = 0;
    fprintf(stderr, "[game data]\n");
    if (!path || !*path) { fprintf(stderr, "  SKIPPED (set NR_DISC to the path of your disc image to run)\n"); g_skip++; return; }

    /* identification + every LSC decodes natively */
    boot1 = run_to(path, NULL, 400, false, &a);
    CHECK_EQ(a.report.status, NR_GAME_OK);
    CHECK(a.p->cd.sectors_read > 0);
    for (i = 0; i < nr_iso_count(a.disc); i++) {
        const nr_iso_entry *e = nr_iso_get(a.disc, i);
        size_t l = strlen(e->path);
        if (l > 4 && strcmp(e->path + l - 4, ".LSC") == 0) {
            u8 *buf; u32 sz; nr_image img; char err[128];
            total++;
            if (nr_iso_read_file(a.disc, e->path, &buf, &sz)) {
                if (nr_lsc_decode(buf, sz, &img, err, sizeof(err))) { ok++; nr_image_free(&img); }
                else fprintf(stderr, "  %s: %s\n", e->path, err);
                free(buf);
            }
        }
    }
    CHECK_EQ(ok, total);
    CHECK_EQ(total, 70);
    nr_app_shutdown(&a);

    /* determinism */
    boot2 = run_to(path, NULL, 400, false, NULL);
    CHECK_EQ(boot1, boot2);

    /* native routines are bit-exact against the original code */
    {
        u32 crc;
        int k;
        nr_app v;
        crc = run_to(path, NULL, 1500, true, &v);
        (void)crc;
        for (k = 0; k < v.p->novr; k++) {
            CHECK_EQ(v.p->ovr[k].verified_bad, 0);
            if (!v.p->ovr[k].observe_only) CHECK(v.p->ovr[k].verified_ok > 0);
        }
        nr_app_shutdown(&v);
    }

    /* memory card: Game Options > Load and Save > Save on a new card, then
     * check the directory entry written to the card image (the game first
     * reports "Save Not Found", saves, and the file holds the EA save). */
    {
        static const char k_save_script[] =
            "3610 cross\n3618 none\n3660 triangle\n3668 none\n3700 down\n3706 none\n3720 down\n3726 none\n"
            "3740 down\n3746 none\n3760 down\n3766 none\n3780 down\n3786 none\n3810 cross\n3818 none\n"
            "3840 down\n3846 none\n3860 down\n3866 none\n3890 cross\n3898 none\n3955 up\n3961 none\n"
            "3975 cross\n3983 none\n";
        u8 *mc = NULL;
        long n;
        remove("nrtest_card.mcd");
        nr_write_file("nrtest_save.txt", k_save_script, strlen(k_save_script));
        g_test_memcard = "nrtest_card.mcd";
        run_to(path, "nrtest_save.txt", 4300, false, NULL);
        g_test_memcard = NULL;
        n = nr_read_file("nrtest_card.mcd", &mc);
        CHECK_EQ(n, 128 * 1024);
        if (n == 128 * 1024) {
            CHECK(mc[0] == 'M' && mc[1] == 'C');
            CHECK_EQ(mc[128], 0x51);                                   /* first block in use */
            CHECK(memcmp(mc + 128 + 10, "BASLUS-01068", 12) == 0);     /* the game's save file */
        }
        free(mc);
        remove("nrtest_card.mcd");
        remove("nrtest_save.txt");
    }

    /* scripted race (menus, loading, 3D race) */
    nr_write_file("nrtest_race.txt", k_race_script, strlen(k_race_script));
    {
        nr_app ra;
        u32 k, nz = 0;
        race = run_to(path, "nrtest_race.txt", 5600, false, &ra);
        /* the race was actually reached: many 3D primitives in the last frames */
        fprintf(stderr, "  primitives: %llu\n", (unsigned long long)ra.p->gpu.prims); CHECK(ra.p->gpu.prims > 100000);
        /* kernel RAM reserved for translated texts (0x6000-0xBFFF) is never
         * touched by the game or by the HLE kernel */
        for (k = 0x6000; k < 0xC000; k++) if (ra.p->ram[k]) nz++;
        CHECK_EQ(nz, 0);
        nr_app_shutdown(&ra);
    }
    /* enhanced rendering (4x, 16x SSAA, 24-bit, bilinear, PGXP) leaves the
     * console VRAM - what the game sees - bit-identical */
    {
        nr_gfx_settings gs;
        u32 enh;
        nr_gfx_defaults(&gs);
        gs.internal_scale = 2; gs.antialias = 16; gs.true_color = true; gs.texture_filter = 2; gs.pgxp = true;
        gs.post_fx = GFX_FX_CRT; gs.tonemap = GFX_TM_ACES;
        g_test_gfx = &gs;
        enh = run_to(path, "nrtest_race.txt", 5600, false, NULL);
        g_test_gfx = NULL;
        CHECK_EQ(enh, race);
    }
    remove("nrtest_race.txt");

    fprintf(stderr, "  VRAM CRC: boot400=%08X race5600=%08X\n", boot1, race);
    if (update_golden) {
        char txt[256];
        snprintf(txt, sizeof(txt), "# VRAM CRC32 (regression) - SLUS-01068 SHA-1 %s\nboot400 %08X\nrace5600 %08X\n", NR_EXPECTED_EXE_SHA1, boot1, race);
        CHECK(nr_write_file(NR_GOLDEN_FILE, txt, strlen(txt)));
        fprintf(stderr, "  reference values updated in %s\n", NR_GOLDEN_FILE);
    } else {
        u32 g1 = golden_get("boot400 "), g2 = golden_get("race5600 ");
        if (!g1 || !g2) { fprintf(stderr, "  warning: %s missing; run 'nrtests --update-golden'\n", NR_GOLDEN_FILE); g_skip++; }
        else {
            CHECK_EQ(boot1, g1);
            CHECK_EQ(race, g2);
            if (boot1 != g1 || race != g2)
                fprintf(stderr, "  The output changed compared with the reference. If the change is intended and validated,\n"
                                "  update it with: nrtests --update-golden\n");
        }
    }
}

static void test_external_catalog(void)
{
    const char *catalog = "[strings]\nS_BTN_PLAY = Start custom\nS_STATUS_BAD = Unsafe %n\nS_BTN_QUIT = Exit custom\nS_BTN_QUIT = Duplicate\nS_BTN_SAVE = \xC0\xAF\nUNKNOWN = Ignore\n";
    int lang = l10n_find("en-CA");
    CHECK(nr_write_file("nrtest-catalog.ini", catalog, strlen(catalog)));
    CHECK(l10n_load_catalog(lang, "nrtest-catalog.ini"));
    l10n_set(lang);
    CHECK(strcmp(T(S_BTN_PLAY), "Start custom") == 0);
    CHECK(strcmp(T(S_BTN_QUIT), "Exit custom") == 0);
    CHECK(strstr(T(S_STATUS_BAD), "%s") != NULL);
    CHECK(strcmp(T(S_BTN_SAVE), "Save") == 0);
    l10n_set(0);
    remove("nrtest-catalog.ini");
}

int main(int argc, char **argv)
{
    bool update = argc > 1 && strcmp(argv[1], "--update-golden") == 0;
    nr_log_set_level(NR_LOG_ERROR);
    test_cpu();
    test_gte();
    test_gpu();
    test_dma();
    test_timers();
    test_bs();
    test_adpcm();
    test_disc();
    test_external_catalog();
    test_misc();
    test_launcher_font();
    test_hle();
    test_audio_sync();
    test_swr();
    test_discsel();
    test_hid();
    test_session();
    test_data(update);
    fprintf(stderr, "\nResult: %d ok, %d failures, %d group(s) skipped\n", g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
