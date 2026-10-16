/* CD-ROM controller (HC05 command interface + CXD1199 host interface),
 * backed by the user's disc image. Reference: psx-spx "CDROM Drive".
 *
 * Implemented: command/parameter/response FIFOs, INT1..INT5 queueing,
 * Setloc/SeekL/SeekP/ReadN/ReadS/Pause/Stop/Init/Setmode/Setfilter/Getparam/
 * GetlocL/GetlocP/GetTN/GetTD/GetID/Test(20h)/Mute/Demute/ReadTOC/Reset, data
 * FIFO (2048/2340 byte modes) with DMA3, single/double speed sector timing.
 * Not implemented (reported at runtime): CD-DA Play/Forward/Backward and
 * XA-ADPCM streaming (this game's disc contains neither audio tracks nor XA
 * audio sectors; verified on the reference dump). Seek times are an estimate. */
#include "psx.h"

#include <stdio.h>
#include <string.h>

#define CD_FIRST_RESP_CYCLES 0xC4E1u
#define CD_INIT_RESP_CYCLES  0x13CCEu
#define CD_SINGLE_SPEED (PSX_CPU_HZ / 75u)
#define CD_DOUBLE_SPEED (PSX_CPU_HZ / 150u)

enum { ST_ERROR = 1, ST_MOTOR = 2, ST_SEEKERR = 4, ST_IDERR = 8, ST_SHELL = 16, ST_READ = 32, ST_SEEK = 64, ST_PLAY = 128 };

/* Command stages processed from EV_CDROM */
enum { STG_NONE = 0, STG_FIRST, STG_SECOND };

static void update_irq(psx *p)
{
    psx_cdrom *c = &p->cd;
    if (c->irq_flags & c->irq_enable & 0x1F) psx_irq(p, IRQ_CDROM);
}

static void set_response(psx *p, u8 irq, const u8 *data, int n)
{
    psx_cdrom *c = &p->cd;
    if (irq != 1) NR_DBG("cdrom", "INT%u resp[0]=%02X n=%d cyc=%llu", irq, data[0], n, (unsigned long long)p->cycles);
    else NR_TRACE("cdrom", "INT1 lba=%u cyc=%llu", p->cd.cur_lba, (unsigned long long)p->cycles);
    memcpy(c->resp, data, (size_t)n);
    c->nresp = n;
    c->resp_pos = 0;
    c->irq_flags = (u8)((c->irq_flags & ~7u) | (irq & 7u));
    update_irq(p);
}

static void queue_second(psx *p, u8 irq, const u8 *data, int n, u64 delay)
{
    psx_cdrom *c = &p->cd;
    memcpy(c->resp2, data, (size_t)n);
    c->nresp2 = n;
    c->resp2_irq = irq;
    c->resp2_valid = true;
    c->cmd_stage = STG_SECOND;
    psx_schedule(p, EV_CDROM, delay);
}

static u8 cur_stat(psx *p) { return p->cd.stat; }

static void error_resp(psx *p, u8 code)
{
    u8 r[2];
    r[0] = (u8)(cur_stat(p) | ST_ERROR);
    r[1] = code;
    set_response(p, 5, r, 2);
}

static u32 msf_to_lba(u8 m, u8 s, u8 f)
{
    u32 a = (u32)nr_unbcd(m) * 4500u + (u32)nr_unbcd(s) * 75u + nr_unbcd(f);
    return a >= 150 ? a - 150 : 0;
}

static u64 seek_time(psx *p, u32 from, u32 to)
{
    u32 dist = from > to ? from - to : to - from;
    u64 t;
    /* Estimate: short seeks ~ a few sectors, long seeks up to ~0.3 s. */
    if (dist < 16) t = (u64)p->cd.read_period * 2;
    else t = 30000u + (u64)dist * 30u;
    if (t > PSX_CPU_HZ / 3) t = PSX_CPU_HZ / 3;
    if (!p->cd.motor_on) t += PSX_CPU_HZ / 2; /* spin-up */
    return t;
}

