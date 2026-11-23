#include "wc_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc_safety.h"
#include "wc_sha256.h"

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

typedef struct dup_cand {
    char *path;
    uint64_t bytes;
    uint8_t partial[32];
    uint8_t full[32];
    bool has_partial;
    bool has_full;
} dup_cand;

typedef struct dup_collect_ctx {
    dup_cand *items;
    size_t count;
    size_t cap;
    const volatile int *cancel_flag;
} dup_collect_ctx;

#define WC_DUP_COLLECT_LIMIT 65536u

static bool dup_collect_cb(const char *path, uint64_t bytes, long long mtime_sec, void *user) {
    (void)mtime_sec;
    dup_collect_ctx *ctx = (dup_collect_ctx *)user;
    if (ctx->cancel_flag && *ctx->cancel_flag) return false;
    if (ctx->count >= WC_DUP_COLLECT_LIMIT) return true;

    if (ctx->count >= ctx->cap) {
        size_t ncap = ctx->cap == 0 ? 512 : ctx->cap * 2;
        if (ncap > WC_DUP_COLLECT_LIMIT) ncap = WC_DUP_COLLECT_LIMIT;
        dup_cand *ni = (dup_cand *)realloc(ctx->items, ncap * sizeof *ni);
        if (!ni) return false;
        ctx->items = ni;
        ctx->cap = ncap;
    }

    size_t plen = strlen(path) + 1;
    char *copy = (char *)malloc(plen);
    if (!copy) return false;
    memcpy(copy, path, plen);

    dup_cand *c = &ctx->items[ctx->count++];
    memset(c, 0, sizeof *c);
    c->path = copy;
    c->bytes = bytes;
    return true;
}

static int cmp_cand_size_desc(const void *pa, const void *pb) {
    const dup_cand *a = (const dup_cand *)pa;
    const dup_cand *b = (const dup_cand *)pb;
    if (b->bytes > a->bytes) return 1;
    if (b->bytes < a->bytes) return -1;
    return strcmp(a->path, b->path);
}

static int cmp_cand_partial(const void *pa, const void *pb) {
    const dup_cand *a = (const dup_cand *)pa;
    const dup_cand *b = (const dup_cand *)pb;
    if (a->has_partial != b->has_partial) return b->has_partial - a->has_partial;
    return memcmp(a->partial, b->partial, 32);
}

static int cmp_cand_full(const void *pa, const void *pb) {
    const dup_cand *a = (const dup_cand *)pa;
    const dup_cand *b = (const dup_cand *)pb;
    if (a->has_full != b->has_full) return b->has_full - a->has_full;
    int c = memcmp(a->full, b->full, 32);
    if (c != 0) return c;
    return strcmp(a->path, b->path);
}

