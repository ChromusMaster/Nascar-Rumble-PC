#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

static nr_log_level g_level = NR_LOG_INFO;
static FILE *g_logfile = NULL;

void nr_log_set_level(nr_log_level lvl) { g_level = lvl; }
nr_log_level nr_log_get_level(void) { return g_level; }

void nr_log_set_file(const char *path)
{
    if (g_logfile) { fclose(g_logfile); g_logfile = NULL; }
    if (path) g_logfile = nr_fopen(path, "w");
}

void nr_log(nr_log_level lvl, const char *sys, const char *fmt, ...)
{
    static const char *names[] = { "ERROR", "WARN", "INFO", "DEBUG", "TRACE" };
    char buf[2048];
    va_list ap;
    if (lvl > g_level) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[%-5s] %-6s %s\n", names[lvl], sys, buf);
    if (g_logfile) { fprintf(g_logfile, "[%-5s] %-6s %s\n", names[lvl], sys, buf); fflush(g_logfile); }
}

/* Each distinct unimplemented feature is reported once (with a counter at
 * shutdown) so logs stay readable but nothing is hidden. */
#define NR_MAX_UNIMPL 256
static struct { char key[96]; unsigned count; } g_unimpl[NR_MAX_UNIMPL];
static int g_unimpl_n;

void nr_unimplemented(const char *sys, const char *what)
{
    char key[96];
    int i;
    snprintf(key, sizeof(key), "%s:%s", sys, what);
    for (i = 0; i < g_unimpl_n; i++) {
        if (strcmp(g_unimpl[i].key, key) == 0) { g_unimpl[i].count++; return; }
    }
    if (g_unimpl_n < NR_MAX_UNIMPL) {
        nr_strlcpy(g_unimpl[g_unimpl_n].key, key, sizeof(g_unimpl[0].key));
        g_unimpl[g_unimpl_n].count = 1;
        g_unimpl_n++;
    }
    nr_log(NR_LOG_WARN, sys, "NOT IMPLEMENTED: %s", what);
}

void nr_unimplemented_report(void)
{
    int i;
    if (!g_unimpl_n) return;
    nr_log(NR_LOG_WARN, "unimpl", "Features NOT implemented that were used during this run:");
    for (i = 0; i < g_unimpl_n; i++) nr_log(NR_LOG_WARN, "unimpl", "  %s (x%u)", g_unimpl[i].key, g_unimpl[i].count);
}

/* CRC32 (IEEE 802.3, reflected) */
static u32 g_crc_table[256];
static int g_crc_init;

u32 nr_crc32(u32 crc, const void *data, size_t len)
{
    const u8 *p = (const u8 *)data;
    if (!g_crc_init) {
        u32 i, j;
        for (i = 0; i < 256; i++) {
            u32 c = i;
            for (j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            g_crc_table[i] = c;
        }
        g_crc_init = 1;
    }
    crc = ~crc;
    while (len--) crc = g_crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* SHA-1 (FIPS 180-1), small portable implementation. */
#define ROL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
static void sha1_block(u32 h[5], const u8 *blk)
{
    u32 w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++) w[i] = nr_rd32be(blk + i * 4);
    for (i = 16; i < 80; i++) w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        u32 f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        t = ROL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void nr_sha1(const void *data, size_t len, u8 out[20])
{
    u32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    const u8 *p = (const u8 *)data;
    u8 tail[128];
    size_t full = len / 64, rem = len % 64, i, tl;
    u64 bits = (u64)len * 8;
    for (i = 0; i < full; i++) sha1_block(h, p + i * 64);
    memset(tail, 0, sizeof(tail));
    memcpy(tail, p + full * 64, rem);
    tail[rem] = 0x80;
    tl = (rem < 56) ? 64 : 128;
    for (i = 0; i < 8; i++) tail[tl - 1 - i] = (u8)(bits >> (8 * i));
    sha1_block(h, tail);
    if (tl == 128) sha1_block(h, tail + 64);
    for (i = 0; i < 5; i++) {
        out[i * 4 + 0] = (u8)(h[i] >> 24); out[i * 4 + 1] = (u8)(h[i] >> 16);
        out[i * 4 + 2] = (u8)(h[i] >> 8);  out[i * 4 + 3] = (u8)h[i];
    }
}

void nr_sha1_hex(const u8 d[20], char out[41])
{
    int i;
    for (i = 0; i < 20; i++) sprintf(out + i * 2, "%02x", d[i]);
    out[40] = 0;
}

int nr_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

void nr_strlcpy(char *dst, const char *src, size_t n)
{
    size_t l;
    if (!n) return;
    l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

#ifdef _WIN32
static wchar_t *to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
#endif

FILE *nr_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *wp = to_wide(path), *wm = to_wide(mode);
    FILE *f = (wp && wm) ? _wfopen(wp, wm) : NULL;
    free(wp);
    free(wm);
    return f;
#else
    return fopen(path, mode);
#endif
}

void nr_utf8_args(int *argc, char ***argv)
{
#ifdef _WIN32
    int n, i;
    LPWSTR *wa = CommandLineToArgvW(GetCommandLineW(), &n);
    char **out;
    if (!wa) return;
    out = (char **)calloc((size_t)n + 1, sizeof(char *));
    for (i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wa[i], -1, NULL, 0, NULL, NULL);
        out[i] = (char *)malloc((size_t)len);
        WideCharToMultiByte(CP_UTF8, 0, wa[i], -1, out[i], len, NULL, NULL);
    }
    LocalFree(wa);
    *argc = n;
    *argv = out;
#else
    (void)argc; (void)argv;
#endif
}

bool nr_file_exists(const char *path)
{
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && !(st.st_mode & S_IFDIR);
#endif
}

bool nr_is_dir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES;
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFDIR);
#endif
}

