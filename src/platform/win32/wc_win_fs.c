#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_disk.h"
#include "wc_platform.h"
#include "wc_safety.h"
#include "wc_types.h"

#define WC_WALK_MAX_DEPTH 64

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

static bool walk_dir(const char *root_utf8, const char *dir_utf8, bool clean, bool dry_run,
                     const volatile int *cancel_flag, wc_exclude_fn is_excluded, void *excl_user,
                     uint64_t *files, uint64_t *bytes, uint64_t *skipped, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    char pattern_utf8[WC_PATH_CAP];
    int pn = snprintf(pattern_utf8, sizeof pattern_utf8, "%s\\*", dir_utf8);
    if (pn < 0 || (size_t)pn >= sizeof pattern_utf8) return true;

    wchar_t wpattern[WC_PATH_CAP];
    if (!utf8_to_wide(pattern_utf8, wpattern, WC_PATH_CAP)) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileExW(wpattern, FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) {
            return true;
        }
        if (depth == 0 && err == ERROR_ACCESS_DENIED) {
            return false;
        }
        if (skipped) (*skipped)++;
        return true;
    }

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) {
            continue;
        }

        char name_utf8[WC_PATH_CAP];
        if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8)) {
            if (skipped) (*skipped)++;
            continue;
        }

        char child_utf8[WC_PATH_CAP];
        int cn = snprintf(child_utf8, sizeof child_utf8, "%s\\%s", dir_utf8, name_utf8);
        if (cn < 0 || (size_t)cn >= sizeof child_utf8) {
            if (skipped) (*skipped)++;
            continue;
        }

        if (!wc_safety_path_within(root_utf8, child_utf8)) {
            if (skipped) (*skipped)++;
            continue;
        }

        /* Never traverse or delete symlinks or directory junctions. */
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        if (is_excluded && is_excluded(child_utf8, excl_user)) {
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!walk_dir(root_utf8, child_utf8, clean, dry_run, cancel_flag, is_excluded,
                          excl_user, files, bytes, skipped, depth + 1)) {
                ok = false;
                break;
            }
            if (clean && !dry_run) {
                wchar_t wchild[WC_PATH_CAP];
                if (utf8_to_wide(child_utf8, wchild, WC_PATH_CAP)) {
                    RemoveDirectoryW(wchild);
                }
            }
        } else {
            uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
            if (!clean || dry_run) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                wchar_t wchild[WC_PATH_CAP];
                if (!utf8_to_wide(child_utf8, wchild, WC_PATH_CAP)) {
                    if (skipped) (*skipped)++;
                    continue;
                }
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    SetFileAttributesW(wchild, ffd.dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
                }
                if (DeleteFileW(wchild)) {
                    if (files) (*files)++;
                    if (bytes) (*bytes) += sz;
                } else {
                    if (skipped) (*skipped)++;
                }
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool prepare_root(const char *root, char *norm_root, size_t cap, bool *out_missing) {
    *out_missing = false;
    if (!wc_safety_normalize_path(root, norm_root, cap)) return false;
    if (!wc_safety_is_valid_target_root(norm_root)) return false;

    wchar_t wroot[WC_PATH_CAP];
    if (!utf8_to_wide(norm_root, wroot, WC_PATH_CAP)) return false;

    DWORD attrs = GetFileAttributesW(wroot);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            *out_missing = true;
            return true;
        }
        return false;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
    return true;
}

bool wc_plat_scan_dir_ex(const char *root, const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user,
                         uint64_t *out_files, uint64_t *out_bytes) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;

    char norm_root[WC_PATH_CAP];
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0;
    bool ok = walk_dir(norm_root, norm_root, false, false, cancel_flag, is_excluded, excl_user,
                       &files, &bytes, NULL, 0);
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    return ok;
}

bool wc_plat_clean_dir_ex(const char *root, bool dry_run, const volatile int *cancel_flag,
                          wc_exclude_fn is_excluded, void *excl_user,
                          uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (out_skipped) *out_skipped = 0;

    char norm_root[WC_PATH_CAP];
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0, skipped = 0;
    bool ok = walk_dir(norm_root, norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                       &files, &bytes, &skipped, 0);
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    if (out_skipped) *out_skipped = skipped;
    return ok;
}

bool wc_plat_scan_dir(const char *root, const volatile int *cancel_flag,
                      uint64_t *out_files, uint64_t *out_bytes) {
    return wc_plat_scan_dir_ex(root, cancel_flag, NULL, NULL, out_files, out_bytes);
}

bool wc_plat_clean_dir(const char *root, bool dry_run, const volatile int *cancel_flag,
                       uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped) {
    return wc_plat_clean_dir_ex(root, dry_run, cancel_flag, NULL, NULL, out_files, out_bytes,
                                out_skipped);
}

bool wc_plat_query_recycle_bin(uint64_t *out_files, uint64_t *out_bytes) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;

    SHQUERYRBINFO info;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    HRESULT hr = SHQueryRecycleBinW(NULL, &info);
    if (FAILED(hr)) return false;

    if (out_files) *out_files = info.i64NumItems > 0 ? (uint64_t)info.i64NumItems : 0;
    if (out_bytes) *out_bytes = info.i64Size > 0 ? (uint64_t)info.i64Size : 0;
    return true;
}

