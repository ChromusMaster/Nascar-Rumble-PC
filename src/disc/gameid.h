/* Identification and validation of the user's NASCAR Rumble disc. */
#ifndef NR_GAMEID_H
#define NR_GAMEID_H

#include "disc.h"

typedef struct {
    const char *path;
    u32 size;
    u32 crc32;
} nr_manifest_entry;

extern const nr_manifest_entry nr_manifest_slus01068[];
extern const int nr_manifest_slus01068_count;

#define NR_EXPECTED_SERIAL   "SLUS_010.68"
#define NR_EXPECTED_EXE_SHA1 "2726ac3e6f3d756f78982f7c66e507fc3aaf7aff"

typedef enum {
    NR_GAME_OK = 0,          /* all checks passed */
    NR_GAME_MODIFIED,        /* identified, but some files differ (hacked/other revision) */
    NR_GAME_NOT_NASCAR,      /* a PlayStation disc, but not SLUS-01068 */
    NR_GAME_UNREADABLE       /* not a usable image */
} nr_game_status;

typedef struct {
    nr_game_status status;
    char boot_path[64];      /* from SYSTEM.CNF */
    char exe_sha1[41];
    bool exe_sha1_match;
    int files_expected;
    int files_missing;
    int files_size_mismatch;
    int files_crc_mismatch;  /* only when full verification requested */
    int files_checked_crc;
    char message[512];       /* human readable summary (pt-BR) */
} nr_game_report;

/* Mounts ISO (if needed) and validates. full_crc = verify every file's CRC32
 * (reads ~250 MB). Problems are logged individually. */
nr_game_status nr_game_identify(nr_disc *d, bool full_crc, nr_game_report *rep);

#endif