long nr_read_file(const char *path, u8 **out)
{
    FILE *f = nr_fopen(path, "rb");
    long sz;
    u8 *buf;
    *out = NULL;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    buf = (u8 *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    buf[sz] = 0;
    fclose(f);
    *out = buf;
    return sz;
}

bool nr_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = nr_fopen(path, "wb");
    bool ok;
    if (!f) return false;
    ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

int nr_list_dir(const char *dir, nr_dir_fn fn, void *user)
{
    int n = 0;
#ifdef _WIN32
    char pat[1100];
    wchar_t *w;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    w = to_wide(pat);
    if (!w) return -1;
    h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        char name[1024];
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL)) continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        fn(user, name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        char full[2048];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        nr_path_join(full, sizeof(full), dir, e->d_name);
        fn(user, e->d_name, nr_is_dir(full));
        n++;
    }
    closedir(d);
#endif
    return n;
}

void nr_exe_dir(char *out, size_t n)
{
    char *s;
    out[0] = 0;
#ifdef _WIN32
    {
        wchar_t w[1024];
        DWORD len = GetModuleFileNameW(NULL, w, 1024);
        if (!len || len >= 1024) return;
        if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL)) { out[0] = 0; return; }
    }
#else
    {
        ssize_t len = readlink("/proc/self/exe", out, n - 1);
        if (len <= 0) { out[0] = 0; return; }
        out[len] = 0;
    }
#endif
    s = strrchr(out, '/');
#ifdef _WIN32
    { char *b = strrchr(out, '\\'); if (b && (!s || b > s)) s = b; }
#endif
    if (s) *s = 0; else out[0] = 0;
}

void nr_path_join(char *out, size_t n, const char *dir, const char *name)
{
    size_t l = strlen(dir);
    if (!l) { nr_strlcpy(out, name, n); return; }
    if (dir[l - 1] == '/' || dir[l - 1] == '\\') snprintf(out, n, "%s%s", dir, name);
#ifdef _WIN32
    else snprintf(out, n, "%s\\%s", dir, name);
#else
    else snprintf(out, n, "%s/%s", dir, name);
#endif
}

const char *nr_path_basename(const char *path)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    const char *s = a > b ? a : b;
    return s ? s + 1 : path;
}

bool nr_mkdir(const char *path)
{
#ifdef _WIN32
    wchar_t *w = to_wide(path);
    BOOL ok = w ? CreateDirectoryW(w, NULL) : FALSE;
    free(w);
    return ok || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0755) == 0 || nr_is_dir(path);
#endif
}
