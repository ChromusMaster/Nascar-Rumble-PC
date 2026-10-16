/* High-level emulation of the PlayStation kernel (BIOS).
 *
 * No Sony code is used. The kernel's observable behaviour (function tables,
 * exception/interrupt dispatch through the four priority chains, events,
 * HookEntryInt, pads and memory cards) is reimplemented from the public
 * psx-spx documentation. Small MIPS routines are generated at start-up in
 * kernel RAM so that the exception chain calls game callbacks exactly like
 * the real kernel (including early exits via ReturnFromException).
 *
 * Kernel RAM layout used by this implementation (physical addresses):
 *   0000-00FF vectors (garbage area, exception vector, A0/B0/C0 vectors)
 *   0100-0157 table of tables, 0180 command line
 *   0200-04FF A-table, 0500-06FF C-table, 0700-0AFF B-table
 *   0B00-0BFF helper stubs, 0C80-0D7F C(06h) patch sink
 *   1000-10FF exception handler, 1100-2BFF A/B/C function stubs
 *   3000-4FFF B(5Bh) patch sink (games patch code at B(5Bh)+offset)
 *   D000      exception stack top
 *   E000-EFFF kernel control blocks (ExCB, PCB, TCB, EvCB, pad buffers)
 */
#include "psx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------ layout */
#define K_ATAB      0x0200u
#define K_CTAB      0x0500u
#define K_BTAB      0x0700u
#define K_HELPERS   0x0B00u
#define K_C06_SINK  0x0C80u
#define K_EXC       0x1000u
#define K_ASTUB     0x1100u
#define K_BSTUB     0x1A00u
#define K_CSTUB     0x2600u
#define K_B5B_SINK  0x3000u
#define K_EXC_STACK 0x8000D000u
#define K_EXCB      0xE000u
#define K_PCB       0xE020u
#define K_TCB       0xE030u
#define K_EVCB      0xE340u
#define K_ELEMS     0xE600u   /* default chain elements, 16 bytes each */
#define K_HOOKDEF   0xE700u   /* default HookEntryInt structure (0x30 bytes) */
#define K_PADBUF    0xE740u   /* PAD_init2 internal buffers (2 x 0x22) */

#define N_TCB 4
#define N_EVCB 16

/* helper stub addresses */
#define H_CALL_RETURN (K_HELPERS + 0x00)
#define H_WAITEVENT   (K_HELPERS + 0x10)
#define H_RCNT0       (K_HELPERS + 0x20)  /* 4 stubs, 12 bytes each */
#define H_PADCARD     (K_HELPERS + 0x50)
#define H_DEFINT      (K_HELPERS + 0x60)
#define H_UNIMPL      (K_HELPERS + 0x70)

/* trap ids */
enum {
    T_A = 0x000, T_B = 0x100, T_C = 0x200,
    T_DISPATCH_A = 0x300, T_DISPATCH_B, T_DISPATCH_C,
    T_EXC_ENTER, T_EXC_LEAVE, T_CALL_RETURN,
    T_RCNT0, T_RCNT1, T_RCNT2, T_RCNT3,
    T_PADCARD, T_DEFINT, T_WAITEVENT,
    T_SET_PAD_ENABLE, T_CLR_PAD_ENABLE
};

/* memory card op */
typedef struct {
    int kind;       /* 0 none, 1 read, 2 write, 3 info, 4 load */
    int port;       /* 0 or 1 */
    u32 sector;
    u32 addr;
    int delay;      /* vblanks to wait */
} card_op;

struct psx_hle {
    u32 hook_addr;
    bool clear_rcnt[4];
    bool clear_pad;
    bool pad_enable;
    bool pad_started;
    u32 pad_buf[2], pad_siz[2];
    u32 pad_button_dest;
    bool pad_init2;
    int call_depth;
    bool call_returned[16];
    char memcard_path[2][1024];
    u8 *memcard[2];
    bool memcard_dirty[2];
    bool card_new[2];
    card_op card_q[8];
    int card_qn;
    u32 card_status[2];
    bool card_auto;
    u32 heap_start, heap_size;
    char tty[1024];
    int tty_len;
    u64 calls_a[256], calls_b[256], calls_c[128];
};

/* ------------------------------------------------------------ names */
static const char *a_name(u32 n)
{
    switch (n) {
    case 0x13: return "setjmp"; case 0x14: return "longjmp"; case 0x15: return "strcat"; case 0x16: return "strncat";
    case 0x17: return "strcmp"; case 0x18: return "strncmp"; case 0x19: return "strcpy"; case 0x1A: return "strncpy";
    case 0x1B: return "strlen"; case 0x1C: return "index"; case 0x1D: return "rindex"; case 0x1E: return "strchr";
    case 0x1F: return "strrchr"; case 0x25: return "toupper"; case 0x26: return "tolower"; case 0x27: return "bcopy";
    case 0x28: return "bzero"; case 0x29: return "bcmp"; case 0x2A: return "memcpy"; case 0x2B: return "memset";
    case 0x2C: return "memmove"; case 0x2D: return "memcmp"; case 0x2E: return "memchr"; case 0x2F: return "rand";
    case 0x30: return "srand"; case 0x33: return "malloc"; case 0x34: return "free"; case 0x39: return "InitHeap";
    case 0x3F: return "printf"; case 0x44: return "FlushCache"; case 0x49: return "GPU_cw"; case 0x70: return "_bu_init";
    case 0x72: return "_96_remove"; case 0xAB: return "_card_info"; case 0xAC: return "_card_load"; case 0xAD: return "_card_auto";
    default: return NULL;
    }
}

static const char *b_name(u32 n)
{
    switch (n) {
    case 0x07: return "DeliverEvent"; case 0x08: return "OpenEvent"; case 0x09: return "CloseEvent";
    case 0x0A: return "WaitEvent"; case 0x0B: return "TestEvent"; case 0x0C: return "EnableEvent";
    case 0x0D: return "DisableEvent"; case 0x12: return "InitPAD2"; case 0x13: return "StartPAD2";
    case 0x14: return "StopPAD2"; case 0x15: return "PAD_init2"; case 0x16: return "PAD_dr";
    case 0x17: return "ReturnFromException"; case 0x18: return "ResetEntryInt"; case 0x19: return "HookEntryInt";
    case 0x20: return "UnDeliverEvent"; case 0x38: return "exit"; case 0x3F: return "puts";
    case 0x4A: return "InitCARD2"; case 0x4B: return "StartCARD2"; case 0x4C: return "StopCARD2";
    case 0x4E: return "_card_write"; case 0x4F: return "_card_read"; case 0x50: return "_new_card";
    case 0x56: return "GetC0Table"; case 0x57: return "GetB0Table"; case 0x5B: return "ChangeClearPad";
    case 0x5C: return "_card_status"; case 0x5D: return "_card_wait";
    default: return NULL;
    }
}

/* ------------------------------------------------------------ helpers */
#define R(p, i) ((p)->cpu.r[i])
#define RAM32(p, a) psx_ram_r32((p), (a))
#define WRAM32(p, a, v) psx_ram_w32((p), (a), (v))

