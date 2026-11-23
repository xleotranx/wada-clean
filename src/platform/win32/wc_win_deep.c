#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_deep.h"
#include "wc_platform.h"
#include "wc_safety.h"

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

static bool resolve_sys32_tool(const wchar_t *rel_tool, wchar_t *out, int cap) {
    wchar_t sysdir[WC_PATH_CAP];
    UINT n = GetSystemDirectoryW(sysdir, WC_PATH_CAP);
    if (n == 0 || n >= WC_PATH_CAP) return false;
    int pn = _snwprintf(out, cap, L"%ls\\%ls", sysdir, rel_tool);
    if (pn <= 0 || pn >= cap) return false;
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

static bool run_sys_capture(const wchar_t *rel_tool, const wchar_t *args,
                            const volatile int *cancel_flag, char *out_buf, size_t out_cap,
                            DWORD *out_exit) {
    if (out_buf && out_cap > 0) out_buf[0] = '\0';
    if (out_exit) *out_exit = 1;

    wchar_t app_path[WC_PATH_CAP];
    if (!resolve_sys32_tool(rel_tool, app_path, WC_PATH_CAP)) return false;

    size_t cmd_cap = wcslen(app_path) + (args ? wcslen(args) : 0) + 16;
    wchar_t *wcmd = (wchar_t *)malloc(cmd_cap * sizeof(wchar_t));
    if (!wcmd) return false;
    if (args && args[0]) {
        _snwprintf(wcmd, cmd_cap, L"\"%ls\" %ls", app_path, args);
    } else {
        _snwprintf(wcmd, cmd_cap, L"\"%ls\"", app_path);
    }

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

        DWORD w = WaitForSingleObject(pi.hProcess, got_data ? 0 : 25);
        if (w == WAIT_OBJECT_0) {
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
    if (!cancelled) {
        WaitForSingleObject(pi.hProcess, 5000);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    if (out_exit) *out_exit = code;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(read_pipe);
    return !cancelled;
}

static void summarize_output(const char *raw, char *out, size_t cap, const char *fallback) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (raw && raw[0]) {
        const char *best = NULL;
        const char *p = raw;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) {
                len--;
            }
            if (len > 4) best = p;
            if (!nl) break;
            p = nl + 1;
        }
        if (best) {
            size_t i = 0;
            while (best[i] && best[i] != '\n' && best[i] != '\r' && i + 1 < cap) {
                out[i] = best[i];
                i++;
            }
            out[i] = '\0';
            return;
        }
    }
    snprintf(out, cap, "%s", fallback ? fallback : "");
}

bool wc_plat_dism_analyze(const volatile int *cancel_flag, wc_winsxs_info *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    char *buf = (char *)malloc(65536);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(L"DISM.exe", L"/Online /Cleanup-Image /AnalyzeComponentStore",
                               cancel_flag, buf, 65536, &code);
    bool ok = false;
    if (ran && code == 0) {
        ok = wc_deep_parse_dism_analyze(buf, out);
    }
    if (!ok) {
        summarize_output(buf, out->raw_summary, sizeof out->raw_summary,
                         "Requires Administrator to analyze WinSxS");
    }
    free(buf);
    return ok;
}

bool wc_plat_dism_cleanup(bool reset_base, bool dry_run, const volatile int *cancel_flag,
                          char *out_summary, size_t summary_cap) {
    if (dry_run) {
        snprintf(out_summary, summary_cap,
                 reset_base ? "Dry-run: would run DISM /StartComponentCleanup /ResetBase"
                            : "Dry-run: would run DISM /StartComponentCleanup");
        return true;
    }
    char *buf = (char *)malloc(65536);
    if (!buf) return false;
    const wchar_t *args = reset_base ? L"/Online /Cleanup-Image /StartComponentCleanup /ResetBase"
                                     : L"/Online /Cleanup-Image /StartComponentCleanup";
    DWORD code = 1;
    bool ran = run_sys_capture(L"DISM.exe", args, cancel_flag, buf, 65536, &code);
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "WinSxS component cleanup completed"
                                        : "DISM cleanup failed (requires Administrator)");
    free(buf);
    return ran && code == 0;
}

