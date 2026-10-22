/* Tiny configuration file reader: "key = value" lines, '#' or ';' comments,
 * "[section]" headers are folded into keys as "section.key". */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#define CONFIG_LIMIT (16u * 1024u * 1024u)
#define CONFIG_KEY_LIMIT 4096u
#define CONFIG_VALUE_LIMIT (1024u * 1024u)

typedef struct { char *key; char *value; } kv;
struct nr_config { kv *items; size_t n, cap; };
typedef struct { char *data; size_t len, cap; } config_buffer;

static char *trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static bool append(config_buffer *b, const void *src, size_t n)
{
    size_t need, cap;
    char *p;
    if (b->len > CONFIG_LIMIT || n > CONFIG_LIMIT - b->len) return false;
    need = b->len + n + 1;
    if (need > b->cap) {
        cap = b->cap ? b->cap : 256;
        while (cap < need) {
            if (cap > (CONFIG_LIMIT + 1) / 2) { cap = CONFIG_LIMIT + 1; break; }
            cap *= 2;
        }
        p = (char *)realloc(b->data, cap);
        if (!p) return false;
        b->data = p;
        b->cap = cap;
    }
    if (n) memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = 0;
    return true;
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static bool config_set(nr_config *c, const char *key, const char *value)
{
    size_t i;
    char *k, *v;
    if (!c || !key || !value || strlen(key) > CONFIG_KEY_LIMIT || strlen(value) > CONFIG_VALUE_LIMIT) return false;
    v = copy_string(value);
    if (!v) return false;
    for (i = 0; i < c->n; i++) {
        if (nr_strcasecmp(c->items[i].key, key) == 0) {
            free(c->items[i].value);
            c->items[i].value = v;
            return true;
        }
    }
    k = copy_string(key);
    if (!k) { free(v); return false; }
    if (c->n == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 32;
        kv *items;
        if (c->cap > 65536 || cap > SIZE_MAX / sizeof(kv)) { free(k); free(v); return false; }
        items = (kv *)realloc(c->items, cap * sizeof(kv));
        if (!items) { free(k); free(v); return false; }
        c->items = items;
        c->cap = cap;
    }
    c->items[c->n].key = k;
    c->items[c->n++].value = v;
    return true;
}

void nr_config_set(nr_config *c, const char *key, const char *value)
{
    if (!config_set(c, key, value)) NR_ERR("config", "setting rejected: invalid size or allocation failure");
}

nr_config *nr_config_load(const char *path)
{
    nr_config *c = (nr_config *)calloc(1, sizeof(*c));
    u8 *txt = NULL;
    char *line, *next, *section = "";
    long n;
    if (!c || !path) return c;
    n = nr_read_file_limited(path, &txt, CONFIG_LIMIT);
    if (n < 0) return c;
    if (memchr(txt, 0, (size_t)n)) { NR_ERR("config", "embedded NUL rejected"); free(txt); return c; }
    line = (char *)txt;
    if (n >= 3 && memcmp(txt, "\xEF\xBB\xBF", 3) == 0) line += 3;
    for (; line && *line; line = next) {
        char *eq, *t;
        next = strpbrk(line, "\r\n");
        if (next) { *next++ = 0; while (*next == '\r' || *next == '\n') next++; }
        t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) { *e = 0; section = trim(t + 1); }
            continue;
        }
        eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        {
            config_buffer key = {0};
            char *k = trim(t), *v = trim(eq + 1);
            size_t vl = strlen(v);
            bool ok = true;
            if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') { v[vl - 1] = 0; v++; }
            if (*section) ok = append(&key, section, strlen(section)) && append(&key, ".", 1);
            ok = ok && append(&key, k, strlen(k)) && config_set(c, key.data, v);
            free(key.data);
            if (!ok) { free(txt); nr_config_free(c); return NULL; }
        }
    }
    free(txt);
    return c;
}

void nr_config_free(nr_config *c)
{
    size_t i;
    if (!c) return;
    for (i = 0; i < c->n; i++) { free(c->items[i].key); free(c->items[i].value); }
    free(c->items);
    free(c);
}

const char *nr_config_get(const nr_config *c, const char *key, const char *def)
{
    size_t i;
    if (!c || !key) return def;
    for (i = 0; i < c->n; i++) if (nr_strcasecmp(c->items[i].key, key) == 0) return c->items[i].value;
    return def;
}

