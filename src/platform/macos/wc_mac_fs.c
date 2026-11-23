#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "wc_disk.h"
#include "wc_platform.h"
#include "wc_safety.h"
#include "wc_types.h"

#define WC_WALK_MAX_DEPTH 64

static bool join_child_posix(char *out, size_t cap, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    bool has_sep = (dlen > 0 && dir[dlen - 1] == '/');
    size_t need = dlen + (has_sep ? 0 : 1) + nlen + 1;
    if (need > cap) return false;
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (!has_sep) out[pos++] = '/';
    memcpy(out + pos, name, nlen + 1);
    return true;
}

static bool prepare_root(const char *root, char *norm_root, size_t cap, bool *out_missing) {
    *out_missing = false;
    if (!wc_safety_normalize_path(root, norm_root, cap)) return false;
    if (!wc_safety_is_valid_target_root(norm_root)) return false;

    struct stat st;
    if (lstat(norm_root, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            *out_missing = true;
            return true;
        }
        return false;
    }
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) return false;
    return true;
}

static bool is_safe_component_posix(const char *name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return strchr(name, '/') == NULL;
}

static int posix_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int posix_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static bool walk_dir(const char *dir, bool clean, bool dry_run, const volatile int *cancel_flag,
                     wc_exclude_fn is_excluded, void *excl_user, wc_quarantine_fn on_quarantine,
                     void *quar_user, uint64_t *files, uint64_t *bytes, uint64_t *skipped,
                     int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (depth == 0 && (errno == EACCES || errno == EPERM)) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!is_safe_component_posix(de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }

        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (is_excluded && is_excluded(child, excl_user)) {
            continue;
        }

        struct stat st;
        if (lstat(child, &st) != 0) {
            if (skipped) (*skipped)++;
            continue;
        }
        /* Never traverse or delete symlinks. */
        if (S_ISLNK(st.st_mode)) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!walk_dir(child, clean, dry_run, cancel_flag, is_excluded, excl_user,
                          on_quarantine, quar_user, files, bytes, skipped, depth + 1)) {
                ok = false;
                break;
            }
            if (clean && !dry_run) {
                (void)rmdir(child);
            }
        } else if (S_ISREG(st.st_mode)) {
            uint64_t sz = st.st_size > 0 ? (uint64_t)st.st_size : 0;
            if (!clean || dry_run) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else if (on_quarantine) {
                if (on_quarantine(child, quar_user)) {
                    if (files) (*files)++;
                    if (bytes) (*bytes) += sz;
                } else {
                    if (skipped) (*skipped)++;
                }
            } else {
                if (unlink(child) == 0) {
                    if (files) (*files)++;
                    if (bytes) (*bytes) += sz;
                } else {
                    if (skipped) (*skipped)++;
                }
            }
        }
    }

    closedir(dp);
    return ok;
}

static bool walk_thumb_cache(const char *dir, bool clean, bool dry_run,
                             const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                             void *excl_user, wc_quarantine_fn on_quarantine, void *quar_user,
                             uint64_t *files, uint64_t *bytes, uint64_t *skipped) {
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (errno == EACCES || errno == EPERM) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        if (!wc_safety_is_thumbcache_file(de->d_name)) continue;

        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (is_excluded && is_excluded(child, excl_user)) continue;

        struct stat st;
        if (lstat(child, &st) != 0) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode)) continue;

        uint64_t sz = st.st_size > 0 ? (uint64_t)st.st_size : 0;
        if (!clean || dry_run) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else if (on_quarantine) {
            if (on_quarantine(child, quar_user)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        } else if (unlink(child) == 0) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else {
            if (skipped) (*skipped)++;
        }
    }

    closedir(dp);
    return ok;
}

