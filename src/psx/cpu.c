/* MIPS R3000A (PlayStation CPU) interpreter.
 * Implements: all R3000A integer instructions, load delay slots, branch delay
 * slots, COP0 exceptions/interrupts, COP2 (GTE) transfer instructions.
 * Reference: psx-spx "CPU Specifications". */
#include "psx.h"

#include <stdio.h>
#include <string.h>

/* Reserved primary opcode used for HLE kernel traps (only honoured inside the
 * kernel area 0x00000000-0x0000FFFF, which the HLE BIOS populates). */
#define OP_HLE 0x1B

static const char *k_regnames[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

const char *cpu_reg_name(int r) { return k_regnames[r & 31]; }

void cpu_reset(psx *p)
{
    psx_cpu *c = &p->cpu;
    memset(c, 0, sizeof(*c));
    c->pc = 0xBFC00000;
    c->next_pc = c->pc + 4;
    c->cop0[C0_SR] = 0x00400000; /* BEV=1 */
    c->cop0[C0_PRID] = 0x00000002;
}

void cpu_set_pc(psx *p, u32 pc)
{
    p->cpu.pc = pc;
    p->cpu.next_pc = pc + 4;
    p->cpu.branch = false;
    p->cpu.in_delay = false;
}

void cpu_exception(psx *p, u32 code)
{
    psx_cpu *c = &p->cpu;
    u32 sr = c->cop0[C0_SR];
    u32 cause = c->cop0[C0_CAUSE];
    sr = (sr & ~0x3Fu) | ((sr << 2) & 0x3Fu);
    cause = (cause & ~0x7Cu) | ((code & 0x1F) << 2);
    if (c->in_delay) {
        c->cop0[C0_EPC] = c->cur_pc - 4;
        cause |= 0x80000000u;
    } else {
        c->cop0[C0_EPC] = c->cur_pc;
        cause &= ~0x80000000u;
    }
    c->cop0[C0_SR] = sr;
    c->cop0[C0_CAUSE] = cause;
    c->pc = (sr & (1u << 22)) ? 0xBFC00180u : 0x80000080u;
    c->next_pc = c->pc + 4;
    c->branch = false;
    c->in_delay = false;
}

/* ------------------------------------------------------------------------ */

static inline void set_reg(psx_cpu *c, u32 r, u32 v, u32 *ld_now_reg)
{
    c->r[r] = v;
    if (*ld_now_reg == r) *ld_now_reg = 0;
}

/* Fetch: fast path for RAM and BIOS. */
static inline bool fetch(psx *p, u32 pc, u32 *out)
{
    u32 phys = pc & 0x1FFFFFFFu;
    if (NR_UNLIKELY(pc & 3)) return false;
    if (NR_LIKELY(phys < 0x00800000u)) {
        *out = nr_rd32(p->ram + (phys & (PSX_RAM_SIZE - 1)));
        return true;
    }
    if (phys >= 0x1FC00000u && phys < 0x1FC80000u) {
        *out = nr_rd32(p->bios + (phys & (PSX_BIOS_SIZE - 1)));
        return true;
    }
    *out = bus_read32(p, pc);
    return true;
}

/* Extra cycles of a CPU load (w = access width in bytes), from hardware
 * measurements (ps1-tests "cpu/access-time", real console log): main RAM
 * ~5, scratchpad ~1, most I/O ports ~3, CD-ROM 8/14/26, SPU 18/18/39,
 * BIOS ROM 8/13/25, expansion 1 7/14/26 (8/16/32-bit). */
static inline u32 load_cost(u32 addr, int w)
{
    u32 phys = addr & 0x1FFFFFFFu;
    if (phys < 0x00800000u) return 5;
    if (phys >= 0x1F800000u && phys < 0x1F800400u) return 1;
    if (phys >= 0x1F801800u && phys < 0x1F801810u) return w == 1 ? 8 : w == 2 ? 14 : 26;
    if (phys >= 0x1F801C00u && phys < 0x1F802000u) return w == 4 ? 39 : 18;
    if (phys >= 0x1FC00000u && phys < 0x1FC80000u) return w == 1 ? 8 : w == 2 ? 13 : 25;
    if (phys >= 0x1F000000u && phys < 0x1F800000u) return w == 1 ? 7 : w == 2 ? 14 : 26;
    return 3;
}

/* Instruction cache, timing only: 4 KB, 256 direct-mapped lines of 4 words;
 * a miss fills from the missed word to the end of the line. Instructions are
 * always read from RAM (the contents are not modelled), so this cannot change
 * what executes, only how long it takes. Miss cost ~3 cycles plus one per
 * filled word. */
static inline u32 icache_cost(psx_cpu *c, u32 pc)
{
    u32 line = (pc >> 4) & 255, tag = (pc & 0x1FFFF000u) | 1u, word = (pc >> 2) & 3;
    if (c->ic_tag[line] == tag && (c->ic_valid[line] & (1u << word))) return 0;
    if (c->ic_tag[line] != tag) { c->ic_tag[line] = tag; c->ic_valid[line] = 0; }
    c->ic_valid[line] |= (u8)(0xFu & (0xFu << word));
    return 3 + (4 - word);
}

void cpu_icache_flush(psx *p)
{
    memset(p->cpu.ic_tag, 0, sizeof(p->cpu.ic_tag));
    memset(p->cpu.ic_valid, 0, sizeof(p->cpu.ic_valid));
}

static void address_error(psx *p, u32 code, u32 addr)
{
    p->cpu.cop0[C0_BADVADDR] = addr;
    cpu_exception(p, code);
}

/* COP0 register read/write */
static u32 cop0_read(psx *p, u32 r)
{
    switch (r) {
    case C0_BPC: case C0_BDA: case C0_JUMPDEST: case C0_DCIC: case C0_BADVADDR:
    case C0_BDAM: case C0_BPCM: case C0_SR: case C0_CAUSE: case C0_EPC: case C0_PRID:
        return p->cpu.cop0[r];
    default:
        return 0;
    }
}

static void cop0_write(psx *p, u32 r, u32 v)
{
    switch (r) {
    case C0_SR:
        p->cpu.cop0[C0_SR] = v;
        psx_update_irq(p);
        break;
    case C0_CAUSE:
        p->cpu.cop0[C0_CAUSE] = (p->cpu.cop0[C0_CAUSE] & ~0x300u) | (v & 0x300u);
        psx_update_irq(p);
        break;
    case C0_BPC: case C0_BDA: case C0_JUMPDEST: case C0_DCIC: case C0_BDAM: case C0_BPCM: case C0_EPC:
        p->cpu.cop0[r] = v;
        break;
    default:
        break;
    }
}

static inline void stall_muldiv(psx *p)
{
    if (p->cpu.muldiv_ready > p->cycles) p->cycles = p->cpu.muldiv_ready;
}

static int mult_cycles(u32 rs_val, bool sign)
{
    u32 v = rs_val;
    if (sign && (s32)v < 0) v = ~v;
    if (v < 0x800) return 6;
    if (v < 0x100000) return 9;
    return 13;
}

void cpu_run(psx *p, u64 until)
{
    psx_cpu *c = &p->cpu;
    u32 ld_now_reg, ld_now_val;
    const u32 cpi = p->cycles_per_instr ? p->cycles_per_instr : 1;

    while (p->cycles < until && !p->stop) {
        u32 instr, op;
        /* Interrupt check (before executing the instruction at pc). */
        if ((c->cop0[C0_SR] & 1) && (c->cop0[C0_SR] & c->cop0[C0_CAUSE] & 0xFF00u)) {
            c->cur_pc = c->pc;
            c->in_delay = c->branch;
            /* A pending load completes before the exception is taken. */
            if (c->pend_reg) { c->r[c->pend_reg] = c->pend_val; c->pend_reg = 0; }
            c->r[0] = 0;
            cpu_exception(p, EXC_INT);
            continue;
        }
        if (NR_UNLIKELY(p->stall != 0) && c->pc == p->stall_pc && !c->branch) {
            u64 n = until - p->cycles;
            if (n > p->stall) n = p->stall;
            p->cycles += n;
            p->stall -= n;
            continue;
        }
        if (NR_UNLIKELY(p->ovr_bitmap != NULL)) {
            u32 phys = c->pc & 0x1FFFFFFFu;
            if (phys < PSX_RAM_SIZE && (p->ovr_bitmap[phys >> 5] & (1u << ((phys >> 2) & 7)))) {
                if (!c->branch && psx_override_try(p, c->pc)) continue;
            }
        }
        if (NR_UNLIKELY(!fetch(p, c->pc, &instr))) {
            c->cur_pc = c->pc;
            c->in_delay = c->branch;
            address_error(p, EXC_ADEL, c->pc);
            continue;
        }
        if (NR_UNLIKELY(p->trace)) {
            char buf[96];
            cpu_disasm(c->pc, instr, buf, sizeof(buf));
            NR_TRACE("cpu", "%08X: %08X  %s", c->pc, instr, buf);
        }
        if (NR_UNLIKELY(c->pc == p->watch_pc && p->watch_pc)) {
            NR_INFO("cpu", "watch pc %08X hit (ra=%08X sp=%08X a0=%08X a1=%08X SR=%08X I_STAT=%03X I_MASK=%03X frame=%llu)", c->pc, c->r[31], c->r[29], c->r[4], c->r[5], c->cop0[C0_SR], p->i_stat, p->i_mask, (unsigned long long)p->gpu.frame_count);
        }
        c->last_instr = instr;
        c->cur_pc = c->pc;
        c->in_delay = c->branch;
        c->branch = false;
        c->pc = c->next_pc;
        c->next_pc += 4;
        ld_now_reg = c->pend_reg;
        ld_now_val = c->pend_val;
        c->pend_reg = 0;
        if (NR_UNLIKELY(p->cpu_overclock > 1)) {
            if (++p->oc_count >= p->cpu_overclock) { p->oc_count = 0; p->cycles += cpi; }
        } else {
            p->cycles += cpi;
        }
        p->instr_count++;
        if ((c->cur_pc & 0xE0000000u) == 0xA0000000u || (c->cur_pc & 0x1FFFFFFFu) >= 0x1FC00000u) p->cycles += 3;
        else if ((c->cur_pc & 0x1FFFFFFFu) < 0x00800000u) p->cycles += icache_cost(c, c->cur_pc);

        op = instr >> 26;
        {
            const u32 rs = (instr >> 21) & 31, rt = (instr >> 16) & 31, rd = (instr >> 11) & 31;
            const u32 sa = (instr >> 6) & 31;
            const u32 imm = instr & 0xFFFF;
            const u32 simm = (u32)(s32)(s16)imm;
            const u32 vs = c->r[rs], vt = c->r[rt];

#define SETR(r_, v_) set_reg(c, (r_), (v_), &ld_now_reg)
#define BRANCH(cond_) do { c->branch = true; if (cond_) c->next_pc = c->pc + (simm << 2); } while (0)
#define LOADDELAY(r_, v_) do { if (r_) { if (ld_now_reg == (r_)) ld_now_reg = 0; c->pend_reg = (r_); c->pend_val = (v_); } } while (0)

            switch (op) {
            case 0x00: /* SPECIAL */
                switch (instr & 0x3F) {
                case 0x00: SETR(rd, vt << sa); break;                         /* SLL */
                case 0x02: SETR(rd, vt >> sa); break;                         /* SRL */
                case 0x03: SETR(rd, (u32)((s32)vt >> sa)); break;             /* SRA */
                case 0x04: SETR(rd, vt << (vs & 31)); break;                  /* SLLV */
                case 0x06: SETR(rd, vt >> (vs & 31)); break;                  /* SRLV */
                case 0x07: SETR(rd, (u32)((s32)vt >> (vs & 31))); break;      /* SRAV */
                case 0x08: /* JR */
                    c->branch = true;
                    c->next_pc = vs;
                    break;
                case 0x09: /* JALR */
                    c->branch = true;
                    SETR(rd, c->pc + 4);
                    c->next_pc = vs;
                    break;
                case 0x0C: /* SYSCALL */
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    cpu_exception(p, EXC_SYS);
                    continue;
                case 0x0D: /* BREAK */
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    cpu_exception(p, EXC_BP);
                    continue;
                case 0x10: stall_muldiv(p); SETR(rd, c->hi); break;           /* MFHI */
                case 0x11: c->hi = vs; break;                                 /* MTHI */
                case 0x12: stall_muldiv(p); SETR(rd, c->lo); break;           /* MFLO */
                case 0x13: c->lo = vs; break;                                 /* MTLO */
                case 0x18: { /* MULT */
                    s64 r = (s64)(s32)vs * (s64)(s32)vt;
                    c->hi = (u32)((u64)r >> 32); c->lo = (u32)r;
                    c->muldiv_ready = p->cycles + (u64)mult_cycles(vs, true);
                    break;
                }
                case 0x19: { /* MULTU */
                    u64 r = (u64)vs * (u64)vt;
                    c->hi = (u32)(r >> 32); c->lo = (u32)r;
                    c->muldiv_ready = p->cycles + (u64)mult_cycles(vs, false);
                    break;
                }
                case 0x1A: { /* DIV */
                    s32 n = (s32)vs, d = (s32)vt;
                    if (d == 0) { c->hi = (u32)n; c->lo = (n >= 0) ? 0xFFFFFFFFu : 1u; }
                    else if ((u32)n == 0x80000000u && d == -1) { c->hi = 0; c->lo = 0x80000000u; }
                    else { c->lo = (u32)(n / d); c->hi = (u32)(n % d); }
                    c->muldiv_ready = p->cycles + 36;
                    break;
                }
                case 0x1B: { /* DIVU */
                    if (vt == 0) { c->hi = vs; c->lo = 0xFFFFFFFFu; }
                    else { c->lo = vs / vt; c->hi = vs % vt; }
                    c->muldiv_ready = p->cycles + 36;
                    break;
                }
                case 0x20: { /* ADD */
                    u32 r = vs + vt;
                    if (((vs ^ r) & (vt ^ r)) & 0x80000000u) {
                        if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                        c->r[0] = 0;
                        cpu_exception(p, EXC_OV);
                        continue;
                    }
                    SETR(rd, r);
                    break;
                }
                case 0x21: SETR(rd, vs + vt); break;                          /* ADDU */
                case 0x22: { /* SUB */
                    u32 r = vs - vt;
                    if (((vs ^ vt) & (vs ^ r)) & 0x80000000u) {
                        if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                        c->r[0] = 0;
                        cpu_exception(p, EXC_OV);
                        continue;
                    }
                    SETR(rd, r);
                    break;
                }
                case 0x23: SETR(rd, vs - vt); break;                          /* SUBU */
                case 0x24: SETR(rd, vs & vt); break;                          /* AND */
                case 0x25: SETR(rd, vs | vt); break;                          /* OR */
                case 0x26: SETR(rd, vs ^ vt); break;                          /* XOR */
                case 0x27: SETR(rd, ~(vs | vt)); break;                       /* NOR */
                case 0x2A: SETR(rd, (s32)vs < (s32)vt); break;                /* SLT */
                case 0x2B: SETR(rd, vs < vt); break;                          /* SLTU */
                default:
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    cpu_exception(p, EXC_RI);
                    continue;
                }
                break;
            case 0x01: { /* REGIMM: BLTZ/BGEZ/BLTZAL/BGEZAL (decoded like hardware: bit16 = GE, bits 20..17 == 0x8 -> link) */
                bool ge = (instr >> 16) & 1;
                bool link = ((instr >> 17) & 0xF) == 0x8;
                bool cond = ge ? ((s32)vs >= 0) : ((s32)vs < 0);
                if (link) SETR(31, c->pc + 4);
                BRANCH(cond);
                break;
            }
            case 0x02: /* J */
                c->branch = true;
                c->next_pc = (c->pc & 0xF0000000u) | ((instr & 0x03FFFFFFu) << 2);
                break;
            case 0x03: /* JAL */
                c->branch = true;
                SETR(31, c->pc + 4);
                c->next_pc = (c->pc & 0xF0000000u) | ((instr & 0x03FFFFFFu) << 2);
                break;
            case 0x04: BRANCH(vs == vt); break;                               /* BEQ */
            case 0x05: BRANCH(vs != vt); break;                               /* BNE */
            case 0x06: BRANCH((s32)vs <= 0); break;                           /* BLEZ */
            case 0x07: BRANCH((s32)vs > 0); break;                            /* BGTZ */
            case 0x08: { /* ADDI */
                u32 r = vs + simm;
                if (((vs ^ r) & (simm ^ r)) & 0x80000000u) {
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    cpu_exception(p, EXC_OV);
                    continue;
                }
                SETR(rt, r);
                break;
            }
            case 0x09: SETR(rt, vs + simm); break;                            /* ADDIU */
            case 0x0A: SETR(rt, (s32)vs < (s32)simm); break;                  /* SLTI */
            case 0x0B: SETR(rt, vs < simm); break;                            /* SLTIU */
            case 0x0C: SETR(rt, vs & imm); break;                             /* ANDI */
            case 0x0D: SETR(rt, vs | imm); break;                             /* ORI */
            case 0x0E: SETR(rt, vs ^ imm); break;                             /* XORI */
            case 0x0F: SETR(rt, imm << 16); break;                            /* LUI */
            case 0x10: { /* COP0 */
                u32 sub = rs;
                if (sub == 0x00) { LOADDELAY(rt, cop0_read(p, rd)); }          /* MFC0 */
                else if (sub == 0x04) { cop0_write(p, rd, vt); }               /* MTC0 */
                else if (sub == 0x10 && (instr & 0x3F) == 0x10) {              /* RFE */
                    u32 sr = c->cop0[C0_SR];
                    c->cop0[C0_SR] = (sr & ~0x0Fu) | ((sr >> 2) & 0x0Fu);
                    psx_update_irq(p);
                } else {
                    nr_unimplemented("cpu", "COP0 instruction variant");
                }
                break;
            }
            case 0x12: { /* COP2 (GTE) */
                if (!(c->cop0[C0_SR] & (1u << 30))) {
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    c->cop0[C0_CAUSE] = (c->cop0[C0_CAUSE] & ~0x30000000u) | (2u << 28);
                    cpu_exception(p, EXC_CPU);
                    continue;
                }
                if (rs & 0x10) {
                    p->cycles += (u64)gte_command(&p->gte, instr & 0x01FFFFFFu);
                } else {
                    switch (rs) {
                    case 0x00: LOADDELAY(rt, gte_read_data(&p->gte, (int)rd)); break;   /* MFC2 */
                    case 0x02: LOADDELAY(rt, gte_read_ctrl(&p->gte, (int)rd)); break;   /* CFC2 */
                    case 0x04: gte_write_data(&p->gte, (int)rd, vt); break;             /* MTC2 */
                    case 0x06: gte_write_ctrl(&p->gte, (int)rd, vt); break;             /* CTC2 */
                    default: nr_unimplemented("cpu", "COP2 transfer variant"); break;
                    }
                }
                break;
            }
            case 0x11: case 0x13: /* COP1/COP3: not present */
                if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                c->r[0] = 0;
                c->cop0[C0_CAUSE] = (c->cop0[C0_CAUSE] & ~0x30000000u) | ((op & 3u) << 28);
                cpu_exception(p, EXC_CPU);
                continue;
            case 0x20: { /* LB */
                u32 a = vs + simm;
                if (c->cop0[C0_SR] & 0x10000) break;
                p->cycles += load_cost(a, 1);
                LOADDELAY(rt, (u32)(s32)(s8)bus_read8(p, a));
                break;
            }
            case 0x21: { /* LH */
                u32 a = vs + simm;
                if (a & 1) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADEL, a); continue; }
                if (c->cop0[C0_SR] & 0x10000) break;
                p->cycles += load_cost(a, 2);
                LOADDELAY(rt, (u32)(s32)(s16)bus_read16(p, a));
                break;
            }
            case 0x22: case 0x26: { /* LWL / LWR */
                u32 a = vs + simm;
                u32 cur = (c->pend_reg == rt && rt) ? c->pend_val : ((ld_now_reg == rt && rt) ? ld_now_val : vt);
                u32 w, sh = a & 3, r;
                p->cycles += load_cost(a, 4);
                w = bus_read32(p, a & ~3u);
                if (op == 0x22) r = (cur & (0x00FFFFFFu >> (sh * 8))) | (w << (24 - sh * 8));
                else r = (cur & (0xFFFFFF00u << ((3 - sh) * 8))) | (w >> (sh * 8));
                LOADDELAY(rt, r);
                break;
            }
            case 0x23: { /* LW */
                u32 a = vs + simm;
                if (a & 3) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADEL, a); continue; }
                if (c->cop0[C0_SR] & 0x10000) break;
                p->cycles += load_cost(a, 4);
                LOADDELAY(rt, bus_read32(p, a));
                break;
            }
            case 0x24: { /* LBU */
                u32 a = vs + simm;
                if (c->cop0[C0_SR] & 0x10000) break;
                p->cycles += load_cost(a, 1);
                LOADDELAY(rt, bus_read8(p, a));
                break;
            }
            case 0x25: { /* LHU */
                u32 a = vs + simm;
                if (a & 1) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADEL, a); continue; }
                if (c->cop0[C0_SR] & 0x10000) break;
                p->cycles += load_cost(a, 2);
                LOADDELAY(rt, bus_read16(p, a));
                break;
            }
            case 0x28: { /* SB */
                u32 a = vs + simm;
                if (c->cop0[C0_SR] & 0x10000) break;   /* cache isolated */
                bus_write8(p, a, (u8)vt);
                break;
            }
            case 0x29: { /* SH */
                u32 a = vs + simm;
                if (a & 1) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADES, a); continue; }
                if (c->cop0[C0_SR] & 0x10000) break;
                bus_write16(p, a, (u16)vt);
                break;
            }
            case 0x2A: case 0x2E: { /* SWL / SWR */
                u32 a = vs + simm, sh = a & 3, w, r;
                if (c->cop0[C0_SR] & 0x10000) break;
                w = bus_read32(p, a & ~3u);
                if (op == 0x2A) r = (w & (0xFFFFFF00u << (sh * 8))) | (vt >> (24 - sh * 8));
                else r = (w & (0x00FFFFFFu >> ((3 - sh) * 8))) | (vt << (sh * 8));
                bus_write32(p, a & ~3u, r);
                break;
            }
            case 0x2B: { /* SW */
                u32 a = vs + simm;
                if (a & 3) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADES, a); continue; }
                if (c->cop0[C0_SR] & 0x10000) break;
                bus_write32(p, a, vt);
                break;
            }
            case 0x32: { /* LWC2 */
                u32 a = vs + simm;
                if (a & 3) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADEL, a); continue; }
                p->cycles += load_cost(a, 4);
                gte_write_data(&p->gte, (int)rt, bus_read32(p, a));
                break;
            }
            case 0x3A: { /* SWC2 */
                u32 a = vs + simm;
                if (a & 3) { if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; } c->r[0] = 0; address_error(p, EXC_ADES, a); continue; }
                bus_write32(p, a, gte_read_data(&p->gte, (int)rt));
                break;
            }
            case OP_HLE:
                if ((c->cur_pc & 0x1FFFFFFFu) < 0x10000u && p->hle) {
                    if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                    c->r[0] = 0;
                    hle_trap(p, instr & 0x03FFFFFFu);
                    continue;
                }
                /* fallthrough */
            default:
                if (ld_now_reg) { c->r[ld_now_reg] = ld_now_val; ld_now_reg = 0; }
                c->r[0] = 0;
                NR_DBG("cpu", "reserved instruction %08X at %08X", instr, c->cur_pc);
                cpu_exception(p, EXC_RI);
                continue;
            }
