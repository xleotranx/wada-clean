#include "wc_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_safety.h"

void wc_disk_insert_top(wc_disk_entry *arr, int *count, int cap, const wc_disk_entry *cand) {
    if (!arr || !count || cap <= 0 || !cand) return;

    int n = *count;
    if (n > cap) n = cap;

    int pos = n;
    for (int i = 0; i < n; i++) {
        if (cand->bytes > arr[i].bytes) {
            pos = i;
            break;
        }
    }
    if (pos >= cap) return;

    int last = (n < cap) ? n : (cap - 1);
    for (int i = last; i > pos; i--) {
        arr[i] = arr[i - 1];
    }
    arr[pos] = *cand;
    if (n < cap) *count = n + 1;
}

bool wc_startup_approved_is_enabled(const uint8_t *data, size_t len) {
    if (!data || len == 0) return true;
    return (data[0] & 1u) == 0;
}

void wc_startup_approved_encode(bool enabled, uint8_t out[12]) {
    if (!out) return;
    memset(out, 0, 12);
    out[0] = enabled ? 0x02u : 0x03u;
}

static void disk_progress_cb(const char *current_dir, uint64_t files, uint64_t bytes, void *user) {
    wc_disk_worker *w = (wc_disk_worker *)user;
    if (!w || !w->mu) return;
    wc_mutex_lock(w->mu);
    if (current_dir) {
        snprintf(w->snap.current_dir, sizeof w->snap.current_dir, "%s", current_dir);
    }
    w->snap.total_files = files;
    w->snap.total_bytes = bytes;
    w->dirty = true;
    wc_mutex_unlock(w->mu);
}

static void disk_worker_entry(void *user) {
    wc_disk_worker *w = (wc_disk_worker *)user;

    char root[WC_PATH_CAP];
    wc_mutex_lock(w->mu);
    snprintf(root, sizeof root, "%s", w->job_root);
    wc_mutex_unlock(w->mu);

    wc_disk_snapshot *result = (wc_disk_snapshot *)calloc(1, sizeof *result);
    if (!result) {
        wc_mutex_lock(w->mu);
        w->snap.state = WC_JOB_ERROR;
        snprintf(w->snap.error_msg, sizeof w->snap.error_msg, "Out of memory");
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return;
    }

    snprintf(result->root_path, sizeof result->root_path, "%s", root);
    bool ok = wc_plat_analyze_disk(root, &w->cancel_flag, disk_progress_cb, w, result);

    wc_mutex_lock(w->mu);
    if (w->cancel_flag) {
        result->state = WC_JOB_CANCELLED;
    } else if (ok) {
        result->state = WC_JOB_DONE;
    } else {
        result->state = WC_JOB_ERROR;
        if (!result->error_msg[0]) {
            snprintf(result->error_msg, sizeof result->error_msg, "Could not analyze path");
        }
    }
    result->current_dir[0] = '\0';
    w->snap = *result;
    w->running = false;
    w->dirty = true;
    wc_mutex_unlock(w->mu);

    free(result);
}

bool wc_disk_worker_init(wc_disk_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    if (!w->mu) return false;
    w->snap.state = WC_JOB_IDLE;
    w->dirty = true;
    return true;
}

void wc_disk_worker_destroy(wc_disk_worker *w) {
    if (!w) return;
    wc_disk_worker_cancel(w);
    wc_thread *t = NULL;
    if (w->mu) {
        wc_mutex_lock(w->mu);
        t = w->thread;
        w->thread = NULL;
        wc_mutex_unlock(w->mu);
    }
    if (t) wc_thread_join(t);
    if (w->mu) {
        wc_mutex_destroy(w->mu);
        w->mu = NULL;
    }
}

bool wc_disk_worker_start(wc_disk_worker *w, const char *root_path) {
    if (!w || !w->mu || !root_path || !root_path[0]) return false;

    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root_path, norm, sizeof norm)) return false;

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }
    wc_thread *old = w->thread;
    w->thread = NULL;
    wc_mutex_unlock(w->mu);

    if (old) wc_thread_join(old);

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }
    memset(&w->snap, 0, sizeof w->snap);
    snprintf(w->job_root, sizeof w->job_root, "%s", norm);
    snprintf(w->snap.root_path, sizeof w->snap.root_path, "%s", norm);
    w->snap.state = WC_JOB_RUNNING;
    w->cancel_flag = 0;
    w->running = true;
    w->dirty = true;

    wc_thread *t = wc_thread_start(disk_worker_entry, w);
    if (!t) {
        w->running = false;
        w->snap.state = WC_JOB_ERROR;
        snprintf(w->snap.error_msg, sizeof w->snap.error_msg, "Failed to start worker thread");
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}

void wc_disk_worker_cancel(wc_disk_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

bool wc_disk_worker_poll(wc_disk_worker *w, wc_disk_snapshot *out) {
    if (!w || !w->mu) return false;
    wc_thread *done_thread = NULL;

    wc_mutex_lock(w->mu);
    if (!w->running && w->thread) {
        done_thread = w->thread;
        w->thread = NULL;
        w->dirty = true;
    }
    bool running = w->running;
    if (out && (running || w->dirty || out->state != w->snap.state)) {
        *out = w->snap;
        if (!running) w->dirty = false;
    }
    wc_mutex_unlock(w->mu);

    if (done_thread) wc_thread_join(done_thread);
    return running;
}
