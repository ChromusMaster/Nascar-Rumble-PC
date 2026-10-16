/* Native decoders for NASCAR Rumble data formats (reverse engineered; see
 * docs/FORMATS.md). They operate on buffers read from the user's disc. */
#ifndef NR_FORMATS_H
#define NR_FORMATS_H

#include "../common/common.h"
#include <stdio.h>

typedef struct {
    int w, h;
    u32 *rgba;
} nr_image;
void nr_image_free(nr_image *img);

/* ---- BS (MDEC bitstream) -------------------------------------------- */
/* Huffman tables: standard Sony BS v1/v2 or Electronic Arts (VLC0 chunk). */
typedef struct {
    u16 value[0xE0];   /* MDEC value for each of the 224 huffman codes */
    bool ea;           /* true: values from a VLC0 chunk */
} nr_bs_table;

void nr_bs_table_standard(nr_bs_table *t);
/* vlc0 points to the 0x1C0 data bytes of a VLC0 chunk (after the 8-byte header) */
void nr_bs_table_ea(nr_bs_table *t, const u8 *vlc0);

/* Decodes a BS v1/v2 frame (8-byte header + bitstream) to MDEC RLE halfwords.
 * Returns number of halfwords written, or -1 on error. *blocks receives the
 * number of 8x8 blocks decoded. */
int nr_bs_decode(const u8 *bs, u32 len, const nr_bs_table *t, u16 *out, u32 out_cap, int *blocks, char *err, size_t errlen);

/* Exact reimplementation of the game's own BS decoder contract (header
 * copy, 64 x FE00 terminator, 1FFh end code in any block). */
int nr_bs_decode_game(const u8 *frame, u32 avail, const nr_bs_table *t, u16 *out, u32 cap);

/* Default MDEC quantisation table used by the game (captured from its MDEC(2)
 * command; identical to the PsyQ default / MPEG-1 intra matrix in zigzag order). */
extern const u8 nr_mdec_default_quant[64];
/* Decodes RLE halfwords into an RGBA picture (column-major macroblocks). */
int nr_mdec_rle_to_image(const u16 *rle, u32 n, int w, int h, nr_image *img);

/* ---- LSC (loading / help screens) ------------------------------------ */
bool nr_lsc_decode(const u8 *data, u32 len, nr_image *img, char *err, size_t errlen);

/* ---- CTRL/SHOC chunk container (.TRK, GLBLDATA.PSX) ---------------- */
void nr_ctrl_dump(const u8 *data, u32 len, FILE *out);
/* Extracts the payload of the n-th resource of a type (FourCC as text, e.g.
 * "TxtR"). Returns a malloc'd buffer or NULL. */
u8 *nr_ctrl_extract(const u8 *data, u32 len, const char *type, int nth, u32 *out_size);

/* ---- SWVR (.AV) and WVE ----------------------------------------------- */
int nr_swvr_dump(const u8 *data, u32 len, FILE *out);
int nr_swvr_to_wav(const u8 *data, u32 len, const char *wav_path, FILE *log);

/* ---- SPU-ADPCM --------------------------------------------------------- */
typedef struct { s32 h1, h2; } nr_adpcm_state;
/* Decodes one 16-byte SPU-ADPCM block (or 15-byte variant without the flag
 * byte when has_flags=false) into 28 samples. Returns the flag byte. */
u8 nr_adpcm_decode_block(const u8 *blk, bool has_flags, nr_adpcm_state *st, s16 out[28]);

#endif