bool wc_dup_find(const char *root, uint64_t min_bytes, const volatile int *cancel_flag,
                 wc_dup_snapshot *out) {
    if (!root || !root[0] || !out) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;

    memset(out, 0, sizeof *out);
    snprintf(out->root_path, sizeof out->root_path, "%s", norm);

    dup_collect_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.cancel_flag = cancel_flag;

    bool ok = wc_plat_walk_regular_files(norm, min_bytes, cancel_flag, dup_collect_cb, &ctx);
    out->scanned_files = (uint64_t)ctx.count;
    if (!ok || (cancel_flag && *cancel_flag)) {
        for (size_t i = 0; i < ctx.count; i++) free(ctx.items[i].path);
        free(ctx.items);
        return false;
    }

    if (ctx.count >= 2) {
        qsort(ctx.items, ctx.count, sizeof ctx.items[0], cmp_cand_size_desc);
    }

    size_t i = 0;
    while (i < ctx.count && out->group_count < WC_DUP_MAX_GROUPS) {
        if (cancel_flag && *cancel_flag) {
            ok = false;
            break;
        }
        size_t j = i + 1;
        while (j < ctx.count && ctx.items[j].bytes == ctx.items[i].bytes) j++;
        size_t bucket_len = j - i;
        if (bucket_len >= 2) {
            for (size_t k = i; k < j; k++) {
                if (cancel_flag && *cancel_flag) {
                    ok = false;
                    break;
                }
                if (wc_sha256_file_partial(ctx.items[k].path, ctx.items[k].bytes,
                                           ctx.items[k].partial)) {
                    ctx.items[k].has_partial = true;
                    out->hashed_files++;
                }
            }
            if (!ok) break;

            qsort(&ctx.items[i], bucket_len, sizeof ctx.items[0], cmp_cand_partial);

            size_t p = i;
            while (p < j && out->group_count < WC_DUP_MAX_GROUPS) {
                if (!ctx.items[p].has_partial) {
                    p++;
                    continue;
                }
                size_t q = p + 1;
                while (q < j && ctx.items[q].has_partial &&
                       memcmp(ctx.items[q].partial, ctx.items[p].partial, 32) == 0) {
                    q++;
                }
                if (q - p >= 2) {
                    for (size_t k = p; k < q; k++) {
                        if (cancel_flag && *cancel_flag) {
                            ok = false;
                            break;
                        }
                        if (wc_sha256_file_full(ctx.items[k].path, cancel_flag,
                                                ctx.items[k].full)) {
                            ctx.items[k].has_full = true;
                        }
                    }
                    if (!ok) break;

                    qsort(&ctx.items[p], q - p, sizeof ctx.items[0], cmp_cand_full);

                    size_t f = p;
                    while (f < q && out->group_count < WC_DUP_MAX_GROUPS) {
                        if (!ctx.items[f].has_full) {
                            f++;
                            continue;
                        }
                        size_t g = f + 1;
                        while (g < q && ctx.items[g].has_full &&
                               memcmp(ctx.items[g].full, ctx.items[f].full, 32) == 0) {
                            g++;
                        }
                        if (g - f >= 2) {
                            wc_dup_group *grp = &out->groups[out->group_count++];
                            memset(grp, 0, sizeof *grp);
                            grp->file_bytes = ctx.items[f].bytes;
                            memcpy(grp->sha256, ctx.items[f].full, 32);
                            for (size_t m = f;
                                 m < g && grp->file_count < WC_DUP_MAX_FILES_PER_GROUP; m++) {
                                snprintf(grp->files[grp->file_count].path, WC_PATH_CAP, "%s",
                                         ctx.items[m].path);
                                /* Keep index 0 strictly unselected/protected by default. */
                                grp->files[grp->file_count].selected = false;
                                grp->file_count++;
                            }
                            if (grp->file_count >= 2) {
                                out->total_wasted_bytes +=
                                    grp->file_bytes * (uint64_t)(grp->file_count - 1);
                            }
                        }
                        f = g;
                    }
                }
                if (!ok) break;
                p = q;
            }
        }
        i = j;
    }

    for (size_t idx = 0; idx < ctx.count; idx++) free(ctx.items[idx].path);
    free(ctx.items);
    return ok && !(cancel_flag && *cancel_flag);
}

typedef struct stale_collect_ctx {
    wc_stale_snapshot *snap;
    long long now_sec;
} stale_collect_ctx;

static bool stale_collect_cb(const char *path, const char *name, uint64_t bytes,
                             long long newest_mtime_sec, void *user) {
    stale_collect_ctx *ctx = (stale_collect_ctx *)user;
    if (ctx->snap->count >= WC_STALE_MAX_ITEMS) return false;
    wc_stale_item *it = &ctx->snap->items[ctx->snap->count++];
    memset(it, 0, sizeof *it);
    snprintf(it->path, sizeof it->path, "%s", path);
    snprintf(it->name, sizeof it->name, "%s", name);
    it->bytes = bytes;
    it->age_days = (ctx->now_sec > newest_mtime_sec)
                       ? ((ctx->now_sec - newest_mtime_sec) / 86400LL)
                       : 0;
    it->selected = false;
    ctx->snap->total_bytes += bytes;
    return true;
}

static int cmp_stale_desc(const void *pa, const void *pb) {
    const wc_stale_item *a = (const wc_stale_item *)pa;
    const wc_stale_item *b = (const wc_stale_item *)pb;
    if (b->bytes > a->bytes) return 1;
    if (b->bytes < a->bytes) return -1;
    return strcmp(a->path, b->path);
}

