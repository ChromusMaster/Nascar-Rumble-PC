/* User mods: translated texts and replacement files (see mods.h, docs/MODS.md).
 *
 * Texts. NASCAR Rumble keeps its texts in two places:
 *  - front-end menus: a "TxtR" resource of CW/FEND/FE.TRK, a list of
 *    "<number> <text>" strings. The game looks a number up with one routine
 *    (80090984, "GetText"). The engine replaces that routine natively: for
 *    numbers present in the translation it returns a pointer to the
 *    translated string, otherwise the original routine runs. The translated
 *    strings live in kernel RAM 0x6000-0xBFFF, which belongs to the (HLE)
 *    BIOS and is never used by the game.
 *  - messages inside the executable (pause menu, warnings...): replaced in
 *    the RAM copy of the executable when the new text fits in the space the
 *    original string occupies (longer texts are cut and reported).
 * Accented letters are converted to plain ASCII unless the mod asks to keep
 * them, because the game's fonts only contain ASCII glyphs.
 *
 * Files: every file under <mod>/files/ replaces the disc file with the same
 * path (case-insensitive) through nr_disc_overlay_file(). */
#include "mods.h"
#include "../common/paths.h"
#include "../disc/gameid.h"
#include "../formats/formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define K_MODSTR_BASE 0x6000u
#define K_MODSTR_END 0xC000u
#define GETTEXT_ADDR 0x80090984u

typedef struct { int id; char *text; u32 addr; } fe_entry;
typedef struct { char *orig; char *repl; } exe_entry;

struct nr_mods {
    fe_entry *fe;
    int nfe;
    exe_entry *ex;
    int nex;
    bool keep_accents;
    int files;
    char names[256];
    psx *p;
};

static nr_mods *g_mods;   /* one instance: the native GetText reads it */

/* ------------------------------------------------------------ text helpers */

static const char *k_translit[64] = {
    /* U+00C0 .. U+00FF */
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
};