static void stop_reading(psx *p)
{
    psx_cdrom *c = &p->cd;
    c->reading = false;
    c->seeking = false;
    c->playing = false;
    c->stat &= (u8)~(ST_READ | ST_SEEK | ST_PLAY);
    psx_unschedule(p, EV_CDROM_READ);
}

void cdrom_reset(psx *p)
{
    psx_cdrom *c = &p->cd;
    memset(c, 0, sizeof(*c));
    c->stat = ST_MOTOR;
    c->motor_on = true;
    c->mode = 0;
    c->vol_ll = c->vol_rr = 0x80;
    c->read_period = CD_SINGLE_SPEED;
}

static u8 hsts(psx *p)
{
    psx_cdrom *c = &p->cd;
    u8 v = c->index & 3;
    if (c->nparam == 0) v |= 0x08;
    if (c->nparam < 16) v |= 0x10;
    if (c->resp_pos < c->nresp) v |= 0x20;
    if (c->data_pos < c->data_len) v |= 0x40;
    if (c->busy) v |= 0x80;
    return v;
}

static u8 cdrom_read_i(psx *p, u32 reg);
u8 cdrom_read(psx *p, u32 reg)
{
    u8 v = cdrom_read_i(p, reg);
    NR_TRACE("cdio", "R %u (idx %u) -> %02X pc=%08X", reg, p->cd.index, v, p->cpu.cur_pc);
    return v;
}

static u8 cdrom_read_i(psx *p, u32 reg)
{
    psx_cdrom *c = &p->cd;
    switch (reg) {
    case 0: return hsts(p);
    case 1: {
        u8 v = 0;
        if (c->resp_pos < c->nresp) v = c->resp[c->resp_pos];
        if (c->resp_pos < 16) c->resp_pos++;
        if (c->resp_pos >= 16) c->resp_pos = 0;
        return v;
    }
    case 2: {
        u8 v = 0;
        if (c->data_pos < c->data_len) v = c->data[c->data_pos++];
        else if (c->data_len) v = c->data[c->data_len - 8 > 0 ? c->data_len - 8 : 0];
        return v;
    }
    case 3:
        if (c->index & 1) return (u8)(0xE0 | c->irq_flags);
        return (u8)(0xE0 | c->irq_enable);
    }
    return 0;
}

static void deliver_pending_sector(psx *p);

static void ack_irq(psx *p, u8 v)
{
    psx_cdrom *c = &p->cd;
    c->irq_flags &= (u8)~(v & 0x1F);
    if (v & 0x40) c->nparam = 0;
    if (v & 0x80) { /* chip reset */ }
    if ((c->irq_flags & 7) == 0) {
        /* response FIFO drained on acknowledge */
        c->nresp = 0;
        c->resp_pos = 0;
        if (c->resp2_valid && p->ev_time[EV_CDROM] == UINT64_MAX) psx_schedule(p, EV_CDROM, 2000);
        if (c->has_pending_sector) deliver_pending_sector(p);
    }
}

static void exec_command(psx *p);

void cdrom_write(psx *p, u32 reg, u8 v)
{
    psx_cdrom *c = &p->cd;
    NR_TRACE("cdio", "W %u (idx %u) <- %02X pc=%08X", reg, c->index, v, p->cpu.cur_pc);
    if (reg == 0) { c->index = v & 3; return; }
    switch ((c->index << 2) | reg) {
    case 0x01: /* COMMAND */
        c->cmd = v;
        c->busy = true;
        c->resp2_valid = false;
        c->cmd_stage = STG_FIRST;
        psx_schedule(p, EV_CDROM, (v == 0x0A || v == 0x1E) ? CD_INIT_RESP_CYCLES : CD_FIRST_RESP_CYCLES);
        break;
    case 0x02: /* PARAMETER */
        if (c->nparam < 16) c->param[c->nparam++] = v;
        break;
    case 0x03: /* HCHPCTL */
        if (v & 0x80) {
            if (c->data_pos >= c->data_len || c->data_len == 0) {
                /* load the current sector into the data FIFO */
                if (c->mode & 0x20) { memcpy(c->data, c->sector + 12, 2340); c->data_len = 2340; }
                else { memcpy(c->data, c->sector + 24, 2048); c->data_len = 2048; }
                c->data_pos = 0;
            }
        } else {
            c->data_len = c->data_pos = 0;
        }
        break;
    case 0x05: break; /* WRDATA (sound map) */
    case 0x06: c->irq_enable = v & 0x1F; update_irq(p); break;
    case 0x07: ack_irq(p, v); break;
    case 0x09: break; /* CI */
    case 0x0A: c->vol_pend[0] = v; break;
    case 0x0B: c->vol_pend[1] = v; break;
    case 0x0D: c->vol_pend[2] = v; break;
    case 0x0E: c->vol_pend[3] = v; break;
    case 0x0F:
        c->adpcm_muted = v & 1;
        if (v & 0x20) { c->vol_ll = c->vol_pend[0]; c->vol_lr = c->vol_pend[1]; c->vol_rr = c->vol_pend[2]; c->vol_rl = c->vol_pend[3]; }
        break;
    default: break;
    }
}

