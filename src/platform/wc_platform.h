#ifndef WC_PLATFORM_H
#define WC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wc_types.h"

typedef struct wc_mutex wc_mutex;
typedef struct wc_thread wc_thread;
typedef void (*wc_thread_fn)(void *user);
typedef void (*wc_disk_progress_fn)(const char *current_dir, uint64_t files, uint64_t bytes,
                                    void *user);
typedef bool (*wc_exclude_fn)(const char *path, void *user);

/* Environment & privilege helpers */
bool wc_plat_get_env(const char *name, char *out, size_t cap);
bool wc_plat_is_admin(void);
bool wc_plat_relaunch_as_admin(void);
bool wc_plat_process_running(const char *exe_name);

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

/* Scheduled cleaning & system tray */
bool wc_plat_schedule_query(wc_schedule_info *out);
bool wc_plat_schedule_set(wc_schedule_freq freq, int hour, int minute);
bool wc_plat_tray_show(const char *tooltip);
void wc_plat_tray_hide(void);
void wc_plat_tray_notify(const char *title, const char *message);

#endif /* WC_PLATFORM_H */