int nr_config_get_int(const nr_config *c, const char *key, int def)
{
    const char *v = nr_config_get(c, key, NULL);
    char *end;
    long n;
    if (!v) return def;
    errno = 0;
    n = strtol(v, &end, 0);
    if (errno || end == v || n < INT_MIN || n > INT_MAX) return def;
    while (isspace((unsigned char)*end)) end++;
    if (*end && *end != ';' && *end != '#') return def;
    return (int)n;
}

static bool valid_name(const char *s)
{
    size_t n = strlen(s);
    return n && !isspace((unsigned char)s[0]) && !isspace((unsigned char)s[n-1]) && !strpbrk(s, "\r\n[]=;#");
}

bool nr_config_update_file(const char *path, const char *fullkey, const char *value)
{
    u8 *txt = NULL;
    char *section = NULL, *scratch = NULL;
    const char *dot, *key, *nl = "\n";
    config_buffer out = {0}, add = {0};
    size_t n, pos, insert = 0, nllen = 1;
    bool in_sec = false, seen = false, done = false, ok = false;
    long count;
    if (!path || !fullkey || !value || strlen(fullkey) > CONFIG_KEY_LIMIT || strlen(value) > CONFIG_VALUE_LIMIT || strpbrk(value, "\r\n")) goto cleanup;
    dot = strchr(fullkey, '.');
    if (!dot) goto cleanup;
    section = copy_string(fullkey);
    if (!section) goto cleanup;
    section[dot-fullkey] = 0;
    key = dot + 1;
    if (!valid_name(section) || !valid_name(key)) goto cleanup;
    errno = 0;
    count = nr_read_file_limited(path, &txt, CONFIG_LIMIT);
    if (count < 0 && errno != ENOENT) goto cleanup;
    n = count < 0 ? 0 : (size_t)count;
    if (n && memchr(txt, 0, n)) goto cleanup;
    scratch = (char *)malloc(n + 1);
    if (!scratch) goto cleanup;
    if (n) {
        const char *lf = (const char *)memchr(txt, '\n', n);
        if (lf && lf > (char *)txt && lf[-1] == '\r') { nl = "\r\n"; nllen = 2; }
    }
    pos = n >= 3 && memcmp(txt, "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
    if (pos && !append(&out, txt, pos)) goto cleanup;
    while (pos < n) {
        size_t start = pos, end, content;
        char *t, *eq;
        bool replace = false;
        while (pos < n && txt[pos] != '\n') pos++;
        end = pos < n ? ++pos : pos;
        content = end-start;
        while (content && (txt[start+content-1] == '\n' || txt[start+content-1] == '\r')) content--;
        memcpy(scratch, txt+start, content);
        scratch[content] = 0;
        t = trim(scratch);
        if (*t == '[') {
            char *e = strchr(t, ']');
            in_sec = false;
            if (e) { *e = 0; in_sec = nr_strcasecmp(trim(t+1), section) == 0; }
            if (in_sec) seen = true;
        } else if (in_sec) {
            char *k = t;
            if (*k == ';' || *k == '#') k = trim(k+1);
            eq = strchr(k, '=');
            if (eq) { *eq = 0; replace = nr_strcasecmp(trim(k), key) == 0; }
        }
        if (replace) {
            if (!append(&out, key, strlen(key)) || !append(&out, " = ", 3) || !append(&out, value, strlen(value)) || !append(&out, txt+start+content, end-start-content)) goto cleanup;
            done = true;
        } else if (!append(&out, txt+start, end-start)) goto cleanup;
        if (in_sec) insert = out.len;
    }
    if (!done) {
        if (!seen) insert = out.len;
        if (insert && out.data[insert-1] != '\n' && !append(&add, nl, nllen)) goto cleanup;
        if (!seen && (!append(&add, "[", 1) || !append(&add, section, strlen(section)) || !append(&add, "]", 1) || !append(&add, nl, nllen))) goto cleanup;
        if (!append(&add, key, strlen(key)) || !append(&add, " = ", 3) || !append(&add, value, strlen(value)) || !append(&add, nl, nllen)) goto cleanup;
        {
            size_t old = out.len;
            if (!append(&out, add.data, add.len)) goto cleanup;
            memmove(out.data+insert+add.len, out.data+insert, old-insert);
            memcpy(out.data+insert, add.data, add.len);
        }
    }
    if (out.len == n && (!n || memcmp(out.data, txt, n) == 0)) ok = true;
    else ok = nr_write_file_atomic(path, out.data, out.len);
cleanup:
    free(txt); free(section); free(scratch); free(out.data); free(add.data);
    if (!ok) NR_ERR("config", "configuration update failed; original file preserved");
    return ok;
}
