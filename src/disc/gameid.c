#include "gameid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_boot(const char *cnf, char *out, size_t n)
{
    const char *p = strstr(cnf, "BOOT");
    const char *s, *e;
    if (!p) return false;
    p = strchr(p, '=');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    s = p;
    if (strncmp(s, "cdrom:", 6) == 0) s += 6;
    while (*s == '\\' || *s == '/') s++;
    e = s;
    while (*e && *e != '\r' && *e != '\n' && *e != ';' && *e != ' ') e++;
    if ((size_t)(e - s) >= n) return false;
    memcpy(out, s, (size_t)(e - s));
    out[e - s] = 0;
    return true;
}

nr_game_status nr_game_identify(nr_disc *d, bool full_crc, nr_game_report *r)
{
    char err[256];
    u8 *cnf = NULL, *exe = NULL;
    u32 cnfsz = 0, exesz = 0;
    u8 dig[20];
    int i;

    memset(r, 0, sizeof(*r));
    r->files_expected = nr_manifest_slus01068_count;
    if (!nr_iso_mount(d, err, sizeof(err))) {
        r->status = NR_GAME_UNREADABLE;
        snprintf(r->message, sizeof(r->message), "Unreadable image: %s", err);
        return r->status;
    }
    if (!nr_iso_read_file(d, "SYSTEM.CNF", &cnf, &cnfsz)) {
        r->status = NR_GAME_NOT_NASCAR;
        snprintf(r->message, sizeof(r->message), "SYSTEM.CNF not found: this does not look like a PlayStation disc.");
        return r->status;
    }
    cnf[cnfsz] = 0;
    if (!parse_boot((const char *)cnf, r->boot_path, sizeof(r->boot_path))) {
        free(cnf);
        r->status = NR_GAME_NOT_NASCAR;
        snprintf(r->message, sizeof(r->message), "SYSTEM.CNF has no valid BOOT line.");
        return r->status;
    }
    free(cnf);
    if (nr_strcasecmp(r->boot_path, NR_EXPECTED_SERIAL) != 0) {
        r->status = NR_GAME_NOT_NASCAR;
        snprintf(r->message, sizeof(r->message),
                 "This disc boots '%s', but the engine expects NASCAR Rumble (USA) - %s.", r->boot_path, NR_EXPECTED_SERIAL);
        return r->status;
    }
    if (!nr_iso_read_file(d, r->boot_path, &exe, &exesz)) {
        r->status = NR_GAME_UNREADABLE;
        snprintf(r->message, sizeof(r->message), "Executable %s is listed but unreadable.", r->boot_path);
        return r->status;
    }
    nr_sha1(exe, exesz, dig);
    nr_sha1_hex(dig, r->exe_sha1);
    free(exe);
    r->exe_sha1_match = strcmp(r->exe_sha1, NR_EXPECTED_EXE_SHA1) == 0;
    if (!r->exe_sha1_match)
        NR_WARN("disc", "Executable SHA-1 (%s) differs from the reference %s (different revision or modified).",
                r->exe_sha1, NR_EXPECTED_EXE_SHA1);

    for (i = 0; i < nr_manifest_slus01068_count; i++) {
        const nr_manifest_entry *m = &nr_manifest_slus01068[i];
        const nr_iso_entry *e = nr_iso_find(d, m->path);
        if (!e) { NR_WARN("disc", "File missing on the disc: %s", m->path); r->files_missing++; continue; }
        if (e->size != m->size) {
            NR_WARN("disc", "Size mismatch: %s (%u, expected %u)", m->path, e->size, m->size);
            r->files_size_mismatch++;
            continue;
        }
        if (full_crc) {
            u8 *buf;
            u32 sz;
            if (!nr_iso_read_file(d, m->path, &buf, &sz)) { r->files_missing++; continue; }
            r->files_checked_crc++;
            if (nr_crc32(0, buf, sz) != m->crc32) {
                NR_WARN("disc", "CRC32 mismatch: %s", m->path);
                r->files_crc_mismatch++;
            }
            free(buf);
        }
    }
    if (r->exe_sha1_match && !r->files_missing && !r->files_size_mismatch && !r->files_crc_mismatch) {
        r->status = NR_GAME_OK;
        snprintf(r->message, sizeof(r->message), "NASCAR Rumble (USA) %s identified. %d/%d files match%s.",
                 NR_EXPECTED_SERIAL, r->files_expected, r->files_expected,
                 full_crc ? " (size + CRC32)" : " (size; use --verify for CRC32)");
    } else {
        r->status = NR_GAME_MODIFIED;
        snprintf(r->message, sizeof(r->message),
                 "NASCAR Rumble identified, with differences: executable %s, %d missing, %d with a different size, %d with a different CRC. "
                 "Behaviour may not match the reference.",
                 r->exe_sha1_match ? "OK" : "DIFFERENT", r->files_missing, r->files_size_mismatch, r->files_crc_mismatch);
    }
    return r->status;
}