/* UTF-8 -> game bytes. Escapes: \xNN (raw byte), \n, \\. */
static char *encode(const char *s, bool keep_accents)
{
    size_t len = strlen(s), o = 0;
    char *out;
    const unsigned char *p = (const unsigned char *)s, *end = p + len;
    if (len > 1024u * 1024u) return NULL;
    out = (char *)malloc(len * 2 + 1);
    if (!out) return NULL;
    while (p < end) {
        unsigned cp;
        if (*p == '\\') {
            if (end-p < 2) goto invalid;
            if (p[1] == 'n') { out[o++] = '\n'; p += 2; continue; }
            if (p[1] == '\\') { out[o++] = '\\'; p += 2; continue; }
            if (p[1] == 'x' && end-p >= 4) {
                int a, b;
                a = p[2] >= '0' && p[2] <= '9' ? p[2]-'0' : p[2] >= 'A' && p[2] <= 'F' ? p[2]-'A'+10 : p[2] >= 'a' && p[2] <= 'f' ? p[2]-'a'+10 : -1;
                b = p[3] >= '0' && p[3] <= '9' ? p[3]-'0' : p[3] >= 'A' && p[3] <= 'F' ? p[3]-'A'+10 : p[3] >= 'a' && p[3] <= 'f' ? p[3]-'a'+10 : -1;
                if (a < 0 || b < 0 || !(a || b)) goto invalid;
                out[o++] = (char)(a * 16 + b); p += 4; continue;
            }
            goto invalid;
        }
        if (*p < 0x80) { out[o++] = (char)*p++; continue; }
        {
            int count, i;
            unsigned min;
            if (*p >= 0xC2 && *p <= 0xDF) { count = 2; min = 0x80; cp = *p & 31; }
            else if (*p >= 0xE0 && *p <= 0xEF) { count = 3; min = 0x800; cp = *p & 15; }
            else if (*p >= 0xF0 && *p <= 0xF4) { count = 4; min = 0x10000; cp = *p & 7; }
            else goto invalid;
            if (end-p < count) goto invalid;
            for (i = 1; i < count; i++) { if ((p[i] & 0xC0) != 0x80) goto invalid; cp = (cp << 6) | (p[i] & 63); }
            if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto invalid;
            p += count;
        }
        if (cp >= 0xC0 && cp <= 0xFF && !keep_accents) {
            const char *t = k_translit[cp - 0xC0];
            while (*t) out[o++] = *t++;
        } else if (cp < 256) out[o++] = keep_accents ? (char)cp : '?';
        else { NR_WARN("mods", "unsupported game character U+%04X; entry rejected", cp); goto invalid; }
    }
    out[o] = 0;
    return out;
invalid:
    free(out);
    return NULL;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* "text" -> text (quotes optional) */
static char *unquote(char *s)
{
    size_t l = strlen(s);
    if (l >= 2 && s[0] == '"' && s[l - 1] == '"') { s[l - 1] = 0; return s + 1; }
    return s;
}

static int cmp_fe(const void *a, const void *b) { int x = ((const fe_entry *)a)->id, y = ((const fe_entry *)b)->id; return (x > y) - (x < y); }

static void load_text(nr_mods *m, const char *path)
{
    u8 *txt;
    long n = nr_read_file_limited(path, &txt, 1024u * 1024u);
    char *line, *next, section[16] = "";
    int lineno = 0;
    if (n < 0) return;
    if (memchr(txt, 0, (size_t)n)) { NR_WARN("mods", "catalog contains NUL; rejected"); free(txt); return; }
    for (line = (char *)txt; line && *line; line = next) {
        char *t, *eq;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        lineno++;
        t = trim(line);
        if (lineno == 1 && strlen(t) >= 3 && memcmp(t, "\xEF\xBB\xBF", 3) == 0) t += 3; /* BOM */
        if (!*t || *t == '#' || *t == ';') continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) { *e = 0; nr_strlcpy(section, t + 1, sizeof(section)); }
            continue;
        }
        if (strcmp(section, "options") == 0) {
            eq = strchr(t, '=');
            if (eq) {
                *eq = 0;
                if (strcmp(trim(t), "accents") == 0) m->keep_accents = strcmp(trim(eq + 1), "keep") == 0;
            }
            continue;
        }
        if (strcmp(section, "fe") == 0) {
            /* 100 = texto */
            int id;
            eq = strchr(t, '=');
            if (!eq) { NR_WARN("mods", "%s:%d: line without '='", path, lineno); continue; }
            *eq = 0;
            {
                char *end;
                long parsed;
                errno = 0;
                parsed = strtol(trim(t), &end, 10);
                if (errno || *end || end == trim(t) || parsed < 0 || parsed > INT_MAX) { NR_WARN("mods", "invalid text ID at line %d", lineno); continue; }
                id = (int)parsed;
            }
            {
                char *val = unquote(trim(eq + 1));
                char *encoded = encode(val, m->keep_accents);
                void *pp;
                int j;
                if (!encoded) { NR_WARN("mods", "invalid text at line %d", lineno); continue; }
                for (j = 0; j < m->nfe; j++) if (m->fe[j].id == id) break;
                if (j < m->nfe) { free(encoded); NR_WARN("mods", "duplicate text ID %d ignored", id); continue; }
                if (m->nfe >= 4096) { free(encoded); NR_WARN("mods", "text entry limit reached"); break; }
                pp = realloc(m->fe, sizeof(fe_entry) * (size_t)(m->nfe + 1));
                if (!pp) { free(encoded); break; }
                m->fe = (fe_entry *)pp;
                m->fe[m->nfe].id = id;
                m->fe[m->nfe].text = encoded;
                m->fe[m->nfe].addr = 0;
                m->nfe++;
            }
        } else if (strcmp(section, "exe") == 0) {
            /* "original" = "translation" */
            char *a = t, *b;
            if (*a != '"') { NR_WARN("mods", "%s:%d: use \"original\" = \"translation\"", path, lineno); continue; }
            b = strstr(a + 1, "\" = \"");
            if (!b) b = strstr(a + 1, "\"=\"");
            if (!b) { NR_WARN("mods", "%s:%d: invalid format", path, lineno); continue; }
            *b = 0;
            b += (b[1] == ' ') ? 5 : 3;
            {
                size_t bl = strlen(b);
                void *pp;
                char *orig, *repl;
                if (bl && b[bl - 1] == '"') b[bl - 1] = 0;
                orig = encode(a + 1, true);
                repl = encode(b, m->keep_accents);
                if (!orig || !repl || !*orig || m->nex >= 4096) { free(orig); free(repl); NR_WARN("mods", "invalid message at line %d", lineno); continue; }
                pp = realloc(m->ex, sizeof(exe_entry) * (size_t)(m->nex + 1));
                if (!pp) { free(orig); free(repl); break; }
                m->ex = (exe_entry *)pp;
                m->ex[m->nex].orig = orig;
                m->ex[m->nex].repl = repl;
                m->nex++;
            }
        }
    }
    free(txt);
}

