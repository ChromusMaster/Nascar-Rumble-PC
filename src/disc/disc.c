#include "disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct nr_disc {
    FILE *f;
    char image_path[1024];
    u32 sector_size;   /* 2352 or 2048 */
    u32 sectors;
    u32 data_offset;   /* byte offset of LBA 0 inside the file */
    /* iso */
    nr_iso_entry *entries;
    int nentries, cap;
    char volume_id[33];
    bool mounted;
    /* overlay (mods) */
    struct { u32 lba, nsec, size; u8 *data; } *ovl;
    int novl;
    struct { u32 lba; u16 off; u32 new_lba, new_size; } *dpatch;
    int ndpatch;
    u32 virt_next;
};

static const u8 k_sync2[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };

/* builds a Mode 2 Form 1 sector carrying 2048 data bytes */
static void synth_sector(u32 lba, const u8 *data, u8 out[NR_SECTOR_RAW])
{
    u32 a = lba + 150;
    memset(out, 0, NR_SECTOR_RAW);
    memcpy(out, k_sync2, 12);
    out[12] = nr_bcd((u8)(a / 4500));
    out[13] = nr_bcd((u8)((a / 75) % 60));
    out[14] = nr_bcd((u8)(a % 75));
    out[15] = 2;
    out[18] = out[22] = 0x08;
    memcpy(out + 24, data, 2048);
}

static void set_err(char *err, size_t n, const char *fmt, const char *a)
{
    if (err && n) snprintf(err, n, fmt, a ? a : "");
}

static void dir_of(const char *path, char *out, size_t n)
{
    const char *s1 = strrchr(path, '/'), *s2 = strrchr(path, '\\');
    const char *s = s1 > s2 ? s1 : s2;
    if (!s) { nr_strlcpy(out, "", n); return; }
    {
        size_t l = (size_t)(s - path) + 1;
        if (l >= n) l = n - 1;
        memcpy(out, path, l);
        out[l] = 0;
    }
}

static bool ends_with_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && nr_strcasecmp(s + a - b, suf) == 0;
}

/* Minimal CUE parser: first FILE + first TRACK must be a data track. */
static bool parse_cue(const char *cue, char *bin, size_t binlen, u32 *sector_size, char *err, size_t errlen)
{
    u8 *txt;
    long n = nr_read_file(cue, &txt);
    char dir[1024];
    char *line, *next;
    bool have_file = false, have_track = false;
    if (n < 0) { set_err(err, errlen, "Could not read the CUE file: %s", cue); return false; }
    dir_of(cue, dir, sizeof(dir));
    for (line = (char *)txt; line && *line; line = next) {
        next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; while (*next == '\r' || *next == '\n') next++; }
        while (*line == ' ' || *line == '\t') line++;
        if (!have_file && strncmp(line, "FILE", 4) == 0) {
            char *q1 = strchr(line, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            char name[256];
            if (q1 && q2) {
                size_t l = (size_t)(q2 - q1 - 1);
                if (l >= sizeof(name)) l = sizeof(name) - 1;
                memcpy(name, q1 + 1, l); name[l] = 0;
            } else {
                if (sscanf(line + 4, " %255s", name) != 1) continue;
            }
            if ((size_t)snprintf(bin, binlen, "%s%s", dir, name) >= binlen) continue;
            have_file = true;
        } else if (!have_track && strncmp(line, "TRACK", 5) == 0) {
            char mode[32] = { 0 };
            int num;
            if (sscanf(line + 5, " %d %31s", &num, mode) != 2) continue;
            if (strcmp(mode, "MODE2/2352") == 0 || strcmp(mode, "MODE1/2352") == 0) *sector_size = 2352;
            else if (strcmp(mode, "MODE1/2048") == 0) *sector_size = 2048;
            else { set_err(err, errlen, "Track mode not supported in the CUE: %s (expected MODE2/2352)", mode); free(txt); return false; }
            have_track = true;
        }
    }
    free(txt);
    if (!have_file || !have_track) { set_err(err, errlen, "Invalid CUE (FILE/TRACK missing): %s", cue); return false; }
    return true;
}

static const u8 k_sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };

nr_disc *nr_disc_open(const char *path, char *err, size_t errlen)
{
    nr_disc *d;
    char bin[1024];
    u32 ss = 0;
    long fsz;
    if (!path || !*path) { set_err(err, errlen, "No disc image path given.%s", ""); return NULL; }
    if (nr_is_dir(path)) {
        set_err(err, errlen, "'%s' is a folder. Select the disc image (.cue/.bin/.iso); extracted files "
                              "are not enough to run the game (the CD-ROM needs the original sectors).", path);
        return NULL;
    }
    if (!nr_file_exists(path)) { set_err(err, errlen, "File not found: %s", path); return NULL; }
    if (ends_with_ci(path, ".cue")) {
        if (!parse_cue(path, bin, sizeof(bin), &ss, err, errlen)) return NULL;
        if (!nr_file_exists(bin)) { set_err(err, errlen, "The CUE refers to a BIN that does not exist: %s", bin); return NULL; }
    } else {
        nr_strlcpy(bin, path, sizeof(bin));
    }
    d = (nr_disc *)calloc(1, sizeof(*d));
    d->f = nr_fopen(bin, "rb");
    if (!d->f) { set_err(err, errlen, "Failed to open: %s", bin); free(d); return NULL; }
    nr_strlcpy(d->image_path, bin, sizeof(d->image_path));
    fseek(d->f, 0, SEEK_END);
    fsz = ftell(d->f);
    fseek(d->f, 0, SEEK_SET);
    if (!ss) {
        /* Auto-detect: raw sectors start with the 12-byte sync pattern. */
        u8 hdr[16];
        if (fread(hdr, 1, 16, d->f) == 16 && memcmp(hdr, k_sync, 12) == 0) ss = 2352;
        else if (fsz % 2048 == 0) ss = 2048;
        else { set_err(err, errlen, "Unrecognized image format: %s", bin); nr_disc_close(d); return NULL; }
    }
    if (fsz <= 0 || fsz % ss != 0) {
        set_err(err, errlen, "Image size is not a multiple of the sector size (truncated image?): %s", bin);
        nr_disc_close(d);
        return NULL;
    }
    d->sector_size = ss;
    d->sectors = (u32)(fsz / ss);
    if (d->sectors < 32) { set_err(err, errlen, "Image too small for a PlayStation disc: %s", bin); nr_disc_close(d); return NULL; }
    return d;
}

void nr_disc_close(nr_disc *d)
{
    if (!d) return;
    if (d->f) fclose(d->f);
    {
        int i;
        for (i = 0; i < d->novl; i++) free(d->ovl[i].data);
    }
    free(d->ovl);
    free(d->dpatch);
    free(d->entries);
    free(d);
}

const char *nr_disc_image_path(const nr_disc *d) { return d->image_path; }
u32 nr_disc_sector_count(const nr_disc *d) { return d->sectors; }
bool nr_disc_is_raw(const nr_disc *d) { return d->sector_size == 2352; }

static bool read_raw_base(nr_disc *d, u32 lba, u8 out[NR_SECTOR_RAW]);

bool nr_disc_read_raw(nr_disc *d, u32 lba, u8 out[NR_SECTOR_RAW])
{
    int i;
    for (i = 0; i < d->novl; i++) {
        if (lba >= d->ovl[i].lba && lba < d->ovl[i].lba + d->ovl[i].nsec) {
            u8 buf[2048];
            u32 off = (lba - d->ovl[i].lba) * 2048, n = d->ovl[i].size > off ? d->ovl[i].size - off : 0;
            if (n > 2048) n = 2048;
            memset(buf, 0, sizeof(buf));
            memcpy(buf, d->ovl[i].data + off, n);
            synth_sector(lba, buf, out);
            return true;
        }
    }
    if (!read_raw_base(d, lba, out)) return false;
    for (i = 0; i < d->ndpatch; i++) {
        if (d->dpatch[i].lba == lba) {
            u8 *r = out + (out[15] == 1 ? 16 : 24) + d->dpatch[i].off;
            u32 nl = d->dpatch[i].new_lba, ns = d->dpatch[i].new_size;
            nr_wr32(r + 2, nl); r[6] = (u8)(nl >> 24); r[7] = (u8)(nl >> 16); r[8] = (u8)(nl >> 8); r[9] = (u8)nl;
            nr_wr32(r + 10, ns); r[14] = (u8)(ns >> 24); r[15] = (u8)(ns >> 16); r[16] = (u8)(ns >> 8); r[17] = (u8)ns;
        }
    }
    return true;
}

