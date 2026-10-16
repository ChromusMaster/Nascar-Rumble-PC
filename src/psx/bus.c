/* Memory map and I/O dispatch. Reference: psx-spx "Memory Map", "I/O Map". */
#include "psx.h"

#include <string.h>

static u32 io_read(psx *p, u32 phys, int width);
static void io_write(psx *p, u32 phys, u32 v, int width);

static inline u32 phys_of(u32 addr)
{
    /* KUSEG/KSEG0/KSEG1 map to the same 512MB physical space. KSEG2 is kept
     * as is (only the cache control register lives there). */
    if (addr >= 0xC0000000u) return addr;
    return addr & 0x1FFFFFFFu;
}

u8 *psx_ram_ptr(psx *p, u32 addr)
{
    u32 ph = phys_of(addr);
    if (ph < 0x00800000u) return p->ram + (ph & (PSX_RAM_SIZE - 1));
    if (ph >= 0x1F800000u && ph < 0x1F800400u) return p->scratch + (ph & 0x3FF);
    return NULL;
}

u32 psx_ram_r32(psx *p, u32 addr) { u8 *m = psx_ram_ptr(p, addr); return m ? nr_rd32(m) : 0; }
void psx_ram_w32(psx *p, u32 addr, u32 v) { u8 *m = psx_ram_ptr(p, addr); if (m) nr_wr32(m, v); }
u16 psx_ram_r16(psx *p, u32 addr) { u8 *m = psx_ram_ptr(p, addr); return m ? nr_rd16(m) : 0; }
void psx_ram_w16(psx *p, u32 addr, u16 v) { u8 *m = psx_ram_ptr(p, addr); if (m) nr_wr16(m, v); }
u8 psx_ram_r8(psx *p, u32 addr) { u8 *m = psx_ram_ptr(p, addr); return m ? *m : 0; }
void psx_ram_w8(psx *p, u32 addr, u8 v) { u8 *m = psx_ram_ptr(p, addr); if (m) *m = v; }

bool psx_ram_strcpy_out(psx *p, u32 addr, char *out, size_t n)
{
    size_t i;
    if (!n) return false;
    for (i = 0; i + 1 < n; i++) {
        u8 *m = psx_ram_ptr(p, addr + (u32)i);
        if (!m) { out[i] = 0; return false; }
        out[i] = (char)*m;
        if (!*m) return true;
    }
    out[n - 1] = 0;
    return true;
}

/* ------------------------------------------------------------------------ */

u32 bus_read32(psx *p, u32 addr)
{
    u32 ph = phys_of(addr);
    if (NR_LIKELY(ph < 0x00800000u)) return nr_rd32(p->ram + (ph & (PSX_RAM_SIZE - 1)));
    if (ph >= 0x1F800000u && ph < 0x1F800400u) {
        if ((addr & 0xE0000000u) == 0xA0000000u) return 0; /* scratchpad not reachable via KSEG1 */
        return nr_rd32(p->scratch + (ph & 0x3FF));
    }
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return nr_rd32(p->bios + (ph & (PSX_BIOS_SIZE - 1)));
    if (ph >= 0x1F801000u && ph < 0x1F803000u) return io_read(p, ph, 4);
    if (ph >= 0x1F000000u && ph < 0x1F800000u) return 0xFFFFFFFFu; /* expansion 1: nothing connected */
    if (ph == 0xFFFE0130u) return p->cache_ctrl;
    NR_DBG("bus", "read32 from unmapped %08X (pc=%08X)", addr, p->cpu.cur_pc);
    return 0;
}

u16 bus_read16(psx *p, u32 addr)
{
    u32 ph = phys_of(addr);
    if (NR_LIKELY(ph < 0x00800000u)) return nr_rd16(p->ram + (ph & (PSX_RAM_SIZE - 1)));
    if (ph >= 0x1F800000u && ph < 0x1F800400u) return nr_rd16(p->scratch + (ph & 0x3FF));
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return nr_rd16(p->bios + (ph & (PSX_BIOS_SIZE - 1)));
    if (ph >= 0x1F801000u && ph < 0x1F803000u) return (u16)io_read(p, ph, 2);
    if (ph >= 0x1F000000u && ph < 0x1F800000u) return 0xFFFF;
    NR_DBG("bus", "read16 from unmapped %08X (pc=%08X)", addr, p->cpu.cur_pc);
    return 0;
}

