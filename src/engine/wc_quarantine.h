#ifndef WC_QUARANTINE_H
#define WC_QUARANTINE_H

#include <stdbool.h>
#include <stdint.h>

#include "wada_records.h"
#include "wc_types.h"

#define WC_QUARANTINE_DEFAULT_TTL_SEC (14LL * 24LL * 3600LL)

typedef struct wc_quarantine {
    wada_records rec;
    char vault_dir[WC_PATH_CAP];
    bool open;
} wc_quarantine;

wada_status wc_quarantine_open(wc_quarantine *q, const wada_store_ops *ops, const char *db_path,
                               const char *vault_dir, wada_err *err);
void wc_quarantine_close(wc_quarantine *q);

/* Moves `orig_path` (file or directory) into the quarantine vault and logs it
 * in SQLite. When `dry_run` is true, inspects size without moving or recording. */
wada_status wc_quarantine_add(wc_quarantine *q, const char *orig_path, const char *source_tag,
                              bool dry_run, wc_quarantine_item *out_item, wada_err *err);

/* Fills `out` with up to `max_items` entries ordered newest-first. */
wada_status wc_quarantine_list(wc_quarantine *q, wc_quarantine_item *out, int max_items,
                               int *out_count, wada_err *err);

/* Restores a quarantined item back to its original path (fails if destination
 * already exists). */
wada_status wc_quarantine_restore(wc_quarantine *q, const char *id, wada_err *err);

/* Permanently deletes a quarantined item from both disk and the vault index. */
wada_status wc_quarantine_delete(wc_quarantine *q, const char *id, wada_err *err);

/* Deletes all quarantined items older than `max_age_sec` relative to `now_sec`
 * (pass 0 to use current time). */
wada_status wc_quarantine_purge_older_than(wc_quarantine *q, long long max_age_sec,
                                           long long now_sec, int *out_removed, wada_err *err);

#endif /* WC_QUARANTINE_H */