static bool read_raw_base(nr_disc *d, u32 lba, u8 out[NR_SECTOR_RAW])
{
    if (lba >= d->sectors) return false;
    if (d->sector_size == 2352) {
        if (fseek(d->f, (long)lba * 2352, SEEK_SET) != 0) return false;
        return fread(out, 1, 2352, d->f) == 2352;
    } else {
        u32 a = lba + 150;
        memset(out, 0, NR_SECTOR_RAW);
        memcpy(out, k_sync, 12);
        out[12] = nr_bcd((u8)(a / 4500));
        out[13] = nr_bcd((u8)((a / 75) % 60));
        out[14] = nr_bcd((u8)(a % 75));
        out[15] = 2;
        out[18] = out[22] = 0x08; /* Form1 data subheader */
        if (fseek(d->f, (long)lba * 2048, SEEK_SET) != 0) return false;
        return fread(out + 24, 1, 2048, d->f) == 2048;
    }
}

bool nr_disc_read_data(nr_disc *d, u32 lba, u8 out[NR_SECTOR_DATA])
{
    if (d->novl == 0 && lba >= d->sectors) return false;
    if (d->sector_size == 2048 && d->novl == 0) {
        if (fseek(d->f, (long)lba * 2048, SEEK_SET) != 0) return false;
        return fread(out, 1, 2048, d->f) == 2048;
    } else {
        u8 raw[NR_SECTOR_RAW];
        if (!nr_disc_read_raw(d, lba, raw)) return false;
        memcpy(out, raw + (raw[15] == 1 ? 16 : 24), 2048);
        return true;
    }
}

/* ISO9660 ------------------------------------------------------------------ */

static void add_entry(nr_disc *d, const char *path, u32 lba, u32 size, bool dir, u32 rec_lba, u32 rec_off)
{
    nr_iso_entry *e;
    if (d->nentries == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 64;
        d->entries = (nr_iso_entry *)realloc(d->entries, sizeof(nr_iso_entry) * (size_t)d->cap);
    }
    e = &d->entries[d->nentries++];
    nr_strlcpy(e->path, path, sizeof(e->path));
    e->lba = lba; e->size = size; e->is_dir = dir;
    e->rec_lba = rec_lba; e->rec_off = (u16)rec_off;
}

static bool walk_dir(nr_disc *d, u32 lba, u32 size, const char *prefix, int depth)
{
    u32 nsec = (size + 2047) / 2048, i;
    u8 buf[2048];
    if (depth > 8) return false;
    for (i = 0; i < nsec; i++) {
        u32 p = 0;
        if (!nr_disc_read_data(d, lba + i, buf)) return false;
        while (p < 2048) {
            u8 len = buf[p];
            u32 elba, esz;
            u8 flags, nl;
            char name[128], full[160];
            if (len == 0) break;
            if (p + len > 2048 || len < 34) return false;
            elba = nr_rd32(buf + p + 2);
            esz = nr_rd32(buf + p + 10);
            flags = buf[p + 25];
            nl = buf[p + 32];
            if (nl == 1 && (buf[p + 33] == 0 || buf[p + 33] == 1)) { p += len; continue; }
            if (nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, buf + p + 33, nl);
            name[nl] = 0;
            { char *semi = strchr(name, ';'); if (semi) *semi = 0; }
            if (name[0] && name[strlen(name) - 1] == '.') name[strlen(name) - 1] = 0;
            if ((size_t)snprintf(full, sizeof(full), "%s%s%s", prefix, *prefix ? "/" : "", name) >= sizeof(full)) { p += len; continue; }
            add_entry(d, full, elba, esz, (flags & 2) != 0, lba + i, p);
            if (flags & 2) {
                if (!walk_dir(d, elba, esz, full, depth + 1)) return false;
            }
            p += len;
        }
    }
    return true;
}