static bool walk_firefox_profiles(const char *dir, bool clean, bool dry_run,
                                  const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                                  void *excl_user, wc_quarantine_fn on_quarantine, void *quar_user,
                                  uint64_t *files, uint64_t *bytes, uint64_t *skipped) {
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (errno == EACCES || errno == EPERM) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    static const char *const cache_subdirs[] = {"cache2", "startupCache", "jumpListCache"};

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;

        char prof[WC_PATH_CAP];
        if (!join_child_posix(prof, sizeof prof, dir, de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (is_excluded && is_excluded(prof, excl_user)) continue;

        struct stat st;
        if (lstat(prof, &st) != 0) continue;
        if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) continue;

        for (size_t s = 0; s < sizeof cache_subdirs / sizeof cache_subdirs[0]; s++) {
            char sub[WC_PATH_CAP];
            if (!join_child_posix(sub, sizeof sub, prof, cache_subdirs[s])) continue;
            if (is_excluded && is_excluded(sub, excl_user)) continue;

            struct stat sst;
            if (lstat(sub, &sst) != 0) continue;
            if (S_ISLNK(sst.st_mode) || !S_ISDIR(sst.st_mode)) continue;

            if (!walk_dir(sub, clean, dry_run, cancel_flag, is_excluded, excl_user, on_quarantine,
                          quar_user, files, bytes, skipped, 1)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    closedir(dp);
    return ok;
}

static bool walk_chromium_profiles(const char *dir, bool clean, bool dry_run,
                                   const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                                   void *excl_user, wc_quarantine_fn on_quarantine,
                                   void *quar_user, uint64_t *files, uint64_t *bytes,
                                   uint64_t *skipped) {
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (errno == EACCES || errno == EPERM) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    static const char *const cache_subdirs[] = {"Cache/Cache_Data", "Code Cache/js"};

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        if (posix_strcasecmp(de->d_name, "Default") != 0 &&
            !(posix_strncasecmp(de->d_name, "Profile ", 8) == 0 && de->d_name[8] != '\0')) {
            continue;
        }

        char prof[WC_PATH_CAP];
        if (!join_child_posix(prof, sizeof prof, dir, de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (is_excluded && is_excluded(prof, excl_user)) continue;

        struct stat st;
        if (lstat(prof, &st) != 0) continue;
        if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) continue;

        for (size_t s = 0; s < sizeof cache_subdirs / sizeof cache_subdirs[0]; s++) {
            char sub[WC_PATH_CAP];
            if (!join_child_posix(sub, sizeof sub, prof, cache_subdirs[s])) continue;
            if (is_excluded && is_excluded(sub, excl_user)) continue;

            struct stat sst;
            if (lstat(sub, &sst) != 0) continue;
            if (S_ISLNK(sst.st_mode) || !S_ISDIR(sst.st_mode)) continue;

            if (!walk_dir(sub, clean, dry_run, cancel_flag, is_excluded, excl_user, on_quarantine,
                          quar_user, files, bytes, skipped, 1)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }

    closedir(dp);
    return ok;
}

static bool walk_old_downloads(const char *dir, bool clean, bool dry_run,
                               const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                               void *excl_user, wc_quarantine_fn on_quarantine, void *quar_user,
                               uint64_t *files, uint64_t *bytes, uint64_t *skipped) {
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (errno == EACCES || errno == EPERM) return false;
        if (skipped) (*skipped)++;
        return true;
    }

    time_t now = time(NULL);
    const time_t thirty_days = 30 * 24 * 3600;

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        if (!wc_safety_is_old_installer_ext(de->d_name)) continue;

        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (is_excluded && is_excluded(child, excl_user)) continue;

        struct stat st;
        if (lstat(child, &st) != 0) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode)) continue;
        if (now <= st.st_mtime || (now - st.st_mtime) < thirty_days) continue;

        uint64_t sz = st.st_size > 0 ? (uint64_t)st.st_size : 0;
        if (!clean || dry_run) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else if (on_quarantine) {
            if (on_quarantine(child, quar_user)) {
                if (files) (*files)++;
                if (bytes) (*bytes) += sz;
            } else {
                if (skipped) (*skipped)++;
            }
        } else if (unlink(child) == 0) {
            if (files) (*files)++;
            if (bytes) (*bytes) += sz;
        } else {
            if (skipped) (*skipped)++;
        }
    }

    closedir(dp);
    return ok;
}

bool wc_plat_scan_target(wc_target_kind kind, const char *root, const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user, uint64_t *out_files,
                         uint64_t *out_bytes) {
    if (out_files) *out_files = 0;
    if (out_bytes) *out_bytes = 0;
    if (kind == WC_TARGET_RECYCLE_BIN) return false;

    char norm_root[WC_PATH_CAP];
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0;
    bool ok = false;
    if (kind == WC_TARGET_THUMB_CACHE) {
        ok = walk_thumb_cache(norm_root, false, false, cancel_flag, is_excluded, excl_user, NULL,
                              NULL, &files, &bytes, NULL);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles(norm_root, false, false, cancel_flag, is_excluded, excl_user,
                                   NULL, NULL, &files, &bytes, NULL);
    } else if (kind == WC_TARGET_CHROMIUM_PROFILES) {
        ok = walk_chromium_profiles(norm_root, false, false, cancel_flag, is_excluded, excl_user,
                                    NULL, NULL, &files, &bytes, NULL);
    } else if (kind == WC_TARGET_OLD_DOWNLOADS) {
        ok = walk_old_downloads(norm_root, false, false, cancel_flag, is_excluded, excl_user, NULL,
                                NULL, &files, &bytes, NULL);
    } else {
        ok = walk_dir(norm_root, false, false, cancel_flag, is_excluded, excl_user, NULL, NULL,
                      &files, &bytes, NULL, 0);
    }
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    return ok;
}

/* The macOS walker does not hand out sample paths yet; the preview panel shows
 * an explanatory empty state on macOS instead of pretending to list files. */
bool wc_plat_scan_target_preview(wc_target_kind kind, const char *root,
                                 const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                                 void *excl_user, wc_sample_fn on_sample, void *sample_user,
                                 int max_samples, uint64_t *out_files, uint64_t *out_bytes) {
    (void)on_sample;
    (void)sample_user;
    (void)max_samples;
    return wc_plat_scan_target(kind, root, cancel_flag, is_excluded, excl_user, out_files,
                               out_bytes);
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
    bool missing = false;
    if (!prepare_root(root, norm_root, sizeof norm_root, &missing)) return false;
    if (missing) return true;

    uint64_t files = 0, bytes = 0, skipped = 0;
    bool ok = false;
    if (kind == WC_TARGET_THUMB_CACHE) {
        ok = walk_thumb_cache(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                              on_quarantine, quar_user, &files, &bytes, &skipped);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                                   on_quarantine, quar_user, &files, &bytes, &skipped);
    } else if (kind == WC_TARGET_CHROMIUM_PROFILES) {
        ok = walk_chromium_profiles(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                                    on_quarantine, quar_user, &files, &bytes, &skipped);
    } else if (kind == WC_TARGET_OLD_DOWNLOADS) {
        ok = walk_old_downloads(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                                on_quarantine, quar_user, &files, &bytes, &skipped);
    } else {
        ok = walk_dir(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user, on_quarantine,
                      quar_user, &files, &bytes, &skipped, 0);
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
                         wc_exclude_fn is_excluded, void *excl_user,
                         uint64_t *out_files, uint64_t *out_bytes) {
    return wc_plat_scan_target(WC_TARGET_DIR_CONTENTS, root, cancel_flag, is_excluded, excl_user,
                               out_files, out_bytes);
}

bool wc_plat_clean_dir_ex(const char *root, bool dry_run, const volatile int *cancel_flag,
                          wc_exclude_fn is_excluded, void *excl_user,
                          uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped) {
    return wc_plat_clean_target(WC_TARGET_DIR_CONTENTS, root, dry_run, cancel_flag, is_excluded,
                                excl_user, out_files, out_bytes, out_skipped);
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

static bool resolve_trash_dir(char *out, size_t cap) {
    char home[WC_PATH_CAP];
    if (!wc_plat_get_env("HOME", home, sizeof home) || !home[0]) return false;
    return join_child_posix(out, cap, home, ".Trash");
}

bool wc_plat_query_recycle_bin(uint64_t *out_files, uint64_t *out_bytes) {
    char trash[WC_PATH_CAP];
    if (!resolve_trash_dir(trash, sizeof trash)) return false;
    volatile int cancel = 0;
    return wc_plat_scan_dir(trash, &cancel, out_files, out_bytes);
}

bool wc_plat_empty_recycle_bin(void) {
    char trash[WC_PATH_CAP];
    if (!resolve_trash_dir(trash, sizeof trash)) return false;
    volatile int cancel = 0;
    uint64_t f = 0, b = 0, s = 0;
    return wc_plat_clean_dir(trash, false, &cancel, &f, &b, &s);
}

static bool analyze_walk(const char *dir, const volatile int *cancel_flag,
                         wc_disk_progress_fn on_progress, void *user, wc_disk_snapshot *out,
                         uint64_t *sub_bytes, uint64_t *sub_files, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) {
        if (depth == 0 && (errno == EACCES || errno == EPERM)) return false;
        out->skipped_count++;
        return true;
    }

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) {
            out->skipped_count++;
            continue;
        }

        struct stat st;
        if (lstat(child, &st) != 0) {
            out->skipped_count++;
            continue;
        }
        if (S_ISLNK(st.st_mode)) continue;

        if (S_ISDIR(st.st_mode)) {
            out->total_dirs++;
            if (depth == 0 && on_progress) {
                on_progress(child, out->total_files, out->total_bytes, user);
            }
            uint64_t cbytes = 0, cfiles = 0;
            if (!analyze_walk(child, cancel_flag, on_progress, user, out, &cbytes, &cfiles,
                              depth + 1)) {
                ok = false;
                break;
            }
            *sub_bytes += cbytes;
            *sub_files += cfiles;

            if (depth == 0) {
                wc_disk_entry entry;
                memset(&entry, 0, sizeof entry);
                snprintf(entry.path, sizeof entry.path, "%s", child);
                snprintf(entry.name, sizeof entry.name, "%.255s", de->d_name);
                entry.bytes = cbytes;
                entry.file_count = cfiles;
                wc_disk_insert_top(out->top_dirs, &out->top_dir_count, WC_DISK_TOP_DIRS, &entry);
            }
        } else if (S_ISREG(st.st_mode)) {
            uint64_t sz = st.st_size > 0 ? (uint64_t)st.st_size : 0;
            out->total_files++;
            out->total_bytes += sz;
            *sub_bytes += sz;
            (*sub_files)++;

            wc_disk_entry fentry;
            memset(&fentry, 0, sizeof fentry);
            snprintf(fentry.path, sizeof fentry.path, "%s", child);
            snprintf(fentry.name, sizeof fentry.name, "%.255s", de->d_name);
            fentry.bytes = sz;
            fentry.file_count = 1;
            wc_disk_insert_top(out->top_files, &out->top_file_count, WC_DISK_TOP_FILES, &fentry);

            if (on_progress && (out->total_files & 255u) == 0) {
                on_progress(dir, out->total_files, out->total_bytes, user);
            }
        }
    }

    closedir(dp);
    return ok;
}