bool wc_stale_find(const char *root, int min_age_days, long long now_sec,
                   const volatile int *cancel_flag, wc_stale_snapshot *out) {
    if (!root || !root[0] || !out || min_age_days < 0) return false;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(root, norm, sizeof norm)) return false;

    if (now_sec <= 0) now_sec = (long long)time(NULL);
    memset(out, 0, sizeof *out);
    snprintf(out->root_path, sizeof out->root_path, "%s", norm);

    long long min_age_sec = (long long)min_age_days * 86400LL;
    stale_collect_ctx ctx = {out, now_sec};
    bool ok = wc_plat_find_stale_dev_dirs(norm, min_age_sec, now_sec, cancel_flag,
                                           stale_collect_cb, &ctx);
    if (out->count > 1) {
        qsort(out->items, (size_t)out->count, sizeof out->items[0], cmp_stale_desc);
    }
    return ok && !(cancel_flag && *cancel_flag);
}

static void extra_worker_entry(void *user) {
    wc_disk_extra_worker *w = (wc_disk_extra_worker *)user;

    wc_mutex_lock(w->mu);
    wc_disk_extra_op op = w->op;
    char root[WC_PATH_CAP];
    snprintf(root, sizeof root, "%s", w->job_root);
    uint64_t min_bytes = w->job_min_bytes;
    int min_age = w->job_min_age_days;
    int pcount = w->job_path_count;
    char stag[64];
    snprintf(stag, sizeof stag, "%s", w->job_source_tag);
    wc_quarantine *q = w->quarantine;
    wc_mutex_unlock(w->mu);

    if (op == WC_DEXTRA_DUPS) {
        wc_dup_snapshot *res = (wc_dup_snapshot *)calloc(1, sizeof *res);
        bool ok = res && wc_dup_find(root, min_bytes, &w->cancel_flag, res);
        wc_mutex_lock(w->mu);
        if (res) *w->dup_snap = *res;
        w->dup_snap->state = w->cancel_flag ? WC_JOB_CANCELLED : (ok ? WC_JOB_DONE : WC_JOB_ERROR);
        if (!ok && !w->cancel_flag) {
            snprintf(w->dup_snap->error_msg, sizeof w->dup_snap->error_msg,
                     "Duplicate scan failed");
        }
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        free(res);
    } else if (op == WC_DEXTRA_STALE) {
        wc_stale_snapshot *res = (wc_stale_snapshot *)calloc(1, sizeof *res);
        bool ok = res && wc_stale_find(root, min_age, 0, &w->cancel_flag, res);
        wc_mutex_lock(w->mu);
        if (res) *w->stale_snap = *res;
        w->stale_snap->state =
            w->cancel_flag ? WC_JOB_CANCELLED : (ok ? WC_JOB_DONE : WC_JOB_ERROR);
        if (!ok && !w->cancel_flag) {
            snprintf(w->stale_snap->error_msg, sizeof w->stale_snap->error_msg,
                     "Stale artifacts scan failed");
        }
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        free(res);
    } else if (op == WC_DEXTRA_QUARANTINE) {
        int moved = 0, failed = 0;
        uint64_t bytes = 0;
        bool is_dup = (strcmp(stag, "duplicate") == 0);
        bool moved_ok[WC_STALE_MAX_ITEMS];
        memset(moved_ok, 0, sizeof moved_ok);

        for (int i = 0; i < pcount && !w->cancel_flag; i++) {
            wc_quarantine_item qitem;
            wada_err err;
            wada_err_clear(&err);
            if (q && q->open &&
                wc_quarantine_add(q, w->job_paths[i], stag, false, &qitem, &err) == WADA_OK) {
                moved++;
                bytes += qitem.bytes;
                if (i < WC_STALE_MAX_ITEMS) moved_ok[i] = true;
            } else {
                failed++;
            }
        }
        char sz[32];
        wc_format_bytes(bytes, sz, sizeof sz);
        wc_mutex_lock(w->mu);
        if (is_dup) {
            int ng = 0;
            w->dup_snap->total_wasted_bytes = 0;
            for (int g = 0; g < w->dup_snap->group_count; g++) {
                wc_dup_group grp = w->dup_snap->groups[g];
                int nf = 0;
                for (int f = 0; f < grp.file_count; f++) {
                    bool was_moved = false;
                    for (int i = 0; i < pcount && i < WC_STALE_MAX_ITEMS; i++) {
                        if (moved_ok[i] && strcmp(grp.files[f].path, w->job_paths[i]) == 0) {
                            was_moved = true;
                            break;
                        }
                    }
                    if (!was_moved) grp.files[nf++] = grp.files[f];
                }
                grp.file_count = nf;
                if (nf >= 2) {
                    w->dup_snap->groups[ng++] = grp;
                    w->dup_snap->total_wasted_bytes += grp.file_bytes * (uint64_t)(nf - 1);
                }
            }
            w->dup_snap->group_count = ng;
            snprintf(w->dup_snap->output_summary, sizeof w->dup_snap->output_summary,
                     "Moved %d duplicates (%s) to Quarantine (%d failed)", moved, sz, failed);
            w->dup_snap->state =
                w->cancel_flag ? WC_JOB_CANCELLED : (moved > 0 || failed == 0 ? WC_JOB_DONE
                                                                             : WC_JOB_ERROR);
        } else {
            int ns = 0;
            w->stale_snap->total_bytes = 0;
            for (int s = 0; s < w->stale_snap->count; s++) {
                bool was_moved = false;
                for (int i = 0; i < pcount && i < WC_STALE_MAX_ITEMS; i++) {
                    if (moved_ok[i] && strcmp(w->stale_snap->items[s].path, w->job_paths[i]) == 0) {
                        was_moved = true;
                        break;
                    }
                }
                if (!was_moved) {
                    w->stale_snap->items[ns++] = w->stale_snap->items[s];
                    w->stale_snap->total_bytes += w->stale_snap->items[s].bytes;
                }
            }
            w->stale_snap->count = ns;
            snprintf(w->stale_snap->output_summary, sizeof w->stale_snap->output_summary,
                     "Moved %d stale folders (%s) to Quarantine (%d failed)", moved, sz, failed);
            w->stale_snap->state =
                w->cancel_flag ? WC_JOB_CANCELLED : (moved > 0 || failed == 0 ? WC_JOB_DONE
                                                                             : WC_JOB_ERROR);
        }
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
    }
}

