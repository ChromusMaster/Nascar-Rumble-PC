/* .LSC loading/help screens (reverse engineered from the user's disc and
 * validated against the image the original program uploads to VRAM):
 *
 *   u32  sizeA          size of part A in bytes
 *   u32  sizeB          size of part B in bytes
 *   part A, part B      each: u16 width, u16 height, then a standard BS v2
 *                       frame (8-byte BS header + bitstream)
 *
 * Both parts observed on the disc are 160x256; part A is the left half and
 * part B the right half of a 320x256 picture. */
#include "formats.h"
#include "../psx/psx.h"

#include <stdlib.h>
#include <string.h>

const u8 nr_mdec_default_quant[64] = {
    2, 16, 16, 19, 16, 19, 22, 22, 22, 22, 22, 22, 26, 24, 26, 27,
    27, 27, 26, 26, 26, 26, 27, 27, 27, 29, 29, 29, 34, 34, 34, 29,
    29, 29, 27, 27, 29, 29, 32, 32, 34, 34, 37, 38, 37, 35, 35, 34,
    35, 38, 38, 40, 40, 40, 48, 48, 46, 46, 56, 56, 58, 69, 69, 83
};

void nr_image_free(nr_image *img)
{
    free(img->rgba);
    img->rgba = NULL;
    img->w = img->h = 0;
}

int nr_mdec_rle_to_image(const u16 *rle, u32 n, int w, int h, nr_image *img)
{
    img->w = w;
    img->h = h;
    img->rgba = (u32 *)calloc((size_t)w * (size_t)h, 4);
    if (!img->rgba) return -1;
    return mdec_decode_rle_image(rle, n, w, h, nr_mdec_default_quant, nr_mdec_default_quant, img->rgba);
}

static bool decode_part(const u8 *p, u32 len, nr_image *out, char *err, size_t errlen)
{
    nr_bs_table t;
    u16 *rle;
    int n, blocks, w, h, mbs;
    if (len < 12) { snprintf(err, errlen, "truncated LSC part"); return false; }
    w = nr_rd16(p);
    h = nr_rd16(p + 2);
    if (w <= 0 || h <= 0 || w > 1024 || h > 512 || (w & 15) || (h & 15)) { snprintf(err, errlen, "invalid LSC dimensions %dx%d", w, h); return false; }
    nr_bs_table_standard(&t);
    rle = (u16 *)malloc(0x40000 * sizeof(u16));
    n = nr_bs_decode(p + 4, len - 4, &t, rle, 0x40000, &blocks, err, errlen);
    if (n < 0) { free(rle); return false; }
    mbs = (w / 16) * (h / 16);
    if (blocks != mbs * 6) {
        snprintf(err, errlen, "block count (%d) does not match %dx%d", blocks, w, h);
        free(rle);
        return false;
    }
    if (nr_mdec_rle_to_image(rle, (u32)n, w, h, out) != mbs) {
        snprintf(err, errlen, "failed to decode macroblocks");
        free(rle);
        nr_image_free(out);
        return false;
    }
    free(rle);
    return true;
}

bool nr_lsc_decode(const u8 *data, u32 len, nr_image *img, char *err, size_t errlen)
{
    u32 sa, sb;
    nr_image a, b;
    int y;
    memset(img, 0, sizeof(*img));
    if (len < 8) { snprintf(err, errlen, "LSC file too small"); return false; }
    sa = nr_rd32(data);
    sb = nr_rd32(data + 4);
    if (8 + (u64)sa + sb > len) { snprintf(err, errlen, "invalid LSC part sizes (%u+%u > %u)", sa, sb, len); return false; }
    if (!decode_part(data + 8, sa, &a, err, errlen)) return false;
    if (!decode_part(data + 8 + sa, sb, &b, err, errlen)) { nr_image_free(&a); return false; }
    if (a.h != b.h) { snprintf(err, errlen, "LSC parts have different heights"); nr_image_free(&a); nr_image_free(&b); return false; }
    img->w = a.w + b.w;
    img->h = a.h;
    img->rgba = (u32 *)malloc((size_t)img->w * (size_t)img->h * 4);
    for (y = 0; y < img->h; y++) {
        memcpy(img->rgba + y * img->w, a.rgba + y * a.w, (size_t)a.w * 4);
        memcpy(img->rgba + y * img->w + a.w, b.rgba + y * b.w, (size_t)b.w * 4);
    }
    nr_image_free(&a);
    nr_image_free(&b);
    return true;
}
