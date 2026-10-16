/* Small Win32 helpers for the launcher. */
#ifndef NR_WINUTIL_H
#define NR_WINUTIL_H

#include "../../common/common.h"

/* System "Open" dialog filtered to disc images; out receives a UTF-8 path. */
bool nr_win_open_file(void *hwnd, const char *title_utf8, char *out, size_t n);
void nr_win_message(const char *title, const char *utf8_text);
/* opens a folder in Explorer */
bool nr_win_open_folder(const char *path);

#endif
