#include "wc_leftovers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"
#include "wc_safety.h"

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool ends_with_ci(const char *s, size_t len, const char *suf) {
    size_t slen = strlen(suf);
    if (len < slen) return false;
    return ascii_strcasecmp(s + len - slen, suf) == 0;
}

/* Strips trailing version/arch suffixes like " 1.2.3", " (x64)", " (64-bit)"
 * so "7-Zip 24.08 (x64)" matches a folder named "7-Zip". */
static void normalize_app_title(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!in || !in[0]) return;

    snprintf(out, cap, "%s", in);
    size_t len = strlen(out);
    while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';

    static const char *const arch_suffixes[] = {
        " (x64)", " (x86)", " (64-bit)", " (32-bit)", " (ARM64)", " x64", " x86",
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < sizeof arch_suffixes / sizeof arch_suffixes[0]; i++) {
            if (ends_with_ci(out, len, arch_suffixes[i])) {
                len -= strlen(arch_suffixes[i]);
                while (len > 0 && out[len - 1] == ' ') len--;
                out[len] = '\0';
                changed = true;
            }
        }
        /* Strip trailing " v1.2.3" or " 1.2.3" token */
        const char *last_sp = NULL;
        for (size_t k = 0; k < len; k++) {
            if (out[k] == ' ') last_sp = out + k;
        }
        if (last_sp && last_sp > out) {
            const char *tok = last_sp + 1;
            if ((tok[0] == 'v' || tok[0] == 'V') && isdigit((unsigned char)tok[1])) tok++;
            if (isdigit((unsigned char)tok[0])) {
                bool ver_like = true;
                bool has_dot = false;
                for (const char *q = tok; *q; q++) {
                    if (*q == '.') {
                        has_dot = true;
                    } else if (!isdigit((unsigned char)*q)) {
                        ver_like = false;
                        break;
                    }
                }
                if (ver_like && (has_dot || strlen(tok) <= 4)) {
                    len = (size_t)(last_sp - out);
                    while (len > 0 && out[len - 1] == ' ') len--;
                    out[len] = '\0';
                    changed = true;
                }
            }
        }
    }
}

static void compact_alnum(const char *in, char *out, size_t cap) {
    size_t pos = 0;
    for (const char *p = in; p && *p && pos + 1 < cap; p++) {
        if (isalnum((unsigned char)*p)) {
            out[pos++] = (char)tolower((unsigned char)*p);
        }
    }
    out[pos] = '\0';
}

bool wc_leftover_is_generic_name(const char *name) {
    if (!name || !name[0]) return true;
    size_t len = strlen(name);
    if (len < 4) return true;

    static const char *const generic[] = {
        "microsoft", "windows", "google", "mozilla", "apple", "adobe", "intel", "nvidia",
        "amd", "lenovo", "common", "common files", "system", "system32", "program files",
        "appdata", "local", "roaming", "programdata", "temp", "cache", "caches", "logs",
        "data", "shared", "runtime", "drivers", "packages", "update", "updater", "setup",
        "installer", "apps", "programs", "users", "default", "public", "desktop",
    };
    for (size_t i = 0; i < sizeof generic / sizeof generic[0]; i++) {
        if (ascii_strcasecmp(name, generic[i]) == 0) return true;
    }
    return false;
}

int wc_leftover_match_name(const char *app_name, const char *publisher, const char *candidate_name) {
    if (!app_name || !app_name[0] || !candidate_name || !candidate_name[0]) return 0;
    if (wc_leftover_is_generic_name(candidate_name)) return 0;

    char norm_app[192];
    normalize_app_title(app_name, norm_app, sizeof norm_app);
    if (!norm_app[0] || wc_leftover_is_generic_name(norm_app)) return 0;

    if (publisher && publisher[0] && ascii_strcasecmp(candidate_name, publisher) == 0 &&
        ascii_strcasecmp(norm_app, publisher) != 0) {
        return 0;
    }

    if (ascii_strcasecmp(norm_app, candidate_name) == 0) {
        return 2;
    }

    char comp_app[192], comp_cand[192];
    compact_alnum(norm_app, comp_app, sizeof comp_app);
    compact_alnum(candidate_name, comp_cand, sizeof comp_cand);
    if (strlen(comp_app) >= 5 && strcmp(comp_app, comp_cand) == 0) {
        return 1;
    }
    return 0;
}

