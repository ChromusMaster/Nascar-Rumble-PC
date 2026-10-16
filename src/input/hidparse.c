#include "hidparse.h"

#include <string.h>

#define MAX_USAGES 64
#define MAX_STACK 4

typedef struct {
    u16 usage_page;
    s32 lmin, lmax;
    u32 rsize, rcount;
    u8 rid;
} gstate;

static u32 item_u(const u8 *p, int n)
{
    u32 v = 0;
    int i;
    for (i = 0; i < n; i++) v |= (u32)p[i] << (8 * i);
    return v;
}

static s32 item_s(const u8 *p, int n)
{
    u32 v = item_u(p, n);
    if (n == 1) return (s8)v;
    if (n == 2) return (s16)v;
    return (s32)v;
}

s16 nr_hid_norm_axis(s32 v, s32 lmin, s32 lmax)
{
    double t;
    if (lmax <= lmin) return 0;
    if (v < lmin) v = lmin;
    if (v > lmax) v = lmax;
    t = ((double)v - (double)lmin) / ((double)lmax - (double)lmin);   /* 0..1 */
    t = t * 65535.0 - 32768.0;
    if (t < -32768.0) t = -32768.0;
    if (t > 32767.0) t = 32767.0;
    return (s16)(t >= 0 ? t + 0.5 : t - 0.5);
}

static bool is_axis_usage(u16 page, u16 usage) { return page == 0x01 && usage >= 0x30 && usage <= 0x37; }

bool nr_hid_parse(const u8 *d, size_t len, nr_hid_layout *out)
{
    gstate g, stack[MAX_STACK];
    int sp = 0, depth = 0, app_depth = -1;
    u32 usages[MAX_USAGES];
    int nusages = 0;
    u32 umin = 0, umax = 0;
    bool have_min = false, have_max = false, found_app = false, done = false;
    u16 bit_off[256];
    size_t i = 0;
    memset(out, 0, sizeof(*out));
    memset(&g, 0, sizeof(g));
    memset(bit_off, 0, sizeof(bit_off));
    while (i < len && !done) {
        u8 prefix = d[i];
        int size, type, tag;
        const u8 *data;
        if (prefix == 0xFE) {                       /* long item: skip */
            if (i + 2 >= len) return false;
            i += 3u + d[i + 1];
            continue;
        }
        size = prefix & 3;
        if (size == 3) size = 4;
        type = (prefix >> 2) & 3;
        tag = prefix >> 4;
        if (i + 1 + (size_t)size > len) return false;
        data = d + i + 1;
        i += 1u + (size_t)size;
        if (type == 1) {                            /* global */
            switch (tag) {
            case 0: g.usage_page = (u16)item_u(data, size); break;
            case 1: g.lmin = item_s(data, size); break;
            case 2: g.lmax = item_s(data, size);
                /* many descriptors store an unsigned maximum in a signed field */
                if (g.lmax < g.lmin) g.lmax = (s32)item_u(data, size);
                break;
            case 7: g.rsize = item_u(data, size); break;
            case 8: g.rid = (u8)item_u(data, size); out->uses_report_ids = true; break;
            case 9: g.rcount = item_u(data, size); break;
            case 10: if (sp < MAX_STACK) stack[sp++] = g; break;
            case 11: if (sp > 0) g = stack[--sp]; break;
            default: break;
            }
        } else if (type == 2) {                     /* local */
            u32 v = item_u(data, size);
            if (size < 4) v |= (u32)g.usage_page << 16;
            switch (tag) {
            case 0: if (nusages < MAX_USAGES) usages[nusages++] = v; break;
            case 1: umin = v; have_min = true; break;
            case 2: umax = v; have_max = true; break;
            default: break;
            }
        } else if (type == 0) {                     /* main */
            u32 flags = item_u(data, size);
            if (tag == 10) {                        /* collection */
                u32 u = nusages ? usages[0] : ((u32)g.usage_page << 16);
                if (data && size && flags == 1 && app_depth < 0 && !found_app) {
                    u16 page = (u16)(u >> 16), us = (u16)u;
                    if (page == 0x01 && (us == 0x04 || us == 0x05 || us == 0x08)) {
                        app_depth = depth;
                        found_app = true;
                        out->top_usage_page = page;
                        out->top_usage = us;
                    }
                }
                depth++;
            } else if (tag == 12) {                 /* end collection */
                depth--;
                if (app_depth >= 0 && depth == app_depth) { app_depth = -1; done = true; }
            } else if (tag == 8) {                  /* input */
                u32 k;
                u16 *off = &bit_off[g.rid];
                bool inside = app_depth >= 0;
                if (!(flags & 1) && inside && (flags & 2)) {   /* data, variable */
                    for (k = 0; k < g.rcount; k++) {
                        u32 u;
                        nr_hid_field f;
                        if (nusages) u = usages[k < (u32)nusages ? k : (u32)nusages - 1];
                        else if (have_min) {
                            u = umin + k;
                            if (have_max && u > umax) u = umax;
                        } else u = (u32)g.usage_page << 16;
                        f.report_id = g.rid;
                        f.bit_offset = (u16)(*off + k * g.rsize);
                        f.bit_size = (u8)(g.rsize > 32 ? 32 : g.rsize);
                        f.logical_min = g.lmin;
                        f.logical_max = g.lmax;
                        f.usage_page = (u16)(u >> 16);
                        f.usage = (u16)u;
                        if (f.usage_page == 0x09 && out->nbuttons < NR_HID_MAX_BUTTONS) out->button[out->nbuttons++] = f;
                        else if (f.usage_page == 0x01 && f.usage == 0x39 && out->nhats < NR_HID_MAX_HATS) out->hat[out->nhats++] = f;
                        else if (is_axis_usage(f.usage_page, f.usage) && out->naxes < NR_HID_MAX_AXES) out->axis[out->naxes++] = f;
                    }
                }
                *off = (u16)(*off + g.rsize * g.rcount);
            }
            /* locals are reset after every main item */
            nusages = 0;
            have_min = have_max = false;
            umin = umax = 0;
        }
    }
    return found_app && (out->nbuttons > 0 || out->naxes > 0 || out->nhats > 0);
}

