/* Native function overrides: the mechanism used to progressively replace
 * original game routines (executed by the core) with reimplemented C code.
 *
 * When the CPU reaches a registered address (outside a branch delay slot)
 * the native function runs instead of the original and returns like
 * "jr ra". Its modelled execution time is added to the cycle counter so
 * that frame timing stays faithful.
 *
 * Verification mode (--verify-natives): every call runs the ORIGINAL code
 * first (nested), then restores the machine and runs the native version,
 * comparing RAM, scratchpad and the result registers. The original's result
 * is kept, so a defective native can never change the game's behaviour in
 * this mode. */
#include "psx.h"

#include <stdlib.h>
#include <string.h>

void psx_override_add(psx *p, u32 addr, psx_native_fn fn, const char *name, bool observe_only)
{
    u32 phys = addr & 0x1FFFFFFFu;
    psx_override *o;
    if (phys >= PSX_RAM_SIZE || (addr & 3)) { NR_ERR("ovr", "invalid override address: %08X", addr); return; }
    if (!p->ovr_bitmap) p->ovr_bitmap = (u8 *)calloc(PSX_RAM_SIZE / 32, 1);
    if (p->novr == p->ovr_cap) {
        p->ovr_cap = p->ovr_cap ? p->ovr_cap * 2 : 16;
        p->ovr = (psx_override *)realloc(p->ovr, sizeof(psx_override) * (size_t)p->ovr_cap);
    }
    o = &p->ovr[p->novr++];
    memset(o, 0, sizeof(*o));
    o->addr = addr;
    o->fn = fn;
    o->name = name;
    o->observe_only = observe_only;
    p->ovr_bitmap[phys >> 5] |= (u8)(1u << ((phys >> 2) & 7));
    NR_INFO("ovr", "native routine active: %s @ %08X%s", name, addr, observe_only ? " (observer)" : "");
}

static void report_diff(psx *p, psx_override *o, const u8 *a, const u8 *b, u32 len, const char *what)
{
    u32 i, first = 0xFFFFFFFFu, last = 0, n = 0;
    for (i = 0; i < len; i++) if (a[i] != b[i]) { if (first == 0xFFFFFFFFu) first = i; last = i; n++; }
    if (n) NR_ERR("ovr", "%s: mismatch in %s: %u bytes between +%06X and +%06X (caller pc %08X)", o->name, what, n, first, last, p->cpu.r[31]);
}

static bool verify_call(psx *p, psx_override *o)
{
    u8 *ram0 = (u8 *)malloc(PSX_RAM_SIZE), *ram_orig = (u8 *)malloc(PSX_RAM_SIZE);
    u8 scr0[PSX_SCRATCH_SIZE], scr_orig[PSX_SCRATCH_SIZE];
    psx_cpu cpu0 = p->cpu, after;
    u64 c0 = p->cycles, orig_cycles, ncyc = 0;
    bool handled, same;
    if (!ram0 || !ram_orig) { free(ram0); free(ram_orig); return false; }
    memcpy(ram0, p->ram, PSX_RAM_SIZE);
    memcpy(scr0, p->scratch, sizeof(scr0));

    /* 1) original (interrupts held off so that IRQ handlers cannot touch RAM
     *    in between; they run right after the call) */
    p->ovr_busy = true;
    {
        u32 sr = p->cpu.cop0[C0_SR];
        p->cpu.cop0[C0_SR] &= ~1u;
        hle_call_capture(p, p->cpu.pc, &after);
        p->cpu.cop0[C0_SR] = sr;
    }
    p->ovr_busy = false;
    orig_cycles = p->cycles - c0;
    memcpy(ram_orig, p->ram, PSX_RAM_SIZE);
    memcpy(scr_orig, p->scratch, sizeof(scr_orig));

    /* 2) native from the same starting state */
    memcpy(p->ram, ram0, PSX_RAM_SIZE);
    memcpy(p->scratch, scr0, sizeof(scr0));
    p->cpu = cpu0;
    handled = o->fn(p, &ncyc);
    if (!handled) {
        o->declined++;
    } else {
        same = memcmp(p->ram, ram_orig, PSX_RAM_SIZE) == 0 && memcmp(p->scratch, scr_orig, sizeof(scr0)) == 0 &&
               p->cpu.r[2] == after.r[2];
        if (same) o->verified_ok++;
        else {
            o->verified_bad++;
            report_diff(p, o, ram_orig, p->ram, PSX_RAM_SIZE, "RAM");
            report_diff(p, o, scr_orig, p->scratch, sizeof(scr0), "scratchpad");
            if (p->cpu.r[2] != after.r[2]) NR_ERR("ovr", "%s: v0 original=%08X native=%08X", o->name, after.r[2], p->cpu.r[2]);
        }
        o->orig_cycles += orig_cycles;
        o->native_cycles += ncyc;
    }
    /* 3) keep the original's results and timing */
    memcpy(p->ram, ram_orig, PSX_RAM_SIZE);
    memcpy(p->scratch, scr_orig, sizeof(scr0));
    {
        u32 cop0[32];
        memcpy(cop0, p->cpu.cop0, sizeof(cop0));
        p->cpu = after;
        memcpy(p->cpu.cop0, cop0, sizeof(cop0));
    }
    cpu_set_pc(p, cpu0.r[31]);
    p->cpu.r[31] = cpu0.r[31];
    free(ram0);
    free(ram_orig);
    return true;
}

bool psx_override_try(psx *p, u32 pc)
{
    int i;
    u32 phys = pc & 0x1FFFFFFFu;
    if (p->ovr_busy) return false;
    for (i = 0; i < p->novr; i++) {
        psx_override *o = &p->ovr[i];
        u64 cyc = 0;
        if ((o->addr & 0x1FFFFFFFu) != phys) continue;
        o->calls++;
        if (o->observe_only) { o->fn(p, &cyc); return false; }
        if (p->ovr_verify) return verify_call(p, o);
        if (o->fn(p, &cyc)) {
            NR_DBG("ovr", "%s cycle=%llu cost=%llu", o->name, (unsigned long long)p->cycles, (unsigned long long)cyc);
            cpu_set_pc(p, p->cpu.r[31]);
            p->stall = cyc;
            p->stall_pc = p->cpu.pc;
            return true;
        }
        o->declined++;
        return false; /* native code declined: run the original */
    }
    return false;
}

void psx_override_report(psx *p)
{
    int i;
    for (i = 0; i < p->novr; i++) {
        psx_override *o = &p->ovr[i];
        NR_INFO("ovr", "%-34s @ %08X calls=%llu declined=%llu verified_ok=%llu mismatched=%llu", o->name, o->addr,
                (unsigned long long)o->calls, (unsigned long long)o->declined, (unsigned long long)o->verified_ok,
                (unsigned long long)o->verified_bad);
        if (o->orig_cycles)
            NR_INFO("ovr", "    measured cycles: original=%llu native model=%llu (%.3f)", (unsigned long long)o->orig_cycles,
                    (unsigned long long)o->native_cycles, (double)o->native_cycles / (double)o->orig_cycles);
    }
}