static bool join_path_sep(char *out, size_t cap, const char *dir, const char *name) {
    if (!out || cap == 0 || !dir || !name || !dir[0] || !name[0]) return false;
    char sep = (dir[0] == '/') ? '/' : '\\';
    size_t dlen = strlen(dir);
    bool has_sep = (dlen > 0 && (dir[dlen - 1] == '\\' || dir[dlen - 1] == '/'));
    int n = has_sep ? snprintf(out, cap, "%s%s", dir, name)
                    : snprintf(out, cap, "%s%c%s", dir, sep, name);
    return n > 0 && (size_t)n < cap;
}

static bool already_recorded(const wc_leftover_item *out, int count, const char *path) {
    for (int i = 0; i < count; i++) {
        if (ascii_strcasecmp(out[i].path, path) == 0) return true;
    }
    return false;
}

static void add_candidate(const char *path, bool is_dir, int conf, wc_leftover_item *out,
                          int max_items, int *count) {
    if (*count >= max_items || !path || !path[0]) return;
    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(path, norm, sizeof norm)) return;
    if (!wc_safety_is_valid_target_root(norm)) return;
    if (already_recorded(out, *count, norm)) return;

    bool exists = false, actual_dir = false;
    uint64_t bytes = 0;
    if (!wc_plat_path_info(norm, &exists, &actual_dir, &bytes) || !exists) return;

    wc_leftover_item *it = &out[(*count)++];
    memset(it, 0, sizeof *it);
    snprintf(it->path, sizeof it->path, "%s", norm);
    it->bytes = bytes;
    it->confidence = conf;
    it->is_dir = is_dir ? actual_dir : false;
    it->selected = (conf >= 2);
}

typedef struct scan_dir_ctx {
    const wc_app_item *app;
    const char *root;
    const volatile int *cancel_flag;
    wc_leftover_item *out;
    int max_items;
    int *count;
    bool check_vendor_subdirs;
} scan_dir_ctx;

static bool on_vendor_child(const char *name, bool is_dir, uint64_t bytes, void *user) {
    (void)bytes;
    scan_dir_ctx *ctx = (scan_dir_ctx *)user;
    if (ctx->cancel_flag && *ctx->cancel_flag) return false;
    if (*ctx->count >= ctx->max_items) return false;

    int conf = wc_leftover_match_name(ctx->app->name, ctx->app->publisher, name);
    if (conf > 0) {
        char full[WC_PATH_CAP];
        if (join_path_sep(full, sizeof full, ctx->root, name)) {
            add_candidate(full, is_dir, conf, ctx->out, ctx->max_items, ctx->count);
        }
    }
    return true;
}

static bool on_root_entry(const char *name, bool is_dir, uint64_t bytes, void *user) {
    (void)bytes;
    scan_dir_ctx *ctx = (scan_dir_ctx *)user;
    if (ctx->cancel_flag && *ctx->cancel_flag) return false;
    if (*ctx->count >= ctx->max_items) return false;
    if (!is_dir) return true;

    char full[WC_PATH_CAP];
    if (!join_path_sep(full, sizeof full, ctx->root, name)) return true;

    int conf = wc_leftover_match_name(ctx->app->name, ctx->app->publisher, name);
    if (conf > 0) {
        add_candidate(full, true, conf, ctx->out, ctx->max_items, ctx->count);
    } else if (ctx->check_vendor_subdirs && ctx->app->publisher[0] &&
               strlen(ctx->app->publisher) >= 3 &&
               ascii_strcasecmp(name, ctx->app->publisher) == 0) {
        scan_dir_ctx sub_ctx = *ctx;
        sub_ctx.root = full;
        sub_ctx.check_vendor_subdirs = false;
        wc_plat_list_dir(full, on_vendor_child, &sub_ctx);
    }
    return true;
}

