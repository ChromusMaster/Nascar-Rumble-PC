/* Tiny configuration file reader: "key = value" lines, '#' or ';' comments,
 * "[section]" headers are folded into keys as "section.key". */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { char key[96]; char value[1024]; } kv;
struct nr_config { kv *items; int n, cap; };

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

void nr_config_set(nr_config *c, const char *key, const char *value)
{
    int i;
    for (i = 0; i < c->n; i++) {
        if (nr_strcasecmp(c->items[i].key, key) == 0) { nr_strlcpy(c->items[i].value, value, sizeof(c->items[i].value)); return; }
    }
    if (c->n == c->cap) {
        int ncap = c->cap ? c->cap * 2 : 32;
        kv *ni = (kv *)realloc(c->items, sizeof(kv) * (size_t)ncap);
        if (!ni) return;           /* out of memory: keep the old table */
        c->items = ni;
        c->cap = ncap;
    }
    nr_strlcpy(c->items[c->n].key, key, sizeof(c->items[0].key));
    nr_strlcpy(c->items[c->n].value, value, sizeof(c->items[0].value));
    c->n++;
}

nr_config *nr_config_load(const char *path)
{
    nr_config *c = (nr_config *)calloc(1, sizeof(nr_config));
    u8 *txt;
    long n;
    char section[64] = "";
    char *line, *next;
    if (!path) return c;
    n = nr_read_file(path, &txt);
    if (n < 0) return c;
    for (line = (char *)txt; line && *line; line = next) {
        char *eq, *t;
        next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; while (*next == '\r' || *next == '\n') next++; }
        t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) { *e = 0; nr_strlcpy(section, trim(t + 1), sizeof(section)); }
            continue;
        }
        eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        {
            char key[96];
            char *k = trim(t), *v = trim(eq + 1);
            size_t vl = strlen(v);
            if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') { v[vl - 1] = 0; v++; }
            if (section[0]) snprintf(key, sizeof(key), "%s.%s", section, k);
            else nr_strlcpy(key, k, sizeof(key));
            nr_config_set(c, key, v);
        }
    }
    free(txt);
    return c;
}

void nr_config_free(nr_config *c)
{
    if (!c) return;
    free(c->items);
    free(c);
}

const char *nr_config_get(const nr_config *c, const char *key, const char *def)
{
    int i;
    if (!c) return def;
    for (i = 0; i < c->n; i++) if (nr_strcasecmp(c->items[i].key, key) == 0) return c->items[i].value;
    return def;
}

int nr_config_get_int(const nr_config *c, const char *key, int def)
{
    const char *v = nr_config_get(c, key, NULL);
    return v ? (int)strtol(v, NULL, 0) : def;
}

/* Rewrites one "section.key" in a file, keeping comments and layout: the
 * existing line (commented-out "; key =" lines count too) is replaced,
 * otherwise the key is added at the end of its section (created if needed). */
bool nr_config_update_file(const char *path, const char *fullkey, const char *value)
{
    u8 *txt = NULL;
    long n = nr_read_file(path, &txt);
    char section[64], key[96], cur[64] = "";
    const char *dot = strchr(fullkey, '.');
    char *out, *line, *next;
    size_t cap, len = 0;
    long sec_end = -1;           /* offset in out after the last line of the section */
    bool done = false, in_sec = false;
    FILE *f;
#define GROW(need_cap) do { char *g_ = (char *)realloc(out, (need_cap)); if (!g_) { free(out); free(txt); return false; } out = g_; cap = (need_cap); } while (0)
    if (!dot || (size_t)(dot - fullkey) >= sizeof(section)) { free(txt); return false; }
    memcpy(section, fullkey, (size_t)(dot - fullkey));
    section[dot - fullkey] = 0;
    nr_strlcpy(key, dot + 1, sizeof(key));
    if (n < 0) { txt = (u8 *)calloc(1, 1); n = 0; }
    cap = (size_t)n + strlen(fullkey) + strlen(value) + 256;
    out = (char *)malloc(cap);
    if (!out) { free(txt); return false; }
    for (line = (char *)txt; line && *line; line = next) {
        char tmp[1200], *t;
        size_t ll;
        next = strchr(line, '\n');
        ll = next ? (size_t)(next - line) : strlen(line);
        if (next) next++;
        if (ll >= sizeof(tmp)) ll = sizeof(tmp) - 1;
        memcpy(tmp, line, ll);
        tmp[ll] = 0;
        if (ll && tmp[ll - 1] == '\r') tmp[--ll] = 0;
        t = trim(tmp);
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (in_sec && !done) sec_end = (long)len;
            if (e) {
                char nm[64];
                size_t nl = (size_t)(e - (t + 1));
                if (nl >= sizeof(nm)) nl = sizeof(nm) - 1;
                memcpy(nm, t + 1, nl);   /* never modify tmp: it is written back */
                nm[nl] = 0;
                nr_strlcpy(cur, trim(nm), sizeof(cur));
            }
            in_sec = nr_strcasecmp(cur, section) == 0;
        } else if (in_sec && !done) {
            char *k = t, *eq;
            if (*k == ';' || *k == '#') k = trim(k + 1);
            eq = strchr(k, '=');
            if (eq) {
                char kk[96];
                size_t kl = (size_t)(eq - k);
                if (kl < sizeof(kk)) {
                    memcpy(kk, k, kl);
                    kk[kl] = 0;
                    if (nr_strcasecmp(trim(kk), key) == 0) {
                        size_t need = strlen(key) + strlen(value) + 8;
                        if (len + need >= cap) GROW(cap * 2 + need);
                        len += (size_t)snprintf(out + len, cap - len, "%s = %s\n", key, value);
                        done = true;
                        continue;
                    }
                }
            }
        }
        if (len + ll + 2 >= cap) GROW(cap * 2 + ll);
        memcpy(out + len, line, ll);     /* the untouched original line */
        len += ll;
        out[len++] = '\n';
    }
    if (in_sec && !done) sec_end = (long)len;
    if (!done) {
        char add[1400];
        int al;
        if (sec_end >= 0) al = snprintf(add, sizeof(add), "%s = %s\n", key, value);
        else { al = snprintf(add, sizeof(add), "\n[%s]\n%s = %s\n", section, key, value); sec_end = (long)len; }
        if (len + (size_t)al + 1 >= cap) GROW(len + (size_t)al + 16);
        memmove(out + sec_end + al, out + sec_end, len - (size_t)sec_end);
        memcpy(out + sec_end, add, (size_t)al);
        len += (size_t)al;
    }
    f = nr_fopen(path, "wb");
    if (!f) { free(out); free(txt); return false; }
    fwrite(out, 1, len, f);
    fclose(f);
    free(out);
    free(txt);
    return true;
#undef GROW
}
