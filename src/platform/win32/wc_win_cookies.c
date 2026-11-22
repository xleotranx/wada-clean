#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"

typedef struct chromium_def {
    const char *label;
    const wchar_t *rel;
    const char *process_name;
} chromium_def;

static const chromium_def g_chromium[] = {
    {"Chrome", L"Google\\Chrome\\User Data", "chrome.exe"},
    {"Edge", L"Microsoft\\Edge\\User Data", "msedge.exe"},
    {"Brave", L"BraveSoftware\\Brave-Browser\\User Data", "brave.exe"},
};

static void wide_to_utf8(const wchar_t *src, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!src) return;
    if (WideCharToMultiByte(CP_UTF8, 0, src, -1, out, (int)cap, NULL, NULL) <= 0) out[0] = '\0';
}

static bool file_exists_wide(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void add_db(wc_cookie_db *out, int max_items, int *count, const char *browser,
                   const wchar_t *profile, const wchar_t *path, const char *process_name,
                   bool is_firefox) {
    if (*count >= max_items) return;
    wc_cookie_db *item = &out[*count];
    memset(item, 0, sizeof *item);
    snprintf(item->browser, sizeof item->browser, "%s", browser);
    wide_to_utf8(profile, item->profile, sizeof item->profile);
    wide_to_utf8(path, item->path, sizeof item->path);
    snprintf(item->process_name, sizeof item->process_name, "%s", process_name);
    item->is_firefox = is_firefox;
    if (item->path[0]) (*count)++;
}

static void scan_chromium(const wchar_t *local_appdata, const chromium_def *def,
                          wc_cookie_db *out, int max_items, int *count) {
    wchar_t base[WC_PATH_CAP];
    if (_snwprintf(base, WC_PATH_CAP, L"%ls\\%ls", local_appdata, def->rel) < 0) return;
    if (base[0] == L'\0') return;

    wchar_t pattern[WC_PATH_CAP];
    if (_snwprintf(pattern, WC_PATH_CAP, L"%ls\\*", base) < 0) return;

    WIN32_FIND_DATAW find_data;
    HANDLE handle = FindFirstFileW(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) return;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }

        wchar_t db_path[WC_PATH_CAP];
        if (_snwprintf(db_path, WC_PATH_CAP, L"%ls\\%ls\\Network\\Cookies", base,
                       find_data.cFileName) < 0) {
            continue;
        }
        if (!file_exists_wide(db_path)) {
            if (_snwprintf(db_path, WC_PATH_CAP, L"%ls\\%ls\\Cookies", base,
                           find_data.cFileName) < 0) {
                continue;
            }
        }
        if (file_exists_wide(db_path)) {
            add_db(out, max_items, count, def->label, find_data.cFileName, db_path,
                   def->process_name, false);
        }
    } while (FindNextFileW(handle, &find_data));
    FindClose(handle);
}

static void scan_firefox(const wchar_t *appdata, wc_cookie_db *out, int max_items, int *count) {
    wchar_t base[WC_PATH_CAP];
    if (_snwprintf(base, WC_PATH_CAP, L"%ls\\Mozilla\\Firefox\\Profiles", appdata) < 0) return;

    wchar_t pattern[WC_PATH_CAP];
    if (_snwprintf(pattern, WC_PATH_CAP, L"%ls\\*", base) < 0) return;

    WIN32_FIND_DATAW find_data;
    HANDLE handle = FindFirstFileW(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) return;

    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }
        wchar_t db_path[WC_PATH_CAP];
        if (_snwprintf(db_path, WC_PATH_CAP, L"%ls\\%ls\\cookies.sqlite", base,
                       find_data.cFileName) < 0) {
            continue;
        }
        if (file_exists_wide(db_path)) {
            add_db(out, max_items, count, "Firefox", find_data.cFileName, db_path, "firefox.exe",
                   true);
        }
    } while (FindNextFileW(handle, &find_data));
    FindClose(handle);
}

static int compare_db(const void *a, const void *b) {
    const wc_cookie_db *x = (const wc_cookie_db *)a;
    const wc_cookie_db *y = (const wc_cookie_db *)b;
    int by_browser = strcmp(x->browser, y->browser);
    if (by_browser != 0) return by_browser;
    return strcmp(x->profile, y->profile);
}

int wc_plat_list_cookie_dbs(wc_cookie_db *out, int max_items) {
    if (!out || max_items <= 0) return 0;
    if (max_items > WC_MAX_COOKIE_DBS) max_items = WC_MAX_COOKIE_DBS;

    wchar_t local_appdata[WC_PATH_CAP];
    wchar_t appdata[WC_PATH_CAP];
    DWORD local_len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, WC_PATH_CAP);
    DWORD roam_len = GetEnvironmentVariableW(L"APPDATA", appdata, WC_PATH_CAP);

    int count = 0;
    if (local_len > 0 && local_len < WC_PATH_CAP) {
        for (size_t i = 0; i < sizeof g_chromium / sizeof g_chromium[0]; i++) {
            scan_chromium(local_appdata, &g_chromium[i], out, max_items, &count);
        }
    }
    if (roam_len > 0 && roam_len < WC_PATH_CAP) {
        scan_firefox(appdata, out, max_items, &count);
    }

    if (count > 1) qsort(out, (size_t)count, sizeof out[0], compare_db);
    return count;
}