static inline u32 enc_hle(u32 id) { return (0x1Bu << 26) | (id & 0x03FFFFFFu); }
static inline u32 enc_jr(u32 rs) { return (rs << 21) | 0x08; }
static inline u32 enc_jalr(u32 rs) { return (rs << 21) | (31u << 11) | 0x09; }
static inline u32 enc_i(u32 op, u32 rs, u32 rt, s32 imm) { return (op << 26) | (rs << 21) | (rt << 16) | ((u32)imm & 0xFFFF); }
static inline u32 enc_r(u32 rs, u32 rt, u32 rd, u32 funct) { return (rs << 21) | (rt << 16) | (rd << 11) | funct; }
#define LW(rt, off, base) enc_i(0x23, base, rt, off)
#define ADDIU(rt, rs, imm) enc_i(0x09, rs, rt, imm)
#define SLTI(rt, rs, imm) enc_i(0x0A, rs, rt, imm)
#define BEQ(rs, rt, off) enc_i(0x04, rs, rt, off)
#define BNE(rs, rt, off) enc_i(0x05, rs, rt, off)
#define ADDU(rd, rs, rt) enc_r(rs, rt, rd, 0x21)
#define NOP 0u
enum { ZERO = 0, V0 = 2, A0 = 4, T0 = 8, T1 = 9, S0 = 16, S1 = 17, RA = 31 };

static void kw(psx *p, u32 addr, u32 v) { nr_wr32(p->ram + (addr & 0x1FFFFF), v); }

static void emit_stub(psx *p, u32 addr, u32 id)
{
    kw(p, addr, enc_hle(id));
    kw(p, addr + 4, enc_jr(RA));
    kw(p, addr + 8, NOP);
}

static void ret(psx *p, u32 v) { R(p, V0) = v; }
static void jump_to(psx *p, u32 pc) { cpu_set_pc(p, pc); }

/* ------------------------------------------------------------ nested call */

static void hle_service_loop(psx *p, int depth)
{
    psx_hle *h = p->hle;
    while (!h->call_returned[depth] && !p->halted) {
        u64 target = p->next_event;
        if (target > p->cycles + 2048) target = p->cycles + 2048;
        if (target <= p->cycles) target = p->cycles + 1;
        p->stop = false;
        cpu_run(p, target);
        if (p->cycles >= p->next_event) psx_dispatch(p);
    }
    p->stop = false;
}

static u32 call_mips(psx *p, u32 func, u32 a0, u32 a1, u32 a2, u32 a3)
{
    psx_hle *h = p->hle;
    psx_cpu save = p->cpu;
    int depth;
    u32 v0;
    if (h->call_depth >= 15) { psx_halt(p, "HLE: too many nested calls"); return 0; }
    depth = ++h->call_depth;
    h->call_returned[depth] = false;
    R(p, 4) = a0; R(p, 5) = a1; R(p, 6) = a2; R(p, 7) = a3;
    R(p, 29) -= 0x20;
    R(p, RA) = 0x80000000u | H_CALL_RETURN;
    p->cpu.pend_reg = 0;
    cpu_set_pc(p, func);
    hle_service_loop(p, depth);
    v0 = R(p, V0);
    h->call_depth--;
    {
        u32 cop0[32];
        memcpy(cop0, p->cpu.cop0, sizeof(cop0));
        p->cpu = save;
        memcpy(p->cpu.cop0, cop0, sizeof(cop0));
    }
    return v0;
}

/* Runs the MIPS function at 'func' with the argument registers currently in
 * the CPU, until it returns. The CPU state right after the return is copied
 * to *after; the caller's CPU state is restored. Used to verify native
 * overrides against the original code. */
void hle_call_capture(psx *p, u32 func, psx_cpu *after)
{
    psx_hle *h = p->hle;
    psx_cpu save = p->cpu;
    int depth;
    if (h->call_depth >= 15) { psx_halt(p, "HLE: too many nested calls"); return; }
    depth = ++h->call_depth;
    h->call_returned[depth] = false;
    R(p, RA) = 0x80000000u | H_CALL_RETURN;
    p->cpu.pend_reg = 0;
    cpu_set_pc(p, func);
    hle_service_loop(p, depth);
    h->call_depth--;
    *after = p->cpu;
    {
        u32 cop0[32];
        memcpy(cop0, p->cpu.cop0, sizeof(cop0));
        p->cpu = save;
        memcpy(p->cpu.cop0, cop0, sizeof(cop0));
    }
}

/* ------------------------------------------------------------ events */

void hle_state_blob(psx *p, void **ptr, size_t *size) { *ptr = p->hle; *size = sizeof(psx_hle); }

/* after a snapshot load: memory-card buffers/paths belong to the running instance */
void hle_restore_host_fields(psx *p, const void *old_blob)
{
    const psx_hle *o = (const psx_hle *)old_blob;
    int i;
    for (i = 0; i < 2; i++) {
        p->hle->memcard[i] = o->memcard[i];
        p->hle->memcard_dirty[i] = o->memcard_dirty[i];
        p->hle->card_new[i] = o->card_new[i];
        memcpy(p->hle->memcard_path[i], o->memcard_path[i], sizeof(o->memcard_path[i]));
    }
}

static u32 evcb_addr(u32 ev) { return K_EVCB + (ev & 0xFFFF) * 0x1C; }

static void deliver_event(psx *p, u32 cls, u32 spec)
{
    int i;
    for (i = 0; i < N_EVCB; i++) {
        u32 a = K_EVCB + (u32)i * 0x1C;
        if (RAM32(p, a + 4) != 0x2000) continue;
        if (RAM32(p, a) != cls || RAM32(p, a + 8) != spec) continue;
        if (RAM32(p, a + 0xC) == 0x2000) {
            WRAM32(p, a + 4, 0x4000);
        } else if (RAM32(p, a + 0xC) == 0x1000) {
            u32 f = RAM32(p, a + 0x10);
            if (f) call_mips(p, f, 0, 0, 0, 0);
        }
    }
}

static void undeliver_event(psx *p, u32 cls, u32 spec)
{
    int i;
    for (i = 0; i < N_EVCB; i++) {
        u32 a = K_EVCB + (u32)i * 0x1C;
        if (RAM32(p, a + 4) == 0x4000 && RAM32(p, a) == cls && RAM32(p, a + 8) == spec && RAM32(p, a + 0xC) == 0x2000)
            WRAM32(p, a + 4, 0x2000);
    }
}

/* ------------------------------------------------------------ exceptions */

static u32 cur_tcb(psx *p) { return RAM32(p, RAM32(p, 0x108)) ; }

static void save_regs(psx *p, u32 tcb)
{
    int i;
    for (i = 1; i < 32; i++) WRAM32(p, tcb + 8 + (u32)i * 4, R(p, i));
    WRAM32(p, tcb + 0x88, p->cpu.cop0[C0_EPC]);
    WRAM32(p, tcb + 0x8C, p->cpu.hi);
    WRAM32(p, tcb + 0x90, p->cpu.lo);
    WRAM32(p, tcb + 0x94, p->cpu.cop0[C0_SR]);
    WRAM32(p, tcb + 0x98, p->cpu.cop0[C0_CAUSE]);
}

static void return_from_exception(psx *p)
{
    u32 tcb = cur_tcb(p);
    u32 sr, epc;
    int i;
    for (i = 1; i < 32; i++) if (i != 26) R(p, i) = RAM32(p, tcb + 8 + (u32)i * 4);
    p->cpu.hi = RAM32(p, tcb + 0x8C);
    p->cpu.lo = RAM32(p, tcb + 0x90);
    sr = RAM32(p, tcb + 0x94);
    sr = (sr & ~0x0Fu) | ((sr >> 2) & 0x0Fu);   /* rfe */
    p->cpu.cop0[C0_SR] = sr;
    epc = RAM32(p, tcb + 0x88);
    R(p, 26) = epc;
    p->cpu.pend_reg = 0;
    jump_to(p, epc);
    psx_update_irq(p);
}