u8 bus_read8(psx *p, u32 addr)
{
    u32 ph = phys_of(addr);
    if (NR_LIKELY(ph < 0x00800000u)) return p->ram[ph & (PSX_RAM_SIZE - 1)];
    if (ph >= 0x1F800000u && ph < 0x1F800400u) return p->scratch[ph & 0x3FF];
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return p->bios[ph & (PSX_BIOS_SIZE - 1)];
    if (ph >= 0x1F801000u && ph < 0x1F803000u) return (u8)io_read(p, ph, 1);
    if (ph >= 0x1F000000u && ph < 0x1F800000u) return 0xFF;
    NR_DBG("bus", "read8 from unmapped %08X (pc=%08X)", addr, p->cpu.cur_pc);
    return 0;
}

static void watch_hit(psx *p, u32 addr, u32 v, int w)
{
    NR_INFO("watch", "write%d to %08X = %08X pc=%08X ra=%08X", w * 8, addr, v, p->cpu.cur_pc, p->cpu.r[31]);
}

#define CHECK_WATCH(w) do { if (NR_UNLIKELY(p->watch_write) && (ph & ~3u) == (p->watch_write & 0x1FFFFFFCu)) watch_hit(p, addr, v, w); } while (0)

void bus_write32(psx *p, u32 addr, u32 v)
{
    u32 ph = phys_of(addr);
    CHECK_WATCH(4);
    if (NR_LIKELY(ph < 0x00800000u)) { nr_wr32(p->ram + (ph & (PSX_RAM_SIZE - 1)), v); return; }
    if (ph >= 0x1F800000u && ph < 0x1F800400u) { nr_wr32(p->scratch + (ph & 0x3FF), v); return; }
    if (ph >= 0x1F801000u && ph < 0x1F803000u) { io_write(p, ph, v, 4); return; }
    if (ph == 0xFFFE0130u) { p->cache_ctrl = v; return; }
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return; /* ROM */
    NR_DBG("bus", "write32 %08X to unmapped %08X (pc=%08X)", v, addr, p->cpu.cur_pc);
}

void bus_write16(psx *p, u32 addr, u16 v)
{
    u32 ph = phys_of(addr);
    CHECK_WATCH(2);
    if (NR_LIKELY(ph < 0x00800000u)) { nr_wr16(p->ram + (ph & (PSX_RAM_SIZE - 1)), v); return; }
    if (ph >= 0x1F800000u && ph < 0x1F800400u) { nr_wr16(p->scratch + (ph & 0x3FF), v); return; }
    if (ph >= 0x1F801000u && ph < 0x1F803000u) { io_write(p, ph, v, 2); return; }
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return;
    NR_DBG("bus", "write16 %04X to unmapped %08X (pc=%08X)", v, addr, p->cpu.cur_pc);
}

void bus_write8(psx *p, u32 addr, u8 v)
{
    u32 ph = phys_of(addr);
    CHECK_WATCH(1);
    if (NR_LIKELY(ph < 0x00800000u)) { p->ram[ph & (PSX_RAM_SIZE - 1)] = v; return; }
    if (ph >= 0x1F800000u && ph < 0x1F800400u) { p->scratch[ph & 0x3FF] = v; return; }
    if (ph >= 0x1F801000u && ph < 0x1F803000u) { io_write(p, ph, v, 1); return; }
    if (ph >= 0x1FC00000u && ph < 0x1FC80000u) return;
    NR_DBG("bus", "write8 %02X to unmapped %08X (pc=%08X)", v, addr, p->cpu.cur_pc);
}

/* ------------------------------------------------------------------- I/O */

