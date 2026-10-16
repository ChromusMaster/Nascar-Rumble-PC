/* Serial I/O 0 (controller/memory card port). Reference: psx-spx
 * "Controllers and Memory Cards". The game reads pads through the BIOS
 * (InitPAD2/StartPAD2), which the HLE kernel services directly; this module
 * still implements the digital-pad protocol on the SIO0 registers so that
 * direct hardware access behaves correctly. Memory cards are serviced by
 * the HLE kernel (file level); on the SIO bus they answer "not connected". */
#include "psx.h"

#include <string.h>

void sio_reset(psx *p)
{
    psx_sio *s = &p->sio;
    u16 b0 = s->buttons[0], b1 = s->buttons[1];
    bool c0 = s->pad_connected[0], c1 = s->pad_connected[1];
    memset(s, 0, sizeof(*s));
    s->stat = 0x0005; /* TX ready 1+2 */
    s->buttons[0] = b0 ? b0 : 0xFFFF;
    s->buttons[1] = b1 ? b1 : 0xFFFF;
    s->pad_connected[0] = c0;
    s->pad_connected[1] = c1;
}

static u8 pad_byte(psx *p, int port, u8 tx, bool *ack)
{
    psx_sio *s = &p->sio;
    u16 btn = p->host.read_pad ? p->host.read_pad(p->host.user, port) : 0xFFFF;
    bool conn = p->host.pad_connected ? p->host.pad_connected(p->host.user, port) : (port == 0);
    *ack = false;
    if (!conn) { s->device = -1; return 0xFF; }
    switch (s->step) {
    case 0:
        if (tx != 0x01) { s->device = -1; return 0xFF; }
        s->device = 1; s->step = 1; *ack = true; return 0xFF;
    case 1:
        if (tx != 0x42) { s->step = 0; s->device = -1; return 0xFF; }
        s->step = 2; *ack = true; return 0x41;
    case 2: s->step = 3; *ack = true; return 0x5A;
    case 3: s->step = 4; *ack = true; return (u8)btn;
    case 4: s->step = 0; s->device = 0; return (u8)(btn >> 8);
    default: s->step = 0; return 0xFF;
    }
}

u32 sio_read(psx *p, u32 addr, int width)
{
    psx_sio *s = &p->sio;
    u32 off = addr & 0x1F;
    (void)width;
    if (addr & 0x10) {
        /* SIO1 (serial port): nothing connected */
        if (off == 0x14) return 0x0005;
        return 0;
    }
    switch (off) {
    case 0x0: {
        u8 v = s->rx_full ? s->rx : 0xFF;
        s->rx_full = false;
        return v;
    }
    case 0x4: {
        u32 st = 0x0005;
        if (s->rx_full) st |= 0x0002;
        if (s->ack_pending) st |= 0x0200;
        return st;
    }
    case 0x8: return s->mode;
    case 0xA: return s->ctrl;
    case 0xE: return s->baud;
    default: return 0;
    }
}

void sio_write(psx *p, u32 addr, u32 v, int width)
{
    psx_sio *s = &p->sio;
    u32 off = addr & 0x1F;
    (void)width;
    if (addr & 0x10) return;
    switch (off) {
    case 0x0: { /* TX data */
        bool ack = false;
        int port = (s->ctrl >> 13) & 1;
        if (!(s->ctrl & 2)) { s->rx = 0xFF; s->rx_full = true; break; }
        if (s->device == 0 || s->device == 1) s->rx = pad_byte(p, port, (u8)v, &ack);
        else s->rx = 0xFF;
        s->rx_full = true;
        if (ack) psx_schedule(p, EV_SIO, 1088);
        break;
    }
    case 0x8: s->mode = (u16)v; break;
    case 0xA:
        s->ctrl = (u16)v;
        if (v & 0x40) { /* reset */ s->step = 0; s->device = 0; s->rx_full = false; s->ack_pending = false; }
        if (v & 0x10) { s->ack_pending = false; } /* acknowledge IRQ */
        if (!(v & 2)) { s->step = 0; s->device = 0; }
        break;
    case 0xE: s->baud = (u16)v; break;
    default: break;
    }
}

void sio_event(psx *p)
{
    psx_sio *s = &p->sio;
    s->ack_pending = true;
    if (s->ctrl & 0x1000) psx_irq(p, IRQ_SIO0);
}
