#ifndef WC_HISTORY_H
#define WC_HISTORY_H

#include <stdbool.h>

#include "wada_records.h"
#include "wc_types.h"

typedef struct wc_history {
    wada_records rec;
    bool open;
} wc_history;

wada_status wc_history_open(wc_history *h, const wada_store_ops *ops, const char *db_path, wada_err *err);
void wc_history_close(wc_history *h);

wada_status wc_history_append(wc_history *h, const wc_history_entry *entry, wada_err *err);

/* Appends one per-target row to an entry before it is persisted. Fields are
 * sanitized for the line-based record format and truncated to the detail cap. */
void wc_history_append_detail(wc_history_entry *entry, const char *id, const char *label,
                              uint64_t files, uint64_t bytes, uint64_t skipped,
                              const char *note);

/* Fills `out` with up to `max_entries` entries ordered newest-first. */
wada_status wc_history_list(wc_history *h, wc_history_entry *out, int max_entries, int *out_count,
                          wada_err *err);

#endif /* WC_HISTORY_H */
