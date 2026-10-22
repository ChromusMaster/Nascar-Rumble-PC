#include "../src/common/common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
static int failure;
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
int __real_fflush(FILE *);
int __real_fsync(int);
int __real_fclose(FILE *);
int __real_rename(const char *, const char *);
size_t __wrap_fwrite(const void *p, size_t s, size_t n, FILE *f) { if (failure == 1) { errno = EIO; return 0; } return __real_fwrite(p, s, n, f); }
int __wrap_fflush(FILE *f) { if (failure == 2) { errno = EIO; return EOF; } return __real_fflush(f); }
int __wrap_fsync(int fd) { if (failure == 3) { errno = EIO; return -1; } return __real_fsync(fd); }
int __wrap_fclose(FILE *f) { int result = __real_fclose(f); if (failure == 4) { errno = EIO; return EOF; } return result; }
int __wrap_rename(const char *a, const char *b) { if (failure == 5) { errno = EIO; return -1; } return __real_rename(a, b); }
int main(void)
{
    int stage;
    const char *path = "security-atomic.ini", *original = "original\n";
    for (stage = 1; stage <= 5; stage++) {
        bool ok;
        u8 *bytes = NULL;
        long n;
        if (!nr_write_file(path, original, strlen(original))) return 1;
        failure = stage;
        ok = nr_write_file_atomic(path, "replacement", 11);
        failure = 0;
        n = nr_read_file(path, &bytes);
        if (ok || n != (long)strlen(original) || memcmp(bytes, original, strlen(original)) != 0) { free(bytes); return 1; }
        free(bytes);
    }
    remove(path);
    puts("atomic IO: five failure stages preserve original");
    return 0;
}