bool wc_disk_extra_init(wc_disk_extra_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    w->dup_snap = (wc_dup_snapshot *)calloc(1, sizeof *w->dup_snap);
    w->stale_snap = (wc_stale_snapshot *)calloc(1, sizeof *w->stale_snap);
    w->job_paths = (char (*)[WC_PATH_CAP])calloc(WC_STALE_MAX_ITEMS, WC_PATH_CAP);
    if (!w->mu || !w->dup_snap || !w->stale_snap || !w->job_paths) {
        wc_mutex_destroy(w->mu);
        free(w->dup_snap);
        free(w->stale_snap);
        free(w->job_paths);
        memset(w, 0, sizeof *w);
        return false;
    }
    w->dup_snap->state = WC_JOB_IDLE;
    w->stale_snap->state = WC_JOB_IDLE;
    w->dirty = true;
    return true;
}

void wc_disk_extra_destroy(wc_disk_extra_worker *w) {
    if (!w) return;
    wc_disk_extra_cancel(w);
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
    free(w->dup_snap);
    free(w->stale_snap);
    free(w->job_paths);
    w->dup_snap = NULL;
    w->stale_snap = NULL;
    w->job_paths = NULL;
}

void wc_disk_extra_set_quarantine(wc_disk_extra_worker *w, wc_quarantine *q) {
    if (!w || !w->mu) return;
    wc_mutex_lock(w->mu);
    w->quarantine = q;
    wc_mutex_unlock(w->mu);
}