static void sum_dir_shallow_w(const wchar_t *dir_w, uint64_t *files, uint64_t *bytes, int depth) {
    if (depth > 8) return;
    wchar_t pat[WC_PATH_CAP];
    if (_snwprintf(pat, WC_PATH_CAP, L"%ls\\*", dir_w) <= 0) return;
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        wchar_t child[WC_PATH_CAP];
        if (_snwprintf(child, WC_PATH_CAP, L"%ls\\%ls", dir_w, ffd.cFileName) <= 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            sum_dir_shallow_w(child, files, bytes, depth + 1);
        } else {
            (*files)++;
            *bytes += ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
}

static void query_delivery_opt_dir(uint64_t *out_files, uint64_t *out_bytes) {
    *out_files = 0;
    *out_bytes = 0;
    wchar_t win_dir[WC_PATH_CAP];
    if (GetWindowsDirectoryW(win_dir, WC_PATH_CAP) == 0) return;
    wchar_t do_dir[WC_PATH_CAP];
    if (_snwprintf(do_dir, WC_PATH_CAP,
                   L"%ls\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache",
                   win_dir) <= 0) {
        return;
    }
    DWORD attrs = GetFileAttributesW(do_dir);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        sum_dir_shallow_w(do_dir, out_files, out_bytes, 0);
    }
}

bool wc_plat_delivery_opt_clean(bool dry_run, uint64_t *out_freed_bytes, char *out_summary,
                                size_t summary_cap) {
    uint64_t before_files = 0, before_bytes = 0;
    query_delivery_opt_dir(&before_files, &before_bytes);
    if (out_freed_bytes) *out_freed_bytes = before_bytes;

    if (dry_run) {
        char sz[32];
        wc_format_bytes(before_bytes, sz, sizeof sz);
        snprintf(out_summary, summary_cap,
                 "Dry-run: would clear Delivery Optimization cache (%s)", sz);
        return true;
    }

    char *buf = (char *)malloc(16384);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(
        L"WindowsPowerShell\\v1.0\\powershell.exe",
        L"-NoProfile -NonInteractive -Command \"Delete-DeliveryOptimizationCache -Force\"",
        NULL, buf, 16384, &code);
    uint64_t after_files = 0, after_bytes = 0;
    query_delivery_opt_dir(&after_files, &after_bytes);
    if (out_freed_bytes) {
        *out_freed_bytes = (before_bytes > after_bytes) ? (before_bytes - after_bytes) : 0;
    }
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "Cleared Delivery Optimization cache"
                                        : "Delivery Optimization cleanup failed");
    free(buf);
    return ran && code == 0;
}

typedef UINT(WINAPI *pfn_MsiEnumProductsW)(DWORD, LPWSTR);
typedef UINT(WINAPI *pfn_MsiGetProductInfoW)(LPCWSTR, LPCWSTR, LPWSTR, LPDWORD);
typedef UINT(WINAPI *pfn_MsiEnumPatchesExW)(LPCWSTR, LPCWSTR, DWORD, DWORD, DWORD, LPWSTR, LPWSTR,
                                            DWORD *, LPWSTR, LPDWORD);
typedef UINT(WINAPI *pfn_MsiGetPatchInfoExW)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPCWSTR, LPWSTR,
                                             LPDWORD);

