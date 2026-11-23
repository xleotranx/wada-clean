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
typedef bool (*wc_quarantine_fn)(const char *path, void *user);

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
bool wc_plat_clean_target_ex(wc_target_kind kind, const char *root, bool dry_run,
                             const volatile int *cancel_flag,
                             wc_exclude_fn is_excluded, void *excl_user,
                             wc_quarantine_fn on_quarantine, void *quar_user,
                             uint64_t *out_files, uint64_t *out_bytes, uint64_t *out_skipped);

/* Quarantine / single-item filesystem helpers (skip/reject reparse points). */
typedef bool (*wc_dir_entry_fn)(const char *name, bool is_dir, uint64_t bytes, void *user);
bool wc_plat_list_dir(const char *dir, wc_dir_entry_fn on_entry, void *user);
bool wc_plat_ensure_dir(const char *dir);
bool wc_plat_path_info(const char *path, bool *out_exists, bool *out_is_dir, uint64_t *out_bytes);
bool wc_plat_move_item(const char *src, const char *dst);
bool wc_plat_delete_item(const char *path);

/* Threading & synchronization */
wc_mutex *wc_mutex_create(void);
void wc_mutex_destroy(wc_mutex *m);
void wc_mutex_lock(wc_mutex *m);
void wc_mutex_unlock(wc_mutex *m);

wc_thread *wc_thread_start(wc_thread_fn fn, void *user);
void wc_thread_join(wc_thread *t);

/* Startup manager */
bool wc_plat_list_startup(wc_startup_item *out, int max_items, int *out_count);
bool wc_plat_set_startup_enabled(const wc_startup_item *item, bool enabled);

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

/* UTF-8 file helpers and duplicate/stale dev walkers */
FILE *wc_plat_fopen_read(const char *utf8_path);
bool wc_plat_fseek64(FILE *fp, uint64_t offset);
typedef bool (*wc_file_visit_fn)(const char *path, uint64_t bytes, long long mtime_sec,
                                 void *user);
bool wc_plat_walk_regular_files(const char *root, uint64_t min_bytes,
                                const volatile int *cancel_flag, wc_file_visit_fn on_file,
                                void *user);
typedef bool (*wc_stale_dir_fn)(const char *path, const char *name, uint64_t bytes,
                                long long newest_mtime_sec, void *user);
bool wc_plat_find_stale_dev_dirs(const char *root, long long min_age_sec, long long now_sec,
                                 const volatile int *cancel_flag, wc_stale_dir_fn on_stale,
                                 void *user);

/* Software updater (winget) */
bool wc_plat_winget_available(void);
bool wc_plat_winget_list_upgrades(const volatile int *cancel_flag, wc_update_item *out,
                                  int max_items, int *out_count);
bool wc_plat_winget_upgrade(const char *package_id, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap);

/* Browser cookie database pruning */
bool wc_plat_prune_cookie_db(const char *db_path, bool is_firefox, const char *const *allowlist,
                             int allow_count, bool dry_run, uint64_t *out_removed,
                             uint64_t *out_kept);
/* Enumerates every browser profile that has a SQLite cookie store.
 * Returns the number written (at most max_items); 0 when none are found. */
int wc_plat_list_cookie_dbs(wc_cookie_db *out, int max_items);

/* Scheduled cleaning & system tray */
bool wc_plat_schedule_query(wc_schedule_info *out);
bool wc_plat_schedule_set(wc_schedule_freq freq, int hour, int minute);
bool wc_plat_tray_show(const char *tooltip);
void wc_plat_tray_hide(void);
void wc_plat_tray_notify(const char *title, const char *message);

/* Deep Windows OS maintenance (Wave 2 & Wave 3) */
typedef struct wc_winsxs_info wc_winsxs_info;
typedef struct wc_driver_item wc_driver_item;
typedef struct wc_orphan_msi_item wc_orphan_msi_item;
typedef struct wc_storage_advisor wc_storage_advisor;
typedef struct wc_vhdx_item wc_vhdx_item;

bool wc_plat_dism_analyze(const volatile int *cancel_flag, wc_winsxs_info *out);
bool wc_plat_dism_cleanup(bool reset_base, bool dry_run, const volatile int *cancel_flag,
                          char *out_summary, size_t summary_cap);
bool wc_plat_delivery_opt_clean(bool dry_run, uint64_t *out_freed_bytes, char *out_summary,
                                size_t summary_cap);
bool wc_plat_list_orphan_msi(wc_orphan_msi_item *out, int max_items, int *out_count);
bool wc_plat_list_drivers(const volatile int *cancel_flag, wc_driver_item *out, int max_items,
                          int *out_count);
bool wc_plat_remove_driver(const char *published_name, bool dry_run, char *out_summary,
                           size_t summary_cap);
bool wc_plat_storage_advisor_query(wc_storage_advisor *out);
bool wc_plat_set_compact_os(bool enable, bool dry_run, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap);
bool wc_plat_set_hibernation(bool enable, bool dry_run, char *out_summary, size_t summary_cap);
bool wc_plat_vss_resize(int max_percent, bool dry_run, char *out_summary, size_t summary_cap);
bool wc_plat_create_restore_point(const char *description);
bool wc_plat_list_vhdx(wc_vhdx_item *out, int max_items, int *out_count);
bool wc_plat_compact_vhdx(const char *path, bool dry_run, const volatile int *cancel_flag,
                          uint64_t *out_before, uint64_t *out_after, char *out_summary,
                          size_t summary_cap);

#endif /* WC_PLATFORM_H */
