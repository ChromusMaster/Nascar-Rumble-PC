/* Registry of native reimplementations of original game routines.
 *
 * Every native routine replaces one function of SLUS-01068 (reference
 * executable SHA-1 2726ac3e...). Before registering, the first instructions
 * at each address are compared with the expected original words, so a
 * different executable never gets a wrong replacement. Each routine was
 * validated with --verify-natives (see docs/NATIVE.md). */
#include "../app/app.h"
#include "../formats/formats.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * BS (MDEC bitstream) decoding.
 *   80098F28  InitVLC(lut_a, lut_b, vlc_values)  builds the game's lookup
 *             tables from 0xE0 MDEC values (vlc_values == 0 selects the
 *             built-in standard table at 800AC290). Observed natively to
 *             capture the active table; the original still runs.
 *   8009905C  DecodeBS(frame, out)  -> replaced by nr_bs_decode_game().
 * ---------------------------------------------------------------------- */

static nr_bs_table g_table;
static bool g_table_valid;

static bool nat_init_vlc(psx *p, u64 *cycles)
{
    u32 src = p->cpu.r[6] ? p->cpu.r[6] : 0x800AC290u;
    int i;
    for (i = 0; i < 0xE0; i++) g_table.value[i] = psx_ram_r16(p, src + (u32)i * 2);
    g_table.ea = true;
    g_table_valid = p->cpu.r[4] != 0 && p->cpu.r[5] != 0;
    (void)cycles;
    return false; /* observation only */
}

static bool nat_decode_bs(psx *p, u64 *cycles)
{
    static u16 out[0x20000];
    u32 frame = p->cpu.r[4], dst = p->cpu.r[5];
    u8 *src = psx_ram_ptr(p, frame);
    u32 avail, i;
    int n;
    if (!g_table_valid || !src) return false;
    avail = PSX_RAM_SIZE - ((frame & 0x1FFFFFFFu) & (PSX_RAM_SIZE - 1));
    n = nr_bs_decode_game(src, avail, &g_table, out, NR_ARRAY_COUNT(out));
    if (n < 0) return false;
    for (i = 0; i < (u32)n; i++) psx_ram_w16(p, dst + i * 2, out[i]);
    p->cpu.r[2] = 0;
    /* execution time model, calibrated with --verify-natives (docs/NATIVE.md) */
    *cycles = 200 + (u64)n * NR_NATIVE_BS_CYCLES_PER_HALFWORD;
    return true;
}

/* ------------------------------------------------------------------------ */

typedef struct {
    u32 addr;
    u32 words[4];
    psx_native_fn fn;
    const char *name;
    bool observe;
} native_entry;

static const native_entry k_natives[] = {
    { 0x80098F28u, { 0xAF840448u, 0xAF85044Cu, 0x10800048u, 0x2402FFFFu }, nat_init_vlc, "InitVLC (table capture)", true },
    { 0x8009905Cu, { 0x00804821u, 0x00004021u, 0x8F8C0448u, 0x8F8B044Cu }, nat_decode_bs, "DecodeBS (MDEC decoder)", false },
};

void nr_native_register_all(psx *p)
{
    size_t i;
    for (i = 0; i < NR_ARRAY_COUNT(k_natives); i++) {
        const native_entry *e = &k_natives[i];
        int k;
        bool match = true;
        for (k = 0; k < 4; k++) if (psx_ram_r32(p, e->addr + (u32)k * 4) != e->words[k]) match = false;
        if (!match) {
            NR_WARN("native", "%s: original code at %08X does not match; native routine not enabled", e->name, e->addr);
            continue;
        }
        psx_override_add(p, e->addr, e->fn, e->name, e->observe);
    }
}
