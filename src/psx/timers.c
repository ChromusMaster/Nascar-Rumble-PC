/* Root counters (timers 0-2). Reference: psx-spx "Timers".
 * Known simplifications (documented in docs/STATUS.md):
 *  - the 1-2 cycle hold at 0000h after reset/wrap is not modelled;
 *  - Timer0 hblank synchronisation modes are approximated at scanline
 *    granularity. */
#include "psx.h"

#include <string.h>

static u32 dot_divider(psx *p)
{
    static const u32 divs[4] = { 10, 8, 5, 4 };
    u32 m = p->gpu.disp_mode;
    if (m & 0x40) return 7; /* 368 mode */
    return divs[m & 3];
}

static void timer_irq(psx *p, int t)
{
    psx_timer *tm = &p->timers[t];
    if (!(tm->mode & 0x40) && tm->irq_done) return;
    tm->irq_done = true;
    if (tm->mode & 0x80) {
        tm->mode ^= 0x400;
        if (!(tm->mode & 0x400)) psx_irq(p, IRQ_TMR0 + t);
    } else {
        tm->mode &= ~0x400u;
        psx_irq(p, IRQ_TMR0 + t);
        tm->mode |= 0x400u; /* short pulse */
    }
}

static void advance(psx *p, int t, u32 n)
{
    psx_timer *tm = &p->timers[t];
    bool zret = (tm->mode >> 3) & 1;
    while (n) {
        u32 c = tm->counter;
        u32 top = (zret && c <= tm->target) ? tm->target : 0xFFFF;
        u32 stop, dist;
        if (c == top) {
            tm->counter = 0;
            n--;
            if (tm->target == 0) { tm->mode |= 0x800; if (tm->mode & 0x10) timer_irq(p, t); }
            continue;
        }
        stop = top;
        if (tm->target > c && tm->target < top) stop = tm->target;
        dist = stop - c;
        if (n < dist) { tm->counter = c + n; return; }
        tm->counter = stop;
        n -= dist;
        if (tm->counter == tm->target) { tm->mode |= 0x800; if (tm->mode & 0x10) timer_irq(p, t); }
        if (tm->counter == 0xFFFF) { tm->mode |= 0x1000; if (tm->mode & 0x20) timer_irq(p, t); }
    }
}

static bool is_paused(psx *p, int t)
{
    psx_timer *tm = &p->timers[t];
    u32 sm;
    if (!(tm->mode & 1)) return false;
    sm = (tm->mode >> 1) & 3;
    if (t == 2) return sm == 0 || sm == 3;
    if (tm->paused) return true;
    if (t == 1) {
        if (sm == 0) return p->gpu.in_vblank;
        if (sm == 2) return !p->gpu.in_vblank;
    }
    return false;
}

/* Bring a timer up to date with the CPU cycle counter. */
static void sync(psx *p, int t)
{
    psx_timer *tm = &p->timers[t];
    u64 elapsed = p->cycles - tm->last_update;
    u32 ticks = 0;
    tm->last_update = p->cycles;
    if (!elapsed) return;
    if (is_paused(p, t)) return;
    if (t == 0 && (tm->mode & 0x100)) {
        u64 num = elapsed * PSX_VCLK_TO_CPU_DEN + tm->frac;
        u64 den = PSX_VCLK_TO_CPU_NUM * dot_divider(p);
        ticks = (u32)(num / den);
        tm->frac = (u32)(num % den);
    } else if (t == 1 && (tm->mode & 0x100)) {
        return; /* hblank-clocked: advanced by timers_hblank */
    } else if (t == 2 && (tm->mode & 0x200)) {
        u64 num = elapsed + tm->frac;
        ticks = (u32)(num / 8);
        tm->frac = (u32)(num % 8);
    } else {
        ticks = (u32)(elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : elapsed);
    }
    if (ticks) advance(p, t, ticks);
}