bool nr_iso_mount(nr_disc *d, char *err, size_t errlen)
{
    u8 pvd[2048];
    if (d->mounted) return true;
    if (!nr_disc_read_data(d, 16, pvd)) { set_err(err, errlen, "Failed to read the volume descriptor (sector 16).%s", ""); return false; }
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        set_err(err, errlen, "Sector 16 does not hold a valid ISO9660 descriptor (corrupted image or another format).%s", "");
        return false;
    }
    memcpy(d->volume_id, pvd + 40, 32);
    d->volume_id[32] = 0;
    { int k = 31; while (k >= 0 && d->volume_id[k] == ' ') d->volume_id[k--] = 0; }
    if (!walk_dir(d, nr_rd32(pvd + 156 + 2), nr_rd32(pvd + 156 + 10), "", 0)) {
        set_err(err, errlen, "Invalid ISO9660 directory structure.%s", "");
        return false;
    }
    d->mounted = true;
    return true;
}

int nr_iso_count(const nr_disc *d) { return d->nentries; }
const nr_iso_entry *nr_iso_get(const nr_disc *d, int i) { return (i >= 0 && i < d->nentries) ? &d->entries[i] : NULL; }
const char *nr_iso_volume_id(const nr_disc *d) { return d->volume_id; }

static void normalize(const char *in, char *out, size_t n)
{
    size_t o = 0;
    while (*in == '/' || *in == '\\') in++;
    if (nr_strcasecmp(in, "") == 0) { out[0] = 0; return; }
    if (strncmp(in, "cdrom:", 6) == 0) { in += 6; while (*in == '/' || *in == '\\') in++; }
    for (; *in && o + 1 < n; in++) {
        char c = *in;
        if (c == ';') break;
        if (c == '\\') c = '/';
        out[o++] = (char)toupper((unsigned char)c);
    }
    out[o] = 0;
}

const nr_iso_entry *nr_iso_find(const nr_disc *d, const char *path)
{
    char want[160];
    int i;
    normalize(path, want, sizeof(want));
    for (i = 0; i < d->nentries; i++)
        if (nr_strcasecmp(d->entries[i].path, want) == 0) return &d->entries[i];
    return NULL;
}

bool nr_iso_read_file(nr_disc *d, const char *path, u8 **out, u32 *size)
{
    const nr_iso_entry *e = nr_iso_find(d, path);
    u32 n, i;
    u8 *buf;
    *out = NULL;
    if (!e || e->is_dir) return false;
    n = (e->size + 2047) / 2048;
    buf = (u8 *)malloc((size_t)n * 2048 + 1);
    if (!buf) return false;
    for (i = 0; i < n; i++) {
        if (!nr_disc_read_data(d, e->lba + i, buf + (size_t)i * 2048)) { free(buf); return false; }
    }
    *out = buf;
    if (size) *size = e->size;
    return true;
}

int nr_disc_overlay_count(const nr_disc *d) { return d->novl; }

bool nr_disc_overlay_file(nr_disc *d, const char *iso_path, const u8 *data, u32 size, char *err, size_t errlen)
{
    const nr_iso_entry *e = nr_iso_find(d, iso_path);
    u32 nsec = (size + 2047) / 2048, old = e ? (e->size + 2047) / 2048 : 0, lba;
    void *p;
    if (!e || e->is_dir) { set_err(err, errlen, "file does not exist on the disc: %s", iso_path); return false; }
    if (!nsec) nsec = 1;
    if (nsec <= old) {
        lba = e->lba;
    } else {
        if (!d->virt_next) d->virt_next = d->sectors + 150;   /* leave a gap after the real data */
        lba = d->virt_next;
        d->virt_next += nsec;
    }
    p = realloc(d->ovl, sizeof(*d->ovl) * (size_t)(d->novl + 1));
    if (!p) return false;
    d->ovl = p;
    d->ovl[d->novl].lba = lba;
    d->ovl[d->novl].nsec = nsec;
    d->ovl[d->novl].size = size;
    d->ovl[d->novl].data = (u8 *)malloc(size ? size : 1);
    if (!d->ovl[d->novl].data) return false;
    memcpy(d->ovl[d->novl].data, data, size);
    d->novl++;
    p = realloc(d->dpatch, sizeof(*d->dpatch) * (size_t)(d->ndpatch + 1));
    if (!p) return false;
    d->dpatch = p;
    d->dpatch[d->ndpatch].lba = e->rec_lba;
    d->dpatch[d->ndpatch].off = e->rec_off;
    d->dpatch[d->ndpatch].new_lba = lba;
    d->dpatch[d->ndpatch].new_size = size;
    d->ndpatch++;
    return true;
}