bool wc_plat_list_orphan_msi(wc_orphan_msi_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    HMODULE hmsi = LoadLibraryW(L"msi.dll");
    if (!hmsi) return false;

    pfn_MsiEnumProductsW pEnumProducts =
        (pfn_MsiEnumProductsW)(void *)GetProcAddress(hmsi, "MsiEnumProductsW");
    pfn_MsiGetProductInfoW pGetProductInfo =
        (pfn_MsiGetProductInfoW)(void *)GetProcAddress(hmsi, "MsiGetProductInfoW");
    pfn_MsiEnumPatchesExW pEnumPatchesEx =
        (pfn_MsiEnumPatchesExW)(void *)GetProcAddress(hmsi, "MsiEnumPatchesExW");
    pfn_MsiGetPatchInfoExW pGetPatchInfoEx =
        (pfn_MsiGetPatchInfoExW)(void *)GetProcAddress(hmsi, "MsiGetPatchInfoExW");

    if (!pEnumProducts || !pGetProductInfo) {
        FreeLibrary(hmsi);
        return false;
    }

    int ref_cap = 1024;
    int ref_count = 0;
    char **refs = (char **)calloc((size_t)ref_cap, sizeof(char *));
    if (!refs) {
        FreeLibrary(hmsi);
        return false;
    }

    wchar_t prod_code[64];
    for (DWORD i = 0; pEnumProducts(i, prod_code) == ERROR_SUCCESS; i++) {
        wchar_t pkg[WC_PATH_CAP];
        DWORD cch = WC_PATH_CAP;
        if (pGetProductInfo(prod_code, L"LocalPackage", pkg, &cch) == ERROR_SUCCESS && pkg[0]) {
            char utf8[WC_PATH_CAP];
            if (wide_to_utf8(pkg, utf8, sizeof utf8)) {
                if (ref_count >= ref_cap) {
                    int ncap = ref_cap * 2;
                    char **nr = (char **)realloc(refs, (size_t)ncap * sizeof(char *));
                    if (nr) {
                        refs = nr;
                        ref_cap = ncap;
                    }
                }
                if (ref_count < ref_cap) {
                    refs[ref_count++] = _strdup(utf8);
                }
            }
        }
    }

    if (pEnumPatchesEx && pGetPatchInfoEx) {
        wchar_t patch_code[64], target_prod[64], target_sid[128];
        DWORD target_ctx = 0;
        for (DWORD idx = 0;; idx++) {
            DWORD sid_cch = 128;
            UINT rc = pEnumPatchesEx(NULL, L"s-1-1-0", 7, 15, idx, patch_code, target_prod,
                                     &target_ctx, target_sid, &sid_cch);
            if (rc != ERROR_SUCCESS) break;
            wchar_t pkg[WC_PATH_CAP];
            DWORD cch = WC_PATH_CAP;
            if (pGetPatchInfoEx(patch_code, target_prod, target_sid[0] ? target_sid : NULL,
                                target_ctx, L"LocalPackage", pkg, &cch) == ERROR_SUCCESS &&
                pkg[0]) {
                char utf8[WC_PATH_CAP];
                if (wide_to_utf8(pkg, utf8, sizeof utf8)) {
                    if (ref_count >= ref_cap) {
                        int ncap = ref_cap * 2;
                        char **nr = (char **)realloc(refs, (size_t)ncap * sizeof(char *));
                        if (nr) {
                            refs = nr;
                            ref_cap = ncap;
                        }
                    }
                    if (ref_count < ref_cap) {
                        refs[ref_count++] = _strdup(utf8);
                    }
                }
            }
        }
    }
    FreeLibrary(hmsi);

    wchar_t win_dir[WC_PATH_CAP];
    if (GetWindowsDirectoryW(win_dir, WC_PATH_CAP) == 0) {
        for (int i = 0; i < ref_count; i++) free(refs[i]);
        free(refs);
        return false;
    }

    wchar_t inst_dir[WC_PATH_CAP];
    _snwprintf(inst_dir, WC_PATH_CAP, L"%ls\\Installer", win_dir);

    int all_cap = 2048;
    int all_count = 0;
    char **all_paths = (char **)calloc((size_t)all_cap, sizeof(char *));
    uint64_t *all_bytes = (uint64_t *)calloc((size_t)all_cap, sizeof(uint64_t));
    if (!all_paths || !all_bytes) {
        free(all_paths);
        free(all_bytes);
        for (int i = 0; i < ref_count; i++) free(refs[i]);
        free(refs);
        return false;
    }

    wchar_t pat[WC_PATH_CAP];
    _snwprintf(pat, WC_PATH_CAP, L"%ls\\*", inst_dir);
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pat, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
                continue;
            }
            size_t wlen = wcslen(ffd.cFileName);
            if (wlen <= 4) continue;
            if (_wcsicmp(ffd.cFileName + wlen - 4, L".msi") != 0 &&
                _wcsicmp(ffd.cFileName + wlen - 4, L".msp") != 0) {
                continue;
            }

            wchar_t full_w[WC_PATH_CAP];
            _snwprintf(full_w, WC_PATH_CAP, L"%ls\\%ls", inst_dir, ffd.cFileName);
            char full_u8[WC_PATH_CAP];
            if (!wide_to_utf8(full_w, full_u8, sizeof full_u8)) continue;

            if (all_count < all_cap) {
                all_paths[all_count] = _strdup(full_u8);
                all_bytes[all_count] =
                    ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
                all_count++;
            }
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }

    int *orphan_idx = (int *)calloc((size_t)max_items, sizeof(int));
    int orphans = 0;
    if (orphan_idx && all_count > 0) {
        orphans = wc_deep_filter_orphan_msi((const char *const *)all_paths, all_count,
                                            (const char *const *)refs, ref_count, orphan_idx,
                                            max_items);
        for (int i = 0; i < orphans; i++) {
            int idx = orphan_idx[i];
            memset(&out[i], 0, sizeof out[i]);
            snprintf(out[i].path, sizeof out[i].path, "%s", all_paths[idx]);
            const char *slash = strrchr(all_paths[idx], '\\');
            snprintf(out[i].name, sizeof out[i].name, "%s", slash ? slash + 1 : all_paths[idx]);
            out[i].bytes = all_bytes[idx];
            out[i].selected = true;
        }
    }
    if (out_count) *out_count = orphans;

    free(orphan_idx);
    for (int i = 0; i < all_count; i++) free(all_paths[i]);
    free(all_paths);
    free(all_bytes);
    for (int i = 0; i < ref_count; i++) free(refs[i]);
    free(refs);
    return true;
}

