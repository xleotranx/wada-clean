#ifndef WC_COOKIE_JOB_H
#define WC_COOKIE_JOB_H

#include <stdbool.h>
#include <stdint.h>

#include "wc_platform.h"
#include "wc_types.h"

typedef struct wc_cookie_snapshot {
    wc_job_state state;
    bool dry_run;
    int total_dbs;
    int done_dbs;
    int skipped_browsers;
    int errors;
    uint64_t removed;
    uint64_t kept;
    char current[128];
} wc_cookie_snapshot;

typedef struct wc_cookie_job {
    wc_mutex *mu;
    wc_thread *thread;
    volatile int cancel;
    bool running;
    bool dirty;

    char allow[WC_MAX_COOKIE_ALLOW][192];
    int allow_count;

    wc_cookie_snapshot snap;
} wc_cookie_job;

bool wc_cookie_job_init(wc_cookie_job *j);
void wc_cookie_job_destroy(wc_cookie_job *j);

/* Prunes every discovered browser cookie store that is not protected by the
 * allowlist. Runs on a background thread; browsers in use are skipped unless
 * `dry_run` is set (preview only reports what would go). */
bool wc_cookie_job_start(wc_cookie_job *j, bool dry_run, const wc_cookie_allow_item *items,
                         int count);
void wc_cookie_job_cancel(wc_cookie_job *j);

/* Copies the latest snapshot into `out`. Returns true while the job runs. */
bool wc_cookie_job_poll(wc_cookie_job *j, wc_cookie_snapshot *out);

#endif /* WC_COOKIE_JOB_H */
