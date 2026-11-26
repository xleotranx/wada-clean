#include "wc_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_rules.h"
#include "wc_safety.h"

typedef struct worker_sample_ctx {
    wc_worker *w;
    const char *target_id;
} worker_sample_ctx;

static void worker_sample(const char *path, uint64_t bytes, void *user) {
    worker_sample_ctx *ctx = (worker_sample_ctx *)user;
    if (!ctx || !ctx->w || !path || !path[0]) return;
    wc_mutex_lock(ctx->w->mu);
    if (ctx->w->preview_count < WC_MAX_PREVIEW) {
        wc_preview_item *item = &ctx->w->previews[ctx->w->preview_count];
        snprintf(item->target_id, sizeof item->target_id, "%s", ctx->target_id);
        snprintf(item->path, sizeof item->path, "%s", path);
        item->bytes = bytes;
        ctx->w->preview_count++;
    }
    wc_mutex_unlock(ctx->w->mu);
}

static void worker_entry(void *user) {
    wc_worker *w = (wc_worker *)user;
    bool is_admin = wc_plat_is_admin();

    wc_mutex_lock(w->mu);
    wc_job_kind kind = w->snap.kind;
    bool dry_run = w->snap.dry_run;
    int count = w->job_target_count;
    wc_mutex_unlock(w->mu);

    const char *proc_names[WC_MAX_TARGETS];
    bool proc_running[WC_MAX_TARGETS];
    memset(proc_running, 0, sizeof proc_running);
    for (int i = 0; i < count; i++) {
        proc_names[i] = (w->job_selected[i] && w->job_defs[i].process_name[0])
                            ? w->job_defs[i].process_name
                            : "";
    }
    wc_plat_processes_running(proc_names, count, proc_running);

    for (int i = 0; i < count; i++) {
        if (w->cancel_flag) break;

        const wc_target_def *def = &w->job_defs[i];
        bool selected = w->job_selected[i];

        wc_mutex_lock(w->mu);
        w->snap.current_index = i;
        snprintf(w->snap.current_label, sizeof w->snap.current_label, "%s", def->label);
        w->dirty = true;
        wc_target_result res = w->snap.results[i];
        wc_mutex_unlock(w->mu);

        snprintf(res.id, sizeof res.id, "%s", def->id);
        res.selected = selected;
        res.admin_blocked = false;
        res.browser_running = false;
        res.note[0] = '\0';

        if (!selected) {
            wc_mutex_lock(w->mu);
            w->snap.results[i] = res;
            w->dirty = true;
            wc_mutex_unlock(w->mu);
            continue;
        }

        if (proc_running[i]) {
            res.browser_running = true;
        }

        if (def->kind == WC_TARGET_RECYCLE_BIN) {
            snprintf(res.resolved_path, sizeof res.resolved_path, "Recycle Bin");
            uint64_t files = 0, bytes = 0;
            if (kind == WC_JOB_ANALYZE) {
                if (wc_plat_query_recycle_bin(&files, &bytes)) {
                    res.analyzed = true;
                    res.files_found = files;
                    res.bytes_found = bytes;
                } else {
                    snprintf(res.note, sizeof res.note, "Could not query Recycle Bin");
                }
            } else if (kind == WC_JOB_CLEAN) {
                wc_plat_query_recycle_bin(&files, &bytes);
                if (dry_run) {
                    res.cleaned = true;
                    res.files_cleaned = files;
                    res.bytes_cleaned = bytes;
                    snprintf(res.note, sizeof res.note, "Dry-run preview");
                } else if (files == 0 && bytes == 0) {
                    res.cleaned = true;
                    res.files_cleaned = 0;
                    res.bytes_cleaned = 0;
                } else if (wc_plat_empty_recycle_bin()) {
                    res.cleaned = true;
                    res.files_cleaned = files;
                    res.bytes_cleaned = bytes;
                } else {
                    res.cleaned = true;
                    res.skipped_count = files > 0 ? files : 1;
                    snprintf(res.note, sizeof res.note, "Failed to empty Recycle Bin");
                }
            }
        } else {
            char path[WC_PATH_CAP];
            bool path_ok = wc_rules_resolve_path(def, path, sizeof path);
            if (!path_ok) {
                snprintf(res.note, sizeof res.note, "Unavailable path");
            } else {
                snprintf(res.resolved_path, sizeof res.resolved_path, "%s", path);
                if (kind == WC_JOB_ANALYZE) {
                    uint64_t files = 0, bytes = 0;
                    int samples_before = 0;
                    int remaining = 0;
                    wc_mutex_lock(w->mu);
                    samples_before = w->preview_count;
                    remaining = WC_MAX_PREVIEW - samples_before;
                    wc_mutex_unlock(w->mu);
                    worker_sample_ctx sample_ctx = {w, def->id};
                    if (wc_plat_scan_target_preview(def->kind, path, &w->cancel_flag, NULL, NULL,
                                                    remaining > 0 ? worker_sample : NULL,
                                                    &sample_ctx, remaining, &files, &bytes)) {
                        res.analyzed = true;
                        res.files_found = files;
                        res.bytes_found = bytes;
                        if (files > (uint64_t)(w->preview_count - samples_before)) {
                            wc_mutex_lock(w->mu);
                            w->preview_truncated = true;
                            wc_mutex_unlock(w->mu);
                        }
                        if (res.browser_running) {
                            snprintf(res.note, sizeof res.note, "Browser is running");
                        }
                    } else if (!w->cancel_flag) {
                        if (def->needs_admin && !is_admin) {
                            res.admin_blocked = true;
                            snprintf(res.note, sizeof res.note, "Requires Administrator");
                        } else {
                            snprintf(res.note, sizeof res.note, "Access denied");
                        }
                    }
                } else if (kind == WC_JOB_CLEAN) {
                    if (res.browser_running) {
                        res.cleaned = true;
                        res.files_cleaned = 0;
                        res.bytes_cleaned = 0;
                        res.skipped_count = res.files_found > 0 ? res.files_found : 1;
                        snprintf(res.note, sizeof res.note, "Skipped (browser running)");
                    } else {
                        uint64_t files = 0, bytes = 0, skipped = 0;
                        if (wc_plat_clean_target(def->kind, path, dry_run, &w->cancel_flag, NULL,
                                                 NULL, &files, &bytes, &skipped)) {
                            res.cleaned = true;
                            res.files_cleaned = files;
                            res.bytes_cleaned = bytes;
                            res.skipped_count = skipped;
                            if (dry_run) {
                                snprintf(res.note, sizeof res.note, "Dry-run preview");
                            } else if (skipped > 0) {
                                snprintf(res.note, sizeof res.note, "Some files skipped (in use)");
                            }
                        } else if (!w->cancel_flag) {
                            if (def->needs_admin && !is_admin) {
                                res.admin_blocked = true;
                                snprintf(res.note, sizeof res.note, "Requires Administrator");
                            } else {
                                snprintf(res.note, sizeof res.note, "Access denied");
                            }
                        }
                    }
                }
            }
        }

        wc_mutex_lock(w->mu);
        w->snap.results[i] = res;
        w->snap.completed_targets++;
        if (kind == WC_JOB_ANALYZE) {
            w->snap.total_files_found += res.files_found;
            w->snap.total_bytes_found += res.bytes_found;
        } else if (kind == WC_JOB_CLEAN) {
            w->snap.total_files_cleaned += res.files_cleaned;
            w->snap.total_bytes_cleaned += res.bytes_cleaned;
            w->snap.total_skipped += res.skipped_count;
        }
        w->dirty = true;
        wc_mutex_unlock(w->mu);
    }

    wc_mutex_lock(w->mu);
    w->snap.current_index = -1;
    w->snap.current_label[0] = '\0';
    w->snap.state = w->cancel_flag ? WC_JOB_CANCELLED : WC_JOB_DONE;
    w->running = false;
    w->dirty = true;
    wc_mutex_unlock(w->mu);
}

