#include "wc_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wada_status wc_history_open(wc_history *h, const wada_store_ops *ops, const char *db_path, wada_err *err) {
    if (!h || !ops || !db_path) return wada_err_set(err, WADA_ERR_INVALID, "history open");
    memset(h, 0, sizeof *h);
    wada_status st = wada_records_open(&h->rec, ops, db_path, "clean_log/", err);
    if (st == WADA_OK) h->open = true;
    return st;
}

void wc_history_close(wc_history *h) {
    if (!h || !h->open) return;
    wada_records_close(&h->rec);
    h->open = false;
}

wada_status wc_history_append(wc_history *h, const wc_history_entry *entry, wada_err *err) {
    if (!h || !h->open || !entry) return wada_err_set(err, WADA_ERR_INVALID, "history append");

    char id[48];
    wada_records_fresh_id(&h->rec, id, sizeof id);

    char clean_summary[WC_SUMMARY_CAP];
    wada_records_sanitize_line(entry->summary, clean_summary, sizeof clean_summary);

    char buf[WC_SUMMARY_CAP + 160];
    int n = snprintf(buf, sizeof buf, "v1\n%lld\n%d\n%llu\n%llu\n%llu\n%s",
                     entry->unix_sec,
                     entry->dry_run ? 1 : 0,
                     (unsigned long long)entry->files_cleaned,
                     (unsigned long long)entry->bytes_cleaned,
                     (unsigned long long)entry->skipped_count,
                     clean_summary);
    if (n < 0 || (size_t)n >= sizeof buf) {
        return wada_err_set(err, WADA_ERR, "encode history entry");
    }
    return wada_records_put(&h->rec, id, buf, (size_t)n, err);
}

typedef struct history_ring {
    wc_history_entry *ring;
    int cap;
    int total;
} history_ring;

static bool history_visit(const char *id, wada_slice value, void *user) {
    history_ring *hr = (history_ring *)user;
    wc_history_entry e;
    memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "%s", id);

    const char *p = (const char *)value.p;
    const char *end = p + value.n;
    char line[WC_SUMMARY_CAP];

    if (!wada_records_next_line(&p, end, line, sizeof line) || strcmp(line, "v1") != 0) {
        return true;
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        e.unix_sec = strtoll(line, NULL, 10);
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        e.dry_run = (line[0] == '1');
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        e.files_cleaned = strtoull(line, NULL, 10);
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        e.bytes_cleaned = strtoull(line, NULL, 10);
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        e.skipped_count = strtoull(line, NULL, 10);
    }
    if (wada_records_next_line(&p, end, line, sizeof line)) {
        snprintf(e.summary, sizeof e.summary, "%s", line);
    }

    if (hr->cap > 0) {
        hr->ring[hr->total % hr->cap] = e;
        hr->total++;
    }
    return true;
}

wada_status wc_history_list(wc_history *h, wc_history_entry *out, int max_entries, int *out_count,
                          wada_err *err) {
    if (out_count) *out_count = 0;
    if (!h || !h->open || !out || max_entries <= 0) {
        return wada_err_set(err, WADA_ERR_INVALID, "history list");
    }

    wc_history_entry *ring = (wc_history_entry *)calloc((size_t)max_entries, sizeof *ring);
    if (!ring) return wada_err_set(err, WADA_ERR_NOMEM, "history list");

    history_ring ctx = {ring, max_entries, 0};
    wada_status st = wada_records_each(&h->rec, history_visit, &ctx, err);
    if (st != WADA_OK) {
        free(ring);
        return st;
    }

    int count = ctx.total < max_entries ? ctx.total : max_entries;
    for (int i = 0; i < count; i++) {
        int src_idx = (ctx.total - 1 - i) % max_entries;
        out[i] = ring[src_idx];
    }
    if (out_count) *out_count = count;
    free(ring);
    return WADA_OK;
}