bool wc_plat_list_drivers(const volatile int *cancel_flag, wc_driver_item *out, int max_items,
                          int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    const size_t cap = 512 * 1024;
    char *drv_buf = (char *)malloc(cap);
    char *dev_buf = (char *)malloc(cap);
    if (!drv_buf || !dev_buf) {
        free(drv_buf);
        free(dev_buf);
        return false;
    }

    DWORD code1 = 1, code2 = 1;
    bool ok1 = run_sys_capture(L"pnputil.exe", L"/enum-drivers", cancel_flag, drv_buf, cap, &code1);
    bool ok2 = false;
    if (ok1 && !(cancel_flag && *cancel_flag)) {
        ok2 = run_sys_capture(L"pnputil.exe", L"/enum-devices", cancel_flag, dev_buf, cap, &code2);
    }

    bool ok = false;
    if (ok1 && code1 == 0) {
        int n = wc_deep_parse_pnputil_drivers(drv_buf, ok2 ? dev_buf : "", out, max_items);
        if (out_count) *out_count = n;
        ok = true;
    }

    free(drv_buf);
    free(dev_buf);
    return ok;
}

bool wc_plat_remove_driver(const char *published_name, bool dry_run, char *out_summary,
                           size_t summary_cap) {
    if (!wc_deep_is_safe_oem_inf(published_name)) {
        snprintf(out_summary, summary_cap, "Invalid driver package name");
        return false;
    }
    if (dry_run) {
        snprintf(out_summary, summary_cap, "Dry-run: would delete driver %s", published_name);
        return true;
    }
    wchar_t wname[64];
    if (!utf8_to_wide(published_name, wname, 64)) return false;

    wchar_t args[128];
    _snwprintf(args, 128, L"/delete-driver %ls", wname);
    char *buf = (char *)malloc(16384);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(L"pnputil.exe", args, NULL, buf, 16384, &code);
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "Driver package deleted" : "Failed to delete driver");
    free(buf);
    return ran && code == 0;
}