bool wc_leftover_scan_roots(const wc_app_item *app, const char *const *roots, int root_count,
                              const volatile int *cancel_flag, wc_leftover_item *out, int max_items,
                              int *out_count) {
    if (out_count) *out_count = 0;
    if (!app || !out || max_items <= 0) return false;

    int count = 0;
    if (app->install_location[0]) {
        add_candidate(app->install_location, true, 2, out, max_items, &count);
    }

    for (int i = 0; i < root_count && count < max_items; i++) {
        if (cancel_flag && *cancel_flag) {
            if (out_count) *out_count = count;
            return false;
        }
        if (!roots || !roots[i] || !roots[i][0]) continue;
        char norm_root[WC_PATH_CAP];
        if (!wc_safety_normalize_path(roots[i], norm_root, sizeof norm_root)) continue;
        scan_dir_ctx ctx = {app, norm_root, cancel_flag, out, max_items, &count, true};
        wc_plat_list_dir(norm_root, on_root_entry, &ctx);
    }

    if (out_count) *out_count = count;
    return !(cancel_flag && *cancel_flag);
}

bool wc_leftover_scan_app(const wc_app_item *app, const volatile int *cancel_flag,
                            wc_leftover_item *out, int max_items, int *out_count) {
    char roots_buf[3][WC_PATH_CAP];
    const char *roots[3];
    int nroots = 0;

#ifdef _WIN32
    static const char *const envs[3] = {"LOCALAPPDATA", "APPDATA", "ProgramData"};
    for (int i = 0; i < 3; i++) {
        if (wc_plat_get_env(envs[i], roots_buf[nroots], WC_PATH_CAP) && roots_buf[nroots][0]) {
            roots[nroots] = roots_buf[nroots];
            nroots++;
        }
    }
#else
    char home[WC_PATH_CAP];
    if (wc_plat_get_env("HOME", home, sizeof home) && home[0]) {
        if (join_path_sep(roots_buf[nroots], WC_PATH_CAP, home, "Library/Application Support")) {
            roots[nroots] = roots_buf[nroots];
            nroots++;
        }
        if (join_path_sep(roots_buf[nroots], WC_PATH_CAP, home, "Library/Caches")) {
            roots[nroots] = roots_buf[nroots];
            nroots++;
        }
    }
#endif
    return wc_leftover_scan_roots(app, roots, nroots, cancel_flag, out, max_items, out_count);
}

static void leftover_worker_entry(void *user) {
    wc_leftover_worker *w = (wc_leftover_worker *)user;
    wc_leftover_snapshot *res = (wc_leftover_snapshot *)calloc(1, sizeof *res);
    if (!res) {
        wc_mutex_lock(w->mu);
        w->snap->state = WC_JOB_ERROR;
        snprintf(w->snap->error_msg, sizeof w->snap->error_msg, "Out of memory");
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return;
    }

    wc_mutex_lock(w->mu);
    int op = w->job_op;
    wc_app_item app = w->job_app;
    int pcount = w->job_path_count;
    wc_quarantine *q = w->quarantine;
    *res = *w->snap;
    wc_mutex_unlock(w->mu);

    res->error_msg[0] = '\0';
    res->output_summary[0] = '\0';
    bool ok = true;

    if (op == 1) {
        int cnt = 0;
        ok = wc_leftover_scan_app(&app, &w->cancel_flag, res->items, WC_MAX_LEFTOVERS, &cnt);
        res->count = cnt;
        snprintf(res->app_name, sizeof res->app_name, "%s", app.name);
        snprintf(res->output_summary, sizeof res->output_summary,
                 "Found %d candidate leftovers for %.120s", cnt, app.name);
    } else if (op == 2) {
        int moved = 0, failed = 0;
        uint64_t bytes = 0;
        for (int i = 0; i < pcount && !w->cancel_flag; i++) {
            wc_quarantine_item qitem;
            wada_err err;
            wada_err_clear(&err);
            if (q && q->open &&
                wc_quarantine_add(q, w->job_paths[i], "app_leftover", false, &qitem, &err) ==
                    WADA_OK) {
                moved++;
                bytes += qitem.bytes;
            } else {
                failed++;
            }
        }
        int cnt = 0;
        wc_leftover_scan_app(&app, &w->cancel_flag, res->items, WC_MAX_LEFTOVERS, &cnt);
        res->count = cnt;
        char sz[32];
        wc_format_bytes(bytes, sz, sizeof sz);
        snprintf(res->output_summary, sizeof res->output_summary,
                 "Quarantined %d leftovers (%s), %d failed", moved, sz, failed);
        ok = (moved > 0 || failed == 0);
    }

    if (w->cancel_flag) {
        res->state = WC_JOB_CANCELLED;
    } else if (ok) {
        res->state = WC_JOB_DONE;
    } else {
        res->state = WC_JOB_ERROR;
        snprintf(res->error_msg, sizeof res->error_msg, "Leftover operation failed");
    }

    wc_mutex_lock(w->mu);
    *w->snap = *res;
    w->running = false;
    w->dirty = true;
    wc_mutex_unlock(w->mu);
    free(res);
}

