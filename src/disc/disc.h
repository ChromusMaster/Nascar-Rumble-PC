/* Disc image access: CUE/BIN (raw 2352-byte sectors) and cooked ISO (2048).
 * LBA 0 is the first sector of track 1 (MSF 00:02:00). */
#ifndef NR_DISC_H
#define NR_DISC_H

#include "../common/common.h"

#define NR_SECTOR_RAW  2352
#define NR_SECTOR_DATA 2048

typedef struct nr_disc nr_disc;

typedef struct {
    char path[160];   /* full path using '/', without ";1" */
    u32 lba;
    u32 size;
    bool is_dir;
    u32 rec_lba;      /* where its directory record lives */
    u16 rec_off;
} nr_iso_entry;

/* Opens a .cue, raw .bin (2352) or .iso (2048). On failure returns NULL and
 * writes a human readable reason in err. */
nr_disc *nr_disc_open(const char *path, char *err, size_t errlen);
void nr_disc_close(nr_disc *d);

const char *nr_disc_image_path(const nr_disc *d);
u32 nr_disc_sector_count(const nr_disc *d);
bool nr_disc_is_raw(const nr_disc *d);

/* Reads a full 2352-byte sector (synthesized header for cooked images). */
bool nr_disc_read_raw(nr_disc *d, u32 lba, u8 out[NR_SECTOR_RAW]);
/* Reads the 2048-byte user data area of a Mode1 / Mode2 Form1 sector. */
bool nr_disc_read_data(nr_disc *d, u32 lba, u8 out[NR_SECTOR_DATA]);

/* ISO9660 ----------------------------------------------------------------- */
/* Parses the volume descriptor + directory tree (cached). */
bool nr_iso_mount(nr_disc *d, char *err, size_t errlen);
int  nr_iso_count(const nr_disc *d);
const nr_iso_entry *nr_iso_get(const nr_disc *d, int index);
/* Case-insensitive lookup; accepts '\\' or '/', optional leading separator,
 * optional ";1" version suffix. */
const nr_iso_entry *nr_iso_find(const nr_disc *d, const char *path);
/* Reads a whole file into a malloc'd buffer. */
bool nr_iso_read_file(nr_disc *d, const char *path, u8 **out, u32 *size);
const char *nr_iso_volume_id(const nr_disc *d);

/* File overlay (mods): from now on the CD drive serves `data` as the content
 * of iso_path. The image file is never written: replaced sectors are
 * produced on the fly, larger files are given new sectors after the end of
 * the disc and the file's directory record is patched when the directory
 * sector is read. Returns false (with a message) if the file does not exist
 * or is a directory. The data is copied. */
bool nr_disc_overlay_file(nr_disc *d, const char *iso_path, const u8 *data, u32 size, char *err, size_t errlen);
int nr_disc_overlay_count(const nr_disc *d);

/* BCD/MSF helpers */
static inline u8 nr_bcd(u8 v) { return (u8)(((v / 10) << 4) | (v % 10)); }
static inline u8 nr_unbcd(u8 v) { return (u8)((v >> 4) * 10 + (v & 15)); }

#endif
