#ifndef WC_WORKER_H
#define WC_WORKER_H

#include <stdbool.h>

#include "wc_platform.h"
#include "wc_types.h"

typedef struct wc_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;

    wc_target_def job_defs[WC_MAX_TARGETS];
    bool job_selected[WC_MAX_TARGETS];
    int job_target_count;

    char exclusions[WC_MAX_EXCLUSIONS][WC_PATH_CAP];
    int exclusion_count;

    wc_worker_snapshot snap;
} wc_worker;

bool wc_worker_init(wc_worker *w);
void wc_worker_destroy(wc_worker *w);

void wc_worker_set_exclusions(wc_worker *w, const wc_exclusion_item *items, int count);

bool wc_worker_start(wc_worker *w, wc_job_kind kind, bool dry_run,
                     const wc_target_def *defs, const bool *selected, int target_count);
void wc_worker_cancel(wc_worker *w);

/* Copies the latest progress snapshot into `out`.
 * Returns true while the worker job is currently running. */
bool wc_worker_poll(wc_worker *w, wc_worker_snapshot *out);

#endif /* WC_WORKER_H */