static void start_read(psx *p)
{
    psx_cdrom *c = &p->cd;
    u64 t;
    c->read_period = (c->mode & 0x80) ? CD_DOUBLE_SPEED : CD_SINGLE_SPEED;
    if (c->setloc_pending) {
        t = seek_time(p, c->cur_lba, c->setloc_lba);
        c->cur_lba = c->setloc_lba;
        c->setloc_pending = false;
        c->seeking = true;
        c->stat = (u8)((c->stat & ~(ST_READ | ST_PLAY)) | ST_SEEK);
    } else {
        t = c->read_period;
        c->stat = (u8)((c->stat & ~(ST_SEEK | ST_PLAY)) | ST_READ);
    }
    c->motor_on = true;
    c->stat |= ST_MOTOR;
    c->reading = true;
    c->has_pending_sector = false;
    psx_schedule(p, EV_CDROM_READ, t + c->read_period);
}

static void exec_command(psx *p)
{
    psx_cdrom *c = &p->cd;
    u8 r[16];
    u8 st;
    int np = c->nparam;
    c->busy = false;
    c->nparam = 0;
    st = cur_stat(p);
    if (nr_log_get_level() >= NR_LOG_DEBUG) {
        char ps[64] = "";
        int k;
        for (k = 0; k < np && k < 16; k++) snprintf(ps + strlen(ps), sizeof(ps) - strlen(ps), "%02X ", c->param[k]);
        NR_DBG("cdrom", "cmd %02X params [%s] stat=%02X lba=%u cyc=%llu", c->cmd, ps, st, c->cur_lba, (unsigned long long)p->cycles);
    }
    r[0] = st;

    if (!p->disc && c->cmd != 0x01 && c->cmd != 0x19 && c->cmd != 0x0A) { error_resp(p, 0x80); return; }

    switch (c->cmd) {
    case 0x01: /* Nop / Getstat */
        set_response(p, 3, r, 1);
        if (!c->shell_open) c->stat &= (u8)~ST_SHELL;
        break;
    case 0x02: /* Setloc */
        if (np != 3) { error_resp(p, 0x20); break; }
        c->setloc_lba = msf_to_lba(c->param[0], c->param[1], c->param[2]);
        c->setloc_pending = true;
        set_response(p, 3, r, 1);
        break;
    case 0x03: case 0x04: case 0x05:
        nr_unimplemented("cdrom", "CD-DA Play/Forward/Backward (no audio tracks on this disc)");
        set_response(p, 3, r, 1);
        break;
    case 0x06: case 0x1B: /* ReadN / ReadS */
        set_response(p, 3, r, 1);
        start_read(p);
        break;
    case 0x07: /* MotorOn */
        if (c->motor_on) { error_resp(p, 0x20); break; }
        set_response(p, 3, r, 1);
        c->motor_on = true;
        c->stat |= ST_MOTOR;
        r[0] = c->stat;
        queue_second(p, 2, r, 1, PSX_CPU_HZ / 4);
        break;
    case 0x08: { /* Stop */
        u64 t = c->motor_on ? (u64)0xD38ACA : 0x1D7B;
        stop_reading(p);
        set_response(p, 3, r, 1);
        c->motor_on = false;
        c->stat &= (u8)~ST_MOTOR;
        r[0] = c->stat;
        queue_second(p, 2, r, 1, t);
        break;
    }
    case 0x09: { /* Pause */
        bool was = c->reading || c->playing;
        u64 t = was ? ((c->mode & 0x80) ? 0x10BD93u : 0x21181Cu) : 0x1DF2u;
        set_response(p, 3, r, 1);
        stop_reading(p);
        r[0] = c->stat;
        queue_second(p, 2, r, 1, t);
        break;
    }
    case 0x0A: /* Init */
        set_response(p, 3, r, 1);
        stop_reading(p);
        c->mode = 0x20;
        c->motor_on = true;
        c->stat = ST_MOTOR;
        r[0] = c->stat;
        queue_second(p, 2, r, 1, 0x1000);
        break;
    case 0x0B: c->muted = true; set_response(p, 3, r, 1); break;
    case 0x0C: c->muted = false; set_response(p, 3, r, 1); break;
    case 0x0D: /* Setfilter */
        if (np != 2) { error_resp(p, 0x20); break; }
        c->filter_file = c->param[0]; c->filter_chan = c->param[1];
        set_response(p, 3, r, 1);
        break;
    case 0x0E: /* Setmode */
        if (np != 1) { error_resp(p, 0x20); break; }
        c->mode = c->param[0];
        if (c->mode & 0x40) nr_unimplemented("cdrom", "XA-ADPCM playback mode");
        set_response(p, 3, r, 1);
        break;
    case 0x0F: /* Getparam */
        r[1] = c->mode; r[2] = 0; r[3] = c->filter_file; r[4] = c->filter_chan;
        set_response(p, 3, r, 5);
        break;
    case 0x10: /* GetlocL */
        if (c->seeking) { error_resp(p, 0x80); break; }
        memcpy(r, c->last_header, 4);
        memcpy(r + 4, c->last_subheader, 4);
        set_response(p, 3, r, 8);
        break;
    case 0x11: { /* GetlocP */
        u32 a = c->cur_lba + 150;
        u32 rel = c->cur_lba; /* single data track starting at LBA 0 */
        r[0] = 0x01; r[1] = 0x01;
        r[2] = nr_bcd((u8)(rel / 4500)); r[3] = nr_bcd((u8)((rel / 75) % 60)); r[4] = nr_bcd((u8)(rel % 75));
        r[5] = nr_bcd((u8)(a / 4500)); r[6] = nr_bcd((u8)((a / 75) % 60)); r[7] = nr_bcd((u8)(a % 75));
        set_response(p, 3, r, 8);
        break;
    }
    case 0x13: /* GetTN */
        r[1] = 0x01; r[2] = 0x01;
        set_response(p, 3, r, 3);
        break;
    case 0x14: { /* GetTD */
        u8 t = np ? c->param[0] : 0;
        if (t == 0) {
            u32 a = nr_disc_sector_count(p->disc) + 150;
            r[1] = nr_bcd((u8)(a / 4500)); r[2] = nr_bcd((u8)((a / 75) % 60));
        } else if (t == 1) {
            r[1] = 0x00; r[2] = 0x02;
        } else { error_resp(p, 0x10); break; }
        set_response(p, 3, r, 3);
        break;
    }
    case 0x15: case 0x16: { /* SeekL / SeekP */
        u64 t;
        stop_reading(p);
        t = seek_time(p, c->cur_lba, c->setloc_lba);
        c->cur_lba = c->setloc_lba;
        c->setloc_pending = false;
        c->motor_on = true;
        c->stat = (u8)((c->stat | ST_MOTOR | ST_SEEK) & ~(ST_READ | ST_PLAY));
        r[0] = c->stat;
        set_response(p, 3, r, 1);
        c->seeking = true;
        r[0] = (u8)((c->stat & ~ST_SEEK));
        queue_second(p, 2, r, 1, t);
        break;
    }
    case 0x19: /* Test */
        if (np >= 1 && c->param[0] == 0x20) {
            /* CDROM BIOS version: use the common SCPH-750x value (vC2) */
            r[0] = 0x97; r[1] = 0x01; r[2] = 0x10; r[3] = 0xC2;
            set_response(p, 3, r, 4);
        } else {
            nr_unimplemented("cdrom", "Test sub-function");
            error_resp(p, 0x10);
        }
        break;
    case 0x1A: /* GetID */
        set_response(p, 3, r, 1);
        r[0] = 0x02; r[1] = 0x00; r[2] = 0x20; r[3] = 0x00;
        r[4] = 'S'; r[5] = 'C'; r[6] = 'E'; r[7] = 'A'; /* NTSC-U licence string */
        queue_second(p, 2, r, 8, 0x4A00);
        break;
    case 0x1C: /* Reset */
        stop_reading(p);
        set_response(p, 3, r, 1);
        c->mode = 0;
        break;
    case 0x1E: /* ReadTOC */
        set_response(p, 3, r, 1);
        r[0] = c->stat;
        queue_second(p, 2, r, 1, PSX_CPU_HZ / 2);
        break;
    default:
        NR_WARN("cdrom", "invalid command %02X", c->cmd);
        error_resp(p, 0x40);
        break;
    }
}

