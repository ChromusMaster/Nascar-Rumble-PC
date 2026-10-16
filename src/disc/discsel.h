/* Locating the user's disc image, modelled on the Silent Hill PC port:
 *
 *   1. a path given on the command line;
 *   2. "game.disc" in nrengine.ini (full path);
 *   3. "game.disc_image" in nrengine.ini: a file name inside gamedata/;
 *   4. the expected names ("NASCAR Rumble (USA).cue" / ".bin") in gamedata/;
 *   5. any .cue/.bin/.iso/.img in gamedata/ whose SYSTEM.CNF boots
 *      SLUS_010.68 (identified by content: file names do not matter).
 *
 * gamedata/ is searched in the data folder (see common/paths.h; next to the
 * executable for portable installs), next to the executable and in the
 * current directory - or only where "game.gamedata" points. Nothing is
 * copied or extracted: the image is read in place. */
#ifndef NR_DISCSEL_H
#define NR_DISCSEL_H

#include "../common/common.h"

#define NR_MAX_CANDIDATES 32
#define NR_MAX_GAMEDATA_DIRS 3

typedef enum { NR_CAND_OK = 0, NR_CAND_OTHER_GAME, NR_CAND_UNREADABLE } nr_cand_status;

typedef struct {
    char path[1024];
    char name[256];
    char boot[64];          /* boot executable from SYSTEM.CNF */
    char volume[33];
    nr_cand_status status;
} nr_disc_candidate;

typedef struct {
    nr_disc_candidate c[NR_MAX_CANDIDATES];
    int n;
    char dirs[NR_MAX_GAMEDATA_DIRS][1024];   /* searched gamedata directories */
    int ndirs;
} nr_disc_list;

/* Quick content probe (mounts ISO9660 and reads SYSTEM.CNF only). */
bool nr_disc_probe(const char *path, nr_disc_candidate *out);
/* Scans gamedata directories. gamedata_cfg may be empty. */
void nr_disc_scan(const char *gamedata_cfg, nr_disc_list *l);
/* Applies the priority rules above. Returns true and fills path when a disc
 * is chosen; how it was chosen is written to how (for the log). */
bool nr_disc_resolve(const char *cli_path, const char *cfg_disc, const char *cfg_disc_image,
                     const nr_disc_list *l, char *path, size_t n, char *how, size_t hn);
const char *nr_cand_status_text(nr_cand_status s);

#endif
