/* Where the engine keeps its files.
 *
 * Portable layout (Windows ZIP, a build directory, any writable folder):
 * everything lives next to the executables - nrengine.ini, gamedata/,
 * mods/, memory cards, screenshots, logs.
 *
 * Installed layout (Linux package under /usr or /opt, or a Windows folder
 * that is not writable):
 *   user dir   settings and logs       ~/.config/nrengine      | %APPDATA%\NREngine
 *   data dir   gamedata/, mods/, memory cards, screenshots
 *                                       ~/.local/share/nrengine | %APPDATA%\NREngine
 *   share dir  read-only files shipped with the engine (example mod)
 *                                       /usr/share/nrengine     | executable folder
 * XDG_CONFIG_HOME / XDG_DATA_HOME are honoured. Folders are created on
 * first use. */
#ifndef NR_PATHS_H
#define NR_PATHS_H

#include "common.h"

void nr_paths_init(void);
bool nr_paths_portable(void);
const char *nr_exe_folder(void);
const char *nr_user_dir(void);
const char *nr_data_dir(void);
const char *nr_share_dir(void);
/* default nrengine.ini path */
const char *nr_default_config_path(void);
/* path relative to the data dir unless already absolute */
void nr_data_path(char *out, size_t n, const char *rel);
bool nr_path_is_absolute(const char *p);

#endif
