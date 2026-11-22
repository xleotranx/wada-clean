#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_disk.h"
#include "wc_platform.h"
#include "wc_types.h"

static bool utf8_to_wide(const char *src, wchar_t *dst, int cap) {
    if (!src || !dst || cap <= 0) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, cap);
    return n > 0 && n <= cap;
}

static bool wide_to_utf8(const wchar_t *src, char *dst, int cap) {
    if (!src || !dst || cap <= 0) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, cap, NULL, NULL);
    return n > 0 && n <= cap;
}

static void approved_location(wc_startup_source src, HKEY *hive, const wchar_t **subkey,
                              REGSAM *wow64) {
    switch (src) {
    case WC_STARTUP_HKCU_RUN:
        *hive = HKEY_CURRENT_USER;
        *subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
        *wow64 = 0;
        break;
    case WC_STARTUP_HKLM_RUN:
        *hive = HKEY_LOCAL_MACHINE;
        *subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
        *wow64 = KEY_WOW64_64KEY;
        break;
    case WC_STARTUP_USER_FOLDER:
        *hive = HKEY_CURRENT_USER;
        *subkey =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";
        *wow64 = 0;
        break;
    case WC_STARTUP_COMMON_FOLDER:
        *hive = HKEY_LOCAL_MACHINE;
        *subkey =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";
        *wow64 = KEY_WOW64_64KEY;
        break;
    }
}

static HKEY open_approved_key(wc_startup_source src) {
    HKEY hive = NULL;
    const wchar_t *subkey = NULL;
    REGSAM wow64 = 0;
    approved_location(src, &hive, &subkey, &wow64);

    HKEY hk = NULL;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ | wow64, &hk) != ERROR_SUCCESS) {
        return NULL;
    }
    return hk;
}

static bool query_startup_approved_key(HKEY hk, const wchar_t *name) {
    if (!hk) return true;
    uint8_t buf[32];
    DWORD len = (DWORD)sizeof buf;
    DWORD type = 0;
    if (RegQueryValueExW(hk, name, NULL, &type, buf, &len) == ERROR_SUCCESS &&
        type == REG_BINARY && len > 0) {
        return wc_startup_approved_is_enabled(buf, (size_t)len);
    }
    return true;
}

static void enum_run_key(HKEY hive, const wchar_t *subkey, REGSAM wow64, wc_startup_source src,
                         bool needs_admin, wc_startup_item *out, int max_items, int *count) {
    HKEY hk = NULL;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ | wow64, &hk) != ERROR_SUCCESS) return;

    HKEY approved_hk = open_approved_key(src);
    wchar_t wname[256];
    wchar_t wval[WC_PATH_CAP];
    for (DWORD idx = 0; *count < max_items; idx++) {
        DWORD name_len = (DWORD)(sizeof wname / sizeof wname[0]);
        DWORD val_bytes = (DWORD)sizeof wval;
        DWORD type = 0;
        LONG rc = RegEnumValueW(hk, idx, wname, &name_len, NULL, &type, (LPBYTE)wval, &val_bytes);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

        /* Ensure NUL-termination of value data. */
        size_t wchar_len = val_bytes / sizeof(wchar_t);
        if (wchar_len >= WC_PATH_CAP) wchar_len = WC_PATH_CAP - 1;
        wval[wchar_len] = L'\0';

        wc_startup_item item;
        memset(&item, 0, sizeof item);
        if (!wide_to_utf8(wname, item.name, sizeof item.name) || !item.name[0]) continue;
        wide_to_utf8(wval, item.command, sizeof item.command);
        item.source = src;
        item.needs_admin = needs_admin;
        item.enabled = query_startup_approved_key(approved_hk, wname);
        out[(*count)++] = item;
    }
    if (approved_hk) RegCloseKey(approved_hk);
    RegCloseKey(hk);
}

static void enum_startup_folder(const char *env_name, const char *rel_path, wc_startup_source src,
                                bool needs_admin, wc_startup_item *out, int max_items,
                                int *count) {
    char base[WC_PATH_CAP];
    if (!wc_plat_get_env(env_name, base, sizeof base) || !base[0]) return;

    char folder[WC_PATH_CAP];
    int fn = snprintf(folder, sizeof folder, "%s\\%s", base, rel_path);
    if (fn < 0 || (size_t)fn >= sizeof folder) return;

    char pattern[WC_PATH_CAP];
    int pn = snprintf(pattern, sizeof pattern, "%s\\*", folder);
    if (pn < 0 || (size_t)pn >= sizeof pattern) return;

    wchar_t wpat[WC_PATH_CAP];
    if (!utf8_to_wide(pattern, wpat, WC_PATH_CAP)) return;

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;

    HKEY approved_hk = open_approved_key(src);
    do {
        if (*count >= max_items) break;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsicmp(ffd.cFileName, L"desktop.ini") == 0) continue;

        wc_startup_item item;
        memset(&item, 0, sizeof item);
        if (!wide_to_utf8(ffd.cFileName, item.name, sizeof item.name) || !item.name[0]) continue;
        int cn = snprintf(item.command, sizeof item.command, "%s\\%s", folder, item.name);
        if (cn < 0 || (size_t)cn >= sizeof item.command) continue;

        item.source = src;
        item.needs_admin = needs_admin;
        item.enabled = query_startup_approved_key(approved_hk, ffd.cFileName);
        out[(*count)++] = item;
    } while (FindNextFileW(h, &ffd));

    if (approved_hk) RegCloseKey(approved_hk);
    FindClose(h);
}

bool wc_plat_list_startup(wc_startup_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    int count = 0;
    enum_run_key(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                 WC_STARTUP_HKCU_RUN, false, out, max_items, &count);
    enum_run_key(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                 KEY_WOW64_64KEY, WC_STARTUP_HKLM_RUN, true, out, max_items, &count);
    enum_startup_folder("APPDATA", "Microsoft\\Windows\\Start Menu\\Programs\\Startup",
                        WC_STARTUP_USER_FOLDER, false, out, max_items, &count);
    enum_startup_folder("ProgramData", "Microsoft\\Windows\\Start Menu\\Programs\\Startup",
                        WC_STARTUP_COMMON_FOLDER, true, out, max_items, &count);

    if (out_count) *out_count = count;
    return true;
}

bool wc_plat_set_startup_enabled(const wc_startup_item *item, bool enabled) {
    if (!item || !item->name[0]) return false;

    wchar_t wname[256];
    if (!utf8_to_wide(item->name, wname, 256)) return false;

    HKEY hive = NULL;
    const wchar_t *subkey = NULL;
    REGSAM wow64 = 0;
    approved_location(item->source, &hive, &subkey, &wow64);

    HKEY hk = NULL;
    if (RegCreateKeyExW(hive, subkey, 0, NULL, 0, KEY_SET_VALUE | wow64, NULL, &hk, NULL) !=
        ERROR_SUCCESS) {
        return false;
    }

    uint8_t data[12];
    wc_startup_approved_encode(enabled, data);
    if (!enabled) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(data + 4, &ft, sizeof ft);
    }

    LONG rc = RegSetValueExW(hk, wname, 0, REG_BINARY, data, (DWORD)sizeof data);
    RegCloseKey(hk);
    return rc == ERROR_SUCCESS;
}
