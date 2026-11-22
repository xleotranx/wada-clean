#include "wc_updater.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool wc_updater_is_safe_id(const char *id) {
    if (!id || !id[0]) return false;
    size_t len = strlen(id);
    if (len >= 160) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];
        if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '+' || c == '~' || c == ':') {
            continue;
        }
        return false;
    }
    return true;
}

static bool is_dash_separator(const char *s, size_t len) {
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    if (len < 10) return false;
    for (size_t i = 0; i < len; i++) {
        if (s[i] != '-') return false;
    }
    return true;
}

/* Extracts the rightmost whitespace-delimited token within [start, *end).
 * Updates *end to the start of the extracted token. */
static bool pop_right_token(const char *start, size_t *end, char *out, size_t cap,
                            size_t *out_tok_start) {
    size_t e = *end;
    while (e > 0 && (start[e - 1] == ' ' || start[e - 1] == '\t')) e--;
    if (e == 0) return false;
    size_t b = e;
    while (b > 0 && start[b - 1] != ' ' && start[b - 1] != '\t') b--;
    size_t tok_len = e - b;
    if (tok_len == 0 || tok_len >= cap) return false;
    memcpy(out, start + b, tok_len);
    out[tok_len] = '\0';
    *end = b;
    if (out_tok_start) *out_tok_start = b;
    return true;
}

static bool parse_winget_row(const char *line, size_t len, wc_update_item *item) {
    size_t end = len;
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
    if (end == 0) return false;

    char source[32];
    char avail[64];
    char cur[64];
    char id[160];
    size_t id_start = 0;

    if (!pop_right_token(line, &end, source, sizeof source, NULL)) return false;
    if (!pop_right_token(line, &end, avail, sizeof avail, NULL)) return false;
    if (!pop_right_token(line, &end, cur, sizeof cur, NULL)) return false;

    /* Peek if previous token is "<" (e.g., "< 1.2.3" in Version column). */
    size_t save_end = end;
    char maybe_lt[160];
    size_t maybe_start = 0;
    if (!pop_right_token(line, &end, maybe_lt, sizeof maybe_lt, &maybe_start)) return false;
    if (strcmp(maybe_lt, "<") == 0) {
        char merged[64];
        int mn = snprintf(merged, sizeof merged, "< %s", cur);
        if (mn < 0 || (size_t)mn >= sizeof merged) return false;
        snprintf(cur, sizeof cur, "%s", merged);
        if (!pop_right_token(line, &end, id, sizeof id, &id_start)) return false;
    } else {
        (void)save_end;
        snprintf(id, sizeof id, "%s", maybe_lt);
        id_start = maybe_start;
    }

    if (!wc_updater_is_safe_id(id)) return false;

    size_t nb = 0;
    while (nb < id_start && (line[nb] == ' ' || line[nb] == '\t')) nb++;
    size_t ne = id_start;
    while (ne > nb && (line[ne - 1] == ' ' || line[ne - 1] == '\t')) ne--;
    if (ne == nb) return false;

    memset(item, 0, sizeof *item);
    size_t nlen = ne - nb;
    if (nlen >= sizeof item->name) nlen = sizeof item->name - 1;
    memcpy(item->name, line + nb, nlen);
    item->name[nlen] = '\0';
    snprintf(item->id, sizeof item->id, "%s", id);
    snprintf(item->current_version, sizeof item->current_version, "%s", cur);
    snprintf(item->available_version, sizeof item->available_version, "%s", avail);
    snprintf(item->source, sizeof item->source, "%s", source);
    return true;
}

