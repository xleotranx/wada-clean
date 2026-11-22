#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"

#define WC_TASK_NAME L"WadaCleanScheduled"
#define WC_TRAY_UID 1001u
#define WC_WM_TRAY (WM_APP + 42)

static HWND g_tray_hwnd = NULL;
static bool g_tray_active = false;

static bool utf8_to_wide(const char *src, wchar_t *dst, int cap) {
    if (!src || !dst || cap <= 0) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, cap);
    return n > 0 && n <= cap;
}

static bool run_schtasks(const wchar_t *args, char *out_buf, size_t out_cap, DWORD *out_code) {
    if (out_buf && out_cap > 0) out_buf[0] = '\0';
    if (out_code) *out_code = 1;

    wchar_t wcmd[1024];
    _snwprintf(wcmd, 1024, L"schtasks.exe %ls", args);
    wcmd[1023] = L'\0';

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    HANDLE rpipe = NULL, wpipe = NULL;
    if (!CreatePipe(&rpipe, &wpipe, &sa, 0)) return false;
    SetHandleInformation(rpipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wpipe;
    si.hStdError = wpipe;

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(wpipe);
    if (!ok) {
        CloseHandle(rpipe);
        return false;
    }

    size_t pos = 0;
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(rpipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            char tmp[1024];
            DWORD to_read = avail < sizeof tmp ? avail : (DWORD)sizeof tmp;
            DWORD got = 0;
            if (!ReadFile(rpipe, tmp, to_read, &got, NULL) || got == 0) break;
            if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                size_t cp = got;
                if (pos + cp >= out_cap) cp = (out_cap - 1) - pos;
                memcpy(out_buf + pos, tmp, cp);
                pos += cp;
            }
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(rpipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                char tmp[1024];
                DWORD to_read = avail < sizeof tmp ? avail : (DWORD)sizeof tmp;
                DWORD got = 0;
                if (!ReadFile(rpipe, tmp, to_read, &got, NULL) || got == 0) break;
                if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                    size_t cp = got;
                    if (pos + cp >= out_cap) cp = (out_cap - 1) - pos;
                    memcpy(out_buf + pos, tmp, cp);
                    pos += cp;
                }
            }
            break;
        }
    }
    if (out_buf && out_cap > 0) out_buf[pos] = '\0';

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (out_code) *out_code = code;

    CloseHandle(rpipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool extract_list_field(const char *text, const char *prefix, char *out, size_t cap) {
    size_t plen = strlen(prefix);
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && p[len - 1] == '\r') len--;

        if (len > plen && _strnicmp(p, prefix, plen) == 0) {
            const char *v = p + plen;
            size_t vlen = len - plen;
            while (vlen > 0 && (*v == ' ' || *v == '\t')) {
                v++;
                vlen--;
            }
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

bool wc_plat_schedule_query(wc_schedule_info *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->freq = WC_SCHED_OFF;
    out->hour = 3;
    out->minute = 0;

    char buf[8192];
    DWORD code = 1;
    if (!run_schtasks(L"/Query /TN \"" WC_TASK_NAME L"\" /FO LIST /V", buf, sizeof buf, &code)) {
        return false;
    }
    if (code != 0) {
        out->active = false;
        out->freq = WC_SCHED_OFF;
        return true;
    }

    out->active = true;
    out->freq = WC_SCHED_DAILY;

    char sched_type[64];
    if (extract_list_field(buf, "Schedule Type:", sched_type, sizeof sched_type)) {
        if (strstr(sched_type, "Weekly") || strstr(sched_type, "WEEKLY")) {
            out->freq = WC_SCHED_WEEKLY;
        } else {
            out->freq = WC_SCHED_DAILY;
        }
    }

    char start_time[32];
    if (extract_list_field(buf, "Start Time:", start_time, sizeof start_time)) {
        int h = 3, m = 0;
        if (sscanf(start_time, "%d:%d", &h, &m) == 2) {
            if (h >= 0 && h <= 23) out->hour = h;
            if (m >= 0 && m <= 59) out->minute = m;
        }
    }

    extract_list_field(buf, "Next Run Time:", out->next_run, sizeof out->next_run);
    return true;
}

bool wc_plat_schedule_set(wc_schedule_freq freq, int hour, int minute) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    if (freq == WC_SCHED_OFF) {
        char buf[1024];
        DWORD code = 1;
        run_schtasks(L"/Delete /TN \"" WC_TASK_NAME L"\" /F", buf, sizeof buf, &code);
        return true;
    }

    wchar_t wexe[WC_PATH_CAP];
    DWORD len = GetModuleFileNameW(NULL, wexe, WC_PATH_CAP);
    if (len == 0 || len >= WC_PATH_CAP) return false;

    const wchar_t *sc = (freq == WC_SCHED_WEEKLY) ? L"WEEKLY /D SUN" : L"DAILY";
    wchar_t args[WC_PATH_CAP + 256];
    _snwprintf(args, WC_PATH_CAP + 256,
               L"/Create /TN \"" WC_TASK_NAME L"\" /TR \"\\\"%ls\\\" --scheduled-clean\" /SC %ls /ST %02d:%02d /F",
               wexe, sc, hour, minute);
    args[WC_PATH_CAP + 255] = L'\0';

    char buf[2048];
    DWORD code = 1;
    if (!run_schtasks(args, buf, sizeof buf, &code)) return false;
    return code == 0;
}

static bool ensure_tray_hwnd(void) {
    if (g_tray_hwnd) return true;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WadaCleanTrayMsg";
    RegisterClassExW(&wc);

    g_tray_hwnd = CreateWindowExW(0, L"WadaCleanTrayMsg", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                  GetModuleHandleW(NULL), NULL);
    return g_tray_hwnd != NULL;
}

bool wc_plat_tray_show(const char *tooltip) {
    if (!ensure_tray_hwnd()) return false;

    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_tray_hwnd;
    nid.uID = WC_TRAY_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WC_WM_TRAY;
    nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512)); /* IDI_APPLICATION */
    if (tooltip && tooltip[0]) {
        utf8_to_wide(tooltip, nid.szTip, 128);
    } else {
        wcscpy(nid.szTip, L"Wada Clean");
    }

    BOOL ok = Shell_NotifyIconW(g_tray_active ? NIM_MODIFY : NIM_ADD, &nid);
    if (ok) g_tray_active = true;
    return ok != FALSE;
}

void wc_plat_tray_hide(void) {
    if (g_tray_active && g_tray_hwnd) {
        NOTIFYICONDATAW nid;
        memset(&nid, 0, sizeof nid);
        nid.cbSize = sizeof nid;
        nid.hWnd = g_tray_hwnd;
        nid.uID = WC_TRAY_UID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_tray_active = false;
    }
    if (g_tray_hwnd) {
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = NULL;
    }
}

void wc_plat_tray_notify(const char *title, const char *message) {
    if (!g_tray_active || !g_tray_hwnd) return;

    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_tray_hwnd;
    nid.uID = WC_TRAY_UID;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    if (title && title[0]) utf8_to_wide(title, nid.szInfoTitle, 64);
    if (message && message[0]) utf8_to_wide(message, nid.szInfo, 256);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