/* ------------------------------------------------------------ files */

typedef struct {
    nr_mods *m;
    nr_disc *disc;
    char base[1024];
} walk_ctx;

static void walk_files(walk_ctx *w, const char *rel);

typedef struct { walk_ctx *w; const char *rel; } dir_ctx;

static void file_cb(void *user, const char *name, bool is_dir)
{
    dir_ctx *dc = (dir_ctx *)user;
    char rel[512];
    if (dc->rel[0]) snprintf(rel, sizeof(rel), "%s/%s", dc->rel, name);
    else nr_strlcpy(rel, name, sizeof(rel));
    if (strlen(dc->rel) + strlen(name) + 2 >= sizeof(rel) || strlen(rel) > 384 || !nr_safe_relative_path(dc->w->base, rel)) { NR_WARN("mods", "unsafe overlay path rejected"); return; }
    if (is_dir) { walk_files(dc->w, rel); return; }
    {
        char full[1600], err[256];
        u8 *data;
        long n;
        nr_path_join(full, sizeof(full), dc->w->base, rel);
        if (nr_strcasecmp(rel, NR_EXPECTED_SERIAL) == 0 || nr_strcasecmp(rel, "SYSTEM.CNF") == 0) {
            NR_WARN("mods", "%s ignored: the executable and SYSTEM.CNF cannot be replaced", rel);
            return;
        }
        n = nr_read_file_limited(full, &data, 64u * 1024u * 1024u);
        if (n < 0) { NR_WARN("mods", "could not read %s", full); return; }
        if (nr_disc_overlay_file(dc->w->disc, rel, data, (u32)n, err, sizeof(err))) {
            dc->w->m->files++;
            NR_INFO("mods", "file replaced: %s (%ld bytes)", rel, n);
        } else {
            NR_WARN("mods", "%s: %s", rel, err);
        }
        free(data);
    }
}

static void walk_files(walk_ctx *w, const char *rel)
{
    char dir[1600];
    dir_ctx dc;
    dc.w = w;
    dc.rel = rel;
    if (rel[0]) nr_path_join(dir, sizeof(dir), w->base, rel);
    else nr_strlcpy(dir, w->base, sizeof(dir));
    nr_list_dir(dir, file_cb, &dc);
}

/* mods/ is looked up in the current directory, the data folder (user
 * files), next to the executable and in the shared data folder. */
static bool find_mod_dir(const char *rel, char *out, size_t n)
{
    const char *bases[4];
    int i;
    char cand[1600], m[1100];
    bases[0] = ".";
    bases[1] = nr_data_dir();
    bases[2] = nr_exe_folder();
    bases[3] = nr_share_dir();
    for (i = 0; i < 4; i++) {
        nr_path_join(m, sizeof(m), bases[i], "mods");
        if (!nr_safe_relative_path(bases[i], "mods") || !nr_safe_relative_path(m, rel)) continue;
        nr_path_join(cand, sizeof(cand), m, rel);
        if (nr_is_dir(cand)) { nr_strlcpy(out, cand, n); return true; }
    }
    return false;
}

static void load_mod(nr_mods *m, const char *rel, nr_disc *disc)
{
    char dir[1024], p[1600];
    walk_ctx w;
    if (!find_mod_dir(rel, dir, sizeof(dir))) { NR_WARN("mods", "mod not found: mods/%s", rel); return; }
    NR_INFO("mods", "loading mods/%s", rel);
    if (m->names[0]) strncat(m->names, ", ", sizeof(m->names) - strlen(m->names) - 1);
    strncat(m->names, rel, sizeof(m->names) - strlen(m->names) - 1);
    nr_path_join(p, sizeof(p), dir, "text.txt");
    if (nr_safe_relative_path(dir, "text.txt")) load_text(m, p);
    nr_path_join(w.base, sizeof(w.base), dir, "files");
    w.m = m;
    w.disc = disc;
    if (nr_safe_relative_path(dir, "files") && nr_is_dir(w.base)) walk_files(&w, "");
}