bool wc_plat_analyze_disk(const char *root, const volatile int *cancel_flag,
                          wc_disk_progress_fn on_progress, void *user, wc_disk_snapshot *out) {
    if (!root || !out) return false;

    char norm_root[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm_root, sizeof norm_root)) return false;

    struct stat st;
    if (lstat(norm_root, &st) != 0) return false;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) return false;

    snprintf(out->root_path, sizeof out->root_path, "%s", norm_root);
    uint64_t rb = 0, rf = 0;
    return analyze_walk(norm_root, cancel_flag, on_progress, user, out, &rb, &rf, 0);
}

bool wc_plat_disk_free_space(const char *path, uint64_t *out_free_bytes, uint64_t *out_total_bytes) {
    if (out_free_bytes) *out_free_bytes = 0;
    if (out_total_bytes) *out_total_bytes = 0;
    if (!path || !path[0]) return false;

    struct statvfs sv;
    if (statvfs(path, &sv) != 0) return false;
    uint64_t unit = sv.f_frsize > 0 ? (uint64_t)sv.f_frsize : (uint64_t)sv.f_bsize;
    if (out_free_bytes) *out_free_bytes = (uint64_t)sv.f_bavail * unit;
    if (out_total_bytes) *out_total_bytes = (uint64_t)sv.f_blocks * unit;
    return true;
}

