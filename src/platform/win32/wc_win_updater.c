#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"
#include "wc_updater.h"

static bool utf8_to_wide(const char *src, wchar_t *dst, int cap) {
    if (!src || !dst || cap <= 0) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, cap);
    return n > 0 && n <= cap;
}

static bool resolve_winget_exe(wchar_t *out, int cap) {
    wchar_t local[WC_PATH_CAP];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, WC_PATH_CAP);
    if (n > 0 && n < WC_PATH_CAP) {
        int pn = _snwprintf(out, cap, L"%ls\\Microsoft\\WindowsApps\\winget.exe", local);
        if (pn > 0 && pn < cap && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) {
            return true;
        }
    }
    DWORD sn = SearchPathW(NULL, L"winget.exe", NULL, (DWORD)cap, out, NULL);
    return sn > 0 && sn < (DWORD)cap;
}

static bool run_hidden_capture(const wchar_t *wcmd_ro, const volatile int *cancel_flag,
                               char *out_buf, size_t out_cap, DWORD *out_exit) {
    if (out_buf && out_cap > 0) out_buf[0] = '\0';
    if (out_exit) *out_exit = 1;

    wchar_t app_path[WC_PATH_CAP];
    if (!resolve_winget_exe(app_path, WC_PATH_CAP)) return false;

    size_t cmd_len = wcslen(wcmd_ro) + 1;
    wchar_t *wcmd = (wchar_t *)malloc(cmd_len * sizeof(wchar_t));
    if (!wcmd) return false;
    memcpy(wcmd, wcmd_ro, cmd_len * sizeof(wchar_t));

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 65536)) {
        free(wcmd);
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;

    BOOL started =
        CreateProcessW(app_path, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(wcmd);
    CloseHandle(write_pipe);

    if (!started) {
        CloseHandle(read_pipe);
        return false;
    }

    size_t pos = 0;
    bool cancelled = false;
    for (;;) {
        if (cancel_flag && *cancel_flag) {
            cancelled = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        bool got_data = false;
        DWORD avail = 0;
        while (PeekNamedPipe(read_pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            char chunk[4096];
            DWORD to_read = avail < sizeof chunk ? avail : (DWORD)sizeof chunk;
            DWORD got = 0;
            if (!ReadFile(read_pipe, chunk, to_read, &got, NULL) || got == 0) break;
            got_data = true;
            if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                size_t copy = got;
                if (pos + copy >= out_cap) copy = (out_cap - 1) - pos;
                memcpy(out_buf + pos, chunk, copy);
                pos += copy;
            }
        }

        DWORD w = WaitForSingleObject(pi.hProcess, got_data ? 0 : 20);
        if (w == WAIT_OBJECT_0) {
            /* Drain remaining buffered stdout. */
            while (PeekNamedPipe(read_pipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                char chunk[4096];
                DWORD to_read = avail < sizeof chunk ? avail : (DWORD)sizeof chunk;
                DWORD got = 0;
                if (!ReadFile(read_pipe, chunk, to_read, &got, NULL) || got == 0) break;
                if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                    size_t copy = got;
                    if (pos + copy >= out_cap) copy = (out_cap - 1) - pos;
                    memcpy(out_buf + pos, chunk, copy);
                    pos += copy;
                }
            }
            break;
        }
    }

    if (out_buf && out_cap > 0) out_buf[pos] = '\0';

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (out_exit) *out_exit = code;

    CloseHandle(read_pipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return !cancelled;
}

static void extract_last_line(const char *text, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!text) return;

    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        const char *seg = p;
        for (size_t i = 0; i < len; i++) {
            if (p[i] == '\r') seg = p + i + 1;
        }
        size_t slen = (size_t)((p + len) - seg);
        while (slen > 0 && (*seg == ' ' || *seg == '\t')) {
            seg++;
            slen--;
        }
        if (slen > 0) {
            if (slen >= cap) slen = cap - 1;
            memcpy(out, seg, slen);
            out[slen] = '\0';
        }
        if (!nl) break;
        p = nl + 1;
    }
}

bool wc_plat_winget_available(void) {
    char buf[256];
    DWORD code = 1;
    if (!run_hidden_capture(L"winget.exe --version", NULL, buf, sizeof buf, &code)) {
        return false;
    }
    return code == 0;
}

bool wc_plat_winget_list_upgrades(const volatile int *cancel_flag, wc_update_item *out,
                                  int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    const size_t cap = 128 * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    DWORD code = 1;
    bool ok = run_hidden_capture(
        L"winget.exe upgrade --accept-source-agreements --disable-interactivity", cancel_flag, buf,
        cap, &code);
    if (!ok) {
        free(buf);
        return false;
    }

    int parsed = wc_updater_parse_winget_table(buf, out, max_items);
    free(buf);
    if (out_count) *out_count = parsed;
    return (code == 0 || parsed > 0);
}

bool wc_plat_winget_upgrade(const char *package_id, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap) {
    if (out_summary && summary_cap > 0) out_summary[0] = '\0';

    wchar_t wcmd[512];
    if (package_id && package_id[0]) {
        if (!wc_updater_is_safe_id(package_id)) return false;
        wchar_t wid[160];
        if (!utf8_to_wide(package_id, wid, 160)) return false;
        _snwprintf(
            wcmd, 512,
            L"winget.exe upgrade --id \"%ls\" --exact --accept-package-agreements --accept-source-agreements --disable-interactivity",
            wid);
        wcmd[511] = L'\0';
    } else {
        wcscpy(
            wcmd,
            L"winget.exe upgrade --all --accept-package-agreements --accept-source-agreements --disable-interactivity");
    }

    const size_t cap = 64 * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    DWORD code = 1;
    bool ok = run_hidden_capture(wcmd, cancel_flag, buf, cap, &code);
    extract_last_line(buf, out_summary, summary_cap);
    free(buf);
    return ok && code == 0;
}