int wc_updater_parse_winget_table(const char *text, wc_update_item *out, int max_items) {
    if (!text || !out || max_items <= 0) return 0;

    int count = 0;
    bool in_table = false;
    const char *p = text;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t raw_len = nl ? (size_t)(nl - p) : strlen(p);

        /* Trim trailing '\r'. */
        size_t len = raw_len;
        while (len > 0 && p[len - 1] == '\r') len--;

        /* If spinner progress overwrote earlier parts of this line via '\r',
         * take only the segment after the last '\r'. */
        const char *seg = p;
        for (size_t i = 0; i < len; i++) {
            if (p[i] == '\r') {
                seg = p + i + 1;
            }
        }
        size_t seg_len = (size_t)((p + len) - seg);

        if (!in_table) {
            if (is_dash_separator(seg, seg_len)) {
                in_table = true;
            }
        } else {
            size_t tlen = seg_len;
            while (tlen > 0 && (seg[tlen - 1] == ' ' || seg[tlen - 1] == '\t')) tlen--;
            if (tlen == 0) {
                if (count > 0) break;
            } else if (count < max_items) {
                wc_update_item item;
                if (parse_winget_row(seg, seg_len, &item)) {
                    out[count++] = item;
                } else if (count > 0) {
                    /* Footer text after rows */
                    break;
                }
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return count;
}

int wc_updater_parse_brew_outdated(const char *text, wc_update_item *out, int max_items) {
    if (!text || !out || max_items <= 0) return 0;

    int count = 0;
    const char *p = text;
    while (*p && count < max_items) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        while (len > 0 && (*p == ' ' || *p == '\t')) {
            p++;
            len--;
        }

        if (len > 0) {
            /* Parse "<name> (<cur>) < <avail>" or "<name> (<cur>) != <avail>" */
            const char *lparen = NULL;
            for (size_t i = 0; i + 1 < len; i++) {
                if (p[i] == ' ' && p[i + 1] == '(') {
                    lparen = p + i + 1;
                    break;
                }
            }
            const char *rparen = lparen ? (const char *)memchr(lparen, ')', (size_t)((p + len) - lparen)) : NULL;
            if (lparen && rparen && lparen > p + 1) {
                size_t id_len = (size_t)((lparen - 1) - p);
                while (id_len > 0 && (p[id_len - 1] == ' ' || p[id_len - 1] == '\t')) id_len--;
                size_t cur_len = (size_t)(rparen - (lparen + 1));

                const char *after = rparen + 1;
                const char *end = p + len;
                while (after < end && (*after == ' ' || *after == '\t')) after++;

                if (after < end && (*after == '<' || (after + 1 < end && after[0] == '!' && after[1] == '='))) {
                    after += (*after == '<') ? 1 : 2;
                    while (after < end && (*after == ' ' || *after == '\t')) after++;
                    const char *av_start = after;
                    while (after < end && *after != ' ' && *after != '\t') after++;
                    size_t av_len = (size_t)(after - av_start);

                    if (id_len > 0 && id_len < 160 && cur_len > 0 && cur_len < 64 && av_len > 0 && av_len < 64) {
                        wc_update_item item;
                        memset(&item, 0, sizeof item);
                        memcpy(item.id, p, id_len);
                        item.id[id_len] = '\0';
                        if (wc_updater_is_safe_id(item.id)) {
                            snprintf(item.name, sizeof item.name, "%s", item.id);
                            memcpy(item.current_version, lparen + 1, cur_len);
                            item.current_version[cur_len] = '\0';
                            memcpy(item.available_version, av_start, av_len);
                            item.available_version[av_len] = '\0';
                            snprintf(item.source, sizeof item.source, "brew");
                            out[count++] = item;
                        }
                    }
                }
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return count;
}

static void updater_worker_entry(void *user) {
    wc_updater_worker *w = (wc_updater_worker *)user;

    wc_mutex_lock(w->mu);
    wc_updater_op op = w->job_op;
    char pkg_id[160];
    snprintf(pkg_id, sizeof pkg_id, "%s", w->job_pkg_id);
    wc_mutex_unlock(w->mu);

    wc_updater_snapshot *res = (wc_updater_snapshot *)calloc(1, sizeof *res);
    if (!res) {
        wc_mutex_lock(w->mu);
        w->snap->state = WC_JOB_ERROR;
        snprintf(w->snap->error_msg, sizeof w->snap->error_msg, "Out of memory");
        w->running = false;
        wc_mutex_unlock(w->mu);
        return;
    }

    res->op = op;
    snprintf(res->active_id, sizeof res->active_id, "%s", pkg_id);
    res->winget_available = wc_plat_winget_available();

    if (!res->winget_available) {
        res->state = WC_JOB_DONE;
        snprintf(res->error_msg, sizeof res->error_msg, "winget is not available on this system");
    } else if (op == WC_UPD_CHECK) {
        int count = 0;
        bool ok = wc_plat_winget_list_upgrades(&w->cancel_flag, res->items, WC_MAX_UPDATES, &count);
        res->count = count;
        if (w->cancel_flag) {
            res->state = WC_JOB_CANCELLED;
        } else if (ok) {
            res->state = WC_JOB_DONE;
        } else {
            res->state = WC_JOB_ERROR;
            snprintf(res->error_msg, sizeof res->error_msg, "Failed to query winget updates");
        }
    } else if (op == WC_UPD_UPGRADE_ONE || op == WC_UPD_UPGRADE_ALL) {
        const char *target = (op == WC_UPD_UPGRADE_ONE) ? pkg_id : NULL;
        bool ok = wc_plat_winget_upgrade(target, &w->cancel_flag, res->output_summary,
                                         sizeof res->output_summary);
        if (w->cancel_flag) {
            res->state = WC_JOB_CANCELLED;
        } else if (ok) {
            int count = 0;
            wc_plat_winget_list_upgrades(&w->cancel_flag, res->items, WC_MAX_UPDATES, &count);
            res->count = count;
            res->state = w->cancel_flag ? WC_JOB_CANCELLED : WC_JOB_DONE;
        } else {
            res->state = WC_JOB_ERROR;
            snprintf(res->error_msg, sizeof res->error_msg, "%.191s",
                     res->output_summary[0] ? res->output_summary : "winget upgrade failed");
        }
    }

    wc_mutex_lock(w->mu);
    *w->snap = *res;
    w->running = false;
    wc_mutex_unlock(w->mu);

    free(res);
}

bool wc_updater_worker_init(wc_updater_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    w->snap = (wc_updater_snapshot *)calloc(1, sizeof *w->snap);
    if (!w->mu || !w->snap) {
        wc_mutex_destroy(w->mu);
        free(w->snap);
        w->mu = NULL;
        w->snap = NULL;
        return false;
    }
    w->snap->state = WC_JOB_IDLE;
    w->snap->winget_available = true;
    return true;
}

void wc_updater_worker_destroy(wc_updater_worker *w) {
    if (!w) return;
    wc_updater_worker_cancel(w);
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

static bool start_updater_job(wc_updater_worker *w, wc_updater_op op, const char *pkg_id) {
    if (!w || !w->mu || !w->snap) return false;
    if (op == WC_UPD_UPGRADE_ONE && !wc_updater_is_safe_id(pkg_id)) return false;

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
    w->job_op = op;
    if (pkg_id) snprintf(w->job_pkg_id, sizeof w->job_pkg_id, "%s", pkg_id);
    else w->job_pkg_id[0] = '\0';

    w->snap->op = op;
    w->snap->state = WC_JOB_RUNNING;
    w->snap->error_msg[0] = '\0';
    w->snap->output_summary[0] = '\0';
    if (pkg_id) snprintf(w->snap->active_id, sizeof w->snap->active_id, "%s", pkg_id);
    else w->snap->active_id[0] = '\0';

    w->cancel_flag = 0;
    w->running = true;

    wc_thread *t = wc_thread_start(updater_worker_entry, w);
    if (!t) {
        w->running = false;
        w->snap->state = WC_JOB_ERROR;
        snprintf(w->snap->error_msg, sizeof w->snap->error_msg, "Failed to start updater thread");
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}

bool wc_updater_worker_start_check(wc_updater_worker *w) {
    return start_updater_job(w, WC_UPD_CHECK, NULL);
}

bool wc_updater_worker_start_upgrade_one(wc_updater_worker *w, const char *package_id) {
    return start_updater_job(w, WC_UPD_UPGRADE_ONE, package_id);
}

bool wc_updater_worker_start_upgrade_all(wc_updater_worker *w) {
    return start_updater_job(w, WC_UPD_UPGRADE_ALL, NULL);
}

void wc_updater_worker_cancel(wc_updater_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

bool wc_updater_worker_poll(wc_updater_worker *w, wc_updater_snapshot *out) {
    if (!w || !w->mu || !w->snap) return false;
    wc_thread *done_thread = NULL;

    wc_mutex_lock(w->mu);
    if (!w->running && w->thread) {
        done_thread = w->thread;
        w->thread = NULL;
    }
    bool running = w->running;
    if (out) *out = *w->snap;
    wc_mutex_unlock(w->mu);

    if (done_thread) wc_thread_join(done_thread);
    return running;
}