bool wc_plat_open_in_explorer(const char *path, bool select_item) {
    if (!path || !path[0]) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (select_item) {
            char *argv[] = {"open", "-R", (char *)path, NULL};
            execvp("open", argv);
        } else {
            char *argv[] = {"open", (char *)path, NULL};
            execvp("open", argv);
        }
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* No portable C folder picker on macOS yet; the Exclusions view only offers
 * Browse on Windows and falls back to typing a path here. */
bool wc_plat_pick_folder(char *out, size_t cap) {
    (void)cap;
    if (out) out[0] = '\0';
    return false;
}

bool wc_plat_list_dir(const char *dir, wc_dir_entry_fn on_entry, void *user) {
    if (!dir || !dir[0] || !on_entry) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(dir, norm, sizeof norm)) return false;

    struct stat root_st;
    if (lstat(norm, &root_st) != 0 || S_ISLNK(root_st.st_mode) || !S_ISDIR(root_st.st_mode)) {
        return false;
    }
    DIR *dp = opendir(norm);
    if (!dp) return false;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (!is_safe_component_posix(de->d_name)) continue;
        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, norm, de->d_name)) continue;
        struct stat st;
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        bool is_dir = S_ISDIR(st.st_mode);
        uint64_t sz = (!is_dir && st.st_size > 0) ? (uint64_t)st.st_size : 0;
        if (!on_entry(de->d_name, is_dir, sz, user)) break;
    }
    closedir(dp);
    return true;
}

