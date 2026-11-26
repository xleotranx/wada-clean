#ifndef WC_WORKER_H
#define WC_WORKER_H

#include <stdbool.h>

#include "wc_platform.h"
#include "wc_types.h"

#define WC_MAX_PREVIEW 256

typedef struct wc_preview_item {
    char target_id[WC_ID_CAP];
    char path[WC_PATH_CAP];
    uint64_t bytes;
} wc_preview_item;

typedef struct wc_worker {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel_flag;
    bool running;
    bool dirty;

    wc_target_def job_defs[WC_MAX_TARGETS];
    bool job_selected[WC_MAX_TARGETS];
    int job_target_count;

    wc_preview_item previews[WC_MAX_PREVIEW];
    int preview_count;
    bool preview_truncated;

    wc_worker_snapshot snap;
} wc_worker;

bool wc_worker_init(wc_worker *w);
void wc_worker_destroy(wc_worker *w);

bool wc_worker_start(wc_worker *w, wc_job_kind kind, bool dry_run,
                     const wc_target_def *defs, const bool *selected, int target_count);
void wc_worker_cancel(wc_worker *w);

/* Copies the latest progress snapshot into `out`.
 * Returns true while the worker job is currently running. */
bool wc_worker_poll(wc_worker *w, wc_worker_snapshot *out);

/* Copies up to `max` sampled file paths collected by the last analysis. */
int wc_worker_preview_copy(wc_worker *w, wc_preview_item *out, int max);
bool wc_worker_preview_truncated(wc_worker *w);

#endif /* WC_WORKER_H */