static void reschedule(psx *p, int t)
{
    psx_timer *tm = &p->timers[t];
    u32 c = tm->counter, dist = 0x10000;
    u64 cyc;
    if (is_paused(p, t) || (t == 1 && (tm->mode & 0x100))) { psx_unschedule(p, EV_TIMER0 + t); return; }
    if (!(tm->mode & 0x30)) {
        /* No IRQ wanted: still resync periodically to keep flags right. */
        psx_schedule(p, EV_TIMER0 + t, 0x10000 * 8);
        return;
    }
    if ((tm->mode & 0x10) && tm->target > c) dist = tm->target - c;
    else if ((tm->mode & 0x10) && (tm->mode & 0x08)) dist = (0xFFFF - c) + 1 + tm->target;
    if ((tm->mode & 0x20)) { u32 d2 = 0xFFFF - c; if (d2 && d2 < dist) dist = d2; }
    if ((tm->mode & 0x08) && c > tm->target) { /* will wrap via FFFF first */ }
    if (dist == 0) dist = 1;
    if (t == 0 && (tm->mode & 0x100)) cyc = ((u64)dist * dot_divider(p) * PSX_VCLK_TO_CPU_NUM) / PSX_VCLK_TO_CPU_DEN + 1;
    else if (t == 2 && (tm->mode & 0x200)) cyc = (u64)dist * 8;
    else cyc = dist;
    psx_schedule(p, EV_TIMER0 + t, cyc);
}

void timers_reset(psx *p)
{
    int t;
    memset(p->timers, 0, sizeof(p->timers));
    for (t = 0; t < 3; t++) {
        p->timers[t].mode = 0x400;
        p->timers[t].last_update = p->cycles;
    }
}

u32 timers_read(psx *p, u32 addr)
{
    int t = (int)((addr >> 4) & 3);
    u32 reg = addr & 0xF;
    psx_timer *tm;
    if (t > 2) return 0;
    tm = &p->timers[t];
    sync(p, t);
    switch (reg & 0xC) {
    case 0x0: return tm->counter;
    case 0x4: {
        u32 v = tm->mode;
        tm->mode &= ~0x1800u;
        return v;
    }
    case 0x8: return tm->target;
    default: return 0;
    }
}

void timers_write(psx *p, u32 addr, u32 v)
{
    int t = (int)((addr >> 4) & 3);
    u32 reg = addr & 0xF;
    psx_timer *tm;
    if (t > 2) return;
    tm = &p->timers[t];
    sync(p, t);
    switch (reg & 0xC) {
    case 0x0: tm->counter = v & 0xFFFF; break;
    case 0x4:
        tm->mode = (v & 0x3FF) | 0x400;
        tm->counter = 0;
        tm->irq_done = false;
        tm->frac = 0;
        tm->paused = false;
        if ((tm->mode & 1) && t < 2 && ((tm->mode >> 1) & 3) == 3) tm->paused = true;
        if ((tm->mode & 1) && t == 0) nr_unimplemented("timer", "timer0 hblank sync (approximated per scanline)");
        break;
    case 0x8: tm->target = v & 0xFFFF; break;
    default: break;
    }
    reschedule(p, t);
}

void timers_event(psx *p, int t)
{
    sync(p, t);
    reschedule(p, t);
}

void timers_hblank(psx *p)
{
    psx_timer *t1 = &p->timers[1];
    psx_timer *t0 = &p->timers[0];
    if (t1->mode & 0x100) {
        if (!is_paused(p, 1)) advance(p, 1, 1);
    }
    if (t0->mode & 1) {
        u32 sm = (t0->mode >> 1) & 3;
        sync(p, 0);
        if (sm == 1 || sm == 2) t0->counter = 0;
        if (sm == 3) t0->paused = false;
        reschedule(p, 0);
    }
}

void timers_vblank(psx *p, bool start)
{
    psx_timer *t1 = &p->timers[1];
    sync(p, 1);
    if ((t1->mode & 1) && start) {
        u32 sm = (t1->mode >> 1) & 3;
        if (sm == 1 || sm == 2) t1->counter = 0;
        if (sm == 3) t1->paused = false;
    }
    reschedule(p, 1);
}