bool wc_leftover_worker_init(wc_leftover_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    w->snap = (wc_leftover_snapshot *)calloc(1, sizeof *w->snap);
    if (!w->mu || !w->snap) {
        wc_mutex_destroy(w->mu);
        free(w->snap);
        w->mu = NULL;
        w->snap = NULL;
        return false;
    }
    w->snap->state = WC_JOB_IDLE;
    w->dirty = true;
    return true;
}

void wc_leftover_worker_destroy(wc_leftover_worker *w) {
    if (!w) return;
    wc_leftover_worker_cancel(w);
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
    free(w->snap);
    w->snap = NULL;
}

void wc_leftover_worker_set_quarantine(wc_leftover_worker *w, wc_quarantine *q) {
    if (!w || !w->mu) return;
    wc_mutex_lock(w->mu);
    w->quarantine = q;
    wc_mutex_unlock(w->mu);
}

void wc_leftover_worker_cancel(wc_leftover_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

bool wc_leftover_worker_poll(wc_leftover_worker *w, wc_leftover_snapshot *out) {
    if (!w || !w->mu || !w->snap) return false;
    wc_thread *done_thread = NULL;
    wc_mutex_lock(w->mu);
    if (!w->running && w->thread) {
        done_thread = w->thread;
        w->thread = NULL;
        w->dirty = true;
    }
    bool running = w->running;
    if (out && (running || w->dirty || out->state != w->snap->state)) {
        *out = *w->snap;
        if (!running) w->dirty = false;
    }
    wc_mutex_unlock(w->mu);
    if (done_thread) wc_thread_join(done_thread);
    return running;
}

bool wc_leftover_worker_start_scan(wc_leftover_worker *w, const wc_app_item *app) {
    if (!w || !w->mu || !w->snap || !app) return false;
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
    w->job_op = 1;
    w->job_app = *app;
    w->job_path_count = 0;
    memset(w->snap, 0, sizeof *w->snap);
    snprintf(w->snap->app_name, sizeof w->snap->app_name, "%s", app->name);
    w->snap->state = WC_JOB_RUNNING;
    w->cancel_flag = 0;
    w->running = true;
    w->dirty = true;

    wc_thread *t = wc_thread_start(leftover_worker_entry, w);
    if (!t) {
        w->running = false;
        w->snap->state = WC_JOB_ERROR;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}

bool wc_leftover_worker_start_quarantine(wc_leftover_worker *w, const char *const *paths,
                                         int count) {
    if (!w || !w->mu || !w->snap || !paths || count <= 0) return false;
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
    w->job_op = 2;
    w->job_path_count = 0;
    for (int i = 0; i < count && w->job_path_count < WC_MAX_LEFTOVERS; i++) {
        if (paths[i] && paths[i][0]) {
            snprintf(w->job_paths[w->job_path_count++], WC_PATH_CAP, "%s", paths[i]);
        }
    }
    w->snap->state = WC_JOB_RUNNING;
    w->snap->output_summary[0] = '\0';
    w->snap->error_msg[0] = '\0';
    w->cancel_flag = 0;
    w->running = true;
    w->dirty = true;

    wc_thread *t = wc_thread_start(leftover_worker_entry, w);
    if (!t) {
        w->running = false;
        w->snap->state = WC_JOB_ERROR;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}