bool wc_plat_empty_recycle_bin(void) {
    HRESULT hr = SHEmptyRecycleBinW(
        NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    return SUCCEEDED(hr);
}

static bool join_child_utf8(char *out, size_t cap, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool has_sep = (dlen > 0 && dir[dlen - 1] == '\\');
    size_t need = dlen + (has_sep ? 0 : 1) + nlen + 1;
    if (need > cap) return false;
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (!has_sep) out[pos++] = '\\';
    memcpy(out + pos, name, nlen + 1);
    return true;
}

static bool analyze_walk(const char *dir_utf8, const volatile int *cancel_flag,
                         wc_disk_progress_fn on_progress, void *user, wc_disk_snapshot *out,
                         uint64_t *sub_bytes, uint64_t *sub_files, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    char pat_utf8[WC_PATH_CAP];
    if (!join_child_utf8(pat_utf8, sizeof pat_utf8, dir_utf8, "*")) {
        out->skipped_count++;
        return true;
    }

    wchar_t wpat[WC_PATH_CAP];
    if (!utf8_to_wide(pat_utf8, wpat, WC_PATH_CAP)) {
        out->skipped_count++;
        return true;
    }

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileExW(wpat, FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) return true;
        if (depth == 0 && err == ERROR_ACCESS_DENIED) return false;
        out->skipped_count++;
        return true;
    }

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

        /* Skip junctions and symlinks to prevent double-counting or loops. */
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        char name_utf8[256];
        if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8)) {
            out->skipped_count++;
            continue;
        }

        char child_utf8[WC_PATH_CAP];
        if (!join_child_utf8(child_utf8, sizeof child_utf8, dir_utf8, name_utf8)) {
            out->skipped_count++;
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            out->total_dirs++;
            if (depth == 0 && on_progress) {
                on_progress(child_utf8, out->total_files, out->total_bytes, user);
            }
            uint64_t child_bytes = 0, child_files = 0;
            if (!analyze_walk(child_utf8, cancel_flag, on_progress, user, out, &child_bytes,
                              &child_files, depth + 1)) {
                ok = false;
                break;
            }
            *sub_bytes += child_bytes;
            *sub_files += child_files;

            if (depth == 0) {
                wc_disk_entry entry;
                memset(&entry, 0, sizeof entry);
                snprintf(entry.path, sizeof entry.path, "%s", child_utf8);
                snprintf(entry.name, sizeof entry.name, "%s", name_utf8);
                entry.bytes = child_bytes;
                entry.file_count = child_files;
                wc_disk_insert_top(out->top_dirs, &out->top_dir_count, WC_DISK_TOP_DIRS, &entry);
            }
        } else {
            uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
            out->total_files++;
            out->total_bytes += sz;
            *sub_bytes += sz;
            (*sub_files)++;

            wc_disk_entry fentry;
            memset(&fentry, 0, sizeof fentry);
            snprintf(fentry.path, sizeof fentry.path, "%s", child_utf8);
            snprintf(fentry.name, sizeof fentry.name, "%s", name_utf8);
            fentry.bytes = sz;
            fentry.file_count = 1;
            wc_disk_insert_top(out->top_files, &out->top_file_count, WC_DISK_TOP_FILES, &fentry);

            if (on_progress && (out->total_files & 255u) == 0) {
                on_progress(dir_utf8, out->total_files, out->total_bytes, user);
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

bool wc_plat_analyze_disk(const char *root, const volatile int *cancel_flag,
                          wc_disk_progress_fn on_progress, void *user, wc_disk_snapshot *out) {
    if (!root || !out) return false;

    char norm_root[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm_root, sizeof norm_root)) return false;

    wchar_t wroot[WC_PATH_CAP];
    if (!utf8_to_wide(norm_root, wroot, WC_PATH_CAP)) return false;

    DWORD attrs = GetFileAttributesW(wroot);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;

    snprintf(out->root_path, sizeof out->root_path, "%s", norm_root);
    uint64_t root_bytes = 0, root_files = 0;
    return analyze_walk(norm_root, cancel_flag, on_progress, user, out, &root_bytes, &root_files, 0);
}

bool wc_plat_open_in_explorer(const char *path, bool select_item) {
    if (!path || !path[0]) return false;

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(path, wpath, WC_PATH_CAP)) return false;

    if (select_item) {
        wchar_t args[WC_PATH_CAP + 32];
        _snwprintf(args, WC_PATH_CAP + 32, L"/select,\"%ls\"", wpath);
        args[WC_PATH_CAP + 31] = L'\0';
        return (INT_PTR)ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL) >
               32;
    }
    return (INT_PTR)ShellExecuteW(NULL, L"open", wpath, NULL, NULL, SW_SHOWNORMAL) > 32;
}

bool wc_plat_disk_free_space(const char *path, uint64_t *out_free_bytes, uint64_t *out_total_bytes) {
    if (out_free_bytes) *out_free_bytes = 0;
    if (out_total_bytes) *out_total_bytes = 0;
    if (!path || !path[0]) return false;

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(path, wpath, WC_PATH_CAP)) return false;

    ULARGE_INTEGER avail, total, total_free;
    if (!GetDiskFreeSpaceExW(wpath, &avail, &total, &total_free)) {
        return false;
    }
    if (out_free_bytes) *out_free_bytes = (uint64_t)avail.QuadPart;
    if (out_total_bytes) *out_total_bytes = (uint64_t)total.QuadPart;
    return true;
}
