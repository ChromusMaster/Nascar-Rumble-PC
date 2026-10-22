#include "../src/common/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static long fail_at = -1, allocations;
static int fail_write;
static void *fault_malloc(size_t n) { return fail_at >= 0 && allocations++ == fail_at ? NULL : malloc(n); }
static void *fault_realloc(void *p, size_t n) { return fail_at >= 0 && allocations++ == fail_at ? NULL : realloc(p, n); }
static bool fault_write(const char *p, const void *d, size_t n) { return !fail_write && nr_write_file_atomic(p, d, n); }
#define malloc fault_malloc
#define realloc fault_realloc
#define nr_write_file_atomic fault_write
#define nr_config_set security_config_set
#define nr_config_load security_config_load
#define nr_config_free security_config_free
#define nr_config_get security_config_get
#define nr_config_get_int security_config_get_int
#define nr_config_update_file security_config_update_file
void security_config_free(nr_config *c);
#include "../src/common/config.c"
#undef malloc
#undef realloc
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int same(const char *path, const char *expected)
{
    u8 *data;
    long n = nr_read_file(path, &data);
    int ok = n >= 0 && (size_t)n == strlen(expected) && memcmp(data, expected, (size_t)n) == 0;
    free(data);
    return ok;
}

int main(void)
{
    const char *path = "security-config.ini";
    const char *original = "\xEF\xBB\xBF; preserve\r\n[game]\r\ndisc = old\r\n[other]\r\nx = unchanged";
    const char *expected = "\xEF\xBB\xBF; preserve\r\n[game]\r\ndisc = new\r\n[other]\r\nx = unchanged";
    int i, failures = 0;
    char *large = (char *)malloc(8193);
    nr_config *c;
    REQUIRE(large);
    memset(large, 'a', 8192); large[8192] = 0;
    REQUIRE(nr_write_file(path, original, strlen(original)));
    REQUIRE(nr_config_update_file(path, "game.disc", "new"));
    REQUIRE(same(path, expected));
    REQUIRE(nr_config_update_file(path, "game.disc", "new"));
    REQUIRE(same(path, expected));
    REQUIRE(!nr_config_update_file(path, "game.disc", "a\n[injected]"));
    REQUIRE(same(path, expected));
    for (i = 0; i < 12; i++) {
        bool ok;
        REQUIRE(nr_write_file(path, original, strlen(original)));
        fail_at = i; allocations = 0;
        ok = nr_config_update_file(path, "game.long", large);
        fail_at = -1;
        if (!ok) { failures++; REQUIRE(same(path, original)); }
    }
    REQUIRE(failures >= 5);
    REQUIRE(nr_write_file(path, original, strlen(original)));
    fail_write = 1;
    REQUIRE(!nr_config_update_file(path, "game.disc", "changed"));
    fail_write = 0;
    REQUIRE(same(path, original));
    for (i = 253; i <= 257; i++) {
        large[i] = 0;
        REQUIRE(nr_config_update_file(path, "game.long", large));
        c = nr_config_load(path);
        REQUIRE(c && strcmp(nr_config_get(c, "game.long", ""), large) == 0);
        nr_config_free(c);
        large[i] = 'a';
    }
    REQUIRE(nr_config_update_file(path, "game.long", large));
    c = nr_config_load(path);
    REQUIRE(c && strlen(nr_config_get(c, "game.long", "")) == 8192);
    nr_config_set(c, "game.disc", "safe");
    fail_at = 0; allocations = 0;
    nr_config_set(c, "game.disc", "lost");
    fail_at = -1;
    REQUIRE(strcmp(nr_config_get(c, "game.disc", ""), "safe") == 0);
    nr_config_set(c, "n.value", "9999999999999999999999999");
    REQUIRE(nr_config_get_int(c, "n.value", 42) == 42);
    nr_config_free(c);
    REQUIRE(nr_write_file(path, "[s]\na = 1\n[s]\na = 2", strlen("[s]\na = 1\n[s]\na = 2")));
    REQUIRE(nr_config_update_file(path, "s.a", "3"));
    c = nr_config_load(path);
    REQUIRE(c && strcmp(nr_config_get(c, "s.a", ""), "3") == 0);
    nr_config_free(c);
    remove(path);
    REQUIRE(nr_config_update_file(path, "new.empty", ""));
    c = nr_config_load(path);
    REQUIRE(c && strcmp(nr_config_get(c, "new.empty", "missing"), "") == 0);
    nr_config_free(c);
    REQUIRE(!nr_config_update_file("missing-dir/config.ini", "s.k", "v"));
    c = nr_config_load(NULL);
    REQUIRE(c);
    for (i = 0; i < 32; i++) { char key[32]; snprintf(key, sizeof(key), "k%d", i); nr_config_set(c, key, "v"); }
    fail_at = 2; allocations = 0;
    nr_config_set(c, "new", "value");
    fail_at = -1;
    REQUIRE(strcmp(nr_config_get(c, "k31", ""), "v") == 0);
    REQUIRE(nr_config_get(c, "new", NULL) == NULL);
    nr_config_free(c);
    remove(path); free(large);
    puts("config security: passed");
    return 0;
}
