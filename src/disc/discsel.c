#include "discsel.h"
#include "../common/paths.h"
#include "disc.h"
#include "gameid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_known_names[] = {
    "NASCAR Rumble (USA).cue", "NASCAR Rumble (USA).bin", "NASCAR Rumble.cue", "NASCAR Rumble.bin",
    "SLUS-01068.cue", "SLUS-01068.bin", "SLUS_010.68.cue", "SLUS_010.68.bin",
};

const char *nr_cand_status_text(nr_cand_status s)
{
    switch (s) {
    case NR_CAND_OK: return "NASCAR Rumble (USA)";
    case NR_CAND_OTHER_GAME: return "another game";
    default: return "unreadable";
    }
}

static bool parse_boot(const char *cnf, char *out, size_t n)
{
    const char *p = strstr(cnf, "BOOT"), *s, *e;
    if (!p || !(p = strchr(p, '='))) return false;
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

bool nr_disc_probe(const char *path, nr_disc_candidate *c)
{
    char err[256];
    nr_disc *d;
    u8 *cnf;
    u32 sz;
    memset(c, 0, sizeof(*c));
    nr_strlcpy(c->path, path, sizeof(c->path));
    nr_strlcpy(c->name, nr_path_basename(path), sizeof(c->name));
    c->status = NR_CAND_UNREADABLE;
    d = nr_disc_open(path, err, sizeof(err));
    if (!d) return false;
    if (nr_iso_mount(d, err, sizeof(err))) {
        nr_strlcpy(c->volume, nr_iso_volume_id(d), sizeof(c->volume));
        if (nr_iso_read_file(d, "SYSTEM.CNF", &cnf, &sz)) {
            cnf[sz] = 0;
            if (parse_boot((const char *)cnf, c->boot, sizeof(c->boot)))
                c->status = nr_strcasecmp(c->boot, NR_EXPECTED_SERIAL) == 0 ? NR_CAND_OK : NR_CAND_OTHER_GAME;
            else
                c->status = NR_CAND_OTHER_GAME;
            free(cnf);
        }
    }
    nr_disc_close(d);
    return c->status == NR_CAND_OK;
}

typedef struct {
    char names[256][256];
    int n;
} name_list;

static void collect(void *user, const char *name, bool is_dir)
{
    name_list *l = (name_list *)user;
    if (is_dir || l->n >= 256) return;
    nr_strlcpy(l->names[l->n++], name, sizeof(l->names[0]));
}

static bool has_ext(const char *name, const char *ext)
{
    size_t a = strlen(name), b = strlen(ext);
    return a > b && nr_strcasecmp(name + a - b, ext) == 0;
}

/* a .bin that has a .cue with the same stem is listed through the cue only */
static bool bin_covered_by_cue(const name_list *l, const char *bin)
{
    char stem[256];
    int i;
    size_t n = strlen(bin) - 4;
    if (n >= sizeof(stem)) return false;
    memcpy(stem, bin, n);
    stem[n] = 0;
    for (i = 0; i < l->n; i++) {
        size_t m = strlen(l->names[i]);
        if (m == n + 4 && has_ext(l->names[i], ".cue") && strncmp(l->names[i], stem, n) == 0) return true;
    }
    return false;
}

static int cmp_names(const void *a, const void *b) { return nr_strcasecmp((const char *)a, (const char *)b); }

static void scan_dir(const char *dir, nr_disc_list *out)
{
    name_list *l = (name_list *)calloc(1, sizeof(name_list));
    int i;
    if (!l) return;
    if (nr_list_dir(dir, collect, l) < 0) { free(l); return; }
    qsort(l->names, (size_t)l->n, sizeof(l->names[0]), cmp_names);
    for (i = 0; i < l->n && out->n < NR_MAX_CANDIDATES; i++) {
        const char *nm = l->names[i];
        char full[1024];
        int k;
        bool dup = false;
        if (!(has_ext(nm, ".cue") || has_ext(nm, ".bin") || has_ext(nm, ".iso") || has_ext(nm, ".img"))) continue;
        if (has_ext(nm, ".bin") && bin_covered_by_cue(l, nm)) continue;
        nr_path_join(full, sizeof(full), dir, nm);
        for (k = 0; k < out->n; k++) if (strcmp(out->c[k].path, full) == 0 || strcmp(out->c[k].name, nm) == 0) dup = true;
        if (dup) continue;
        nr_disc_probe(full, &out->c[out->n]);
        out->n++;
    }
    free(l);
}

void nr_disc_scan(const char *gamedata_cfg, nr_disc_list *l)
{
    char exe[1024], d[1024];
    memset(l, 0, sizeof(*l));
    if (gamedata_cfg && *gamedata_cfg) {
        nr_strlcpy(l->dirs[l->ndirs++], gamedata_cfg, sizeof(l->dirs[0]));
    } else {
        /* data dir (user files), executable folder, current directory */
        int k;
        nr_path_join(d, sizeof(d), nr_data_dir(), "gamedata");
        nr_strlcpy(l->dirs[l->ndirs++], d, sizeof(l->dirs[0]));
        nr_path_join(exe, sizeof(exe), nr_exe_folder(), "gamedata");
        if (strcmp(exe, d) != 0) nr_strlcpy(l->dirs[l->ndirs++], exe, sizeof(l->dirs[0]));
        for (k = 0; k < l->ndirs; k++) if (strcmp(l->dirs[k], "gamedata") == 0) break;
        if (k == l->ndirs && l->ndirs < NR_MAX_GAMEDATA_DIRS) nr_strlcpy(l->dirs[l->ndirs++], "gamedata", sizeof(l->dirs[0]));
    }
    {
        int k;
        for (k = 0; k < l->ndirs; k++) scan_dir(l->dirs[k], l);
    }
}

bool nr_disc_resolve(const char *cli, const char *cfg_disc, const char *cfg_image, const nr_disc_list *l,
                     char *path, size_t n, char *how, size_t hn)
{
    int i, k, d;
    path[0] = 0;
    if (cli && *cli) { nr_strlcpy(path, cli, n); snprintf(how, hn, "command line"); return true; }
    if (cfg_disc && *cfg_disc) { nr_strlcpy(path, cfg_disc, n); snprintf(how, hn, "nrengine.ini (game.disc)"); return true; }
    if (cfg_image && *cfg_image) {
        for (d = 0; d < l->ndirs; d++) {
            char full[1024];
            nr_path_join(full, sizeof(full), l->dirs[d], cfg_image);
            if (nr_file_exists(full)) {
                nr_strlcpy(path, full, n);
                snprintf(how, hn, "nrengine.ini (game.disc_image) in %s", l->dirs[d]);
                return true;
            }
        }
        NR_WARN("disc", "game.disc_image = %s not found in gamedata/; using automatic detection", cfg_image);
    }
    for (k = 0; k < (int)NR_ARRAY_COUNT(k_known_names); k++)
        for (i = 0; i < l->n; i++)
            if (l->c[i].status == NR_CAND_OK && nr_strcasecmp(l->c[i].name, k_known_names[k]) == 0) {
                nr_strlcpy(path, l->c[i].path, n);
                snprintf(how, hn, "standard file name in gamedata/");
                return true;
            }
    for (i = 0; i < l->n; i++)
        if (l->c[i].status == NR_CAND_OK) {
            nr_strlcpy(path, l->c[i].path, n);
            snprintf(how, hn, "automatic detection by boot serial (%s)", l->c[i].boot);
            return true;
        }
    return false;
}
