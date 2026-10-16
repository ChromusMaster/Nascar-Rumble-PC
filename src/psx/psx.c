/* Machine glue: creation, reset, scheduler, interrupt controller, frame loop. */
#include "psx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

psx *psx_create(void)
{
    psx *p = (psx *)calloc(1, sizeof(psx));
    if (!p) return NULL;
    p->ram = (u8 *)calloc(1, PSX_RAM_SIZE);
    p->bios = (u8 *)calloc(1, PSX_BIOS_SIZE);
    if (!p->ram || !p->bios) { psx_destroy(p); return NULL; }
    gpu_init(p);
    spu_init(p);
    mdec_init(p);
    if (!hle_init(p)) { psx_destroy(p); return NULL; }
    p->cycles_per_instr = 1;
    psx_reset(p);
    return p;
}

void psx_destroy(psx *p)
{
    if (!p) return;
    hle_free(p);
    mdec_free(p);
    spu_free(p);
    gpu_free(p);
    free(p->ovr);
    free(p->ovr_bitmap);
    free(p->ram);
    free(p->bios);
    free(p);
}

void psx_reset(psx *p)
{
    int i;
    memset(p->ram, 0, PSX_RAM_SIZE);
    memset(p->scratch, 0, sizeof(p->scratch));
    p->i_stat = p->i_mask = 0;
    memset(p->mem_ctrl, 0, sizeof(p->mem_ctrl));
    p->ram_size = 0x00000B88;
    p->cache_ctrl = 0;
    p->cycles = 0;
    p->stop = false;
    p->halted = false;
    p->halt_reason[0] = 0;
    p->instr_count = 0;
    for (i = 0; i < EV_COUNT; i++) p->ev_time[i] = UINT64_MAX;
    p->next_event = UINT64_MAX;
    cpu_reset(p);
    gte_reset(&p->gte);
    dma_reset(p);
    timers_reset(p);
    gpu_reset(p);
    cdrom_reset(p);
    spu_reset(p);
    mdec_reset(p);
    sio_reset(p);
    hle_reset(p);
}

void psx_attach_disc(psx *p, nr_disc *d) { p->disc = d; }

void psx_halt(psx *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->halt_reason, sizeof(p->halt_reason), fmt, ap);
    va_end(ap);
    p->halted = true;
    p->stop = true;
    NR_ERR("psx", "HALT: %s (pc=%08X)", p->halt_reason, p->cpu.cur_pc);
}

/* ------------------------------------------------------------ scheduler */

void psx_update_next_event(psx *p)
{
    u64 m = UINT64_MAX;
    int i;
    for (i = 0; i < EV_COUNT; i++) if (p->ev_time[i] < m) m = p->ev_time[i];
    p->next_event = m;
}

void psx_schedule(psx *p, int ev, u64 delta)
{
    p->ev_time[ev] = p->cycles + (delta ? delta : 1);
    psx_update_next_event(p);
}

void psx_schedule_periodic(psx *p, int ev, u64 delta)
{
    u64 base = p->ev_due ? p->ev_due : p->cycles;
    p->ev_time[ev] = base + (delta ? delta : 1);
    psx_update_next_event(p);
}

void psx_unschedule(psx *p, int ev)
{
    p->ev_time[ev] = UINT64_MAX;
    psx_update_next_event(p);
}

void psx_dispatch(psx *p)
{
    int i;
    bool any = true;
    while (any) {
        any = false;
        for (i = 0; i < EV_COUNT; i++) {
            if (p->ev_time[i] <= p->cycles) {
                p->ev_due = p->ev_time[i];
                p->ev_time[i] = UINT64_MAX;
                any = true;
                switch (i) {
                case EV_GPU_LINE: gpu_event(p); break;
                case EV_TIMER0: timers_event(p, 0); break;
                case EV_TIMER1: timers_event(p, 1); break;
                case EV_TIMER2: timers_event(p, 2); break;
                case EV_CDROM: cdrom_event(p); break;
                case EV_CDROM_READ: cdrom_read_event(p); break;
                case EV_DMA: dma_event(p); break;
                case EV_SPU: spu_event(p); break;
                case EV_SIO: sio_event(p); break;
                default: break;
                }
                p->ev_due = 0;
            }
        }
    }
    psx_update_next_event(p);
}

/* ------------------------------------------------------------ interrupts */

void psx_update_irq(psx *p)
{
    if (p->i_stat & p->i_mask) p->cpu.cop0[C0_CAUSE] |= 0x400u;
    else p->cpu.cop0[C0_CAUSE] &= ~0x400u;
}

void psx_irq(psx *p, int line)
{
    p->i_stat |= 1u << line;
    psx_update_irq(p);
}

/* ------------------------------------------------------------ frame loop */

void psx_run_frame(psx *p)
{
    p->gpu.frame_ready = false;
    p->stop = false;
    while (!p->gpu.frame_ready && !p->halted) {
        u64 target = p->next_event;
        if (target > p->cycles + 2048) target = p->cycles + 2048;
        if (target <= p->cycles) target = p->cycles + 1;
        cpu_run(p, target);
        if (p->cycles >= p->next_event) psx_dispatch(p);
        if (p->stop && !p->halted) p->stop = false;
    }
}

/* ------------------------------------------------------------ snapshots */

#define ST_MAGIC 0x5453524Eu   /* "NRST" */
#define ST_VERSION 1u

typedef struct { u8 *p; size_t n, cap; bool ok; } wbuf;