void wc_disk_extra_cancel(wc_disk_extra_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

bool wc_disk_extra_poll(wc_disk_extra_worker *w, wc_dup_snapshot *out_dup,
                        wc_stale_snapshot *out_stale, wc_disk_extra_op *out_op) {
    if (!w || !w->mu) return false;
    wc_thread *done_thread = NULL;
    wc_mutex_lock(w->mu);
    if (!w->running && w->thread) {
        done_thread = w->thread;
        w->thread = NULL;
        w->dirty = true;
    }
    bool running = w->running;
    if (out_op) *out_op = w->op;
    if (running || w->dirty) {
        if (out_dup && w->dup_snap) *out_dup = *w->dup_snap;
        if (out_stale && w->stale_snap) *out_stale = *w->stale_snap;
        if (!running) w->dirty = false;
    }
    wc_mutex_unlock(w->mu);
    if (done_thread) wc_thread_join(done_thread);
    return running;
}

static bool start_disk_extra(wc_disk_extra_worker *w, wc_disk_extra_op op, const char *root_path,
                             uint64_t min_bytes, int min_age_days, const char *const *paths,
                             int path_count, const char *source_tag) {
    if (!w || !w->mu) return false;
    char norm[WC_PATH_CAP];
    norm[0] = '\0';
    if (op != WC_DEXTRA_QUARANTINE) {
        if (!root_path || !wc_safety_normalize_path(root_path, norm, sizeof norm)) return false;
    }

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
    w->op = op;
    snprintf(w->job_root, sizeof w->job_root, "%s", norm);
    w->job_min_bytes = min_bytes;
    w->job_min_age_days = min_age_days;
    snprintf(w->job_source_tag, sizeof w->job_source_tag, "%s", source_tag ? source_tag : "");
    w->job_path_count = 0;
    if (paths && path_count > 0) {
        for (int i = 0; i < path_count && w->job_path_count < WC_STALE_MAX_ITEMS; i++) {
            if (paths[i] && paths[i][0]) {
                snprintf(w->job_paths[w->job_path_count++], WC_PATH_CAP, "%s", paths[i]);
            }
        }
    }
    if (op == WC_DEXTRA_DUPS) {
        memset(w->dup_snap, 0, sizeof *w->dup_snap);
        snprintf(w->dup_snap->root_path, sizeof w->dup_snap->root_path, "%s", norm);
        w->dup_snap->state = WC_JOB_RUNNING;
    } else if (op == WC_DEXTRA_STALE) {
        memset(w->stale_snap, 0, sizeof *w->stale_snap);
        snprintf(w->stale_snap->root_path, sizeof w->stale_snap->root_path, "%s", norm);
        w->stale_snap->state = WC_JOB_RUNNING;
    } else if (op == WC_DEXTRA_QUARANTINE) {
        if (source_tag && strcmp(source_tag, "duplicate") == 0) {
            w->dup_snap->state = WC_JOB_RUNNING;
            w->dup_snap->output_summary[0] = '\0';
        } else {
            w->stale_snap->state = WC_JOB_RUNNING;
            w->stale_snap->output_summary[0] = '\0';
        }
    }

    w->cancel_flag = 0;
    w->running = true;
    w->dirty = true;
    wc_thread *t = wc_thread_start(extra_worker_entry, w);
    if (!t) {
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}

bool wc_disk_extra_start_dups(wc_disk_extra_worker *w, const char *root_path, uint64_t min_bytes) {
    return start_disk_extra(w, WC_DEXTRA_DUPS, root_path, min_bytes, 0, NULL, 0, NULL);
}

bool wc_disk_extra_start_stale(wc_disk_extra_worker *w, const char *root_path, int min_age_days) {
    return start_disk_extra(w, WC_DEXTRA_STALE, root_path, 0, min_age_days, NULL, 0, NULL);
}

bool wc_disk_extra_start_quarantine(wc_disk_extra_worker *w, const char *const *paths, int count,
                                    const char *source_tag) {
    if (!paths || count <= 0) return false;
    return start_disk_extra(w, WC_DEXTRA_QUARANTINE, NULL, 0, 0, paths, count, source_tag);
}
