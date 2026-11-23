#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
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

#if _WIN32_WINNT >= 0x0601
#define WC_FIND_INFO_LEVEL FindExInfoBasic
#else
#define WC_FIND_INFO_LEVEL FindExInfoStandard
#endif

#ifdef FIND_FIRST_EX_LARGE_FETCH
#define WC_FIND_FLAGS FIND_FIRST_EX_LARGE_FETCH
#else
#define WC_FIND_FLAGS 0
#endif

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

static bool join_child_w(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name) {
    size_t dlen = wcslen(dir);
    size_t nlen = wcslen(name);
    bool has_sep = (dlen > 0 && dir[dlen - 1] == L'\\');
    size_t need = dlen + (has_sep ? 0 : 1) + nlen + 1;
    if (need > cap) return false;
    memcpy(out, dir, dlen * sizeof(wchar_t));
    size_t pos = dlen;
    if (!has_sep) out[pos++] = L'\\';
    memcpy(out + pos, name, (nlen + 1) * sizeof(wchar_t));
    return true;
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

static bool is_safe_component_w(const wchar_t *name) {
    if (!name || !name[0]) return false;
    if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) return false;
    for (const wchar_t *p = name; *p; p++) {
        if (*p == L'\\' || *p == L'/') return false;
    }
    return true;
}

