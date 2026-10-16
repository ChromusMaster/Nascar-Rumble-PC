/* EA "CTRL/SHOC" chunk container used by .TRK files and GLBLDATA.PSX.
 * Reverse engineered from the user's disc (see docs/FORMATS.md):
 *
 *  Every chunk starts with a FourCC stored as a little-endian u32 (so the
 *  bytes read backwards, e.g. "LRTC" = CTRL) followed by u32 total size.
 *    CTRL  group header (0x18 bytes); +0x14 = size of the group in bytes
 *    SHOC  +0x08,+0x0C zero; +0x10 FourCC subtype:
 *            SHDR resource header: +0x14 version, +0x18 type FourCC,
 *                 +0x1C resource id, +0x20 resource data size
 *            SDAT payload bytes (+0x14 .. end) appended to the current
 *                 resource until data size is reached
 *    FILL  padding up to the next 0x6000-byte boundary; when fewer than 8
 *          bytes remain only the 4-byte tag is present
 *    SWVR / VAGB / VAGM / SYNC   embedded audio streams (see swvr.c)
 */
#include "formats.h"

#include <stdlib.h>
#include <string.h>

static void fourcc(const u8 *p, char out[5])
{
    out[0] = (char)p[3]; out[1] = (char)p[2]; out[2] = (char)p[1]; out[3] = (char)p[0]; out[4] = 0;
}

void nr_ctrl_dump(const u8 *d, u32 len, FILE *out)
{
    u32 off = 0;
    char tag[5], sub[5], typ[5];
    u32 res_size = 0, res_got = 0, nres = 0, nstream = 0, groups = 0;
    while (off + 4 <= len) {
        u32 sz;
        fourcc(d + off, tag);
        if (strcmp(tag, "FILL") == 0) {
            u32 to_boundary = 0x6000 - (off % 0x6000);
            sz = to_boundary < 8 ? to_boundary : (off + 8 <= len ? nr_rd32(d + off + 4) : 4);
            if (sz != to_boundary) {
                fprintf(out, "%08X FILL with unexpected size (%X, expected %X)\n", off, sz, to_boundary);
                if (sz < 4 || off + sz > len) break;
            }
            off += sz;
            continue;
        }
        if (off + 8 > len) break;
        sz = nr_rd32(d + off + 4);
        if (sz < 8 || off + sz > len) { fprintf(out, "%08X invalid chunk '%s' size %X\n", off, tag, sz); break; }
        if (strcmp(tag, "CTRL") == 0) {
            groups++;
            fprintf(out, "%08X CTRL group #%u: group size=%X (fields %08X %08X %08X)\n", off, groups,
                     sz >= 0x18 ? nr_rd32(d + off + 0x14) : 0, nr_rd32(d + off + 8), nr_rd32(d + off + 0xC), nr_rd32(d + off + 0x10));
        } else if (strcmp(tag, "SHOC") == 0 && sz >= 0x14) {
            fourcc(d + off + 0x10, sub);
            if (strcmp(sub, "SHDR") == 0 && sz >= 0x24) {
                if (nres && res_got != res_size)
                    fprintf(out, "         warning: previous resource has %X of %X bytes\n", res_got, res_size);
                fourcc(d + off + 0x18, typ);
                res_size = nr_rd32(d + off + 0x20);
                res_got = 0;
                nres++;
                fprintf(out, "%08X   resource %-4s id=%08X version=%u size=%X\n", off, typ, nr_rd32(d + off + 0x1C), nr_rd32(d + off + 0x14), res_size);
            } else if (strcmp(sub, "SDAT") == 0) {
                res_got += sz - 0x14;
            } else {
                fprintf(out, "%08X   SHOC unknown subtype '%s'\n", off, sub);
            }
        } else if (strcmp(tag, "SWVR") == 0) {
            char name[64] = { 0 };
            u32 k;
            for (k = 0; k < 63 && 0x14 + k < sz && d[off + 0x14 + k]; k++) name[k] = (char)d[off + 0x14 + k];
            nstream++;
            fprintf(out, "%08X   stream SWVR \"%s\"\n", off, name);
        } else if (strcmp(tag, "VAGB") == 0 || strcmp(tag, "VAGM") == 0 || strcmp(tag, "SYNC") == 0) {
            /* audio stream payload, described by swvr.c */
        } else {
            fprintf(out, "%08X chunk '%s' size %X\n", off, tag, sz);
        }
        off += sz;
    }
    fprintf(out, "Summary: %u CTRL group(s), %u resource(s), %u SWVR stream(s), %X of %X bytes walked\n", groups, nres, nstream, off, len);
}

u8 *nr_ctrl_extract(const u8 *d, u32 len, const char *type, int nth, u32 *out_size)
{
    u32 off = 0, res_size = 0, got = 0;
    int idx = -1;
    bool want = false;
    u8 *buf = NULL;
    char tag[5], sub[5], typ[5];
    while (off + 4 <= len) {
        u32 sz;
        fourcc(d + off, tag);
        if (strcmp(tag, "FILL") == 0) {
            u32 tb = 0x6000 - (off % 0x6000);
            sz = tb < 8 ? tb : (off + 8 <= len ? nr_rd32(d + off + 4) : 4);
            if (sz < 4 || off + sz > len) break;
            off += sz;
            continue;
        }
        if (off + 8 > len) break;
        sz = nr_rd32(d + off + 4);
        if (sz < 8 || off + sz > len) break;
        if (strcmp(tag, "SHOC") == 0 && sz >= 0x14) {
            fourcc(d + off + 0x10, sub);
            if (strcmp(sub, "SHDR") == 0 && sz >= 0x24) {
                if (want) break;
                fourcc(d + off + 0x18, typ);
                if (strcmp(typ, type) == 0 && ++idx == nth) {
                    want = true;
                    res_size = nr_rd32(d + off + 0x20);
                    buf = (u8 *)calloc(1, res_size + 1);
                    if (!buf) return NULL;
                    got = 0;
                }
            } else if (strcmp(sub, "SDAT") == 0 && want) {
                u32 n = sz - 0x14;
                if (got + n > res_size) n = res_size - got;
                memcpy(buf + got, d + off + 0x14, n);
                got += n;
                if (got >= res_size) break;
            }
        }
        off += sz;
    }
    if (!want) return NULL;
    *out_size = got;
    return buf;
}
