#include "winutil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

bool nr_win_open_file(void *hwnd, const char *title, char *out, size_t n)
{
    wchar_t file[1024] = L"", wtitle[256];
    OPENFILENAMEW ofn;
    if (!MultiByteToWideChar(CP_UTF8, 0, title ? title : "", -1, wtitle, 256)) wtitle[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)hwnd;
    ofn.lpstrFilter = L"*.cue; *.bin; *.iso; *.img\0*.cue;*.bin;*.iso;*.img\0*.*\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)n, NULL, NULL) > 0;
}

void nr_win_message(const char *title, const char *text)
{
    wchar_t wt[256], wx[1024];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 256);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wx, 1024);
    MessageBoxW(NULL, wx, wt, MB_OK | MB_ICONWARNING);
}

bool nr_win_open_folder(const char *path)
{
    wchar_t w[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) return false;
    return (INT_PTR)ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL) > 32;
}