bool wc_plat_storage_advisor_query(wc_storage_advisor *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);

    char *buf = (char *)malloc(32768);
    if (!buf) return false;

    DWORD code = 1;
    if (run_sys_capture(L"compact.exe", L"/compactos:query", NULL, buf, 32768, &code)) {
        out->compact_os_known = wc_deep_parse_compact_query(buf, &out->compact_os_enabled);
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(L"C:\\hiberfil.sys", GetFileExInfoStandard, &fad)) {
        out->hiberfil_present = true;
        out->hiberfil_bytes = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    }

    if (run_sys_capture(L"vssadmin.exe", L"list shadowstorage", NULL, buf, 32768, &code)) {
        wc_deep_parse_vss_storage(buf, out);
    }

    query_delivery_opt_dir(&out->delivery_opt_files, &out->delivery_opt_bytes);
    free(buf);
    return true;
}

bool wc_plat_set_compact_os(bool enable, bool dry_run, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap) {
    if (dry_run) {
        snprintf(out_summary, summary_cap,
                 enable ? "Dry-run: would run compact /compactos:always"
                        : "Dry-run: would run compact /compactos:never");
        return true;
    }
    char *buf = (char *)malloc(32768);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(L"compact.exe", enable ? L"/compactos:always" : L"/compactos:never",
                               cancel_flag, buf, 32768, &code);
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "CompactOS updated"
                                        : "CompactOS update failed (requires Administrator)");
    free(buf);
    return ran && code == 0;
}

bool wc_plat_set_hibernation(bool enable, bool dry_run, char *out_summary, size_t summary_cap) {
    if (dry_run) {
        snprintf(out_summary, summary_cap,
                 enable ? "Dry-run: would enable hibernation" : "Dry-run: would disable hibernation");
        return true;
    }
    char *buf = (char *)malloc(8192);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(L"powercfg.exe", enable ? L"/hibernate on" : L"/hibernate off", NULL,
                               buf, 8192, &code);
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "Hibernation updated"
                                        : "Hibernation change failed (requires Administrator)");
    free(buf);
    return ran && code == 0;
}

bool wc_plat_vss_resize(int max_percent, bool dry_run, char *out_summary, size_t summary_cap) {
    if (max_percent < 1 || max_percent > 50) return false;
    if (dry_run) {
        snprintf(out_summary, summary_cap,
                 "Dry-run: would resize VSS shadow storage on C: to %d%%", max_percent);
        return true;
    }
    wchar_t args[128];
    _snwprintf(args, 128, L"resize shadowstorage /for=C: /on=C: /maxsize=%d%%", max_percent);
    char *buf = (char *)malloc(8192);
    if (!buf) return false;
    DWORD code = 1;
    bool ran = run_sys_capture(L"vssadmin.exe", args, NULL, buf, 8192, &code);
    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "Shadow storage limit updated"
                                        : "VSS resize failed (requires Administrator)");
    free(buf);
    return ran && code == 0;
}

#pragma pack(push, 1)
typedef struct wc_restore_pt_info_w {
    DWORD dwEventType;
    DWORD dwRestorePtType;
    INT64 llSequenceNumber;
    WCHAR szDescription[256];
} wc_restore_pt_info_w;

typedef struct wc_state_mgr_status {
    UINT nStatus;
    INT64 llSequenceNumber;
} wc_state_mgr_status;
#pragma pack(pop)

typedef BOOL(WINAPI *pfn_SRSetRestorePointW)(wc_restore_pt_info_w *, wc_state_mgr_status *);

