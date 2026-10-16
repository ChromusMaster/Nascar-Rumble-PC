#include "proc.h"
#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

void nr_sibling_exe(char *out, size_t n, const char *name)
{
    char file[128];
#ifdef _WIN32
    snprintf(file, sizeof(file), "%s.exe", name);
#else
    snprintf(file, sizeof(file), "%s", name);
#endif
    nr_path_join(out, n, nr_exe_folder(), file);
}

void nr_session_token(char out[33])
{
    u8 r[16];
    int i;
    bool ok = false;
#ifdef _WIN32
    ok = BCryptGenRandom(NULL, r, sizeof(r), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) { ok = fread(r, 1, sizeof(r), f) == sizeof(r); fclose(f); }
    }
#endif
    if (!ok) {
        u32 s = (u32)time(NULL) ^ (u32)(size_t)&r;
        for (i = 0; i < 16; i++) { s = s * 1664525u + 1013904223u; r[i] = (u8)(s >> 24); }
    }
    for (i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", r[i]);
}

bool nr_session_valid(const char *arg)
{
    const char *env = getenv(NR_SESSION_ENV);
    size_t i;
    if (!arg || !env || strlen(arg) != 32 || strcmp(arg, env) != 0) return false;
    for (i = 0; i < 32; i++) {
        char c = arg[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

#ifdef _WIN32
struct nr_proc { HANDLE h; };

static void append_arg(wchar_t *cmd, size_t cap, const char *a)
{
    wchar_t w[2048];
    size_t l = wcslen(cmd), i;
    int bs = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, a, -1, w, 2048)) w[0] = 0;
    if (l && l + 1 < cap) cmd[l++] = L' ';
    if (l + 1 < cap) cmd[l++] = L'"';
    for (i = 0; w[i] && l + 4 < cap; i++) {
        if (w[i] == L'\\') { bs++; cmd[l++] = w[i]; continue; }
        if (w[i] == L'"') { while (bs-- > 0 && l + 4 < cap) cmd[l++] = L'\\'; cmd[l++] = L'\\'; }
        bs = 0;
        cmd[l++] = w[i];
    }
    while (bs-- > 0 && l + 3 < cap) cmd[l++] = L'\\';
    if (l + 2 < cap) cmd[l++] = L'"';
    cmd[l] = 0;
}

nr_proc *nr_proc_spawn(const char *const *argv, const char *env_name, const char *env_value, char *err, size_t n)
{
    static wchar_t cmd[16384];
    wchar_t exe[1024], wn[64], wv[256];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    nr_proc *p;
    int i;
    cmd[0] = 0;
    for (i = 0; argv[i]; i++) append_arg(cmd, 16384, argv[i]);
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[0], -1, exe, 1024)) { snprintf(err, n, "invalid path"); return NULL; }
    if (env_name) {
        MultiByteToWideChar(CP_UTF8, 0, env_name, -1, wn, 64);
        MultiByteToWideChar(CP_UTF8, 0, env_value ? env_value : "", -1, wv, 256);
        SetEnvironmentVariableW(wn, wv);          /* inherited by the child */
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        snprintf(err, n, "cannot start %s (error %lu)", argv[0], (unsigned long)GetLastError());
        if (env_name) SetEnvironmentVariableW(wn, NULL);
        return NULL;
    }
    if (env_name) SetEnvironmentVariableW(wn, NULL);
    CloseHandle(pi.hThread);
    p = (nr_proc *)calloc(1, sizeof(*p));
    if (!p) { CloseHandle(pi.hProcess); return NULL; }
    p->h = pi.hProcess;
    return p;
}

bool nr_proc_done(nr_proc *p, int *code)
{
    DWORD c = 0;
    if (WaitForSingleObject(p->h, 0) != WAIT_OBJECT_0) return false;
    GetExitCodeProcess(p->h, &c);
    if (code) *code = (int)c;
    return true;
}

void nr_proc_free(nr_proc *p)
{
    if (!p) return;
    CloseHandle(p->h);
    free(p);
}
#else
struct nr_proc { pid_t pid; bool done; int code; };

nr_proc *nr_proc_spawn(const char *const *argv, const char *env_name, const char *env_value, char *err, size_t n)
{
    pid_t pid;
    int rc;
    nr_proc *p;
    if (env_name) setenv(env_name, env_value ? env_value : "", 1);
    rc = posix_spawn(&pid, argv[0], NULL, NULL, (char *const *)argv, environ);
    if (env_name) unsetenv(env_name);
    if (rc != 0) { snprintf(err, n, "cannot start %s: %s", argv[0], strerror(rc)); return NULL; }
    p = (nr_proc *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->pid = pid;
    return p;
}

bool nr_proc_done(nr_proc *p, int *code)
{
    int st = 0;
    if (!p->done) {
        pid_t r = waitpid(p->pid, &st, WNOHANG);
        if (r == 0) return false;
        p->done = true;
        p->code = r < 0 ? -1 : WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    }
    if (code) *code = p->code;
    return true;
}

void nr_proc_free(nr_proc *p) { free(p); }
#endif