nr_mods *nr_mods_open(const char *language, const char *extra, nr_disc *disc)
{
    nr_mods *m;
    if ((!language || !*language) && (!extra || !*extra)) return NULL;
    m = (nr_mods *)calloc(1, sizeof(nr_mods));
    if (!m) return NULL;
    if (language && *language) {
        char rel[256];
        if (strlen(language) >= sizeof(rel) - 10) { free(m); return NULL; }
        snprintf(rel, sizeof(rel), "Language/%s", language);
        load_mod(m, rel, disc);
    }
    if (extra && *extra) {
        char list[1024], *t, *save;
        nr_strlcpy(list, extra, sizeof(list));
        for (t = list; t && *t; t = save) {
            save = strchr(t, ',');
            if (save) *save++ = 0;
            t = trim(t);
            if (*t) load_mod(m, t, disc);
        }
    }
    if (m->nfe) qsort(m->fe, (size_t)m->nfe, sizeof(fe_entry), cmp_fe);
    NR_INFO("mods", "active mods: %s (%d menu texts, %d messages, %d files)", m->names, m->nfe, m->nex, m->files);
    return m;
}

/* ------------------------------------------------------------ attach */

static bool nat_gettext(psx *p, u64 *cycles)
{
    nr_mods *m = g_mods;
    int lo = 0, hi, id = (int)(s32)p->cpu.r[4];
    if (!m) return false;
    hi = m->nfe - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (m->fe[mid].id == id) {
            if (!m->fe[mid].addr) return false;
            p->cpu.r[2] = m->fe[mid].addr;
            *cycles = 200;   /* the original scans the table: a few hundred cycles to a few thousand */
            return true;
        }
        if (m->fe[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    return false;  /* not translated: the original routine runs */
}

static void patch_exe_strings(nr_mods *m, psx *p)
{
    u8 *ram = psx_ram_ptr(p, 0x80010000u);
    u32 len = 0xA0000, i, k;
    int done = 0;
    if (!ram) return;
    for (i = 0; i < (u32)m->nex; i++) {
        const char *o = m->ex[i].orig, *r = m->ex[i].repl;
        size_t ol = strlen(o), rl = strlen(r);
        bool found = false;
        if (!ol) continue;
        for (k = 1; k + ol < len; k++) {
            u32 cap;
            if (ram[k - 1] != 0 || memcmp(ram + k, o, ol) != 0 || ram[k + ol] != 0) continue;
            /* space up to the next string (the terminating zero must stay) */
            cap = (u32)ol;

            if (rl > cap) { NR_WARN("mods", "translation exceeds original message capacity (%u bytes); skipped", cap); found = true; continue; }
            memset(ram + k, 0, cap + 1);
            memcpy(ram + k, r, rl);
            found = true;
            done++;
            k += cap;
        }
        if (!found) NR_WARN("mods", "original message not found in the executable: \"%s\"", o);
    }
    if (m->nex) NR_INFO("mods", "%d executable messages translated", done);
}

void nr_mods_attach(nr_mods *m, psx *p)
{
    static const u32 sig[4] = { 0x8F83086Cu, 0x00805821u, 0x946201E8u, 0x00000000u };
    u32 at = K_MODSTR_BASE;
    int i, k;
    bool ok = true;
    if (!m) return;
    m->p = p;
    g_mods = m;
    patch_exe_strings(m, p);
    if (!m->nfe) return;
    for (k = 0; k < 4; k++) if (psx_ram_r32(p, GETTEXT_ADDR + (u32)k * 4) != sig[k]) ok = false;
    if (!ok) { NR_WARN("mods", "the game text routine does not match; menu texts not translated"); return; }
    for (i = 0; i < m->nfe; i++) {
        size_t l = strlen(m->fe[i].text) + 1;
        if (at + l > K_MODSTR_END) { NR_WARN("mods", "menu texts exceed 24 KB; the rest stay in the original language"); break; }
        memcpy(psx_ram_ptr(p, 0x80000000u + at), m->fe[i].text, l);
        m->fe[i].addr = 0x80000000u + at;
        at += (u32)l;
    }
    psx_override_add(p, GETTEXT_ADDR, nat_gettext, "GetText (translation)", false);
    NR_INFO("mods", "%d menu texts in memory (%u bytes)", i, at - K_MODSTR_BASE);
}

void nr_mods_frame(nr_mods *m, psx *p) { (void)m; (void)p; }

void nr_mods_close(nr_mods *m)
{
    int i;
    if (!m) return;
    if (g_mods == m) g_mods = NULL;
    for (i = 0; i < m->nfe; i++) free(m->fe[i].text);
    for (i = 0; i < m->nex; i++) { free(m->ex[i].orig); free(m->ex[i].repl); }
    free(m->fe);
    free(m->ex);
    free(m);
}

/* ------------------------------------------------------------ export */

static void put_escaped(FILE *f, const u8 *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (s[i] == '\\') fputs("\\\\", f);
        else if (s[i] == '"') fputs("\\x22", f);
        else if (s[i] < 0x20 || s[i] >= 0x7F) fprintf(f, "\\x%02X", s[i]);
        else fputc(s[i], f);
    }
}

bool nr_mods_export_text(nr_disc *d, const char *out_path, char *err, size_t errlen)
{
    u8 *trk = NULL, *exe = NULL, *tab;
    u32 trk_sz = 0, exe_sz = 0, tab_sz = 0, i;
    FILE *f;
    if (!nr_iso_read_file(d, "CW/FEND/FE.TRK", &trk, &trk_sz)) { snprintf(err, errlen, "CW/FEND/FE.TRK not found"); return false; }
    tab = nr_ctrl_extract(trk, trk_sz, "TxtR", 0, &tab_sz);
    free(trk);
    if (!tab) { snprintf(err, errlen, "text resource (TxtR) not found in FE.TRK"); return false; }
    f = nr_fopen(out_path, "wb");
    if (!f) { free(tab); snprintf(err, errlen, "could not create %s", out_path); return false; }
    fprintf(f, "# NASCAR Rumble translation template, created from YOUR copy of the game.\n"
               "# Do not distribute this file with the original texts; distribute only the\n"
               "# translated lines. See docs/MODS.md.\n"
               "#\n"
               "# [fe]  menu texts: number = text (delete the lines you do not translate)\n"
               "# [exe] executable messages: \"original\" = \"translation\" (the translation cannot\n"
               "#       be longer than the space of the original; the excess is cut)\n"
               "# Codes such as [+11] and \\x13 control formatting/colours: keep them.\n\n"
               "[options]\n# accents = ascii   (default: accented letters become ASCII; the game font has no accents)\n# accents = keep\n\n[fe]\n");
    for (i = 0; i < tab_sz;) {
        u32 s = i;
        while (i < tab_sz && tab[i]) i++;
        if (i > s) {
            const u8 *e = tab + s, *sp = memchr(e, ' ', i - s);
            if (sp) {
                fprintf(f, "%.*s = ", (int)(sp - e), (const char *)e);
                put_escaped(f, sp + 1, (size_t)(tab + i - sp - 1));
                fputc('\n', f);
            }
        }
        i++;
    }
    free(tab);
    if (nr_iso_read_file(d, NR_EXPECTED_SERIAL, &exe, &exe_sz)) {
        /* readable messages of the executable (letters, at least one space) */
        fprintf(f, "\n[exe]\n# Remove the '# ' from the lines you translate.\n");
        for (i = 0x800; i < exe_sz; i++) {
            u32 s = i, letters = 0, spaces = 0, bad = 0;
            if (exe[i - 1] != 0) continue;
            while (i < exe_sz && exe[i]) {
                u8 c = exe[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letters++;
                else if (c == ' ') spaces++;
                else if (c < 0x10 || c >= 0x7F) bad++;
                i++;
            }
            if (i - s >= 6 && letters >= 4 && spaces >= 1 && !bad && (s & 3) == 0) {
                fputs("# \"", f);
                put_escaped(f, exe + s, i - s);
                fputs("\" = \"", f);
                put_escaped(f, exe + s, i - s);
                fputs("\"\n", f);
            }
        }
        free(exe);
    }
    fclose(f);
    return true;
}
