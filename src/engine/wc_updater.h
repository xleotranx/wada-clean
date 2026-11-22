#ifndef WC_UPDATER_H
#define WC_UPDATER_H

#include <stdbool.h>
#include <stddef.h>

#include "wc_platform.h"
#include "wc_types.h"

typedef struct wc_updater_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    wc_updater_op job_op;
    char job_pkg_id[160];
    wc_updater_snapshot *snap;
} wc_updater_worker;

/* Pure parsers for `winget upgrade` and `brew outdated --verbose` stdout. */
int wc_updater_parse_winget_table(const char *text, wc_update_item *out, int max_items);
int wc_updater_parse_brew_outdated(const char *text, wc_update_item *out, int max_items);
bool wc_updater_is_safe_id(const char *id);

/* Async updater worker. */
bool wc_updater_worker_init(wc_updater_worker *w);
void wc_updater_worker_destroy(wc_updater_worker *w);
bool wc_updater_worker_start_check(wc_updater_worker *w);
bool wc_updater_worker_start_upgrade_one(wc_updater_worker *w, const char *package_id);
bool wc_updater_worker_start_upgrade_all(wc_updater_worker *w);
void wc_updater_worker_cancel(wc_updater_worker *w);
bool wc_updater_worker_poll(wc_updater_worker *w, wc_updater_snapshot *out);

#endif /* WC_UPDATER_H */
