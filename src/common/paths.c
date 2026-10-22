#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

static bool g_init, g_portable;
static char g_exe[1024], g_user[1024], g_data[1024], g_share[1024], g_ini[1100];

bool nr_path_is_absolute(const char *p)
{
    if (!p || !*p) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

/* creates every missing component of dir */
static void mkdirs(const char *dir)
{
    char tmp[1024];
    size_t i;
    nr_strlcpy(tmp, dir, sizeof(tmp));
    for (i = 1; tmp[i]; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            if (i == 2 && tmp[1] == ':') continue;
            tmp[i] = 0;
            if (!nr_is_dir(tmp)) nr_mkdir(tmp);
            tmp[i] = c;
        }
    }
    if (!nr_is_dir(tmp)) nr_mkdir(tmp);
}

static bool dir_writable(const char *dir)
{
    char probe[1100];
    FILE *f;
    nr_path_join(probe, sizeof(probe), dir, ".nrengine_write_test");
    f = nr_fopen(probe, "wb");
    if (!f) return false;
    fclose(f);
    remove(probe);
    return true;
}

void nr_paths_init(void)
{
    if (g_init) return;
    g_init = true;
    nr_exe_dir(g_exe, sizeof(g_exe));
    if (!g_exe[0]) nr_strlcpy(g_exe, ".", sizeof(g_exe));
#ifdef _WIN32
    g_portable = dir_writable(g_exe);
    nr_strlcpy(g_share, g_exe, sizeof(g_share));
    if (g_portable) {
        nr_strlcpy(g_user, g_exe, sizeof(g_user));
        nr_strlcpy(g_data, g_exe, sizeof(g_data));
    } else {
        wchar_t w[MAX_PATH];
        char base[1024] = ".";
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, w)))
            WideCharToMultiByte(CP_UTF8, 0, w, -1, base, sizeof(base), NULL, NULL);
        nr_path_join(g_user, sizeof(g_user), base, "NREngine");
        nr_strlcpy(g_data, g_user, sizeof(g_data));
    }
#else
    {
        bool system_install = strncmp(g_exe, "/usr/", 5) == 0 || strncmp(g_exe, "/opt/", 5) == 0;
        g_portable = !system_install && dir_writable(g_exe);
        if (g_portable) {
            nr_strlcpy(g_user, g_exe, sizeof(g_user));
            nr_strlcpy(g_data, g_exe, sizeof(g_data));
            nr_strlcpy(g_share, g_exe, sizeof(g_share));
        } else {
            const char *home = getenv("HOME"), *xc = getenv("XDG_CONFIG_HOME"), *xd = getenv("XDG_DATA_HOME");
            char tmp[1024];
            if (!home || !*home) home = ".";
            if (xc && *xc) nr_path_join(g_user, sizeof(g_user), xc, "nrengine");
            else { snprintf(tmp, sizeof(tmp), "%s/.config", home); nr_path_join(g_user, sizeof(g_user), tmp, "nrengine"); }
            if (xd && *xd) nr_path_join(g_data, sizeof(g_data), xd, "nrengine");
            else { snprintf(tmp, sizeof(tmp), "%s/.local/share", home); nr_path_join(g_data, sizeof(g_data), tmp, "nrengine"); }
            /* /usr/games/nrengine -> /usr/share/nrengine; /opt/x/bin -> /opt/x/share/nrengine */
            if (strncmp(g_exe, "/usr/local/", 11) == 0) nr_strlcpy(g_share, "/usr/local/share/nrengine", sizeof(g_share));
            else if (strncmp(g_exe, "/usr/", 5) == 0) nr_strlcpy(g_share, "/usr/share/nrengine", sizeof(g_share));
            else if ((size_t)snprintf(g_share, sizeof(g_share), "%s/../share/nrengine", g_exe) >= sizeof(g_share)) nr_strlcpy(g_share, g_exe, sizeof(g_share));
        }
    }
#endif
    mkdirs(g_user);
    mkdirs(g_data);
    nr_path_join(g_ini, sizeof(g_ini), g_user, "nrengine.ini");
}

bool nr_paths_portable(void) { nr_paths_init(); return g_portable; }
const char *nr_exe_folder(void) { nr_paths_init(); return g_exe; }
const char *nr_user_dir(void) { nr_paths_init(); return g_user; }
const char *nr_data_dir(void) { nr_paths_init(); return g_data; }
const char *nr_share_dir(void) { nr_paths_init(); return g_share; }
const char *nr_default_config_path(void) { nr_paths_init(); return g_ini; }

void nr_data_path(char *out, size_t n, const char *rel)
{
    nr_paths_init();
    if (!rel || !*rel) { out[0] = 0; return; }
    if (nr_path_is_absolute(rel)) nr_strlcpy(out, rel, n);
    else nr_path_join(out, n, g_data, rel);
}