bool wc_plat_ensure_dir(const char *dir) {
    if (!dir || !dir[0]) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(dir, norm, sizeof norm)) return false;
    if (norm[0] != '/') return false;

    char cur[WC_PATH_CAP];
    snprintf(cur, sizeof cur, "%s", norm);
    size_t len = strlen(cur);
    for (size_t i = 1; i <= len; i++) {
        if (cur[i] == '/' || cur[i] == '\0') {
            char saved = cur[i];
            cur[i] = '\0';
            struct stat st;
            if (lstat(cur, &st) != 0) {
                if (mkdir(cur, 0755) != 0 && errno != EEXIST) return false;
            } else if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
                return false;
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

    struct stat st;
    if (lstat(norm, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return true;
        return false;
    }
    if (S_ISLNK(st.st_mode)) return false;
    if (out_exists) *out_exists = true;
    if (S_ISDIR(st.st_mode)) {
        if (out_is_dir) *out_is_dir = true;
        if (out_bytes) {
            uint64_t files = 0, bytes = 0;
            walk_dir(norm, false, false, NULL, NULL, NULL, NULL, NULL, &files, &bytes, NULL, 0);
            *out_bytes = bytes;
        }
    } else if (S_ISREG(st.st_mode)) {
        if (out_bytes) *out_bytes = st.st_size > 0 ? (uint64_t)st.st_size : 0;
    }
    return true;
}

static bool copy_file_posix(const char *src, const char *dst) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return false;
    int out_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return false;
    }
    char buf[16384];
    ssize_t r;
    bool ok = true;
    while ((r = read(in_fd, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out_fd, buf + off, (size_t)(r - off));
            if (w <= 0) {
                ok = false;
                break;
            }
            off += w;
        }
        if (!ok) break;
    }
    if (r < 0) ok = false;
    close(out_fd);
    close(in_fd);
    if (!ok) (void)unlink(dst);
    return ok;
}

static bool copy_tree_posix(const char *src, const char *dst, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return false;
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) return false;
    DIR *dp = opendir(src);
    if (!dp) return false;

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!is_safe_component_posix(de->d_name)) continue;

        char sc[WC_PATH_CAP], dc[WC_PATH_CAP];
        if (!join_child_posix(sc, sizeof sc, src, de->d_name) ||
            !join_child_posix(dc, sizeof dc, dst, de->d_name)) {
            ok = false;
            break;
        }
        struct stat st;
        if (lstat(sc, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!copy_tree_posix(sc, dc, depth + 1)) {
                ok = false;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (!copy_file_posix(sc, dc)) {
                ok = false;
                break;
            }
        }
    }
    closedir(dp);
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

    struct stat st;
    if (lstat(nsrc, &st) != 0 || S_ISLNK(st.st_mode)) return false;
    struct stat dst_st;
    if (lstat(ndst, &dst_st) == 0) return false;

    char parent[WC_PATH_CAP];
    snprintf(parent, sizeof parent, "%s", ndst);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) return false;
    *slash = '\0';
    if (!wc_plat_ensure_dir(parent)) return false;

    if (rename(nsrc, ndst) == 0) return true;
    if (errno == EXDEV) {
        if (S_ISDIR(st.st_mode)) {
            if (!copy_tree_posix(nsrc, ndst, 0)) {
                wc_plat_delete_item(ndst);
                return false;
            }
            return wc_plat_delete_item(nsrc);
        }
        if (S_ISREG(st.st_mode)) {
            if (!copy_file_posix(nsrc, ndst)) return false;
            return unlink(nsrc) == 0;
        }
    }
    return false;
}

