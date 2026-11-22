#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
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

static bool walk_dir(const char *dir, bool clean, bool dry_run, const volatile int *cancel_flag,
                     wc_exclude_fn is_excluded, void *excl_user, uint64_t *files, uint64_t *bytes,
                     uint64_t *skipped, int depth) {
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
            if (!walk_dir(child, clean, dry_run, cancel_flag, is_excluded, excl_user, files,
                          bytes, skipped, depth + 1)) {
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
                             void *excl_user, uint64_t *files, uint64_t *bytes, uint64_t *skipped) {
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
                                  void *excl_user, uint64_t *files, uint64_t *bytes,
                                  uint64_t *skipped) {
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

            if (!walk_dir(sub, clean, dry_run, cancel_flag, is_excluded, excl_user, files, bytes,
                          skipped, 1)) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
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
        ok = walk_thumb_cache(norm_root, false, false, cancel_flag, is_excluded, excl_user, &files,
                              &bytes, NULL);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles(norm_root, false, false, cancel_flag, is_excluded, excl_user,
                                   &files, &bytes, NULL);
    } else {
        ok = walk_dir(norm_root, false, false, cancel_flag, is_excluded, excl_user, &files, &bytes,
                      NULL, 0);
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

bool wc_plat_clean_target(wc_target_kind kind, const char *root, bool dry_run,
                          const volatile int *cancel_flag, wc_exclude_fn is_excluded,
                          void *excl_user, uint64_t *out_files, uint64_t *out_bytes,
                          uint64_t *out_skipped) {
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
        ok = walk_thumb_cache(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user, &files,
                              &bytes, &skipped);
    } else if (kind == WC_TARGET_FIREFOX_PROFILES) {
        ok = walk_firefox_profiles(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user,
                                   &files, &bytes, &skipped);
    } else {
        ok = walk_dir(norm_root, true, dry_run, cancel_flag, is_excluded, excl_user, &files, &bytes,
                      &skipped, 0);
    }
    if (out_files) *out_files = files;
    if (out_bytes) *out_bytes = bytes;
    if (out_skipped) *out_skipped = skipped;
    return ok;
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