static void exc_enter(psx *p)
{
    u32 tcb = cur_tcb(p);
    u32 cause = p->cpu.cop0[C0_CAUSE];
    u32 code = (cause >> 2) & 0x1F;
    save_regs(p, tcb);
    if (code == EXC_SYS) {
        u32 a0 = RAM32(p, tcb + 8 + 4 * 4);
        u32 sr = RAM32(p, tcb + 0x94);
        switch (a0) {
        case 0: break;
        case 1: /* EnterCriticalSection */
            WRAM32(p, tcb + 8 + 2 * 4, ((sr & 0x404) == 0x404) ? 1u : 0u);
            WRAM32(p, tcb + 0x94, sr & ~0x404u);
            break;
        case 2: /* ExitCriticalSection */
            WRAM32(p, tcb + 0x94, sr | 0x404u);
            break;
        case 3: { /* ChangeThreadSubFunction */
            u32 newtcb = RAM32(p, tcb + 8 + 5 * 4);
            WRAM32(p, tcb + 0x88, RAM32(p, tcb + 0x88) + 4);
            WRAM32(p, RAM32(p, 0x108), newtcb);
            return_from_exception(p);
            return;
        }
        default:
            deliver_event(p, 0xF0000010u, 0x4000);
            break;
        }
        WRAM32(p, tcb + 0x88, RAM32(p, tcb + 0x88) + 4);
        return_from_exception(p);
        return;
    }
    if (code != EXC_INT) {
        static const char *names[] = { "INT", "MOD", "TLBL", "TLBS", "AdEL", "AdES", "IBE", "DBE", "Syscall", "BREAK", "RI", "CpU", "Ov" };
        u32 epc = p->cpu.cop0[C0_EPC];
        u32 instr = bus_read32(p, epc);
        char dis[96];
        cpu_disasm(epc, instr, dis, sizeof(dis));
        deliver_event(p, 0xF0000010u, 0x1000);
        psx_halt(p, "Exception not handled by the kernel: %s (code %u) EPC=%08X [%s] BADVADDR=%08X ra=%08X",
                 code < 13 ? names[code] : "?", code, epc, dis, p->cpu.cop0[C0_BADVADDR], R(p, 31));
        return;
    }
    R(p, 29) = K_EXC_STACK;
    /* continue with the generated chain loop at K_EXC+4 */
}

static void exc_leave(psx *p)
{
    psx_hle *h = p->hle;
    u32 hk = h->hook_addr ? h->hook_addr : (0x80000000u | K_HOOKDEF);
    int i;
    R(p, RA) = RAM32(p, hk + 0x00);
    R(p, 29) = RAM32(p, hk + 0x04);
    R(p, 30) = RAM32(p, hk + 0x08);
    for (i = 0; i < 8; i++) R(p, 16 + i) = RAM32(p, hk + 0x0C + (u32)i * 4);
    R(p, 28) = RAM32(p, hk + 0x2C);
    R(p, V0) = 1;
    jump_to(p, R(p, RA));
}

/* ------------------------------------------------------------ memory card */

static void card_format(u8 *m)
{
    int i, j;
    memset(m, 0, 128 * 1024);
    m[0] = 'M'; m[1] = 'C';
    for (i = 1; i < 16; i++) {
        u8 *f = m + i * 128;
        f[0] = 0xA0;
        f[8] = 0xFF; f[9] = 0xFF;
    }
    for (i = 16; i < 36; i++) {
        u8 *f = m + i * 128;
        f[0] = f[1] = f[2] = f[3] = 0xFF;
        f[8] = 0xFF; f[9] = 0xFF;
    }
    for (i = 0; i < 64; i++) {
        u8 *f = m + i * 128, x = 0;
        if (i >= 36 && i < 63) continue;
        for (j = 0; j < 127; j++) x ^= f[j];
        f[127] = x;
    }
    memcpy(m + 63 * 128, m, 128);
}

void hle_set_memcard(psx *p, int slot, const char *path)
{
    psx_hle *h = p->hle;
    u8 *buf;
    long n;
    if (slot < 0 || slot > 1) return;
    free(h->memcard[slot]);
    h->memcard[slot] = NULL;
    h->memcard_path[slot][0] = 0;
    if (!path || !*path) return;
    nr_strlcpy(h->memcard_path[slot], path, sizeof(h->memcard_path[slot]));
    h->memcard[slot] = (u8 *)malloc(128 * 1024);
    n = nr_read_file(path, &buf);
    if (n == 128 * 1024) {
        memcpy(h->memcard[slot], buf, 128 * 1024);
        free(buf);
        NR_INFO("card", "slot %d: memory card loaded from %s", slot + 1, path);
    } else {
        if (n >= 0) { free(buf); NR_WARN("card", "%s has an invalid size (%ld); a new formatted card will be used", path, n); }
        else NR_INFO("card", "slot %d: %s does not exist; creating a formatted card", slot + 1, path);
        card_format(h->memcard[slot]);
        h->memcard_dirty[slot] = true;
    }
    h->card_new[slot] = true;
}

void hle_flush_memcards(psx *p)
{
    psx_hle *h = p->hle;
    int s;
    for (s = 0; s < 2; s++) {
        if (h->memcard[s] && h->memcard_dirty[s] && h->memcard_path[s][0]) {
            if (nr_write_file(h->memcard_path[s], h->memcard[s], 128 * 1024)) h->memcard_dirty[s] = false;
            else NR_ERR("card", "failed to write %s", h->memcard_path[s]);
        }
    }
}

static void card_queue(psx *p, int kind, u32 port, u32 sector, u32 addr)
{
    psx_hle *h = p->hle;
    card_op *o;
    if (h->card_qn >= 8) { NR_WARN("card", "operation queue full"); return; }
    o = &h->card_q[h->card_qn++];
    NR_DBG("card", "%s port %02X sector %u buffer %08X", kind == 1 ? "read" : kind == 2 ? "write" : kind == 3 ? "info" : "load", port, sector, addr);
    o->kind = kind;
    o->port = (port >> 4) & 1;
    o->sector = sector;
    o->addr = addr;
    o->delay = 1;
    h->card_status[o->port] = kind == 1 ? 0x02 : kind == 2 ? 0x04 : 0x08;
}