static void wput(wbuf *b, const void *d, size_t n)
{
    if (!b->ok) return;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : (4u << 20);
        u8 *q;
        while (nc < b->n + n) nc *= 2;
        q = (u8 *)realloc(b->p, nc);
        if (!q) { b->ok = false; return; }
        b->p = q;
        b->cap = nc;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

bool psx_state_save(const psx *p, u8 **out, size_t *len)
{
    wbuf b;
    u32 hdr[4];
    void *ext, *hle;
    size_t ext_n, hle_n;
    memset(&b, 0, sizeof(b));
    b.ok = true;
    spu_state_ext((psx *)p, &ext, &ext_n);
    hle_state_blob((psx *)p, &hle, &hle_n);
    hdr[0] = ST_MAGIC; hdr[1] = ST_VERSION; hdr[2] = (u32)sizeof(psx); hdr[3] = (u32)(ext_n + hle_n);
    wput(&b, hdr, sizeof(hdr));
    wput(&b, p, sizeof(psx));
    wput(&b, p->ram, PSX_RAM_SIZE);
    wput(&b, p->gpu.vram, PSX_VRAM_W * PSX_VRAM_H * 2);
    wput(&b, p->spu.ram, PSX_SPU_RAM_SIZE);
    wput(&b, ext, ext_n);
    wput(&b, hle, hle_n);
    wput(&b, p->mdec.in, (size_t)p->mdec.in_len * 2);
    wput(&b, p->mdec.out, (size_t)p->mdec.out_len * 4);
    if (!b.ok) { free(b.p); return false; }
    *out = b.p;
    *len = b.n;
    return true;
}

bool psx_state_load(psx *p, const u8 *in, size_t len)
{
    const u32 *hdr = (const u32 *)in;
    psx keep = *p, s;
    void *ext, *hle;
    size_t ext_n, hle_n, need, off;
    if (len < 16 + sizeof(psx) || hdr[0] != ST_MAGIC || hdr[1] != ST_VERSION || hdr[2] != (u32)sizeof(psx)) return false;
    spu_state_ext(p, &ext, &ext_n);
    hle_state_blob(p, &hle, &hle_n);
    if (hdr[3] != (u32)(ext_n + hle_n)) return false;
    memcpy(&s, in + 16, sizeof(psx));
    need = 16 + sizeof(psx) + PSX_RAM_SIZE + PSX_VRAM_W * PSX_VRAM_H * 2 + PSX_SPU_RAM_SIZE + ext_n + hle_n +
           (size_t)s.mdec.in_len * 2 + (size_t)s.mdec.out_len * 4;
    if (len < need || s.mdec.in_len > keep.mdec.in_cap || s.mdec.out_len > keep.mdec.out_cap) return false;
    *p = s;
    /* host-side pointers and settings stay those of the running instance */
    p->ram = keep.ram; p->bios = keep.bios;
    p->gpu.vram = keep.gpu.vram; p->gpu.rec = keep.gpu.rec; p->gpu.pgxp = keep.gpu.pgxp; p->gpu.timing = keep.gpu.timing;
    p->gte.pgxp = keep.gte.pgxp; p->gte.ws_num = keep.gte.ws_num; p->gte.ws_den = keep.gte.ws_den;
    p->spu.ram = keep.spu.ram; p->spu.out = keep.spu.out; p->spu.out_cap = keep.spu.out_cap; p->spu.ext = keep.spu.ext;
    p->spu.out_w = p->spu.out_r = 0;
    p->mdec.in = keep.mdec.in; p->mdec.in_cap = keep.mdec.in_cap; p->mdec.out = keep.mdec.out; p->mdec.out_cap = keep.mdec.out_cap;
    p->hle = keep.hle; p->disc = keep.disc; p->host = keep.host;
    p->ovr = keep.ovr; p->novr = keep.novr; p->ovr_cap = keep.ovr_cap; p->ovr_bitmap = keep.ovr_bitmap; p->ovr_verify = keep.ovr_verify;
    p->ovr_busy = false;
    p->trace = keep.trace; p->watch_pc = keep.watch_pc; p->watch_write = keep.watch_write; p->pcprof = keep.pcprof;
    p->cycles_per_instr = keep.cycles_per_instr; p->cpu_overclock = keep.cpu_overclock;
    off = 16 + sizeof(psx);
    memcpy(p->ram, in + off, PSX_RAM_SIZE); off += PSX_RAM_SIZE;
    memcpy(p->gpu.vram, in + off, PSX_VRAM_W * PSX_VRAM_H * 2); off += PSX_VRAM_W * PSX_VRAM_H * 2;
    memcpy(p->spu.ram, in + off, PSX_SPU_RAM_SIZE); off += PSX_SPU_RAM_SIZE;
    memcpy(ext, in + off, ext_n); off += ext_n;
    {
        /* the HLE block holds memory-card buffers and file paths of the
         * running instance: keep those */
        u8 *tmp = (u8 *)malloc(hle_n);
        if (tmp) {
            memcpy(tmp, hle, hle_n);
            memcpy(hle, in + off, hle_n);
            hle_restore_host_fields(p, tmp);
            free(tmp);
        }
        off += hle_n;
    }
    memcpy(p->mdec.in, in + off, (size_t)p->mdec.in_len * 2); off += (size_t)p->mdec.in_len * 2;
    memcpy(p->mdec.out, in + off, (size_t)p->mdec.out_len * 4);
    return true;
}