bool wc_plat_create_restore_point(const char *description) {
    HMODULE hsr = LoadLibraryW(L"srclient.dll");
    if (!hsr) return false;
    pfn_SRSetRestorePointW pSR =
        (pfn_SRSetRestorePointW)(void *)GetProcAddress(hsr, "SRSetRestorePointW");
    if (!pSR) {
        FreeLibrary(hsr);
        return false;
    }

    wc_restore_pt_info_w rp;
    wc_state_mgr_status st;
    memset(&rp, 0, sizeof rp);
    memset(&st, 0, sizeof st);
    rp.dwEventType = 100;     /* BEGIN_SYSTEM_CHANGE */
    rp.dwRestorePtType = 12;  /* MODIFY_SETTINGS */
    if (!utf8_to_wide(description ? description : "Wada Clean Restore Point", rp.szDescription,
                      256)) {
        wcsncpy(rp.szDescription, L"Wada Clean Restore Point", 255);
    }

    BOOL ok = pSR(&rp, &st);
    if (ok) {
        rp.dwEventType = 101; /* END_SYSTEM_CHANGE */
        rp.llSequenceNumber = st.llSequenceNumber;
        pSR(&rp, &st);
    }
    FreeLibrary(hsr);
    return ok != FALSE;
}

static void add_vhdx_if_valid(const wchar_t *wpath, const char *label, wc_vhdx_item *out,
                              int max_items, int *count) {
    if (*count >= max_items) return;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) return;
    if (fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return;
    char utf8[WC_PATH_CAP];
    if (!wide_to_utf8(wpath, utf8, sizeof utf8)) return;

    for (int i = 0; i < *count; i++) {
        if (_stricmp(out[i].path, utf8) == 0) return;
    }

    wc_vhdx_item *it = &out[(*count)++];
    memset(it, 0, sizeof *it);
    snprintf(it->path, sizeof it->path, "%s", utf8);
    snprintf(it->label, sizeof it->label, "%s", label);
    it->bytes = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    it->selected = true;
}

static void find_vhdx_recursive_w(const wchar_t *dir_w, const char *label_prefix,
                                  wc_vhdx_item *out, int max_items, int *count, int depth) {
    if (depth > 3 || *count >= max_items) return;
    wchar_t pat[WC_PATH_CAP];
    if (_snwprintf(pat, WC_PATH_CAP, L"%ls\\*", dir_w) <= 0) return;
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pat, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        wchar_t child[WC_PATH_CAP];
        if (_snwprintf(child, WC_PATH_CAP, L"%ls\\%ls", dir_w, ffd.cFileName) <= 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            find_vhdx_recursive_w(child, label_prefix, out, max_items, count, depth + 1);
        } else {
            size_t len = wcslen(ffd.cFileName);
            if (len > 5 && _wcsicmp(ffd.cFileName + len - 5, L".vhdx") == 0) {
                char name_u8[96];
                char lbl[96];
                if (wide_to_utf8(ffd.cFileName, name_u8, sizeof name_u8)) {
                    snprintf(lbl, sizeof lbl, "%.30s (%.58s)", label_prefix, name_u8);
                    add_vhdx_if_valid(child, lbl, out, max_items, count);
                }
            }
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
}

bool wc_plat_list_vhdx(wc_vhdx_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    wchar_t local[WC_PATH_CAP];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, WC_PATH_CAP);
    if (n == 0 || n >= WC_PATH_CAP) return false;

    int count = 0;

    /* 1. Docker Desktop WSL disks */
    wchar_t docker_wsl[WC_PATH_CAP];
    if (_snwprintf(docker_wsl, WC_PATH_CAP, L"%ls\\Docker\\wsl", local) > 0) {
        find_vhdx_recursive_w(docker_wsl, "Docker Desktop", out, max_items, &count, 0);
    }

    /* 2. WSL Store distributions under %LOCALAPPDATA%\Packages\*\LocalState\ext4.vhdx */
    wchar_t pkgs_pat[WC_PATH_CAP];
    if (_snwprintf(pkgs_pat, WC_PATH_CAP, L"%ls\\Packages\\*", local) > 0) {
        WIN32_FIND_DATAW ffd;
        HANDLE h = FindFirstFileW(pkgs_pat, &ffd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
                if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                wchar_t vhdx[WC_PATH_CAP];
                if (_snwprintf(vhdx, WC_PATH_CAP, L"%ls\\Packages\\%ls\\LocalState\\ext4.vhdx",
                               local, ffd.cFileName) > 0) {
                    char pkg_u8[64];
                    wide_to_utf8(ffd.cFileName, pkg_u8, sizeof pkg_u8);
                    char lbl[96];
                    snprintf(lbl, sizeof lbl, "WSL (%s)", pkg_u8);
                    add_vhdx_if_valid(vhdx, lbl, out, max_items, &count);
                }
            } while (FindNextFileW(h, &ffd) && count < max_items);
            FindClose(h);
        }
    }

    if (out_count) *out_count = count;
    return true;
}