void cdrom_event(psx *p)
{
    psx_cdrom *c = &p->cd;
    if (c->cmd_stage == STG_FIRST) {
        if (c->irq_flags & 7) {
            /* previous interrupt not acknowledged yet: wait */
            psx_schedule(p, EV_CDROM, 1000);
            return;
        }
        c->cmd_stage = STG_NONE;
        exec_command(p);
        return;
    }
    if (c->resp2_valid) {
        if (c->irq_flags & 7) {
            psx_schedule(p, EV_CDROM, 1000);
            return;
        }
        c->resp2_valid = false;
        c->cmd_stage = STG_NONE;
        if (c->cmd == 0x15 || c->cmd == 0x16) { c->seeking = false; c->stat &= (u8)~ST_SEEK; }
        set_response(p, c->resp2_irq, c->resp2, c->nresp2);
    }
}

static void deliver_pending_sector(psx *p)
{
    psx_cdrom *c = &p->cd;
    u8 r[1];
    if (!c->has_pending_sector || (c->irq_flags & 7)) return;
    memcpy(c->sector, c->pending_sector, sizeof(c->sector));
    c->has_pending_sector = false;
    r[0] = c->stat;
    set_response(p, 1, r, 1);
}

void cdrom_read_event(psx *p)
{
    psx_cdrom *c = &p->cd;
    u8 raw[2352];
    u8 r[1];
    if (!c->reading) return;
    if (c->seeking) {
        c->seeking = false;
        c->stat = (u8)((c->stat & ~ST_SEEK) | ST_READ);
    }
    if (!nr_disc_read_raw(p->disc, c->cur_lba, raw)) {
        NR_WARN("cdrom", "read past the end of the disc (LBA %u)", c->cur_lba);
        stop_reading(p);
        r[0] = (u8)(c->stat | ST_ERROR);
        set_response(p, 4, r, 1);
        return;
    }
    memcpy(c->last_header, raw + 12, 4);
    memcpy(c->last_subheader, raw + 16, 4);
    c->cur_lba++;
    c->sectors_read++;
    /* XA real-time audio sectors would go to the ADPCM decoder here. */
    if ((c->mode & 0x40) && raw[15] == 2 && (raw[18] & 0x44) == 0x44) {
        nr_unimplemented("cdrom", "XA-ADPCM sector playback");
    } else if (c->irq_flags & 7) {
        /* Host has not acknowledged the previous INT yet: buffer (overrun drops older). */
        memcpy(c->pending_sector, raw, sizeof(raw));
        c->has_pending_sector = true;
    } else {
        memcpy(c->sector, raw, sizeof(raw));
        r[0] = c->stat;
        set_response(p, 1, r, 1);
    }
    psx_schedule_periodic(p, EV_CDROM_READ, c->read_period);
}

u32 cdrom_dma_read(psx *p)
{
    u32 v = 0;
    int i;
    for (i = 0; i < 4; i++) v |= (u32)cdrom_read(p, 2) << (i * 8);
    return v;
}
