/* Minimal dependency-free PNG writer (stored/uncompressed deflate blocks).
 * Used for diagnostic screenshots and VRAM dumps. */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static void chunk(FILE *f, const char *type, const u8 *data, u32 len)
{
    u8 hdr[8];
    u32 crc;
    be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    crc = nr_crc32(0, hdr + 4, 4);
    /* continue CRC over data: nr_crc32 supports chaining */
    crc = nr_crc32(crc, data, len);
    be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

static u32 adler32(const u8 *d, size_t n)
{
    u32 a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) { a = (a + d[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

bool nr_write_png(const char *path, const u32 *rgba, int w, int h)
{
    FILE *f;
    size_t raw_len = (size_t)h * ((size_t)w * 3 + 1), i, pos = 0, zlen;
    u8 *raw, *z;
    u8 ihdr[13];
    int x, y;
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    raw = (u8 *)malloc(raw_len);
    if (!raw) return false;
    for (y = 0; y < h; y++) {
        raw[pos++] = 0;
        for (x = 0; x < w; x++) {
            u32 c = rgba[y * w + x];
            raw[pos++] = (u8)c; raw[pos++] = (u8)(c >> 8); raw[pos++] = (u8)(c >> 16);
        }
    }
    zlen = 2 + raw_len + ((raw_len + 65534) / 65535) * 5 + 4;
    z = (u8 *)malloc(zlen);
    if (!z) { free(raw); return false; }
    pos = 0;
    z[pos++] = 0x78; z[pos++] = 0x01;
    for (i = 0; i < raw_len; i += 65535) {
        size_t n = raw_len - i > 65535 ? 65535 : raw_len - i;
        z[pos++] = (i + n >= raw_len) ? 1 : 0;
        z[pos++] = (u8)n; z[pos++] = (u8)(n >> 8);
        z[pos++] = (u8)~n; z[pos++] = (u8)(~n >> 8);
        memcpy(z + pos, raw + i, n);
        pos += n;
    }
    be32(z + pos, adler32(raw, raw_len));
    pos += 4;
    f = nr_fopen(path, "wb");
    if (!f) { free(raw); free(z); return false; }
    fwrite(sig, 1, 8, f);
    be32(ihdr, (u32)w); be32(ihdr + 4, (u32)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (u32)pos);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return true;
}