#undef SETR
#undef BRANCH
#undef LOADDELAY
        }
        if (ld_now_reg) c->r[ld_now_reg] = ld_now_val;
        c->r[0] = 0;
    }
}

/* ------------------------------------------------------------ disassembler */
void cpu_disasm(u32 pc, u32 in, char *out, size_t n)
{
    static const char *spec[64] = {
        "sll", 0, "srl", "sra", "sllv", 0, "srlv", "srav", "jr", "jalr", 0, 0, "syscall", "break", 0, 0,
        "mfhi", "mthi", "mflo", "mtlo", 0, 0, 0, 0, "mult", "multu", "div", "divu", 0, 0, 0, 0,
        "add", "addu", "sub", "subu", "and", "or", "xor", "nor", 0, 0, "slt", "sltu", 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const char *prim[64] = {
        0, 0, "j", "jal", "beq", "bne", "blez", "bgtz", "addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui",
        "cop0", "cop1", "cop2", "cop3", 0, 0, 0, 0, 0, 0, 0, "hle", 0, 0, 0, 0,
        "lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", 0, "sb", "sh", "swl", "sw", 0, 0, "swr", 0,
        0, 0, "lwc2", 0, 0, 0, 0, 0, 0, 0, "swc2", 0, 0, 0, 0, 0 };
    u32 op = in >> 26, rs = (in >> 21) & 31, rt = (in >> 16) & 31, rd = (in >> 11) & 31, sa = (in >> 6) & 31;
    s32 simm = (s16)(in & 0xFFFF);
    if (in == 0) { snprintf(out, n, "nop"); return; }
    if (op == 0) {
        u32 f = in & 63;
        const char *m = spec[f] ? spec[f] : "???";
        if (f <= 3) snprintf(out, n, "%s %s,%s,%u", m, k_regnames[rd], k_regnames[rt], sa);
        else if (f <= 7) snprintf(out, n, "%s %s,%s,%s", m, k_regnames[rd], k_regnames[rt], k_regnames[rs]);
        else if (f == 8) snprintf(out, n, "jr %s", k_regnames[rs]);
        else if (f == 9) snprintf(out, n, "jalr %s,%s", k_regnames[rd], k_regnames[rs]);
        else if (f == 12 || f == 13) snprintf(out, n, "%s 0x%X", m, (in >> 6) & 0xFFFFF);
        else if (f == 16 || f == 18) snprintf(out, n, "%s %s", m, k_regnames[rd]);
        else if (f == 17 || f == 19) snprintf(out, n, "%s %s", m, k_regnames[rs]);
        else if (f >= 24 && f <= 27) snprintf(out, n, "%s %s,%s", m, k_regnames[rs], k_regnames[rt]);
        else snprintf(out, n, "%s %s,%s,%s", m, k_regnames[rd], k_regnames[rs], k_regnames[rt]);
        return;
    }
    if (op == 1) {
        snprintf(out, n, "%s %s,%08X", (rt & 1) ? ((rt & 0x10) ? "bgezal" : "bgez") : ((rt & 0x10) ? "bltzal" : "bltz"),
                 k_regnames[rs], pc + 4 + (u32)(simm << 2));
        return;
    }
    if (op == 2 || op == 3) { snprintf(out, n, "%s %08X", prim[op], ((pc + 4) & 0xF0000000u) | ((in & 0x3FFFFFF) << 2)); return; }
    if (op >= 4 && op <= 7) {
        if (op <= 5) snprintf(out, n, "%s %s,%s,%08X", prim[op], k_regnames[rs], k_regnames[rt], pc + 4 + (u32)(simm << 2));
        else snprintf(out, n, "%s %s,%08X", prim[op], k_regnames[rs], pc + 4 + (u32)(simm << 2));
        return;
    }
    if (op >= 8 && op <= 14) { snprintf(out, n, "%s %s,%s,%d", prim[op], k_regnames[rt], k_regnames[rs], (op >= 12) ? (int)(in & 0xFFFF) : simm); return; }
    if (op == 15) { snprintf(out, n, "lui %s,0x%04X", k_regnames[rt], in & 0xFFFF); return; }
    if (op == 0x10) {
        if (rs == 0) snprintf(out, n, "mfc0 %s,cop0r%u", k_regnames[rt], rd);
        else if (rs == 4) snprintf(out, n, "mtc0 %s,cop0r%u", k_regnames[rt], rd);
        else if ((in & 0x3F) == 0x10) snprintf(out, n, "rfe");
        else snprintf(out, n, "cop0 %07X", in & 0x1FFFFFF);
        return;
    }
    if (op == 0x12) {
        if (rs & 0x10) snprintf(out, n, "gte cmd %07X (op %02X)", in & 0x1FFFFFF, in & 0x3F);
        else if (rs == 0) snprintf(out, n, "mfc2 %s,gd%u", k_regnames[rt], rd);
        else if (rs == 2) snprintf(out, n, "cfc2 %s,gc%u", k_regnames[rt], rd);
        else if (rs == 4) snprintf(out, n, "mtc2 %s,gd%u", k_regnames[rt], rd);
        else if (rs == 6) snprintf(out, n, "ctc2 %s,gc%u", k_regnames[rt], rd);
        else snprintf(out, n, "cop2 ???");
        return;
    }
    if (op == OP_HLE) { snprintf(out, n, "hle_trap 0x%X", in & 0x3FFFFFF); return; }
    if (prim[op]) { snprintf(out, n, "%s %s,%d(%s)", prim[op], (op == 0x32 || op == 0x3A) ? "gte" : k_regnames[rt], simm, k_regnames[rs]); return; }
    snprintf(out, n, "??? %08X", in);
}