static u32 io_read(psx *p, u32 ph, int width)
{
    u32 off = ph - 0x1F801000u; /* relative to 1F801000 */
    if (off < 0x24) return p->mem_ctrl[off >> 2];
    if (off >= 0x40 && off < 0x60) return sio_read(p, ph, width);
    if (off == 0x60) return p->ram_size;
    if (off >= 0x70 && off < 0x74) return p->i_stat >> ((ph & 3) * 8);
    if (off >= 0x74 && off < 0x78) return p->i_mask >> ((ph & 3) * 8);
    if (off >= 0x80 && off < 0x100) {
        u32 v = dma_read(p, ph & ~3u);
        return v >> ((ph & 3) * 8);
    }
    if (off >= 0x100 && off < 0x130) return timers_read(p, ph);
    if (off >= 0x800 && off < 0x804) return cdrom_read(p, ph & 3);
    if (off == 0x810 || off == 0x814) return gpu_read(p, off == 0x814);
    if (off == 0x820 || off == 0x824) return mdec_read(p, off == 0x824);
    if (off >= 0xC00 && off < 0x1000) {
        if (width == 4) return (u32)spu_read(p, ph) | ((u32)spu_read(p, ph + 2) << 16);
        if (width == 1) return (u8)(spu_read(p, ph & ~1u) >> ((ph & 1) * 8));
        return spu_read(p, ph);
    }
    if (off >= 0x1000) {
        /* Expansion region 2 (debug/DUART). Nothing attached. */
        return 0xFFFFFFFFu;
    }
    NR_DBG("bus", "io read %08X (w%d) unhandled", ph, width);
    return 0;
}

static void io_write(psx *p, u32 ph, u32 v, int width)
{
    u32 off = ph - 0x1F801000u;
    if (off < 0x24) { p->mem_ctrl[off >> 2] = v; return; }
    if (off >= 0x40 && off < 0x60) { sio_write(p, ph, v, width); return; }
    if (off == 0x60) { p->ram_size = v; return; }
    if (off >= 0x70 && off < 0x74) { p->i_stat &= (v << ((ph & 3) * 8)) | ~(0xFFFFFFFFu >> (32 - width * 8) << ((ph & 3) * 8)); psx_update_irq(p); return; }
    if (off >= 0x74 && off < 0x78) { NR_DBG("irq", "I_MASK <- %04X (w%d) pc=%08X", v, width, p->cpu.cur_pc); if (width == 4) p->i_mask = v & 0x7FF; else p->i_mask = (p->i_mask & ~(0xFFFFu)) | (v & 0x7FF); psx_update_irq(p); return; }
    if (off >= 0x80 && off < 0x100) {
        if (width != 4) {
            u32 cur = dma_read(p, ph & ~3u);
            u32 sh = (ph & 3) * 8;
            u32 mask = (width == 2 ? 0xFFFFu : 0xFFu) << sh;
            v = (cur & ~mask) | ((v << sh) & mask);
        }
        dma_write(p, ph & ~3u, v);
        return;
    }
    if (off >= 0x100 && off < 0x130) { timers_write(p, ph, v); return; }
    if (off >= 0x800 && off < 0x804) { cdrom_write(p, ph & 3, (u8)v); return; }
    if (off == 0x810) { gpu_write_gp0(p, v); return; }
    if (off == 0x814) { gpu_write_gp1(p, v); return; }
    if (off == 0x820 || off == 0x824) { mdec_write(p, off == 0x824, v); return; }
    if (off >= 0xC00 && off < 0x1000) {
        if (width == 4) { spu_write(p, ph, (u16)v); spu_write(p, ph + 2, (u16)(v >> 16)); }
        else spu_write(p, ph & ~1u, (u16)v);
        return;
    }
    if (off >= 0x1000) {
        if (off == 0x1041) NR_DBG("bus", "POST %02X", v & 0xFF);
        else if (off == 0x1023 || off == 0x1080) {
            /* DUART TX (debug console) */
        }
        return;
    }
    NR_DBG("bus", "io write %08X = %08X (w%d) unhandled", ph, v, width);
}
