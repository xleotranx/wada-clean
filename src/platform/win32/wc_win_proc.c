#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"
#include "wc_types.h"

struct wc_mutex {
    CRITICAL_SECTION cs;
};

struct wc_thread {
    HANDLE handle;
    wc_thread_fn fn;
    void *user;
};

bool wc_plat_get_env(const char *name, char *out, size_t cap) {
    if (!name || !name[0] || !out || cap == 0) return false;
    out[0] = '\0';

    wchar_t wname[128];
    int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wname, 128);
    if (wn <= 0 || wn > 128) return false;

    wchar_t wval[WC_PATH_CAP];
    DWORD len = GetEnvironmentVariableW(wname, wval, WC_PATH_CAP);
    if (len == 0 || len >= WC_PATH_CAP) return false;

    int un = WideCharToMultiByte(CP_UTF8, 0, wval, -1, out, (int)cap, NULL, NULL);
    return un > 0 && un <= (int)cap;
}

bool wc_plat_is_admin(void) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elev;
    DWORD ret_len = 0;
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof elev, &ret_len)) {
        elevated = (elev.TokenIsElevated != 0);
    }
    CloseHandle(token);
    return elevated;
}

bool wc_plat_relaunch_as_admin(void) {
    wchar_t wexe[WC_PATH_CAP];
    DWORD len = GetModuleFileNameW(NULL, wexe, WC_PATH_CAP);
    if (len == 0 || len >= WC_PATH_CAP) return false;
    return (INT_PTR)ShellExecuteW(NULL, L"runas", wexe, NULL, NULL, SW_SHOWNORMAL) > 32;
}

bool wc_plat_process_running(const char *exe_name) {
    if (!exe_name || !exe_name[0]) return false;

    wchar_t wexe[128];
    int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, exe_name, -1, wexe, 128);
    if (wn <= 0 || wn > 128) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof pe;
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wexe) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

wc_mutex *wc_mutex_create(void) {
    wc_mutex *m = (wc_mutex *)calloc(1, sizeof *m);
    if (!m) return NULL;
    InitializeCriticalSection(&m->cs);
    return m;
}

void wc_mutex_destroy(wc_mutex *m) {
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    free(m);
}

void wc_mutex_lock(wc_mutex *m) {
    if (m) EnterCriticalSection(&m->cs);
}

void wc_mutex_unlock(wc_mutex *m) {
    if (m) LeaveCriticalSection(&m->cs);
}

static DWORD WINAPI win_thread_trampoline(LPVOID param) {
    wc_thread *t = (wc_thread *)param;
    if (t && t->fn) t->fn(t->user);
    return 0;
}

wc_thread *wc_thread_start(wc_thread_fn fn, void *user) {
    if (!fn) return NULL;
    wc_thread *t = (wc_thread *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->user = user;
    t->handle = CreateThread(NULL, 0, win_thread_trampoline, t, 0, NULL);
    if (!t->handle) {
        free(t);
        return NULL;
    }
    return t;
}

void wc_thread_join(wc_thread *t) {
    if (!t) return;
    if (t->handle) {
        WaitForSingleObject(t->handle, INFINITE);
        CloseHandle(t->handle);
    }
    free(t);
}