bool wc_plat_compact_vhdx(const char *path, bool dry_run, const volatile int *cancel_flag,
                          uint64_t *out_before, uint64_t *out_after, char *out_summary,
                          size_t summary_cap) {
    if (out_before) *out_before = 0;
    if (out_after) *out_after = 0;
    if (!path || !path[0]) return false;

    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return false;
    size_t len = strlen(norm);
    if (len <= 5 || _stricmp(norm + len - 5, ".vhdx") != 0) return false;
    for (const char *p = norm; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '"') return false;
    }

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wpath, WC_PATH_CAP)) return false;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) return false;
    if (fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }
    uint64_t before = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    if (out_before) *out_before = before;

    if (dry_run) {
        if (out_after) *out_after = before;
        snprintf(out_summary, summary_cap, "Dry-run: would run wsl --shutdown and compact %s",
                 norm);
        return true;
    }

    char *buf = (char *)malloc(32768);
    if (!buf) return false;
    DWORD code = 0;
    run_sys_capture(L"wsl.exe", L"--shutdown", cancel_flag, buf, 32768, &code);
    if (cancel_flag && *cancel_flag) {
        free(buf);
        return false;
    }

    wchar_t temp_dir[WC_PATH_CAP];
    if (GetTempPathW(WC_PATH_CAP, temp_dir) == 0) {
        free(buf);
        return false;
    }
    wchar_t script_path[WC_PATH_CAP];
    _snwprintf(script_path, WC_PATH_CAP, L"%lswada_compact_%lu.txt", temp_dir,
               (unsigned long)GetCurrentProcessId());

    wchar_t dp_wpath[WC_PATH_CAP];
    wcsncpy(dp_wpath, wpath, WC_PATH_CAP - 1);
    dp_wpath[WC_PATH_CAP - 1] = L'\0';
    wchar_t short_w[WC_PATH_CAP];
    DWORD sn = GetShortPathNameW(wpath, short_w, WC_PATH_CAP);
    if (sn > 0 && sn < WC_PATH_CAP) {
        wcsncpy(dp_wpath, short_w, WC_PATH_CAP - 1);
        dp_wpath[WC_PATH_CAP - 1] = L'\0';
    }
    char dp_acp[WC_PATH_CAP];
    if (WideCharToMultiByte(CP_ACP, 0, dp_wpath, -1, dp_acp, sizeof dp_acp, NULL, NULL) <= 0) {
        snprintf(dp_acp, sizeof dp_acp, "%s", norm);
    }

    FILE *fp = _wfopen(script_path, L"wb");
    if (!fp) {
        free(buf);
        return false;
    }
    fprintf(fp,
            "select vdisk file=\"%s\"\r\n"
            "attach vdisk readonly\r\n"
            "compact vdisk\r\n"
            "detach vdisk\r\n"
            "exit\r\n",
            dp_acp);
    fclose(fp);

    wchar_t dp_args[WC_PATH_CAP + 16];
    _snwprintf(dp_args, WC_PATH_CAP + 16, L"/s \"%ls\"", script_path);
    code = 1;
    bool ran = run_sys_capture(L"diskpart.exe", dp_args, cancel_flag, buf, 32768, &code);
    DeleteFileW(script_path);

    uint64_t after = before;
    if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) {
        after = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    }
    if (out_after) *out_after = after;

    summarize_output(buf, out_summary, summary_cap,
                     (ran && code == 0) ? "VHDX compacted"
                                        : "diskpart compact failed (requires Administrator)");
    free(buf);
    return ran && code == 0;
}
