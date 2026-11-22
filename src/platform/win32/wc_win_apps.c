#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int cmp_app_name(const void *pa, const void *pb) {
    const wc_app_item *a = (const wc_app_item *)pa;
    const wc_app_item *b = (const wc_app_item *)pb;
    int c = ascii_strcasecmp(a->name, b->name);
    if (c != 0) return c;
    return ascii_strcasecmp(a->version, b->version);
}

static bool reg_read_str(HKEY hk, const wchar_t *val_name, char *out, int out_cap) {
    wchar_t wbuf[WC_PATH_CAP];
    DWORD bytes = (DWORD)sizeof wbuf;
    DWORD type = 0;
    if (RegQueryValueExW(hk, val_name, NULL, &type, (LPBYTE)wbuf, &bytes) != ERROR_SUCCESS) {
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
    size_t wlen = bytes / sizeof(wchar_t);
    if (wlen >= WC_PATH_CAP) wlen = WC_PATH_CAP - 1;
    wbuf[wlen] = L'\0';
    return wide_to_utf8(wbuf, out, out_cap) && out[0] != '\0';
}

static bool reg_read_dword(HKEY hk, const wchar_t *val_name, DWORD *out) {
    DWORD val = 0;
    DWORD bytes = (DWORD)sizeof val;
    DWORD type = 0;
    if (RegQueryValueExW(hk, val_name, NULL, &type, (LPBYTE)&val, &bytes) != ERROR_SUCCESS) {
        return false;
    }
    if (type != REG_DWORD) return false;
    *out = val;
    return true;
}

static void format_install_date(const char *raw, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (strlen(raw) == 8) {
        for (int i = 0; i < 8; i++) {
            if (!isdigit((unsigned char)raw[i])) {
                snprintf(out, cap, "%.15s", raw);
                return;
            }
        }
        snprintf(out, cap, "%.4s-%.2s-%.2s", raw, raw + 4, raw + 6);
    } else {
        snprintf(out, cap, "%.15s", raw);
    }
}

static bool app_exists(const wc_app_item *items, int count, const char *name, const char *version) {
    for (int i = 0; i < count; i++) {
        if (ascii_strcasecmp(items[i].name, name) == 0 &&
            ascii_strcasecmp(items[i].version, version) == 0) {
            return true;
        }
    }
    return false;
}

static void scan_uninstall_key(HKEY hive, REGSAM wow64, bool is_user, wc_app_item *out,
                               int max_items, int *count) {
    HKEY root = NULL;
    if (RegOpenKeyExW(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                      KEY_READ | wow64, &root) != ERROR_SUCCESS) {
        return;
    }

    wchar_t sub_name[256];
    for (DWORD idx = 0; *count < max_items; idx++) {
        DWORD name_len = (DWORD)(sizeof sub_name / sizeof sub_name[0]);
        LONG rc = RegEnumKeyExW(root, idx, sub_name, &name_len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;

        HKEY app_key = NULL;
        if (RegOpenKeyExW(root, sub_name, 0, KEY_READ | wow64, &app_key) != ERROR_SUCCESS) {
            continue;
        }

        DWORD sys_comp = 0;
        char parent[64];
        if ((reg_read_dword(app_key, L"SystemComponent", &sys_comp) && sys_comp == 1) ||
            reg_read_str(app_key, L"ParentKeyName", parent, sizeof parent)) {
            RegCloseKey(app_key);
            continue;
        }

        wc_app_item item;
        memset(&item, 0, sizeof item);
        if (!reg_read_str(app_key, L"DisplayName", item.name, sizeof item.name) ||
            !reg_read_str(app_key, L"UninstallString", item.uninstall_cmd,
                          sizeof item.uninstall_cmd)) {
            RegCloseKey(app_key);
            continue;
        }

        reg_read_str(app_key, L"Publisher", item.publisher, sizeof item.publisher);
        reg_read_str(app_key, L"DisplayVersion", item.version, sizeof item.version);

        char raw_date[32];
        if (reg_read_str(app_key, L"InstallDate", raw_date, sizeof raw_date)) {
            format_install_date(raw_date, item.install_date, sizeof item.install_date);
        }

        DWORD size_kb = 0;
        if (reg_read_dword(app_key, L"EstimatedSize", &size_kb)) {
            item.estimated_size_bytes = (uint64_t)size_kb * 1024ULL;
        }
        item.is_user_install = is_user;

        RegCloseKey(app_key);

        if (!app_exists(out, *count, item.name, item.version)) {
            out[(*count)++] = item;
        }
    }

    RegCloseKey(root);
}

bool wc_plat_list_apps(wc_app_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    int count = 0;
    scan_uninstall_key(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, false, out, max_items, &count);
    scan_uninstall_key(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, false, out, max_items, &count);
    scan_uninstall_key(HKEY_CURRENT_USER, 0, true, out, max_items, &count);

    if (count > 1) {
        qsort(out, (size_t)count, sizeof out[0], cmp_app_name);
    }
    if (out_count) *out_count = count;
    return true;
}

static void split_cmd_exe_args(const wchar_t *cmd, wchar_t *exe, size_t exe_cap,
                               const wchar_t **out_args) {
    while (*cmd == L' ' || *cmd == L'\t') cmd++;
    *out_args = L"";
    exe[0] = L'\0';

    if (cmd[0] == L'"') {
        const wchar_t *end = wcschr(cmd + 1, L'"');
        if (end) {
            size_t len = (size_t)(end - (cmd + 1));
            if (len >= exe_cap) len = exe_cap - 1;
            wcsncpy(exe, cmd + 1, len);
            exe[len] = L'\0';
            const wchar_t *a = end + 1;
            while (*a == L' ' || *a == L'\t') a++;
            *out_args = a;
            return;
        }
    }

    /* Unquoted command: look for ".exe " (case-insensitive) for paths containing spaces. */
    const wchar_t *split = NULL;
    for (const wchar_t *p = cmd; *p; p++) {
        if (_wcsnicmp(p, L".exe", 4) == 0 && (p[4] == L' ' || p[4] == L'\t' || p[4] == L'\0')) {
            split = p + 4;
            break;
        }
    }
    if (!split) {
        split = wcschr(cmd, L' ');
    }
    if (split) {
        size_t len = (size_t)(split - cmd);
        if (len >= exe_cap) len = exe_cap - 1;
        wcsncpy(exe, cmd, len);
        exe[len] = L'\0';
        while (*split == L' ' || *split == L'\t') split++;
        *out_args = split;
    } else {
        wcsncpy(exe, cmd, exe_cap - 1);
        exe[exe_cap - 1] = L'\0';
    }
}

bool wc_plat_launch_uninstaller(const char *uninstall_cmd) {
    if (!uninstall_cmd || !uninstall_cmd[0]) return false;

    wchar_t wraw[WC_PATH_CAP];
    if (!utf8_to_wide(uninstall_cmd, wraw, WC_PATH_CAP)) return false;

    wchar_t wcmd[WC_PATH_CAP];
    DWORD exp = ExpandEnvironmentStringsW(wraw, wcmd, WC_PATH_CAP);
    if (exp == 0 || exp > WC_PATH_CAP) {
        wcsncpy(wcmd, wraw, WC_PATH_CAP - 1);
        wcmd[WC_PATH_CAP - 1] = L'\0';
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;

    if (CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    if (GetLastError() == ERROR_ELEVATION_REQUIRED) {
        wchar_t exe[WC_PATH_CAP];
        const wchar_t *args = NULL;
        split_cmd_exe_args(wcmd, exe, WC_PATH_CAP, &args);
        if (exe[0]) {
            INT_PTR r = (INT_PTR)ShellExecuteW(NULL, L"runas", exe,
                                               (args && args[0]) ? args : NULL, NULL, SW_SHOWNORMAL);
            return r > 32;
        }
    }
    return false;
}