static bool delete_file_w(const wchar_t *path_w, DWORD attrs) {
    if (attrs & FILE_ATTRIBUTE_READONLY) {
        SetFileAttributesW(path_w, attrs & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    }
    return DeleteFileW(path_w) != FALSE;
}

static bool walk_dir_w(const wchar_t *dir_w, const char *dir_utf8, bool clean, bool dry_run,
                       const volatile int *cancel_flag, wc_exclude_fn is_excluded, void *excl_user,
                       wc_quarantine_fn on_quarantine, void *quar_user, uint64_t *files,
                       uint64_t *bytes, uint64_t *skipped, wc_sample_fn sample, void *sample_user,
                       int max_samples, int *sample_count, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpattern[WC_PATH_CAP];
    if (!join_child_w(wpattern, WC_PATH_CAP, dir_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpattern, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
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
        if (!is_safe_component_w(ffd.cFileName)) {
            if (skipped) (*skipped)++;
            continue;
        }

        /* Never traverse or delete symlinks or directory junctions. */
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }

        wchar_t child_w[WC_PATH_CAP];
        if (!join_child_w(child_w, WC_PATH_CAP, dir_w, ffd.cFileName)) {
            if (skipped) (*skipped)++;
            continue;
        }

        char child_utf8[WC_PATH_CAP];
        const char *next_utf8 = NULL;
        if ((is_excluded || sample || on_quarantine) && dir_utf8) {
            char name_utf8[WC_PATH_CAP];
            if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8) ||
                !join_child_utf8(child_utf8, sizeof child_utf8, dir_utf8, name_utf8)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (is_excluded && is_excluded(child_utf8, excl_user)) {
                continue;
            }
            next_utf8 = child_utf8;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!walk_dir_w(child_w, next_utf8, clean, dry_run, cancel_flag, is_excluded,
                            excl_user, on_quarantine, quar_user, files, bytes, skipped, sample,
                            sample_user, max_samples, sample_count, depth + 1)) {
                ok = false;
                break;
            }
            if (clean && !dry_run) {
                RemoveDirectoryW(child_w);
            }
        } else {
            uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
            if (sample && next_utf8 && sample_count && *sample_count < max_samples) {
                sample(next_utf8, sz, sample_user);
                (*sample_count)++;
            }
            if (!clean || dry_run) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else if (on_quarantine) {
                if (next_utf8 && on_quarantine(next_utf8, quar_user)) {
                    if (files) (*files)++;
                    if (bytes) (*bytes) += sz;
                } else {
                    if (skipped) (*skipped)++;
                }
            } else if (delete_file_w(child_w, ffd.dwFileAttributes)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool walk_thumb_cache_w(const wchar_t *root_w, const char *root_utf8, bool clean,
                               bool dry_run, const volatile int *cancel_flag,
                               wc_exclude_fn is_excluded, void *excl_user,
                               wc_quarantine_fn on_quarantine, void *quar_user, uint64_t *files,
                               uint64_t *bytes, uint64_t *skipped, wc_sample_fn sample,
                               void *sample_user, int max_samples, int *sample_count) {
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpattern[WC_PATH_CAP];
    if (!join_child_w(wpattern, WC_PATH_CAP, root_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpattern, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) return true;
        if (err == ERROR_ACCESS_DENIED) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        if (!is_safe_component_w(ffd.cFileName)) continue;

        char name_utf8[WC_PATH_CAP];
        if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8)) continue;
        if (!wc_safety_is_thumbcache_file(name_utf8)) continue;

        char child_utf8[WC_PATH_CAP];
        bool have_child_utf8 = false;
        if ((is_excluded || sample || on_quarantine) && root_utf8) {
            if (!join_child_utf8(child_utf8, sizeof child_utf8, root_utf8, name_utf8)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (is_excluded && is_excluded(child_utf8, excl_user)) continue;
            have_child_utf8 = true;
        }

        uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
        if (sample && have_child_utf8 && sample_count && *sample_count < max_samples) {
            sample(child_utf8, sz, sample_user);
            (*sample_count)++;
        }
        if (!clean || dry_run) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else if (on_quarantine) {
            if (have_child_utf8 && on_quarantine(child_utf8, quar_user)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        } else {
            wchar_t child_w[WC_PATH_CAP];
            if (!join_child_w(child_w, WC_PATH_CAP, root_w, ffd.cFileName)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (delete_file_w(child_w, ffd.dwFileAttributes)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool walk_firefox_profiles_w(const wchar_t *root_w, const char *root_utf8, bool clean,
                                    bool dry_run, const volatile int *cancel_flag,
                                    wc_exclude_fn is_excluded, void *excl_user,
                                    wc_quarantine_fn on_quarantine, void *quar_user,
                                    uint64_t *files, uint64_t *bytes, uint64_t *skipped,
                                    wc_sample_fn sample, void *sample_user, int max_samples,
                                    int *sample_count) {
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpattern[WC_PATH_CAP];
    if (!join_child_w(wpattern, WC_PATH_CAP, root_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpattern, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) return true;
        if (err == ERROR_ACCESS_DENIED) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    static const wchar_t *const cache_subdirs[] = {L"cache2", L"startupCache", L"jumpListCache"};
    static const char *const cache_subdirs_utf8[] = {"cache2", "startupCache", "jumpListCache"};

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;

        wchar_t prof_w[WC_PATH_CAP];
        if (!join_child_w(prof_w, WC_PATH_CAP, root_w, ffd.cFileName)) {
            if (skipped) (*skipped)++;
            continue;
        }

        char prof_utf8[WC_PATH_CAP];
        const char *prof_utf8_ptr = NULL;
        if ((is_excluded || sample || on_quarantine) && root_utf8) {
            char name_utf8[WC_PATH_CAP];
            if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8) ||
                !join_child_utf8(prof_utf8, sizeof prof_utf8, root_utf8, name_utf8)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (is_excluded && is_excluded(prof_utf8, excl_user)) continue;
            prof_utf8_ptr = prof_utf8;
        }

        for (size_t s = 0; s < sizeof cache_subdirs / sizeof cache_subdirs[0]; s++) {
            wchar_t sub_w[WC_PATH_CAP];
            if (!join_child_w(sub_w, WC_PATH_CAP, prof_w, cache_subdirs[s])) continue;

            DWORD attrs = GetFileAttributesW(sub_w);
            if (attrs == INVALID_FILE_ATTRIBUTES) continue;
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            char sub_utf8[WC_PATH_CAP];
            const char *sub_utf8_ptr = NULL;
            if ((is_excluded || sample || on_quarantine) && prof_utf8_ptr) {
                if (!join_child_utf8(sub_utf8, sizeof sub_utf8, prof_utf8_ptr,
                                     cache_subdirs_utf8[s])) {
                    continue;
                }
                if (is_excluded && is_excluded(sub_utf8, excl_user)) continue;
                sub_utf8_ptr = sub_utf8;
            }

            if (!walk_dir_w(sub_w, sub_utf8_ptr, clean, dry_run, cancel_flag, is_excluded,
                            excl_user, on_quarantine, quar_user, files, bytes, skipped, sample,
                            sample_user, max_samples, sample_count, 1)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool is_chromium_profile_dir_w(const wchar_t *name) {
    if (!name || !name[0]) return false;
    if (_wcsicmp(name, L"Default") == 0) return true;
    if (_wcsnicmp(name, L"Profile ", 8) == 0 && name[8] != L'\0') return true;
    return false;
}

static bool walk_chromium_profiles_w(const wchar_t *root_w, const char *root_utf8, bool clean,
                                     bool dry_run, const volatile int *cancel_flag,
                                     wc_exclude_fn is_excluded, void *excl_user,
                                     wc_quarantine_fn on_quarantine, void *quar_user,
                                     uint64_t *files, uint64_t *bytes, uint64_t *skipped,
                                     wc_sample_fn sample, void *sample_user, int max_samples,
                                     int *sample_count) {
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpattern[WC_PATH_CAP];
    if (!join_child_w(wpattern, WC_PATH_CAP, root_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpattern, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) return true;
        if (err == ERROR_ACCESS_DENIED) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    static const wchar_t *const rel_parents[] = {L"Cache", L"Code Cache"};
    static const wchar_t *const rel_leaves[] = {L"Cache_Data", L"js"};
    static const char *const rel_utf8[] = {"Cache\\Cache_Data", "Code Cache\\js"};

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;
        if (!is_chromium_profile_dir_w(ffd.cFileName)) continue;

        wchar_t prof_w[WC_PATH_CAP];
        if (!join_child_w(prof_w, WC_PATH_CAP, root_w, ffd.cFileName)) {
            if (skipped) (*skipped)++;
            continue;
        }

        char prof_utf8[WC_PATH_CAP];
        const char *prof_utf8_ptr = NULL;
        if ((is_excluded || sample || on_quarantine) && root_utf8) {
            char name_utf8[WC_PATH_CAP];
            if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8) ||
                !join_child_utf8(prof_utf8, sizeof prof_utf8, root_utf8, name_utf8)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (is_excluded && is_excluded(prof_utf8, excl_user)) continue;
            prof_utf8_ptr = prof_utf8;
        }

        for (size_t s = 0; s < sizeof rel_parents / sizeof rel_parents[0]; s++) {
            wchar_t mid_w[WC_PATH_CAP];
            if (!join_child_w(mid_w, WC_PATH_CAP, prof_w, rel_parents[s])) continue;
            DWORD mid_attrs = GetFileAttributesW(mid_w);
            if (mid_attrs == INVALID_FILE_ATTRIBUTES ||
                (mid_attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (mid_attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                continue;
            }

            wchar_t sub_w[WC_PATH_CAP];
            if (!join_child_w(sub_w, WC_PATH_CAP, mid_w, rel_leaves[s])) continue;
            DWORD attrs = GetFileAttributesW(sub_w);
            if (attrs == INVALID_FILE_ATTRIBUTES ||
                (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                continue;
            }

            char sub_utf8[WC_PATH_CAP];
            const char *sub_utf8_ptr = NULL;
            if ((is_excluded || sample || on_quarantine) && prof_utf8_ptr) {
                if (!join_child_utf8(sub_utf8, sizeof sub_utf8, prof_utf8_ptr, rel_utf8[s])) {
                    continue;
                }
                if (is_excluded && is_excluded(sub_utf8, excl_user)) continue;
                sub_utf8_ptr = sub_utf8;
            }

            if (!walk_dir_w(sub_w, sub_utf8_ptr, clean, dry_run, cancel_flag, is_excluded,
                            excl_user, on_quarantine, quar_user, files, bytes, skipped, sample,
                            sample_user, max_samples, sample_count, 1)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool walk_old_downloads_w(const wchar_t *root_w, const char *root_utf8, bool clean,
                                 bool dry_run, const volatile int *cancel_flag,
                                 wc_exclude_fn is_excluded, void *excl_user,
                                 wc_quarantine_fn on_quarantine, void *quar_user, uint64_t *files,
                                 uint64_t *bytes, uint64_t *skipped, wc_sample_fn sample,
                                 void *sample_user, int max_samples, int *sample_count) {
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpattern[WC_PATH_CAP];
    if (!join_child_w(wpattern, WC_PATH_CAP, root_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpattern, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) return true;
        if (err == ERROR_ACCESS_DENIED) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    uint64_t now64 = ((uint64_t)now_ft.dwHighDateTime << 32) | (uint64_t)now_ft.dwLowDateTime;
    const uint64_t thirty_days = 30ULL * 24ULL * 3600ULL * 10000000ULL;

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        if (!is_safe_component_w(ffd.cFileName)) continue;

        char name_utf8[WC_PATH_CAP];
        if (!wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8)) continue;
        if (!wc_safety_is_old_installer_ext(name_utf8)) continue;

        uint64_t mtime64 =
            ((uint64_t)ffd.ftLastWriteTime.dwHighDateTime << 32) | (uint64_t)ffd.ftLastWriteTime.dwLowDateTime;
        if (now64 <= mtime64 || (now64 - mtime64) < thirty_days) continue;

        char child_utf8[WC_PATH_CAP];
        bool have_child_utf8 = false;
        if ((is_excluded || sample || on_quarantine) && root_utf8) {
            if (!join_child_utf8(child_utf8, sizeof child_utf8, root_utf8, name_utf8)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (is_excluded && is_excluded(child_utf8, excl_user)) continue;
            have_child_utf8 = true;
        }

        uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
        if (sample && have_child_utf8 && sample_count && *sample_count < max_samples) {
            sample(child_utf8, sz, sample_user);
            (*sample_count)++;
        }
        if (!clean || dry_run) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else if (on_quarantine) {
            if (have_child_utf8 && on_quarantine(child_utf8, quar_user)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        } else {
            wchar_t child_w[WC_PATH_CAP];
            if (!join_child_w(child_w, WC_PATH_CAP, root_w, ffd.cFileName)) {
                if (skipped) (*skipped)++;
                continue;
            }
            if (delete_file_w(child_w, ffd.dwFileAttributes)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

static bool prepare_root(const char *root, char *norm_root, size_t cap, wchar_t *wroot,
                         size_t wcap, bool *out_missing) {
    *out_missing = false;
    if (!wc_safety_normalize_path(root, norm_root, cap)) return false;
    if (!wc_safety_is_valid_target_root(norm_root)) return false;
    if (!utf8_to_wide(norm_root, wroot, (int)wcap)) return false;

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

static bool scan_target_impl(wc_target_kind kind, const char *root,
                             const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                             void *excl_user, wc_sample_fn sample, void *sample_user,
                             int max_samples, int *sample_count, uint64_t *out_files,
                             uint64_t *out_bytes) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (kind == WC_TARGET_RECYCLE_BIN) return false;

    char norm_root[WC_PATH_CAP];
    wchar_t wroot[WC_PATH_CAP];
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, wroot, WC_PATH_CAP, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0;
    bool ok = false;
    if (kind == WC_TARGET_THUMB_CACHE) {
        ok = walk_thumb_cache_w(wroot, norm_root, false, false, cancel_flag, is_excluded, excl_user,
                                NULL, NULL, &files, &bytes, NULL, sample, sample_user, max_samples,
                                sample_count);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles_w(wroot, norm_root, false, false, cancel_flag, is_excluded,
                                     excl_user, NULL, NULL, &files, &bytes, NULL, sample,
                                     sample_user, max_samples, sample_count);
    } else if (kind == WC_TARGET_CHROMIUM_PROFILES) {
        ok = walk_chromium_profiles_w(wroot, norm_root, false, false, cancel_flag, is_excluded,
                                      excl_user, NULL, NULL, &files, &bytes, NULL, sample,
                                      sample_user, max_samples, sample_count);
    } else if (kind == WC_TARGET_OLD_DOWNLOADS) {
        ok = walk_old_downloads_w(wroot, norm_root, false, false, cancel_flag, is_excluded,
                                  excl_user, NULL, NULL, &files, &bytes, NULL, sample, sample_user,
                                  max_samples, sample_count);
    } else {
        ok = walk_dir_w(wroot, norm_root, false, false, cancel_flag, is_excluded, excl_user, NULL,
                        NULL, &files, &bytes, NULL, sample, sample_user, max_samples, sample_count,
                        0);
    }
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    return ok;
}

bool wc_plat_scan_target(wc_target_kind kind, const char *root, const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user, uint64_t *out_files,
                         uint64_t *out_bytes) {
    return scan_target_impl(kind, root, cancel_flag, is_excluded, excl_user, NULL, NULL, 0, NULL,
                            out_files, out_bytes);
}

bool wc_plat_scan_target_preview(wc_target_kind kind, const char *root,
                                 const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                                 void *excl_user, wc_sample_fn on_sample, void *sample_user,
                                 int max_samples, uint64_t *out_files, uint64_t *out_bytes) {
    int samples = 0;
    return scan_target_impl(kind, root, cancel_flag, is_excluded, excl_user, on_sample,
                            sample_user, max_samples, &samples, out_files, out_bytes);
}

bool wc_plat_clean_target_ex(wc_target_kind kind, const char *root, bool dry_run,
                             const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                             void *excl_user, wc_quarantine_fn on_quarantine, void *quar_user,
                             uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (out_skipped) *out_skipped = 0;
    if (kind == WC_TARGET_RECYCLE_BIN) return false;

    char norm_root[WC_PATH_CAP];
    wchar_t wroot[WC_PATH_CAP];
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, wroot, WC_PATH_CAP, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0, skipped = 0;
    bool ok = false;
    if (kind == WC_TARGET_THUMB_CACHE) {
        ok = walk_thumb_cache_w(wroot, norm_root, true, dry_run, cancel_flag, is_excluded,
                                excl_user, on_quarantine, quar_user, &files, &bytes, &skipped, NULL,
                                NULL, 0, NULL);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles_w(wroot, norm_root, true, dry_run, cancel_flag, is_excluded,
                                     excl_user, on_quarantine, quar_user, &files, &bytes, &skipped,
                                     NULL, NULL, 0, NULL);
    } else if (kind == WC_TARGET_CHROMIUM_PROFILES) {
        ok = walk_chromium_profiles_w(wroot, norm_root, true, dry_run, cancel_flag, is_excluded,
                                      excl_user, on_quarantine, quar_user, &files, &bytes, &skipped,
                                      NULL, NULL, 0, NULL);
    } else if (kind == WC_TARGET_OLD_DOWNLOADS) {
        ok = walk_old_downloads_w(wroot, norm_root, true, dry_run, cancel_flag, is_excluded,
                                  excl_user, on_quarantine, quar_user, &files, &bytes, &skipped,
                                  NULL, NULL, 0, NULL);
    } else {
        ok = walk_dir_w(wroot, norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                        on_quarantine, quar_user, &files, &bytes, &skipped, NULL, NULL, 0, NULL, 0);
    }
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    if (out_skipped) *out_skipped = skipped;
    return ok;
}

bool wc_plat_clean_target(wc_target_kind kind, const char *root, bool dry_run,
                          const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                          void *excl_user, uint64_t *out_files, uint64_t *out_bytes,
                          uint64_t *out_skipped) {
    return wc_plat_clean_target_ex(kind, root, dry_run, cancel_flag, is_excluded, excl_user, NULL,
                                   NULL, out_files, out_bytes, out_skipped);
}

bool wc_plat_scan_dir_ex(const char *root, const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user, uint64_t *out_files,
                         uint64_t *out_bytes) {
    return wc_plat_scan_target(WC_TARGET_DIR_CONTENTS, root, cancel_flag, is_excluded, excl_user,
                               out_files, out_bytes);
}

bool wc_plat_clean_dir_ex(const char *root, bool dry_run, const volatile int *cancel_flag,
                          wc_exclude_fn is_excluded, void *excl_user, uint64_t *out_files,
                          uint64_t *out_bytes, uint64_t *out_skipped) {
    return wc_plat_clean_target(WC_TARGET_DIR_CONTENTS, root, dry_run, cancel_flag, is_excluded,
                                excl_user, out_files, out_bytes, out_skipped);
}

bool wc_plat_scan_dir(const char *root, const volatile int *cancel_flag, uint64_t *out_files,
                      uint64_t *out_bytes) {
    return wc_plat_scan_target(WC_TARGET_DIR_CONTENTS, root, cancel_flag, NULL, NULL, out_files,
                               out_bytes);
}

bool wc_plat_clean_dir(const char *root, bool dry_run, const volatile int *cancel_flag,
                       uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped) {
    return wc_plat_clean_target(WC_TARGET_DIR_CONTENTS, root, dry_run, cancel_flag, NULL, NULL,
                                out_files, out_bytes, out_skipped);
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

static bool analyze_walk_w(const wchar_t *dir_w, const char *dir_utf8,
                           const volatile int *cancel_flag, wc_disk_progress_fn on_progress,
                           void *user, wc_disk_snapshot *out, uint64_t *sub_bytes,
                           uint64_t *sub_files, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    wchar_t wpat[WC_PATH_CAP];
    if (!join_child_w(wpat, WC_PATH_CAP, dir_w, L"*")) {
        out->skipped_count++;
        return true;
    }

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(wpat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
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
        if (!is_safe_component_w(ffd.cFileName)) {
            out->skipped_count++;
            continue;
        }

        wchar_t child_w[WC_PATH_CAP];
        if (!join_child_w(child_w, WC_PATH_CAP, dir_w, ffd.cFileName)) {
            out->skipped_count++;
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            out->total_dirs++;
            char name_utf8[256];
            char child_utf8[WC_PATH_CAP];
            const char *next_utf8 = NULL;

            if (dir_utf8 && wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8) &&
                join_child_utf8(child_utf8, sizeof child_utf8, dir_utf8, name_utf8)) {
                next_utf8 = child_utf8;
            }
            if (depth == 0 && on_progress && next_utf8) {
                on_progress(next_utf8, out->total_files, out->total_bytes, user);
            }
            uint64_t child_bytes = 0, child_files = 0;
            if (!analyze_walk_w(child_w, next_utf8, cancel_flag, on_progress, user, out,
                                &child_bytes, &child_files, depth + 1)) {
                ok = false;
                break;
            }
            *sub_bytes += child_bytes;
            *sub_files += child_files;

            if (depth == 0 && next_utf8) {
                wc_disk_entry entry;
                memset(&entry, 0, sizeof entry);
                snprintf(entry.path, sizeof entry.path, "%s", next_utf8);
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

            if (out->top_file_count < WC_DISK_TOP_FILES ||
                sz > out->top_files[WC_DISK_TOP_FILES - 1].bytes) {
                char name_utf8[256];
                char child_utf8[WC_PATH_CAP];
                if (dir_utf8 && wide_to_utf8(ffd.cFileName, name_utf8, sizeof name_utf8) &&
                    join_child_utf8(child_utf8, sizeof child_utf8, dir_utf8, name_utf8)) {
                    wc_disk_entry fentry;
                    memset(&fentry, 0, sizeof fentry);
                    snprintf(fentry.path, sizeof fentry.path, "%s", child_utf8);
                    snprintf(fentry.name, sizeof fentry.name, "%s", name_utf8);
                    fentry.bytes = sz;
                    fentry.file_count = 1;
                    wc_disk_insert_top(out->top_files, &out->top_file_count, WC_DISK_TOP_FILES,
                                       &fentry);
                }
            }

            if (on_progress && dir_utf8 && (out->total_files & 255u) == 0) {
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
    return analyze_walk_w(wroot, norm_root, cancel_flag, on_progress, user, out, &root_bytes,
                          &root_files, 0);
}

bool wc_plat_open_in_explorer(const char *path, bool select_item) {
    if (!path || !path[0]) return false;

    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return false;

    for (const char *p = norm; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '"') return false;
    }

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wpath, WC_PATH_CAP)) return false;

    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;

    if (select_item) {
        wchar_t win_dir[WC_PATH_CAP];
        UINT wlen = GetWindowsDirectoryW(win_dir, WC_PATH_CAP);
        if (wlen == 0 || wlen >= WC_PATH_CAP) return false;

        wchar_t explorer_path[WC_PATH_CAP];
        int en = _snwprintf(explorer_path, WC_PATH_CAP, L"%ls\\explorer.exe", win_dir);
        if (en <= 0 || en >= WC_PATH_CAP) return false;

        wchar_t args[WC_PATH_CAP + 32];
        int an = _snwprintf(args, WC_PATH_CAP + 32, L"/select,\"%ls\"", wpath);
        if (an <= 0 || an >= WC_PATH_CAP + 32) return false;
        return (INT_PTR)ShellExecuteW(NULL, L"open", explorer_path, args, NULL, SW_SHOWNORMAL) >
               32;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    return (INT_PTR)ShellExecuteW(NULL, L"explore", wpath, NULL, NULL, SW_SHOWNORMAL) > 32;
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

bool wc_plat_list_dir(const char *dir, wc_dir_entry_fn on_entry, void *user) {
    if (!dir || !dir[0] || !on_entry) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(dir, norm, sizeof norm)) return false;
    wchar_t wdir[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wdir, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(wdir);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    wchar_t pat[WC_PATH_CAP];
    if (!join_child_w(pat, WC_PATH_CAP, wdir, L"*")) return false;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(pat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;
        char name_u8[WC_PATH_CAP];
        if (!wide_to_utf8(ffd.cFileName, name_u8, sizeof name_u8)) continue;
        bool is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        uint64_t sz = is_dir ? 0 : (((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow);
        if (!on_entry(name_u8, is_dir, sz, user)) break;
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
    return true;
}

bool wc_plat_ensure_dir(const char *dir) {
    if (!dir || !dir[0]) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(dir, norm, sizeof norm)) return false;

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wpath, WC_PATH_CAP)) return false;

    size_t len = wcslen(wpath);
    if (len < 3) return false;

    /* Walk each segment past "X:\" */
    wchar_t cur[WC_PATH_CAP];
    wcscpy(cur, wpath);
    for (size_t i = 3; i <= len; i++) {
        if (cur[i] == L'\\' || cur[i] == L'\0') {
            wchar_t saved = cur[i];
            cur[i] = L'\0';
            DWORD attrs = GetFileAttributesW(cur);
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(cur, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            } else {
                if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                    (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    return false;
                }
            }
            cur[i] = saved;
        }
    }
    return true;
}

bool wc_plat_path_info(const char *path, bool *out_exists, bool *out_is_dir, uint64_t *out_bytes) {
    if (out_exists) *out_exists = false;
    if (out_is_dir) *out_is_dir = false;
    if (out_bytes) *out_bytes = 0;
    if (!path || !path[0]) return false;

    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return false;

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wpath, WC_PATH_CAP)) return false;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        return false;
    }
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        return false;
    }
    if (out_exists) *out_exists = true;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (out_is_dir) *out_is_dir = true;
        if (out_bytes) {
            uint64_t files = 0, bytes = 0;
            walk_dir_w(wpath, norm, false, false, NULL, NULL, NULL, NULL, NULL, &files, &bytes,
                       NULL, NULL, NULL, 0, NULL, 0);
            *out_bytes = bytes;
        }
    } else {
        if (out_bytes) {
            *out_bytes = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
        }
    }
    return true;
}

static bool copy_dir_tree_w(const wchar_t *src_w, const wchar_t *dst_w, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return false;
    if (!CreateDirectoryW(dst_w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }

    wchar_t pat[WC_PATH_CAP];
    if (!join_child_w(pat, WC_PATH_CAP, src_w, L"*")) return false;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(pat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES);
    }

    bool ok = true;
    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;

        wchar_t s_child[WC_PATH_CAP], d_child[WC_PATH_CAP];
        if (!join_child_w(s_child, WC_PATH_CAP, src_w, ffd.cFileName) ||
            !join_child_w(d_child, WC_PATH_CAP, dst_w, ffd.cFileName)) {
            ok = false;
            break;
        }
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copy_dir_tree_w(s_child, d_child, depth + 1)) {
                ok = false;
                break;
            }
        } else {
            if (!CopyFileW(s_child, d_child, TRUE)) {
                ok = false;
                break;
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

bool wc_plat_move_item(const char *src, const char *dst) {
    if (!src || !dst || !src[0] || !dst[0]) return false;

    char nsrc[WC_PATH_CAP], ndst[WC_PATH_CAP];
    if (!wc_safety_normalize_path(src, nsrc, sizeof nsrc) ||
        !wc_safety_normalize_path(dst, ndst, sizeof ndst)) {
        return false;
    }
    if (!wc_safety_is_quarantine_safe_path(nsrc) || !wc_safety_is_quarantine_safe_path(ndst)) {
        return false;
    }

    wchar_t wsrc[WC_PATH_CAP], wdst[WC_PATH_CAP];
    if (!utf8_to_wide(nsrc, wsrc, WC_PATH_CAP) || !utf8_to_wide(ndst, wdst, WC_PATH_CAP)) {
        return false;
    }

    DWORD sattrs = GetFileAttributesW(wsrc);
    if (sattrs == INVALID_FILE_ATTRIBUTES || (sattrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    if (GetFileAttributesW(wdst) != INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    char parent[WC_PATH_CAP];
    snprintf(parent, sizeof parent, "%s", ndst);
    char *slash = strrchr(parent, '\\');
    if (!slash || slash <= parent + 2) return false;
    *slash = '\0';
    if (!wc_plat_ensure_dir(parent)) return false;

    if (MoveFileExW(wsrc, wdst, MOVEFILE_COPY_ALLOWED)) {
        return true;
    }
    if ((sattrs & FILE_ATTRIBUTE_DIRECTORY) != 0 && GetLastError() == ERROR_NOT_SAME_DEVICE) {
        if (!copy_dir_tree_w(wsrc, wdst, 0)) {
            wc_plat_delete_item(ndst);
            return false;
        }
        return wc_plat_delete_item(nsrc);
    }
    return false;
}

bool wc_plat_delete_item(const char *path) {
    if (!path || !path[0]) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return false;

    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wpath, WC_PATH_CAP)) return false;

    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        return false;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (!wc_safety_is_valid_target_root(norm)) return false;
        uint64_t files = 0, bytes = 0, skipped = 0;
        if (!walk_dir_w(wpath, norm, true, false, NULL, NULL, NULL, NULL, NULL, &files, &bytes,
                        &skipped, NULL, NULL, 0, NULL, 0)) {
            return false;
        }
        return RemoveDirectoryW(wpath) != FALSE;
    }
    if (!wc_safety_is_quarantine_safe_path(norm)) return false;
    return delete_file_w(wpath, attrs);
}

static long long filetime_to_unix_sec(FILETIME ft) {
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
    const uint64_t epoch_diff = 116444736000000000ULL;
    if (ticks <= epoch_diff) return 0;
    return (long long)((ticks - epoch_diff) / 10000000ULL);
}

FILE *wc_plat_fopen_read(const char *utf8_path) {
    if (!utf8_path || !utf8_path[0]) return NULL;
    wchar_t wpath[WC_PATH_CAP];
    if (!utf8_to_wide(utf8_path, wpath, WC_PATH_CAP)) return NULL;
    return _wfopen(wpath, L"rb");
}

bool wc_plat_fseek64(FILE *fp, uint64_t offset) {
    if (!fp) return false;
    return _fseeki64(fp, (__int64)offset, SEEK_SET) == 0;
}

static bool walk_reg_files_w(const wchar_t *dir_w, const char *dir_utf8, uint64_t min_bytes,
                             const volatile int *cancel_flag, wc_file_visit_fn on_file, void *user,
                             int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    wchar_t pat[WC_PATH_CAP];
    if (!join_child_w(pat, WC_PATH_CAP, dir_w, L"*")) return true;

    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(pat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (depth == 0 && err == ERROR_ACCESS_DENIED) return false;
        return true;
    }

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;

        wchar_t child_w[WC_PATH_CAP];
        char name_u8[WC_PATH_CAP];
        char child_u8[WC_PATH_CAP];
        if (!join_child_w(child_w, WC_PATH_CAP, dir_w, ffd.cFileName) ||
            !wide_to_utf8(ffd.cFileName, name_u8, sizeof name_u8) ||
            !join_child_utf8(child_u8, sizeof child_u8, dir_utf8, name_u8)) {
            continue;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!walk_reg_files_w(child_w, child_u8, min_bytes, cancel_flag, on_file, user,
                                  depth + 1)) {
                ok = false;
                break;
            }
        } else {
            uint64_t sz = ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
            if (sz >= min_bytes) {
                long long mt = filetime_to_unix_sec(ffd.ftLastWriteTime);
                if (!on_file(child_u8, sz, mt, user)) {
                    ok = false;
                    break;
                }
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

bool wc_plat_walk_regular_files(const char *root, uint64_t min_bytes,
                                const volatile int *cancel_flag, wc_file_visit_fn on_file,
                                void *user) {
    if (!root || !root[0] || !on_file) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;
    wchar_t wroot[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wroot, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(wroot);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    return walk_reg_files_w(wroot, norm, min_bytes, cancel_flag, on_file, user, 0);
}

static bool is_dev_artifact_dir_name_w(const wchar_t *name) {
    static const wchar_t *const names[] = {
        L"node_modules", L"target", L".venv", L"__pycache__", L"build",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (_wcsicmp(name, names[i]) == 0) return true;
    }
    return false;
}

static bool measure_dev_dir_w(const wchar_t *dir_w, const volatile int *cancel_flag,
                              uint64_t *bytes, long long *newest_mtime, uint64_t *file_cnt,
                              int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    wchar_t pat[WC_PATH_CAP];
    if (!join_child_w(pat, WC_PATH_CAP, dir_w, L"*")) return true;
    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(pat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) return true;

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;

        wchar_t child_w[WC_PATH_CAP];
        if (!join_child_w(child_w, WC_PATH_CAP, dir_w, ffd.cFileName)) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!measure_dev_dir_w(child_w, cancel_flag, bytes, newest_mtime, file_cnt,
                                   depth + 1)) {
                ok = false;
                break;
            }
        } else {
            (*file_cnt)++;
            *bytes += ((uint64_t)ffd.nFileSizeHigh << 32) | (uint64_t)ffd.nFileSizeLow;
            long long mt = filetime_to_unix_sec(ffd.ftLastWriteTime);
            if (mt > *newest_mtime) *newest_mtime = mt;
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
    return ok;
}

static bool walk_stale_dev_w(const wchar_t *dir_w, const char *dir_utf8, long long min_age_sec,
                             long long now_sec, const volatile int *cancel_flag,
                             wc_stale_dir_fn on_stale, void *user, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    wchar_t pat[WC_PATH_CAP];
    if (!join_child_w(pat, WC_PATH_CAP, dir_w, L"*")) return true;
    WIN32_FIND_DATAW ffd;
    HANDLE h =
        FindFirstFileExW(pat, WC_FIND_INFO_LEVEL, &ffd, FindExSearchNameMatch, NULL, WC_FIND_FLAGS);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (depth == 0 && err == ERROR_ACCESS_DENIED) return false;
        return true;
    }

    bool ok = true;
    do {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!is_safe_component_w(ffd.cFileName)) continue;

        wchar_t child_w[WC_PATH_CAP];
        char name_u8[WC_PATH_CAP];
        char child_u8[WC_PATH_CAP];
        if (!join_child_w(child_w, WC_PATH_CAP, dir_w, ffd.cFileName) ||
            !wide_to_utf8(ffd.cFileName, name_u8, sizeof name_u8) ||
            !join_child_utf8(child_u8, sizeof child_u8, dir_utf8, name_u8)) {
            continue;
        }

        if (is_dev_artifact_dir_name_w(ffd.cFileName)) {
            if (!wc_safety_is_valid_target_root(child_u8)) continue;
            uint64_t bytes = 0, file_cnt = 0;
            long long newest = 0;
            if (!measure_dev_dir_w(child_w, cancel_flag, &bytes, &newest, &file_cnt, 0)) {
                ok = false;
                break;
            }
            if (file_cnt == 0) newest = filetime_to_unix_sec(ffd.ftLastWriteTime);
            if (newest > 0 && now_sec > newest && (now_sec - newest) >= min_age_sec) {
                if (!on_stale(child_u8, name_u8, bytes, newest, user)) {
                    ok = false;
                    break;
                }
            }
        } else {
            if (!walk_stale_dev_w(child_w, child_u8, min_age_sec, now_sec, cancel_flag, on_stale,
                                  user, depth + 1)) {
                ok = false;
                break;
            }
        }
    } while (FindNextFileW(h, &ffd));

    FindClose(h);
    return ok;
}

bool wc_plat_find_stale_dev_dirs(const char *root, long long min_age_sec, long long now_sec,
                                 const volatile int *cancel_flag, wc_stale_dir_fn on_stale,
                                 void *user) {
    if (!root || !root[0] || !on_stale) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;
    wchar_t wroot[WC_PATH_CAP];
    if (!utf8_to_wide(norm, wroot, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(wroot);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    return walk_stale_dev_w(wroot, norm, min_age_sec, now_sec, cancel_flag, on_stale, user, 0);
}
