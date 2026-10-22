/* NASCAR Rumble Reimplementation Engine - common definitions. */
#ifndef NR_COMMON_H
#define NR_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#define NR_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#if defined(__GNUC__) || defined(__clang__)
#define NR_LIKELY(x)   __builtin_expect(!!(x), 1)
#define NR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define NR_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define NR_LIKELY(x)   (x)
#define NR_UNLIKELY(x) (x)
#define NR_PRINTF(a, b)
#endif

static inline u16 nr_rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 nr_rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static inline u32 nr_rd32be(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }
static inline u16 nr_rd16be(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static inline void nr_wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void nr_wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

/* Logging ---------------------------------------------------------------- */
typedef enum { NR_LOG_ERROR = 0, NR_LOG_WARN, NR_LOG_INFO, NR_LOG_DEBUG, NR_LOG_TRACE } nr_log_level;

void nr_log_set_level(nr_log_level lvl);
nr_log_level nr_log_get_level(void);
void nr_log_set_file(const char *path); /* NULL = stderr only */
void nr_log(nr_log_level lvl, const char *sys, const char *fmt, ...) NR_PRINTF(3, 4);

#define NR_ERR(sys, ...)   nr_log(NR_LOG_ERROR, sys, __VA_ARGS__)
#define NR_WARN(sys, ...)  nr_log(NR_LOG_WARN, sys, __VA_ARGS__)
#define NR_INFO(sys, ...)  nr_log(NR_LOG_INFO, sys, __VA_ARGS__)
#define NR_DBG(sys, ...)   do { if (nr_log_get_level() >= NR_LOG_DEBUG) nr_log(NR_LOG_DEBUG, sys, __VA_ARGS__); } while (0)
#define NR_TRACE(sys, ...) do { if (nr_log_get_level() >= NR_LOG_TRACE) nr_log(NR_LOG_TRACE, sys, __VA_ARGS__); } while (0)

/* Unimplemented-feature reporting: every path that the engine does not yet
 * implement must go through this so it is visible (never silently ignored). */
void nr_unimplemented(const char *sys, const char *what);
/* Logs the list of unimplemented features that were hit (with counts). */
void nr_unimplemented_report(void);

/* Checksums ---------------------------------------------------------------- */
u32 nr_crc32(u32 crc, const void *data, size_t len);
void nr_sha1(const void *data, size_t len, u8 out[20]);
void nr_sha1_hex(const u8 digest[20], char out[41]);

/* Misc ------------------------------------------------------------------- */
int nr_strcasecmp(const char *a, const char *b);
void nr_strlcpy(char *dst, const char *src, size_t n);
#include <stdio.h>
/* fopen() that accepts UTF-8 paths on every platform (uses _wfopen on Windows). */
FILE *nr_fopen(const char *path, const char *mode);
/* On Windows replaces argv with UTF-8 converted arguments (no-op elsewhere). */
void nr_utf8_args(int *argc, char ***argv);
bool nr_safe_relative_path(const char *base, const char *relative);
bool nr_file_exists(const char *path);
bool nr_is_dir(const char *path);
/* Reads a whole file. Caller frees *out. Returns size or -1. */
long nr_read_file(const char *path, u8 **out);
long nr_read_file_limited(const char *path, u8 **out, size_t limit);
bool nr_write_file_atomic(const char *path, const void *data, size_t len);
bool nr_write_file(const char *path, const void *data, size_t len);
/* Calls fn for every entry (files and directories, without "." and "..") of
 * dir; names are UTF-8. Returns the number of entries or -1. */
typedef void (*nr_dir_fn)(void *user, const char *name, bool is_dir);
int nr_list_dir(const char *dir, nr_dir_fn fn, void *user);
/* Directory containing the running executable (empty if unknown). */
void nr_exe_dir(char *out, size_t n);
/* dir + separator + name */
void nr_path_join(char *out, size_t n, const char *dir, const char *name);
const char *nr_path_basename(const char *path);
bool nr_mkdir(const char *path);

/* Image output (image.c) */
bool nr_write_png(const char *path, const u32 *rgba, int w, int h);

/* Config (config.c): simple INI-like key=value store */
typedef struct nr_config nr_config;
nr_config *nr_config_load(const char *path); /* returns empty config if missing */
void nr_config_free(nr_config *c);
const char *nr_config_get(const nr_config *c, const char *key, const char *def);
int nr_config_get_int(const nr_config *c, const char *key, int def);
void nr_config_set(nr_config *c, const char *key, const char *value);
bool nr_config_update_file(const char *path, const char *section_key, const char *value);

#endif
