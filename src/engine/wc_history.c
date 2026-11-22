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

static void sanitize_field(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    size_t n = 0;
    if (in) {
        for (; *in && n + 1 < cap; in++) {
            char c = *in;
            if (c == '\n' || c == '\r' || c == '|') c = ' ';
            out[n++] = c;
        }
    }
    out[n] = '\0';
}

static void sanitize_details(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return;
    size_t n = 0;
    if (in) {
        for (; *in && n + 1 < cap; in++) {
            char c = *in;
            if (c == '\r') continue;
            if (c == '\n' && (n == 0 || out[n - 1] == '\n')) continue;
            out[n++] = c;
        }
        while (n > 0 && out[n - 1] == '\n') n--;
    }
    out[n] = '\0';
}

void wc_history_append_detail(wc_history_entry *entry, const char *id, const char *label,
                              uint64_t files, uint64_t bytes, uint64_t skipped,
                              const char *note) {
    if (!entry || !id || !label) return;

    char clean_id[WC_ID_CAP];
    char clean_label[WC_LABEL_CAP];
    char clean_note[128];
    sanitize_field(id, clean_id, sizeof clean_id);
    sanitize_field(label, clean_label, sizeof clean_label);
    sanitize_field(note, clean_note, sizeof clean_note);

    char line[WC_ID_CAP + WC_LABEL_CAP + 128 + 64];
    int n = snprintf(line, sizeof line, "%s|%s|%llu|%llu|%llu|%s", clean_id, clean_label,
                     (unsigned long long)files, (unsigned long long)bytes,
                     (unsigned long long)skipped, clean_note);
    if (n < 0) return;

    size_t used = strlen(entry->details);
    if (used + 2 >= sizeof entry->details) return;
    if (used > 0) entry->details[used++] = '\n';
    size_t room = sizeof entry->details - used - 1;
    size_t len = (size_t)n < room ? (size_t)n : room;
    memcpy(entry->details + used, line, len);
    entry->details[used + len] = '\0';
}

wada_status wc_history_append(wc_history *h, const wc_history_entry *entry, wada_err *err) {
    if (!h || !h->open || !entry) return wada_err_set(err, WADA_ERR_INVALID, "history append");

    char id[48];
    wada_records_fresh_id(&h->rec, id, sizeof id);

    char clean_summary[WC_SUMMARY_CAP];
    wada_records_sanitize_line(entry->summary, clean_summary, sizeof clean_summary);

    char clean_details[WC_HISTORY_DETAIL_CAP];
    sanitize_details(entry->details, clean_details, sizeof clean_details);

    char buf[WC_SUMMARY_CAP + WC_HISTORY_DETAIL_CAP + 256];
    int n = snprintf(buf, sizeof buf, "v2\n%lld\n%d\n%llu\n%llu\n%llu\n%s",
                     entry->unix_sec,
                     entry->dry_run ? 1 : 0,
                     (unsigned long long)entry->files_cleaned,
                     (unsigned long long)entry->bytes_cleaned,
                     (unsigned long long)entry->skipped_count,
                     clean_summary);
    if (n < 0) {
        return wada_err_set(err, WADA_ERR, "encode history entry");
    }
    size_t used = (size_t)n;
    if (clean_details[0] && used + 1 < sizeof buf) {
        buf[used++] = '\n';
        size_t detail_len = strlen(clean_details);
        if (detail_len > sizeof buf - used - 1) detail_len = sizeof buf - used - 1;
        memcpy(buf + used, clean_details, detail_len);
        used += detail_len;
        buf[used] = '\0';
    }
    if (used >= sizeof buf) {
        return wada_err_set(err, WADA_ERR, "encode history entry");
    }
    return wada_records_put(&h->rec, id, buf, used, err);
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
    char line[WC_HISTORY_DETAIL_CAP];

    if (!wada_records_next_line(&p, end, line, sizeof line)) return true;
    bool v2 = strcmp(line, "v2") == 0;
    if (!v2 && strcmp(line, "v1") != 0) return true;

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
        snprintf(e.summary, sizeof e.summary, "%.*s", (int)sizeof e.summary - 1, line);
    }

    if (v2) {
        size_t used = 0;
        while (wada_records_next_line(&p, end, line, sizeof line)) {
            size_t len = strlen(line);
            if (len == 0) continue;
            if (used > 0) {
                if (used + 2 >= sizeof e.details) break;
                e.details[used++] = '\n';
            }
            size_t room = sizeof e.details - used - 1;
            if (len > room) len = room;
            memcpy(e.details + used, line, len);
            used += len;
            e.details[used] = '\0';
            if (used >= sizeof e.details - 1) break;
        }
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
