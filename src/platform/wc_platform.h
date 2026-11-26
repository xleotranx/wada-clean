#ifndef WC_PLATFORM_H
#define WC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "wc_types.h"

typedef struct wc_mutex wc_mutex;
typedef struct wc_thread wc_thread;
typedef void (*wc_thread_fn)(void *user);
typedef void (*wc_disk_progress_fn)(const char *current_dir, uint64_t files, uint64_t bytes,
                                    void *user);
typedef bool (*wc_exclude_fn)(const char *path, void *user);
typedef void (*wc_sample_fn)(const char *path, uint64_t bytes, void *user);

/* Environment & privilege helpers */
bool wc_plat_get_env(const char *name, char *out, size_t cap);
bool wc_plat_is_admin(void);
bool wc_plat_relaunch_as_admin(void);
bool wc_plat_process_running(const char *exe_name);
/* Checks which of `exe_names[0..count-1]` are currently running using a single
 * OS process snapshot, writing results into `out_running[0..count-1]`. */
void wc_plat_processes_running(const char *const *exe_names, int count, bool *out_running);

/* Recycle bin */
bool wc_plat_query_recycle_bin(uint64_t *out_files, uint64_t *out_bytes);
bool wc_plat_empty_recycle_bin(void);

/* Filesystem scan and clean inside a validated target root.
 * Never removes `root` itself and never traverses or deletes reparse points
 * (symlinks/junctions). */
bool wc_plat_scan_dir(const char *root, const volatile int *cancel_flag,
                      uint64_t *out_files, uint64_t *out_bytes);
bool wc_plat_clean_dir(const char *root, bool dry_run, const volatile int *cancel_flag,
                       uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped);
bool wc_plat_scan_dir_ex(const char *root, const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user,
                         uint64_t *out_files, uint64_t *out_bytes);
bool wc_plat_clean_dir_ex(const char *root, bool dry_run, const volatile int *cancel_flag,
                          wc_exclude_fn is_excluded, void *excl_user,
                          uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped);
bool wc_plat_scan_target(wc_target_kind kind, const char *root,
                         const volatile int *cancel_flag,
                         wc_exclude_fn is_excluded, void *excl_user,
                         uint64_t *out_files, uint64_t *out_bytes);
/* Same scan, but also hands up to `max_samples` regular file paths (with sizes)
 * to `on_sample`, in walk order. Totals always include every file. */
bool wc_plat_scan_target_preview(wc_target_kind kind, const char *root,
                                 const volatile int *cancel_flag,
                                 wc_exclude_fn is_excluded, void *excl_user,
                                 wc_sample_fn on_sample, void *sample_user, int max_samples,
                                 uint64_t *out_files, uint64_t *out_bytes);
bool wc_plat_clean_target(wc_target_kind kind, const char *root, bool dry_run,
                          const volatile int *cancel_flag,
                          wc_exclude_fn is_excluded, void *excl_user,
                          uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped);

/* Threading & synchronization */
wc_mutex *wc_mutex_create(void);
void wc_mutex_destroy(wc_mutex *m);
void wc_mutex_lock(wc_mutex *m);
void wc_mutex_unlock(wc_mutex *m);

wc_thread *wc_thread_start(wc_thread_fn fn, void *user);
void wc_thread_join(wc_thread *t);

/* Installed applications / uninstaller */
bool wc_plat_list_apps(wc_app_item *out, int max_items, int *out_count);
bool wc_plat_launch_uninstaller(const char *uninstall_cmd);

/* Disk space analyzer & explorer integration */
bool wc_plat_analyze_disk(const char *root, const volatile int *cancel_flag,
                          wc_disk_progress_fn on_progress, void *user, wc_disk_snapshot *out);
bool wc_plat_disk_free_space(const char *path, uint64_t *out_free_bytes, uint64_t *out_total_bytes);
bool wc_plat_open_in_explorer(const char *path, bool select_item);
/* Native folder picker. Returns false when cancelled or unsupported on this
 * platform; `out` is always NUL-terminated. */
bool wc_plat_pick_folder(char *out, size_t cap);

#endif /* WC_PLATFORM_H */
