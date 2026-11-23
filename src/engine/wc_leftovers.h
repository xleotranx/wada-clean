#ifndef WC_LEFTOVERS_H
#define WC_LEFTOVERS_H

#include <stdbool.h>
#include <stddef.h>

#include "wc_quarantine.h"
#include "wc_types.h"

typedef struct wc_leftover_snapshot {
    wc_job_state state;
    char app_name[192];
    int count;
    wc_leftover_item items[WC_MAX_LEFTOVERS];
    char output_summary[192];
    char error_msg[192];
} wc_leftover_snapshot;

typedef struct wc_mutex wc_mutex;
typedef struct wc_thread wc_thread;

typedef struct wc_leftover_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;
    int job_op; /* 1 = scan, 2 = quarantine */
    wc_app_item job_app;
    char job_paths[WC_MAX_LEFTOVERS][WC_PATH_CAP];
    int job_path_count;
    wc_quarantine *quarantine;
    wc_leftover_snapshot *snap;
} wc_leftover_worker;

/* Pure C99 matching helpers (unit-testable offline). */
bool wc_leftover_is_generic_name(const char *name);
int wc_leftover_match_name(const char *app_name, const char *publisher, const char *candidate_name);

/* Scans explicit root directories (plus app->install_location if user-writable
 * and safe) for candidate leftover directories/files matching `app`. */
bool wc_leftover_scan_roots(const wc_app_item *app, const char *const *roots, int root_count,
                              const volatile int *cancel_flag, wc_leftover_item *out, int max_items,
                              int *out_count);

/* Scans standard OS app-data roots (`LOCALAPPDATA`, `APPDATA`, `PROGRAMDATA` on Windows,
 * `~/Library/Application Support`, `~/Library/Caches` on macOS). */
bool wc_leftover_scan_app(const wc_app_item *app, const volatile int *cancel_flag,
                            wc_leftover_item *out, int max_items, int *out_count);

bool wc_leftover_worker_init(wc_leftover_worker *w);
void wc_leftover_worker_destroy(wc_leftover_worker *w);
void wc_leftover_worker_set_quarantine(wc_leftover_worker *w, wc_quarantine *q);
void wc_leftover_worker_cancel(wc_leftover_worker *w);
bool wc_leftover_worker_poll(wc_leftover_worker *w, wc_leftover_snapshot *out);
bool wc_leftover_worker_start_scan(wc_leftover_worker *w, const wc_app_item *app);
bool wc_leftover_worker_start_quarantine(wc_leftover_worker *w, const char *const *paths,
                                         int count);

#endif /* WC_LEFTOVERS_H */
