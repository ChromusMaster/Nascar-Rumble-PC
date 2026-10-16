/* DMA controller. Reference: psx-spx "DMA Channels".
 * Transfers are executed immediately (block copies); completion IRQs are
 * raised after a short modelled delay so that software polling the busy bit
 * observes a transfer in progress like on hardware. */
#include "psx.h"

#include <string.h>

enum { DMA_MDEC_IN = 0, DMA_MDEC_OUT, DMA_GPU, DMA_CDROM, DMA_SPU, DMA_PIO, DMA_OTC };

void dma_reset(psx *p)
{
    memset(&p->dma, 0, sizeof(p->dma));
    p->dma.dpcr = 0x07654321;
}

static void update_master(psx *p)
{
    psx_dma *d = &p->dma;
    bool old = (d->dicr >> 31) & 1;
    bool force = (d->dicr >> 15) & 1;
    bool en = (d->dicr >> 23) & 1;
    u32 flags = (d->dicr >> 24) & 0x7F, mask = (d->dicr >> 16) & 0x7F;
    bool m = force || (en && (flags & mask));
    if (m) d->dicr |= 0x80000000u; else d->dicr &= ~0x80000000u;
    if (m && !old) psx_irq(p, IRQ_DMA);
}

u32 dma_read(psx *p, u32 addr)
{
    u32 off = addr & 0x7F;
    int ch = (int)(off >> 4);
    if (ch < 7) {
        switch (off & 0xF) {
        case 0x0: return p->dma.madr[ch];
        case 0x4: return p->dma.bcr[ch];
        case 0x8: return p->dma.chcr[ch];
        default: return 0;
        }
    }
    if (off == 0x70) return p->dma.dpcr;
    if (off == 0x74) return p->dma.dicr;
    return 0;
}

static void finish(psx *p, int ch)
{
    p->dma.chcr[ch] &= ~((1u << 24) | (1u << 28));
    if (p->dma.dicr & (1u << (16 + ch))) {
        p->dma.dicr |= 1u << (24 + ch);
    }
    update_master(p);
}

static void do_transfer(psx *p, int ch)
{
    psx_dma *d = &p->dma;
    u32 chcr = d->chcr[ch];
    u32 sync = (chcr >> 9) & 3;
    bool from_ram = chcr & 1;
    s32 step = (chcr & 2) ? -4 : 4;
    u32 addr = d->madr[ch] & 0x1FFFFC;
    u32 words, i;
    u64 cost;

    if (ch == DMA_OTC) {
        words = d->bcr[ch] & 0xFFFF;
        if (!words) words = 0x10000;
        for (i = 0; i < words; i++) {
            u32 v = (i == words - 1) ? 0x00FFFFFFu : ((addr - 4) & 0x1FFFFF);
            nr_wr32(p->ram + addr, v);
            addr = (addr - 4) & 0x1FFFFC;
        }
        cost = words;
    } else if (sync == 2) {
        /* Linked list (GPU) */
        u32 count = 0, start = addr, cur = d->madr[ch];
        if (ch != DMA_GPU) nr_unimplemented("dma", "linked-list mode on non-GPU channel");
        /* The end marker (bit 23) is checked before each header fetch, so a
         * transfer started at 0xFFFFFF finishes immediately. */
        while (!(cur & 0x800000u)) {
            u32 hdr;
            addr = cur & 0x1FFFFC;
            hdr = nr_rd32(p->ram + addr);
            u32 n = hdr >> 24;
            for (i = 0; i < n; i++) {
                addr = (addr + 4) & 0x1FFFFC;
                gpu_write_gp0(p, nr_rd32(p->ram + addr));
            }
            count += n + 1;
            cur = hdr & 0xFFFFFF;
            if (count > 0x200000) {
                NR_WARN("dma", "GPU linked list too long (loop?) start=%06X current=%06X hdr=%08X frame=%llu pc=%08X",
                        start, addr, hdr, (unsigned long long)p->gpu.frame_count, p->cpu.cur_pc);
                break;
            }
        }
        d->madr[ch] = 0x00FFFFFF;
        cost = count;
    } else {
        if (sync == 0) {
            words = d->bcr[ch] & 0xFFFF;
            if (!words) words = 0x10000;
        } else {
            u32 bs = d->bcr[ch] & 0xFFFF, ba = d->bcr[ch] >> 16;
            if (!bs) bs = 0x10000;
            words = bs * ba;
        }
        for (i = 0; i < words; i++) {
            u32 a = addr & 0x1FFFFC;
            if (from_ram) {
                u32 v = nr_rd32(p->ram + a);
                switch (ch) {
                case DMA_MDEC_IN: mdec_dma_write(p, v); break;
                case DMA_GPU: gpu_write_gp0(p, v); break;
                case DMA_SPU: spu_dma_write(p, v); break;
                default: nr_unimplemented("dma", "write direction for this channel"); break;
                }
            } else {
                u32 v = 0;
                switch (ch) {
                case DMA_MDEC_OUT: v = mdec_dma_read(p); break;
                case DMA_GPU: v = gpu_dma_read(p); break;
                case DMA_CDROM: v = cdrom_dma_read(p); break;
                case DMA_SPU: v = spu_dma_read(p); break;
                default: nr_unimplemented("dma", "read direction for this channel"); break;
                }
                nr_wr32(p->ram + a, v);
            }
            addr += (u32)step;
        }
        if (sync == 1) {
            d->madr[ch] = addr & 0xFFFFFF;
            d->bcr[ch] &= 0xFFFF; /* BA decremented to 0 */
        }
        cost = words;
    }
    /* Completion after a modelled delay (about one word per cycle); GPU
     * transfers additionally wait for the modelled drawing time. */
    p->dma.pending_done |= 1u << ch;
    if (ch == DMA_GPU) {
        u64 gb = gpu_busy_remaining(p);
        if (gb > cost) cost = gb;
    }
    {
        u64 when = p->cycles + cost + 16;
        if (p->ev_time[EV_DMA] == UINT64_MAX || when < p->ev_time[EV_DMA]) {
            p->ev_time[EV_DMA] = when;
            psx_update_next_event(p);
        }
    }
}