static void card_process(psx *p)
{
    psx_hle *h = p->hle;
    card_op o;
    u32 cls_hw = 0xF0000011u, cls_sw = 0xF4000001u;
    if (!h->card_qn) return;
    if (--h->card_q[0].delay > 0) return;
    o = h->card_q[0];
    memmove(&h->card_q[0], &h->card_q[1], sizeof(card_op) * (size_t)(h->card_qn - 1));
    h->card_qn--;
    if (!h->memcard[o.port]) {
        h->card_status[o.port] = 0x11;
        deliver_event(p, (o.kind <= 2) ? cls_hw : cls_sw, 0x0100);
        return;
    }
    switch (o.kind) {
    case 1: case 2:
        if (o.sector >= 1024) { h->card_status[o.port] = 0x21; deliver_event(p, cls_hw, 0x8000); return; }
        if (o.kind == 1) {
            u32 i;
            for (i = 0; i < 128; i++) psx_ram_w8(p, o.addr + i, h->memcard[o.port][o.sector * 128 + i]);
        } else {
            u32 i;
            for (i = 0; i < 128; i++) h->memcard[o.port][o.sector * 128 + i] = psx_ram_r8(p, o.addr + i);
            h->memcard_dirty[o.port] = true;
            h->card_new[o.port] = false;
        }
        h->card_status[o.port] = 0x01;
        deliver_event(p, cls_hw, 0x0004);
        break;
    case 3: case 4:
        h->card_status[o.port] = 0x01;
        if (h->card_new[o.port]) {
            h->card_new[o.port] = false;
            deliver_event(p, cls_sw, 0x2000);
        } else {
            deliver_event(p, cls_sw, 0x0004);
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------ pads */

static void pad_write_buffer(psx *p, int port, u32 buf)
{
    bool conn = p->host.pad_connected ? p->host.pad_connected(p->host.user, port) : (port == 0);
    u16 b = p->host.read_pad ? p->host.read_pad(p->host.user, port) : 0xFFFF;
    if (!buf) return;
    if (!conn) {
        psx_ram_w8(p, buf, 0xFF);
        return;
    }
    psx_ram_w8(p, buf + 0, 0x00);
    psx_ram_w8(p, buf + 1, 0x41);
    psx_ram_w8(p, buf + 2, (u8)b);
    psx_ram_w8(p, buf + 3, (u8)(b >> 8));
}

static u32 pad_dr(psx *p)
{
    u32 v = 0;
    int port;
    for (port = 0; port < 2; port++) {
        u32 buf = 0x80000000u | (K_PADBUF + (u32)port * 0x22);
        u32 half;
        if (psx_ram_r8(p, buf) != 0 || psx_ram_r8(p, buf + 1) != 0x41) half = 0xFFFF;
        else half = ((u32)psx_ram_r8(p, buf + 2) << 8) | psx_ram_r8(p, buf + 3);
        v |= half << (port * 16);
    }
    return v;
}

static void padcard_irq(psx *p)
{
    psx_hle *h = p->hle;
    if (p->i_stat & 1) {
        if (h->pad_enable) {
            pad_write_buffer(p, 0, h->pad_buf[0]);
            pad_write_buffer(p, 1, h->pad_buf[1]);
            if (h->pad_init2 && h->pad_button_dest) psx_ram_w32(p, h->pad_button_dest, pad_dr(p));
        }
        card_process(p);
        if (h->clear_pad) { p->i_stat &= ~1u; psx_update_irq(p); }
    }
    ret(p, 0);
}

/* ------------------------------------------------------------ chains */

static void enq_int(psx *p, u32 prio, u32 elem)
{
    u32 head = 0x80000000u | (K_EXCB + prio * 8);
    WRAM32(p, elem, RAM32(p, head));
    WRAM32(p, head, elem);
}

static void deq_int(psx *p, u32 prio, u32 elem)
{
    u32 prev = 0x80000000u | (K_EXCB + prio * 8);
    u32 cur = RAM32(p, prev);
    int guard = 0;
    while (cur && guard++ < 64) {
        if ((cur & 0x1FFFFF) == (elem & 0x1FFFFF)) {
            WRAM32(p, prev, RAM32(p, cur));
            return;
        }
        prev = cur;
        cur = RAM32(p, cur);
    }
}

static void rcnt_irq(psx *p, int n)
{
    psx_hle *h = p->hle;
    static const u32 bits[4] = { 1u << IRQ_TMR0, 1u << IRQ_TMR1, 1u << IRQ_TMR2, 1u << IRQ_VBLANK };
    if ((p->i_stat & p->i_mask & bits[n])) {
        deliver_event(p, 0xF2000000u | (u32)n, 0x0002);
        if (h->clear_rcnt[n]) {
            p->i_stat &= ~bits[n];
            psx_update_irq(p);
            return_from_exception(p);
            return;
        }
    }
    ret(p, 0);
}

static void defint_irq(psx *p)
{
    static const u32 cls[11] = { 1, 2, 3, 4, 5, 6, 6, 8, 0xB, 9, 0xA };
    u32 pend = p->i_stat & p->i_mask;
    int i;
    for (i = 0; i < 11; i++) if (pend & (1u << i)) deliver_event(p, 0xF0000000u | cls[i], 0x1000);
    ret(p, 0);
}

/* ------------------------------------------------------------ printf */

static void tty_out(psx *p, const char *s)
{
    psx_hle *h = p->hle;
    for (; *s; s++) {
        if (*s == '\n' || h->tty_len >= (int)sizeof(h->tty) - 1) {
            h->tty[h->tty_len] = 0;
            NR_INFO("tty", "%s", h->tty);
            h->tty_len = 0;
            if (*s == '\n') continue;
        }
        h->tty[h->tty_len++] = *s;
    }
}

static void mips_printf(psx *p, u32 fmt_addr, int first_arg_reg)
{
    char fmt[512], out[1024];
    int argi = first_arg_reg, o = 0;
    const char *f;
    psx_ram_strcpy_out(p, fmt_addr, fmt, sizeof(fmt));
    for (f = fmt; *f && o < (int)sizeof(out) - 64; f++) {
        char spec[32];
        int sl = 0;
        u32 arg;
        if (*f != '%') { out[o++] = *f; continue; }
        spec[sl++] = *f++;
        while (*f && strchr("-+ #0123456789.lh", *f) && sl < 28) { if (*f != 'l' && *f != 'h') spec[sl++] = *f; f++; }
        if (!*f) break;
        if (*f == '%') { out[o++] = '%'; continue; }
        spec[sl++] = *f;
        spec[sl] = 0;
        arg = argi < 8 ? R(p, argi) : RAM32(p, R(p, 29) + 0x10 + (u32)(argi - 8) * 4);
        argi++;
        switch (*f) {
        case 's': { char s[256]; psx_ram_strcpy_out(p, arg, s, sizeof(s)); o += snprintf(out + o, sizeof(out) - (size_t)o, spec, s); break; }
        case 'c': o += snprintf(out + o, sizeof(out) - (size_t)o, spec, (int)(arg & 0xFF)); break;
        case 'd': case 'i': o += snprintf(out + o, sizeof(out) - (size_t)o, spec, (int)(s32)arg); break;
        case 'u': case 'x': case 'X': case 'o': o += snprintf(out + o, sizeof(out) - (size_t)o, spec, (unsigned)arg); break;
        case 'p': o += snprintf(out + o, sizeof(out) - (size_t)o, "%08X", arg); break;
        default: o += snprintf(out + o, sizeof(out) - (size_t)o, "<%s>", spec); break;
        }
        if (o >= (int)sizeof(out)) o = (int)sizeof(out) - 1;
    }
    out[o] = 0;
    tty_out(p, out);
}

/* ------------------------------------------------------------ A/B/C */

static void unimpl(psx *p, char tbl, u32 n)
{
    char what[64];
    snprintf(what, sizeof(what), "BIOS %c(%02Xh)", tbl, n);
    nr_unimplemented("hle", what);
    ret(p, 0);
}

static void fn_a(psx *p, u32 n)
{
    psx_hle *h = p->hle;
    u32 a0 = R(p, 4), a1 = R(p, 5), a2 = R(p, 6);
    h->calls_a[n & 0xFF]++;
    switch (n) {
    case 0x13: { /* setjmp */
        int i;
        WRAM32(p, a0 + 0x00, R(p, RA));
        WRAM32(p, a0 + 0x04, R(p, 29));
        WRAM32(p, a0 + 0x08, R(p, 30));
        for (i = 0; i < 8; i++) WRAM32(p, a0 + 0x0C + (u32)i * 4, R(p, 16 + i));
        WRAM32(p, a0 + 0x2C, R(p, 28));
        ret(p, 0);
        break;
    }
    case 0x14: { /* longjmp */
        int i;
        R(p, RA) = RAM32(p, a0 + 0x00);
        R(p, 29) = RAM32(p, a0 + 0x04);
        R(p, 30) = RAM32(p, a0 + 0x08);
        for (i = 0; i < 8; i++) R(p, 16 + i) = RAM32(p, a0 + 0x0C + (u32)i * 4);
        R(p, 28) = RAM32(p, a0 + 0x2C);
        ret(p, a1);
        jump_to(p, R(p, RA));
        break;
    }
    case 0x15: { /* strcat */
        u32 d = a0;
        if (!a0 || !a1) { ret(p, 0); break; }
        while (psx_ram_r8(p, d)) d++;
        while ((psx_ram_w8(p, d, psx_ram_r8(p, a1)), psx_ram_r8(p, a1))) { d++; a1++; }
        ret(p, a0);
        break;
    }
    case 0x17: case 0x18: { /* strcmp / strncmp */
        u32 lim = (n == 0x18) ? a2 : 0xFFFFFFFFu, i;
        s32 r = 0;
        if (!a0 || !a1) { ret(p, a0 ? 1 : (a1 ? (u32)-1 : 0)); break; }
        for (i = 0; i < lim; i++) {
            u8 c1 = psx_ram_r8(p, a0 + i), c2 = psx_ram_r8(p, a1 + i);
            if (c1 != c2) { r = (s32)c1 - (s32)c2; break; }
            if (!c1) break;
        }
        ret(p, (u32)r);
        break;
    }
    case 0x19: { /* strcpy */
        u32 i = 0;
        if (!a0 || !a1) { ret(p, 0); break; }
        do { psx_ram_w8(p, a0 + i, psx_ram_r8(p, a1 + i)); } while (psx_ram_r8(p, a1 + i++));
        ret(p, a0);
        break;
    }
    case 0x1B: { /* strlen */
        u32 i = 0;
        if (!a0) { ret(p, 0); break; }
        while (psx_ram_r8(p, a0 + i)) i++;
        ret(p, i);
        break;
    }
    case 0x25: { u32 c = a0 & 0xFF; ret(p, (c >= 'a' && c <= 'z') ? c - 32 : c); break; }
    case 0x26: { u32 c = a0 & 0xFF; ret(p, (c >= 'A' && c <= 'Z') ? c + 32 : c); break; }
    case 0x28: { u32 i; if (a0) for (i = 0; i < a1; i++) psx_ram_w8(p, a0 + i, 0); ret(p, a0); break; }
    case 0x2A: { /* memcpy */
        u32 i;
        if (a0 && a1) for (i = 0; i < a2; i++) psx_ram_w8(p, a0 + i, psx_ram_r8(p, a1 + i));
        ret(p, a0);
        break;
    }
    case 0x2B: { u32 i; if (a0) for (i = 0; i < a2; i++) psx_ram_w8(p, a0 + i, (u8)a1); ret(p, a0); break; }
    case 0x2E: { /* memchr */
        u32 i, r = 0;
        if (a0) for (i = 0; i < a2; i++) if (psx_ram_r8(p, a0 + i) == (u8)a1) { r = a0 + i; break; }
        ret(p, r);
        break;
    }
    case 0x39: h->heap_start = a0; h->heap_size = a1; ret(p, 0); break;
    case 0x3F: mips_printf(p, a0, 5); ret(p, 0); break;
    case 0x44: cpu_icache_flush(p); ret(p, 0); break; /* FlushCache */
    case 0x49: gpu_write_gp0(p, a0); ret(p, 0); break;
    case 0x70: ret(p, 0); break; /* _bu_init */
    case 0x72: ret(p, 0); break; /* _96_remove: the HLE kernel never installs the CD handlers */
    case 0xAB: card_queue(p, 3, a0, 0, 0); ret(p, 1); break;
    case 0xAC: card_queue(p, 4, a0, 0, 0); ret(p, 1); break;
    case 0xAD: h->card_auto = a0 != 0; ret(p, 0); break;
    default: unimpl(p, 'A', n); break;
    }
}

static void fn_b(psx *p, u32 n)
{
    psx_hle *h = p->hle;
    u32 a0 = R(p, 4), a1 = R(p, 5), a2 = R(p, 6), a3 = R(p, 7);
    h->calls_b[n & 0xFF]++;
    switch (n) {
    case 0x07: deliver_event(p, a0, a1); ret(p, 0); break;
    case 0x08: { /* OpenEvent */
        int i;
        u32 r = 0xFFFFFFFFu;
        for (i = 0; i < N_EVCB; i++) {
            u32 a = K_EVCB + (u32)i * 0x1C;
            if (RAM32(p, a + 4) == 0) {
                WRAM32(p, a + 0, a0);
                WRAM32(p, a + 4, 0x1000);
                WRAM32(p, a + 8, a1);
                WRAM32(p, a + 0xC, a2);
                WRAM32(p, a + 0x10, a3);
                r = 0xF1000000u | (u32)i;
                break;
            }
        }
        if (r == 0xFFFFFFFFu) NR_WARN("hle", "OpenEvent: no free EvCB");
        ret(p, r);
        break;
    }
    case 0x09: WRAM32(p, evcb_addr(a0) + 4, 0); ret(p, 1); break;
    case 0x0A: /* WaitEvent: handled by the looping stub (T_WAITEVENT) */
        ret(p, 0);
        break;
    case 0x0B: { /* TestEvent */
        u32 a = evcb_addr(a0);
        if (RAM32(p, a + 4) == 0x4000) { WRAM32(p, a + 4, 0x2000); ret(p, 1); }
        else ret(p, 0);
        break;
    }
    case 0x0C: WRAM32(p, evcb_addr(a0) + 4, 0x2000); ret(p, 1); break;
    case 0x0D: WRAM32(p, evcb_addr(a0) + 4, 0x1000); ret(p, 1); break;
    case 0x12: { /* InitPAD2 */
        u32 i;
        h->pad_buf[0] = a0; h->pad_siz[0] = a1;
        h->pad_buf[1] = a2; h->pad_siz[1] = a3;
        for (i = 0; i < a1 && a0; i++) psx_ram_w8(p, a0 + i, 0);
        for (i = 0; i < a3 && a2; i++) psx_ram_w8(p, a2 + i, 0);
        h->pad_enable = true;
        ret(p, 2);
        break;
    }
    case 0x13: /* StartPAD2 */
        if (!h->pad_started) {
            enq_int(p, 2, 0x80000000u | (K_ELEMS + 4 * 16));
            h->pad_started = true;
        }
        /* real kernel enables the vblank IRQ for the pad handler */
        p->i_mask |= 1;
        psx_update_irq(p);
        ret(p, 1);
        break;
    case 0x14: /* StopPAD2 */
        if (h->pad_started) { deq_int(p, 2, 0x80000000u | (K_ELEMS + 4 * 16)); h->pad_started = false; }
        ret(p, 1);
        break;
    case 0x15: { /* PAD_init2(type, button_dest, ...) */
        u32 b1 = 0x80000000u | K_PADBUF, b2 = b1 + 0x22, i;
        if (a0 != 0x20000000u && a0 != 0x20000001u) { ret(p, 0); break; }
        for (i = 0; i < 0x22; i++) { psx_ram_w8(p, b1 + i, 0); psx_ram_w8(p, b2 + i, 0); }
        h->pad_buf[0] = b1; h->pad_buf[1] = b2;
        h->pad_siz[0] = h->pad_siz[1] = 0x22;
        h->pad_button_dest = a1;
        h->pad_init2 = true;
        h->pad_enable = true;
        if (!h->pad_started) { enq_int(p, 2, 0x80000000u | (K_ELEMS + 4 * 16)); h->pad_started = true; }
        p->i_mask |= 1;
        psx_update_irq(p);
        ret(p, 2);
        break;
    }
    case 0x16: { u32 v = pad_dr(p); if (h->pad_button_dest) psx_ram_w32(p, h->pad_button_dest, v); else psx_ram_w32(p, 0, v); ret(p, v); break; }
    case 0x17: return_from_exception(p); break;
    case 0x18: h->hook_addr = 0; ret(p, 0x80000000u | K_HOOKDEF); break;
    case 0x19: h->hook_addr = a0; ret(p, 0); break;
    case 0x20: undeliver_event(p, a0, a1); ret(p, 0); break;
    case 0x38: psx_halt(p, "the game called exit(%d)", (int)a0); break;
    case 0x3F: { char s[512]; psx_ram_strcpy_out(p, a0, s, sizeof(s)); tty_out(p, s); tty_out(p, "\n"); ret(p, 0); break; }
    case 0x4A: h->pad_enable = a0 != 0; ret(p, 0); break;  /* InitCARD2(pad_enable) */
    case 0x4B: /* StartCARD2 */
        if (!h->pad_started) { enq_int(p, 2, 0x80000000u | (K_ELEMS + 4 * 16)); h->pad_started = true; }
        ret(p, 0);
        break;
    case 0x4C: ret(p, 0); break; /* StopCARD2 */
    case 0x4E: if (a1 > 0x400) { ret(p, 0); break; } card_queue(p, 2, a0, a1, a2); ret(p, 1); break;
    case 0x4F: if (a1 > 0x400) { ret(p, 0); break; } card_queue(p, 1, a0, a1, a2); ret(p, 1); break;
    case 0x50: { int s; for (s = 0; s < 2; s++) h->card_new[s] = false; ret(p, 0); break; }
    case 0x56: ret(p, 0x80000000u | K_CTAB); break;
    case 0x57: ret(p, 0x80000000u | K_BTAB); break;
    case 0x5B: { u32 old = h->clear_pad; h->clear_pad = a0 != 0; ret(p, old); break; }
    case 0x5C: ret(p, h->card_status[(a0 >> 4) & 1]); break;
    case 0x5D: ret(p, h->card_status[(a0 >> 4) & 1]); break;
    default: unimpl(p, 'B', n); break;
    }
}

static void fn_c(psx *p, u32 n)
{
    psx_hle *h = p->hle;
    u32 a0 = R(p, 4), a1 = R(p, 5);
    h->calls_c[n & 0x7F]++;
    switch (n) {
    case 0x02: if (a0 < 4) enq_int(p, a0, a1); ret(p, 0); break;
    case 0x03: if (a0 < 4) deq_int(p, a0, a1); ret(p, 0); break;
    case 0x0A: { u32 old = 0; if (a0 < 4) { old = h->clear_rcnt[a0]; h->clear_rcnt[a0] = a1 != 0; } ret(p, old); break; }
    default: unimpl(p, 'C', n); break;
    }
}

/* ------------------------------------------------------------ dispatch */

void hle_trap(psx *p, u32 id)
{
    switch (id) {
    case T_DISPATCH_A: case T_DISPATCH_B: case T_DISPATCH_C: {
        u32 n = R(p, T1);
        u32 tab = id == T_DISPATCH_A ? K_ATAB : id == T_DISPATCH_B ? K_BTAB : K_CTAB;
        u32 lim = id == T_DISPATCH_A ? 0xC0 : id == T_DISPATCH_B ? 0x100 : 0x80;
        u32 target;
        if (n >= lim) {
            char what[64];
            snprintf(what, sizeof(what), "BIOS %c(%Xh) outside the table", "ABC"[id - T_DISPATCH_A], n);
            nr_unimplemented("hle", what);
            ret(p, 0);
            jump_to(p, R(p, RA));
            return;
        }
        target = RAM32(p, 0x80000000u | (tab + n * 4));
        if (nr_log_get_level() >= NR_LOG_DEBUG) {
            const char *nm = id == T_DISPATCH_A ? a_name(n) : id == T_DISPATCH_B ? b_name(n) : NULL;
            NR_DBG("hle", "%c(%02Xh) %s a0=%08X a1=%08X a2=%08X a3=%08X ra=%08X", "ABC"[id - T_DISPATCH_A], n,
                   nm ? nm : "", R(p, 4), R(p, 5), R(p, 6), R(p, 7), R(p, RA));
        }
        jump_to(p, target);
        return;
    }
    case T_EXC_ENTER: exc_enter(p); return;
    case T_EXC_LEAVE: exc_leave(p); return;
    case T_CALL_RETURN:
        if (p->hle->call_depth > 0) p->hle->call_returned[p->hle->call_depth] = true;
        p->stop = true;
        return;
    case T_RCNT0: case T_RCNT1: case T_RCNT2: case T_RCNT3: rcnt_irq(p, (int)(id - T_RCNT0)); return;
    case T_PADCARD: padcard_irq(p); return;
    case T_DEFINT: defint_irq(p); return;
    case T_WAITEVENT: {
        u32 a = evcb_addr(R(p, 4));
        u32 st = RAM32(p, a + 4);
        if (st == 0x1000 || st == 0) { ret(p, 0); jump_to(p, R(p, RA)); return; }
        if (st == 0x4000) { WRAM32(p, a + 4, 0x2000); ret(p, 1); jump_to(p, R(p, RA)); return; }
        /* not ready: fall through into the stub's loop */
        return;
    }
    case T_SET_PAD_ENABLE: p->hle->pad_enable = true; return;
    case T_CLR_PAD_ENABLE: p->hle->pad_enable = false; return;
    default:
        if (id < 0x100) { fn_a(p, id); return; }
        if (id < 0x200) { fn_b(p, id - 0x100); return; }
        if (id < 0x280) { fn_c(p, id - 0x200); return; }
        psx_halt(p, "unknown HLE trap %X", id);
        return;
    }
}

/* ------------------------------------------------------------ init */

bool hle_init(psx *p)
{
    p->hle = (psx_hle *)calloc(1, sizeof(psx_hle));
    return p->hle != NULL;
}

void hle_free(psx *p)
{
    if (!p->hle) return;
    hle_flush_memcards(p);
    free(p->hle->memcard[0]);
    free(p->hle->memcard[1]);
    free(p->hle);
    p->hle = NULL;
}

void hle_reset(psx *p)
{
    psx_hle *h = p->hle;
    u32 i;
    char paths[2][1024];
    u8 *cards[2];
    bool newc[2], dirty[2];
    /* keep memory card configuration across resets */
    memcpy(paths, h->memcard_path, sizeof(paths));
    cards[0] = h->memcard[0]; cards[1] = h->memcard[1];
    newc[0] = h->card_new[0]; newc[1] = h->card_new[1];
    dirty[0] = h->memcard_dirty[0]; dirty[1] = h->memcard_dirty[1];
    memset(h, 0, sizeof(*h));
    memcpy(h->memcard_path, paths, sizeof(paths));
    h->memcard[0] = cards[0]; h->memcard[1] = cards[1];
    h->card_new[0] = newc[0]; h->card_new[1] = newc[1];
    h->memcard_dirty[0] = dirty[0]; h->memcard_dirty[1] = dirty[1];
    for (i = 0; i < 4; i++) h->clear_rcnt[i] = true;
    h->clear_pad = true;

    /* BIOS ROM area: fill with a recognisable pattern (no code needed). */
    memset(p->bios, 0, PSX_BIOS_SIZE);

    /* garbage area + exception vector */
    kw(p, 0x00, 0x00000003);
    kw(p, 0x04, 0x275A0000u | (K_EXC & 0xFFFF));
    kw(p, 0x08, enc_jr(26));
    kw(p, 0x0C, NOP);
    kw(p, 0x60, 2);
    kw(p, 0x64, 0);
    kw(p, 0x68, 0xFF);
    /* 80000080: lui k0,0x8000 ; addiu k0,k0,K_EXC ; jr k0 ; nop */
    kw(p, 0x80, 0x3C1A8000u);
    kw(p, 0x84, 0x275A0000u | (K_EXC & 0xFFFF));
    kw(p, 0x88, enc_jr(26));
    kw(p, 0x8C, NOP);
    kw(p, 0xA0, enc_hle(T_DISPATCH_A)); kw(p, 0xA4, NOP); kw(p, 0xA8, NOP); kw(p, 0xAC, NOP);
    kw(p, 0xB0, enc_hle(T_DISPATCH_B)); kw(p, 0xB4, NOP); kw(p, 0xB8, NOP); kw(p, 0xBC, NOP);
    kw(p, 0xC0, enc_hle(T_DISPATCH_C)); kw(p, 0xC4, NOP); kw(p, 0xC8, NOP); kw(p, 0xCC, NOP);

    /* table of tables */
    kw(p, 0x100, 0x80000000u | K_EXCB); kw(p, 0x104, 4 * 8);
    kw(p, 0x108, 0x80000000u | K_PCB);  kw(p, 0x10C, 4);
    kw(p, 0x110, 0x80000000u | K_TCB);  kw(p, 0x114, N_TCB * 0xC0);
    kw(p, 0x120, 0x80000000u | K_EVCB); kw(p, 0x124, N_EVCB * 0x1C);

    /* function tables + stubs */
    emit_stub(p, H_UNIMPL, 0x3FF);
    for (i = 0; i < 0xC0; i++) { emit_stub(p, K_ASTUB + i * 12, T_A + i); kw(p, K_ATAB + i * 4, 0x80000000u | (K_ASTUB + i * 12)); }
    for (i = 0; i < 0x100; i++) { emit_stub(p, K_BSTUB + i * 12, T_B + i); kw(p, K_BTAB + i * 4, 0x80000000u | (K_BSTUB + i * 12)); }
    for (i = 0; i < 0x80; i++) { emit_stub(p, K_CSTUB + i * 12, T_C + i); kw(p, K_CTAB + i * 4, 0x80000000u | (K_CSTUB + i * 12)); }

    /* WaitEvent: looping stub */
    kw(p, H_WAITEVENT + 0, enc_hle(T_WAITEVENT));
    kw(p, H_WAITEVENT + 4, BEQ(ZERO, ZERO, -2));
    kw(p, H_WAITEVENT + 8, NOP);
    kw(p, K_BTAB + 0x0A * 4, 0x80000000u | H_WAITEVENT);

    /* C(06h) patch sink (exception handler address reported to games) */
    for (i = 0; i < 0x100; i += 8) { kw(p, K_C06_SINK + i, enc_jr(RA)); kw(p, K_C06_SINK + i + 4, NOP); }
    kw(p, K_CTAB + 6 * 4, 0x80000000u | K_C06_SINK);

    /* B(5Bh) patch sink: stub at offset 0, harmless "jr ra" sled afterwards,
     * with SetPadEnableFlag/ClearPadEnableFlag entry points (patch targets). */
    for (i = 0; i < 0x2000; i += 8) { kw(p, K_B5B_SINK + i, enc_jr(RA)); kw(p, K_B5B_SINK + i + 4, NOP); }
    emit_stub(p, K_B5B_SINK, T_B + 0x5B);
    emit_stub(p, K_B5B_SINK + 0x884, T_SET_PAD_ENABLE);
    emit_stub(p, K_B5B_SINK + 0x894, T_CLR_PAD_ENABLE);
    kw(p, K_BTAB + 0x5B * 4, 0x80000000u | K_B5B_SINK);

    /* helper stubs */
    kw(p, H_CALL_RETURN, enc_hle(T_CALL_RETURN));
    kw(p, H_CALL_RETURN + 4, NOP);
    for (i = 0; i < 4; i++) emit_stub(p, H_RCNT0 + i * 12, T_RCNT0 + i);
    emit_stub(p, H_PADCARD, T_PADCARD);
    emit_stub(p, H_DEFINT, T_DEFINT);

    /* exception handler: enter, walk the 4 priority chains, leave */
    {
        static u32 code[31];
        int k = 0;
        code[k++] = enc_hle(T_EXC_ENTER);          /* 0 */
        code[k++] = ADDIU(S1, ZERO, 0);            /* 1 */
        code[k++] = LW(T0, 0x100, ZERO);           /* 2  L_PRIO */
        code[k++] = NOP;                           /* 3 */
        code[k++] = ADDU(T0, T0, S1);              /* 4 */
        code[k++] = LW(S0, 0, T0);                 /* 5 */
        code[k++] = NOP;                           /* 6 */
        code[k++] = BEQ(S0, ZERO, 18);             /* 7  L_ELEM -> 26 */
        code[k++] = NOP;                           /* 8 */
        code[k++] = LW(T1, 8, S0);                 /* 9 */
        code[k++] = NOP;                           /* 10 */
        code[k++] = BEQ(T1, ZERO, 11);             /* 11 -> 23 */
        code[k++] = NOP;                           /* 12 */
        code[k++] = enc_jalr(T1);                  /* 13 */
        code[k++] = NOP;                           /* 14 */
        code[k++] = BEQ(V0, ZERO, 7);              /* 15 -> 23 */
        code[k++] = NOP;                           /* 16 */
        code[k++] = LW(T1, 4, S0);                 /* 17 */
        code[k++] = NOP;                           /* 18 */
        code[k++] = BEQ(T1, ZERO, 3);              /* 19 -> 23 */
        code[k++] = NOP;                           /* 20 */
        code[k++] = enc_jalr(T1);                  /* 21 */
        code[k++] = ADDU(A0, V0, ZERO);            /* 22 */
        code[k++] = LW(S0, 0, S0);                 /* 23 L_NEXTELEM */
        code[k++] = BEQ(ZERO, ZERO, -18);          /* 24 -> 7 */
        code[k++] = NOP;                           /* 25 */
        code[k++] = ADDIU(S1, S1, 8);              /* 26 L_NEXTPRIO */
        code[k++] = SLTI(T0, S1, 32);              /* 27 */
        code[k++] = BNE(T0, ZERO, -27);            /* 28 -> 2 */
        code[k++] = NOP;                           /* 29 */
        code[k++] = enc_hle(T_EXC_LEAVE);          /* 30 */
        for (i = 0; i < (u32)k; i++) kw(p, K_EXC + i * 4, code[i]);
    }

    /* control blocks */
    memset(p->ram + K_EXCB, 0, 0x1000);
    kw(p, K_PCB, 0x80000000u | K_TCB);
    for (i = 0; i < N_TCB; i++) kw(p, K_TCB + i * 0xC0, 0x1000);
    kw(p, K_TCB, 0x4000); /* boot thread */
    for (i = 0; i < N_EVCB; i++) kw(p, K_EVCB + i * 0x1C + 4, 0);

    /* default HookEntryInt structure: ReturnFromException + exception stack */
    kw(p, K_HOOKDEF + 0x00, 0x80000000u | (K_BSTUB + 0x17 * 12));
    kw(p, K_HOOKDEF + 0x04, K_EXC_STACK - 4);

    /* default chain elements: prio1 = Timer0, Timer1, Timer2, VBlank (rcnt);
     * prio3 = DefInt; element 4 = PadCardIrq (enqueued by StartPAD2) */
    for (i = 0; i < 4; i++) {
        u32 e = K_ELEMS + i * 16;
        kw(p, e + 0, 0);
        kw(p, e + 4, 0);
        kw(p, e + 8, 0x80000000u | (H_RCNT0 + i * 12));
        kw(p, e + 12, 0);
    }
    kw(p, K_ELEMS + 4 * 16 + 8, 0x80000000u | H_PADCARD);
    kw(p, K_ELEMS + 5 * 16 + 8, 0x80000000u | H_DEFINT);
    /* order inside prio 1 chain (first = highest): CardSpecific (n/a), VBlank, Timer2, Timer1, Timer0 */
    enq_int(p, 1, 0x80000000u | (K_ELEMS + 0 * 16));
    enq_int(p, 1, 0x80000000u | (K_ELEMS + 1 * 16));
    enq_int(p, 1, 0x80000000u | (K_ELEMS + 2 * 16));
    enq_int(p, 1, 0x80000000u | (K_ELEMS + 3 * 16));
    enq_int(p, 3, 0x80000000u | (K_ELEMS + 5 * 16));

    /* Hardware state as left by the BIOS boot sequence: the CD-ROM controller
     * interrupt mask is 1Fh (all HC05 interrupts enabled). */
    p->cd.irq_enable = 0x1F;

    /* CPU state as left by the kernel: BEV cleared, IRQs enabled (IEc, IM2) */
    p->cpu.cop0[C0_SR] = 0x00000401u;
}

/* ------------------------------------------------------------ boot */

static u32 cnf_value(const char *cnf, const char *key, u32 def)
{
    const char *k = strstr(cnf, key);
    unsigned long v;
    if (!k) return def;
    k = strchr(k, '=');
    if (!k) return def;
    k++;
    while (*k == ' ' || *k == '\t') k++;
    v = strtoul(k, NULL, 16);
    return (u32)v;
}

bool hle_boot_disc(psx *p, char *err, size_t errlen)
{
    u8 *cnf = NULL, *exe = NULL;
    u32 cnfsz, exesz;
    char boot[64] = { 0 };
    const char *b;
    u32 pc0, gp0, taddr, tsize, baddr, bsize, saddr, ssize, stack;
    u32 i;

    if (!p->disc) { snprintf(err, errlen, "No disc attached."); return false; }
    if (!nr_iso_read_file(p->disc, "SYSTEM.CNF", &cnf, &cnfsz)) { snprintf(err, errlen, "SYSTEM.CNF not found on the disc."); return false; }
    cnf[cnfsz] = 0;
    b = strstr((char *)cnf, "BOOT");
    if (b) b = strchr(b, '=');
    if (b) {
        size_t n = 0;
        b++;
        while (*b == ' ') b++;
        if (strncmp(b, "cdrom:", 6) == 0) b += 6;
        while (*b == '\\' || *b == '/') b++;
        while (b[n] && b[n] != '\r' && b[n] != '\n' && b[n] != ';' && b[n] != ' ' && n < sizeof(boot) - 1) n++;
        memcpy(boot, b, n);
    }
    stack = cnf_value((char *)cnf, "STACK", 0x801FFF00u);
    free(cnf);
    if (!boot[0]) { snprintf(err, errlen, "SYSTEM.CNF has no BOOT entry."); return false; }
    if (!nr_iso_read_file(p->disc, boot, &exe, &exesz)) { snprintf(err, errlen, "Executable %s not found.", boot); return false; }
    if (exesz < 0x800 || memcmp(exe, "PS-X EXE", 8) != 0) { free(exe); snprintf(err, errlen, "%s is not a valid PS-X EXE.", boot); return false; }
    pc0 = nr_rd32(exe + 0x10); gp0 = nr_rd32(exe + 0x14);
    taddr = nr_rd32(exe + 0x18); tsize = nr_rd32(exe + 0x1C);
    baddr = nr_rd32(exe + 0x28); bsize = nr_rd32(exe + 0x2C);
    saddr = nr_rd32(exe + 0x30); ssize = nr_rd32(exe + 0x34);
    if (tsize > exesz - 0x800) tsize = exesz - 0x800;
    for (i = 0; i < tsize; i++) psx_ram_w8(p, taddr + i, exe[0x800 + i]);
    free(exe);
    for (i = 0; i < bsize; i++) psx_ram_w8(p, baddr + i, 0);
    /* command line (empty) */
    psx_ram_w8(p, 0x180, 0);
    NR_INFO("hle", "Boot %s: entry=%08X gp=%08X text=%08X..%08X bss=%08X+%X stack=%08X", boot, pc0, gp0, taddr, taddr + tsize, baddr, bsize,
            saddr ? saddr + ssize : stack);
    memset(p->cpu.r, 0, sizeof(p->cpu.r));
    R(p, 28) = gp0;
    R(p, 29) = saddr ? saddr + ssize : stack;
    R(p, 30) = R(p, 29);
    R(p, RA) = 0x80000000u | (K_BSTUB + 0x38 * 12); /* returning from main -> exit() */
    R(p, 4) = 1; R(p, 5) = 0;
    cpu_set_pc(p, pc0);
    return true;
}

void hle_vblank(psx *p) { (void)p; }

void hle_dump_state(psx *p)
{
    psx_hle *h = p->hle;
    int i;
    NR_INFO("hle", "hook=%08X clear_rcnt=%d%d%d%d clear_pad=%d pad_started=%d", h->hook_addr,
            h->clear_rcnt[0], h->clear_rcnt[1], h->clear_rcnt[2], h->clear_rcnt[3], h->clear_pad, h->pad_started);
    for (i = 0; i < 4; i++) {
        u32 e = RAM32(p, 0x80000000u | (K_EXCB + (u32)i * 8));
        while (e) {
            NR_INFO("hle", "  chain[%d] elem %08X f1=%08X f2=%08X", i, e, RAM32(p, e + 8), RAM32(p, e + 4));
            e = RAM32(p, e);
        }
    }
    for (i = 0; i < N_EVCB; i++) {
        u32 a = 0x80000000u | (K_EVCB + (u32)i * 0x1C);
        if (RAM32(p, a + 4))
            NR_INFO("hle", "  ev %d class=%08X spec=%04X status=%04X mode=%04X fn=%08X", i, RAM32(p, a), RAM32(p, a + 8),
                    RAM32(p, a + 4), RAM32(p, a + 0xC), RAM32(p, a + 0x10));
    }
    for (i = 0; i < 256; i++) if (h->calls_a[i]) NR_INFO("hle", "  A(%02X) %-14s x%llu", i, a_name((u32)i) ? a_name((u32)i) : "?", (unsigned long long)h->calls_a[i]);
    for (i = 0; i < 256; i++) if (h->calls_b[i]) NR_INFO("hle", "  B(%02X) %-14s x%llu", i, b_name((u32)i) ? b_name((u32)i) : "?", (unsigned long long)h->calls_b[i]);
    for (i = 0; i < 128; i++) if (h->calls_c[i]) NR_INFO("hle", "  C(%02X) x%llu", i, (unsigned long long)h->calls_c[i]);
}