bool wc_worker_init(wc_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    if (!w->mu) return false;
    w->snap.state = WC_JOB_IDLE;
    w->snap.current_index = -1;
    w->dirty = true;
    return true;
}

void wc_worker_destroy(wc_worker *w) {
    if (!w) return;
    wc_worker_cancel(w);
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

bool wc_worker_start(wc_worker *w, wc_job_kind kind, bool dry_run,
                     const wc_target_def *defs, const bool *selected, int target_count) {
    if (!w || !w->mu || !defs || !selected) return false;
    if (target_count <= 0 || target_count > WC_MAX_TARGETS) return false;
    if (kind != WC_JOB_ANALYZE && kind != WC_JOB_CLEAN) return false;

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }
    wc_thread *old_thread = w->thread;
    w->thread = NULL;
    wc_mutex_unlock(w->mu);

    if (old_thread) wc_thread_join(old_thread);

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }

    bool keep_analyze = (kind == WC_JOB_CLEAN && w->snap.target_count == target_count);
    if (keep_analyze) {
        for (int i = 0; i < target_count; i++) {
            if (strcmp(w->snap.results[i].id, defs[i].id) != 0) {
                keep_analyze = false;
                break;
            }
        }
    }

    if (!keep_analyze) {
        memset(&w->snap, 0, sizeof w->snap);
    } else {
        w->snap.total_files_cleaned = 0;
        w->snap.total_bytes_cleaned = 0;
        w->snap.total_skipped = 0;
        w->snap.error_msg[0] = '\0';
        for (int i = 0; i < target_count; i++) {
            w->snap.results[i].cleaned = false;
            w->snap.results[i].files_cleaned = 0;
            w->snap.results[i].bytes_cleaned = 0;
            w->snap.results[i].skipped_count = 0;
            w->snap.results[i].note[0] = '\0';
        }
    }

    int total_sel = 0;
    for (int i = 0; i < target_count; i++) {
        w->job_defs[i] = defs[i];
        w->job_selected[i] = selected[i];
        snprintf(w->snap.results[i].id, sizeof w->snap.results[i].id, "%s", defs[i].id);
        w->snap.results[i].selected = selected[i];
        if (selected[i]) total_sel++;
    }
    w->job_target_count = target_count;
    w->cancel_flag = 0;

    w->preview_count = 0;
    w->preview_truncated = false;

    w->snap.kind = kind;
    w->snap.state = WC_JOB_RUNNING;
    w->snap.dry_run = dry_run;
    w->snap.total_selected = total_sel;
    w->snap.completed_targets = 0;
    w->snap.current_index = -1;
    w->snap.current_label[0] = '\0';
    w->snap.target_count = target_count;
    w->running = true;
    w->dirty = true;

    wc_thread *t = wc_thread_start(worker_entry, w);
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

void wc_worker_cancel(wc_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

int wc_worker_preview_copy(wc_worker *w, wc_preview_item *out, int max) {
    if (!w || !w->mu || !out || max <= 0) return 0;
    wc_mutex_lock(w->mu);
    int count = w->preview_count;
    if (count > max) count = max;
    if (count > 0) memcpy(out, w->previews, (size_t)count * sizeof out[0]);
    wc_mutex_unlock(w->mu);
    return count;
}

bool wc_worker_preview_truncated(wc_worker *w) {
    if (!w || !w->mu) return false;
    wc_mutex_lock(w->mu);
    bool truncated = w->preview_truncated;
    wc_mutex_unlock(w->mu);
    return truncated;
}

bool wc_worker_poll(wc_worker *w, wc_worker_snapshot *out) {
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
