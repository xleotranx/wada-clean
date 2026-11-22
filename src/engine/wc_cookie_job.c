#include "wc_cookie_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cookie_job_entry(void *user) {
    wc_cookie_job *j = (wc_cookie_job *)user;
    wc_cookie_db *dbs = (wc_cookie_db *)calloc(WC_MAX_COOKIE_DBS, sizeof *dbs);
    int db_count = dbs ? wc_plat_list_cookie_dbs(dbs, WC_MAX_COOKIE_DBS) : 0;

    if (!dbs) {
        wc_mutex_lock(j->mu);
        j->snap.errors++;
        j->snap.state = WC_JOB_ERROR;
        j->running = false;
        j->dirty = true;
        wc_mutex_unlock(j->mu);
        return;
    }

    bool dry_run;
    int allow_count;
    wc_mutex_lock(j->mu);
    dry_run = j->snap.dry_run;
    allow_count = j->allow_count;
    j->snap.total_dbs = db_count;
    j->snap.done_dbs = 0;
    j->snap.current[0] = '\0';
    j->dirty = true;
    wc_mutex_unlock(j->mu);

    const char *allow_ptrs[WC_MAX_COOKIE_ALLOW];
    int allow_n = 0;
    for (int i = 0; i < allow_count && i < WC_MAX_COOKIE_ALLOW; i++) {
        if (j->allow[i][0]) allow_ptrs[allow_n++] = j->allow[i];
    }

    /* One process snapshot for all profiles of the same browser. */
    const char *proc_names[WC_MAX_COOKIE_DBS];
    bool proc_running[WC_MAX_COOKIE_DBS];
    bool proc_skipped[WC_MAX_COOKIE_DBS];
    int proc_count = 0;
    memset(proc_skipped, 0, sizeof proc_skipped);
    for (int i = 0; i < db_count; i++) {
        const char *name = dbs[i].process_name;
        if (!name[0]) continue;
        bool known = false;
        for (int k = 0; k < proc_count; k++) {
            if (strcmp(proc_names[k], name) == 0) {
                known = true;
                break;
            }
        }
        if (!known && proc_count < WC_MAX_COOKIE_DBS) {
            proc_names[proc_count++] = name;
        }
    }
    if (proc_count > 0) wc_plat_processes_running(proc_names, proc_count, proc_running);

    for (int i = 0; i < db_count; i++) {
        if (j->cancel) break;

        int proc_idx = -1;
        for (int k = 0; k < proc_count; k++) {
            if (strcmp(proc_names[k], dbs[i].process_name) == 0) {
                proc_idx = k;
                break;
            }
        }

        wc_mutex_lock(j->mu);
        snprintf(j->snap.current, sizeof j->snap.current, "%s · %s", dbs[i].browser,
                 dbs[i].profile);
        j->dirty = true;
        wc_mutex_unlock(j->mu);

        if (proc_idx >= 0 && proc_running[proc_idx]) {
            proc_skipped[proc_idx] = true;
            wc_mutex_lock(j->mu);
            j->snap.done_dbs++;
            j->dirty = true;
            wc_mutex_unlock(j->mu);
            continue;
        }

        uint64_t removed = 0;
        uint64_t kept = 0;
        bool ok = wc_plat_prune_cookie_db(dbs[i].path, dbs[i].is_firefox, allow_ptrs, allow_n,
                                          dry_run, &removed, &kept);

        wc_mutex_lock(j->mu);
        if (ok) {
            j->snap.removed += removed;
            j->snap.kept += kept;
        } else {
            j->snap.errors++;
        }
        j->snap.done_dbs++;
        j->dirty = true;
        wc_mutex_unlock(j->mu);
    }

    int skipped = 0;
    for (int k = 0; k < proc_count; k++) {
        if (proc_skipped[k]) skipped++;
    }

    wc_mutex_lock(j->mu);
    j->snap.skipped_browsers = skipped;
    j->snap.current[0] = '\0';
    j->snap.state = j->cancel ? WC_JOB_CANCELLED : WC_JOB_DONE;
    j->running = false;
    j->dirty = true;
    wc_mutex_unlock(j->mu);

    free(dbs);
}

bool wc_cookie_job_init(wc_cookie_job *j) {
    if (!j) return false;
    memset(j, 0, sizeof *j);
    j->mu = wc_mutex_create();
    if (!j->mu) return false;
    j->snap.state = WC_JOB_IDLE;
    j->dirty = true;
    return true;
}

void wc_cookie_job_destroy(wc_cookie_job *j) {
    if (!j) return;
    wc_cookie_job_cancel(j);
    wc_thread *thread = NULL;
    if (j->mu) {
        wc_mutex_lock(j->mu);
        thread = j->thread;
        j->thread = NULL;
        wc_mutex_unlock(j->mu);
    }
    if (thread) wc_thread_join(thread);
    if (j->mu) {
        wc_mutex_destroy(j->mu);
        j->mu = NULL;
    }
}

bool wc_cookie_job_start(wc_cookie_job *j, bool dry_run, const wc_cookie_allow_item *items,
                         int count) {
    if (!j || !j->mu) return false;

    wc_mutex_lock(j->mu);
    if (j->running) {
        wc_mutex_unlock(j->mu);
        return false;
    }
    wc_thread *old_thread = j->thread;
    j->thread = NULL;
    wc_mutex_unlock(j->mu);
    if (old_thread) wc_thread_join(old_thread);

    wc_mutex_lock(j->mu);
    j->allow_count = 0;
    if (items && count > 0) {
        int n = count > WC_MAX_COOKIE_ALLOW ? WC_MAX_COOKIE_ALLOW : count;
        for (int i = 0; i < n; i++) {
            if (!items[i].domain[0]) continue;
            snprintf(j->allow[j->allow_count], sizeof j->allow[j->allow_count], "%s",
                     items[i].domain);
            j->allow_count++;
        }
    }
    memset(&j->snap, 0, sizeof j->snap);
    j->snap.state = WC_JOB_RUNNING;
    j->snap.dry_run = dry_run;
    j->cancel = 0;
    j->running = true;
    j->dirty = true;

    wc_thread *thread = wc_thread_start(cookie_job_entry, j);
    if (!thread) {
        j->running = false;
        j->snap.state = WC_JOB_ERROR;
        j->dirty = true;
        wc_mutex_unlock(j->mu);
        return false;
    }
    j->thread = thread;
    wc_mutex_unlock(j->mu);
    return true;
}

void wc_cookie_job_cancel(wc_cookie_job *j) {
    if (!j) return;
    j->cancel = 1;
}

bool wc_cookie_job_poll(wc_cookie_job *j, wc_cookie_snapshot *out) {
    if (!j || !j->mu) return false;
    wc_thread *done_thread = NULL;

    wc_mutex_lock(j->mu);
    if (!j->running && j->thread) {
        done_thread = j->thread;
        j->thread = NULL;
        j->dirty = true;
    }
    bool running = j->running;
    if (out && (running || j->dirty || out->state != j->snap.state)) {
        *out = j->snap;
        if (!running) j->dirty = false;
    }
    wc_mutex_unlock(j->mu);

    if (done_thread) wc_thread_join(done_thread);
    return running;
}