void nr_hid_state_reset(nr_hid_state *s)
{
    memset(s, 0, sizeof(*s));
    s->hat[0] = s->hat[1] = -1;
}

static u32 get_bits(const u8 *p, size_t len, u32 off, u32 n)
{
    u32 v = 0, i;
    for (i = 0; i < n; i++) {
        u32 b = off + i;
        if ((b >> 3) >= len) break;
        if (p[b >> 3] & (1u << (b & 7))) v |= 1u << i;
    }
    return v;
}

static s32 field_value(const nr_hid_field *f, const u8 *p, size_t len)
{
    u32 v = get_bits(p, len, f->bit_offset, f->bit_size);
    if (f->logical_min < 0 && f->bit_size < 32 && (v & (1u << (f->bit_size - 1)))) v |= ~0u << f->bit_size;
    return (s32)v;
}

void nr_hid_decode(const nr_hid_layout *l, const u8 *report, size_t len, nr_hid_state *s)
{
    u8 id = 0;
    int i;
    if (l->uses_report_ids) {
        if (len < 1) return;
        id = report[0];
        report++;
        len--;
    }
    for (i = 0; i < l->nbuttons; i++) {
        const nr_hid_field *f = &l->button[i];
        if (f->report_id != id) continue;
        if (field_value(f, report, len) != 0) s->buttons |= 1u << i;
        else s->buttons &= ~(1u << i);
    }
    for (i = 0; i < l->naxes; i++) {
        const nr_hid_field *f = &l->axis[i];
        if (f->report_id != id) continue;
        s->axis[i] = nr_hid_norm_axis(field_value(f, report, len), f->logical_min, f->logical_max);
    }
    for (i = 0; i < l->nhats; i++) {
        const nr_hid_field *f = &l->hat[i];
        s32 v;
        if (f->report_id != id) continue;
        v = field_value(f, report, len) - f->logical_min;
        if (f->logical_max - f->logical_min == 3) s->hat[i] = (s8)((v >= 0 && v <= 3) ? v * 2 : -1);   /* 4-way hats */
        else s->hat[i] = (s8)((v >= 0 && v <= 7 && f->logical_max - f->logical_min >= 7) ? v : -1);
    }
}