void dma_event(psx *p)
{
    int ch;
    u32 pend = p->dma.pending_done;
    p->dma.pending_done = 0;
    for (ch = 0; ch < 7; ch++) if (pend & (1u << ch)) { p->dma.active &= ~(1u << ch); finish(p, ch); }
}

static void maybe_start(psx *p, int ch)
{
    u32 chcr = p->dma.chcr[ch];
    bool enabled = (p->dma.dpcr >> (ch * 4 + 3)) & 1;
    u32 sync = (chcr >> 9) & 3;
    bool start = (chcr >> 24) & 1;
    bool trigger = (chcr >> 28) & 1;
    if (start) NR_TRACE("dma", "ch%d chcr=%08X madr=%06X bcr=%08X enabled=%d", ch, chcr, p->dma.madr[ch], p->dma.bcr[ch], enabled);
    if (!enabled || !start) return;
    if (p->dma.active & (1u << ch)) return; /* already transferring */
    if (sync == 0 && !trigger && ch != DMA_OTC) return;
    if (ch == DMA_OTC && sync == 0 && !trigger) return;
    p->dma.chcr[ch] &= ~(1u << 28);
    p->dma.active |= 1u << ch;
    do_transfer(p, ch);
}

void dma_write(psx *p, u32 addr, u32 v)
{
    u32 off = addr & 0x7F;
    int ch = (int)(off >> 4);
    if (ch < 7) {
        switch (off & 0xF) {
        case 0x0: p->dma.madr[ch] = v & 0xFFFFFF; break;
        case 0x4: p->dma.bcr[ch] = v; break;
        case 0x8:
            if (ch == DMA_OTC) v = (v & 0x51000000u) | 2u;
            /* The data of a transfer is moved at once and only its completion
             * is delayed. If software starts the channel again before that
             * modelled completion (on hardware the CPU is stalled while a
             * block transfer runs, so the previous one would already be
             * over), the previous transfer is completed first instead of the
             * new request being dropped. */
            if ((v & (1u << 24)) && (p->dma.active & (1u << ch))) {
                p->dma.active &= ~(1u << ch);
                p->dma.pending_done &= ~(1u << ch);
                finish(p, ch);
            }
            p->dma.chcr[ch] = v;
            maybe_start(p, ch);
            break;
        default: break;
        }
        return;
    }
    if (off == 0x70) {
        p->dma.dpcr = v;
        for (ch = 0; ch < 7; ch++) maybe_start(p, ch);
        return;
    }
    if (off == 0x74) {
        u32 d = p->dma.dicr;
        d = (d & 0xFF000000u) | (v & 0x00FF803Fu);
        d &= ~(v & 0x7F000000u);            /* write 1 to ack flags */
        p->dma.dicr = d;
        update_master(p);
        return;
    }
}