bool wc_plat_delete_item(const char *path) {
    if (!path || !path[0]) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return false;

    struct stat st;
    if (lstat(norm, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISLNK(st.st_mode)) return false;
    if (S_ISDIR(st.st_mode)) {
        if (!wc_safety_is_valid_target_root(norm)) return false;
        uint64_t files = 0, bytes = 0, skipped = 0;
        if (!walk_dir(norm, true, false, NULL, NULL, NULL, NULL, NULL, &files, &bytes, &skipped,
                      0)) {
            return false;
        }
        return rmdir(norm) == 0;
    }
    if (!wc_safety_is_quarantine_safe_path(norm)) return false;
    return unlink(norm) == 0;
}

FILE *wc_plat_fopen_read(const char *utf8_path) {
    if (!utf8_path || !utf8_path[0]) return NULL;
    return fopen(utf8_path, "rb");
}

bool wc_plat_fseek64(FILE *fp, uint64_t offset) {
    if (!fp) return false;
    return fseeko(fp, (off_t)offset, SEEK_SET) == 0;
}

static bool walk_reg_files_posix(const char *dir, uint64_t min_bytes,
                                 const volatile int *cancel_flag, wc_file_visit_fn on_file,
                                 void *user, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) return depth != 0 || (errno != EACCES && errno != EPERM);

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) continue;
        struct stat st;
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!walk_reg_files_posix(child, min_bytes, cancel_flag, on_file, user, depth + 1)) {
                ok = false;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            uint64_t sz = st.st_size > 0 ? (uint64_t)st.st_size : 0;
            if (sz >= min_bytes) {
                if (!on_file(child, sz, (long long)st.st_mtime, user)) {
                    ok = false;
                    break;
                }
            }
        }
    }
    closedir(dp);
    return ok;
}

bool wc_plat_walk_regular_files(const char *root, uint64_t min_bytes,
                                const volatile int *cancel_flag, wc_file_visit_fn on_file,
                                void *user) {
    if (!root || !root[0] || !on_file) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;
    struct stat st;
    if (lstat(norm, &st) != 0 || S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) return false;
    return walk_reg_files_posix(norm, min_bytes, cancel_flag, on_file, user, 0);
}

static bool is_dev_artifact_dir_name_posix(const char *name) {
    static const char *const names[] = {
        "node_modules", "target", ".venv", "__pycache__", "build",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (posix_strcasecmp(name, names[i]) == 0) return true;
    }
    return false;
}

static bool measure_dev_dir_posix(const char *dir, const volatile int *cancel_flag, uint64_t *bytes,
                                  long long *newest_mtime, uint64_t *file_cnt, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) return true;
    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) continue;
        struct stat st;
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!measure_dev_dir_posix(child, cancel_flag, bytes, newest_mtime, file_cnt,
                                       depth + 1)) {
                ok = false;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            (*file_cnt)++;
            if (st.st_size > 0) *bytes += (uint64_t)st.st_size;
            if ((long long)st.st_mtime > *newest_mtime) *newest_mtime = (long long)st.st_mtime;
        }
    }
    closedir(dp);
    return ok;
}

static bool walk_stale_dev_posix(const char *dir, long long min_age_sec, long long now_sec,
                                 const volatile int *cancel_flag, wc_stale_dir_fn on_stale,
                                 void *user, int depth) {
    if (depth > WC_WALK_MAX_DEPTH) return true;
    if (cancel_flag && *cancel_flag) return false;

    DIR *dp = opendir(dir);
    if (!dp) return depth != 0 || (errno != EACCES && errno != EPERM);

    bool ok = true;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        if (!is_safe_component_posix(de->d_name)) continue;
        char child[WC_PATH_CAP];
        if (!join_child_posix(child, sizeof child, dir, de->d_name)) continue;
        struct stat st;
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) continue;

        if (is_dev_artifact_dir_name_posix(de->d_name)) {
            if (!wc_safety_is_valid_target_root(child)) continue;
            uint64_t bytes = 0, file_cnt = 0;
            long long newest = 0;
            if (!measure_dev_dir_posix(child, cancel_flag, &bytes, &newest, &file_cnt, 0)) {
                ok = false;
                break;
            }
            if (file_cnt == 0) newest = (long long)st.st_mtime;
            if (newest > 0 && now_sec > newest && (now_sec - newest) >= min_age_sec) {
                if (!on_stale(child, de->d_name, bytes, newest, user)) {
                    ok = false;
                    break;
                }
            }
        } else {
            if (!walk_stale_dev_posix(child, min_age_sec, now_sec, cancel_flag, on_stale, user,
                                      depth + 1)) {
                ok = false;
                break;
            }
        }
    }
    closedir(dp);
    return ok;
}

bool wc_plat_find_stale_dev_dirs(const char *root, long long min_age_sec, long long now_sec,
                                 const volatile int *cancel_flag, wc_stale_dir_fn on_stale,
                                 void *user) {
    if (!root || !root[0] || !on_stale) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;
    struct stat st;
    if (lstat(norm, &st) != 0 || S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) return false;
    return walk_stale_dev_posix(norm, min_age_sec, now_sec, cancel_flag, on_stale, user, 0);
}
