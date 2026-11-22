#ifndef WC_EXCLUSIONS_H
#define WC_EXCLUSIONS_H

#include <stdbool.h>

#include "wada_records.h"
#include "wc_types.h"

typedef struct wc_exclusions {
    wada_records rec;
    bool open;
} wc_exclusions;

wada_status wc_exclusions_open(wc_exclusions *ex, const wada_store_ops *ops, const char *db_path,
                               wada_err *err);
void wc_exclusions_close(wc_exclusions *ex);

wada_status wc_exclusions_add(wc_exclusions *ex, const char *raw_path, wada_err *err);
wada_status wc_exclusions_remove(wc_exclusions *ex, const char *id, wada_err *err);
wada_status wc_exclusions_list(wc_exclusions *ex, wc_exclusion_item *out, int max_items,
                               int *out_count, wada_err *err);

/* Pure matcher: returns true if normalized `candidate` equals any pattern or
 * resides inside any directory pattern (case-insensitive on directory boundary). */
bool wc_exclusion_matches(const char *candidate, const char *const *patterns, int count);

/* Fast matcher where `norm_candidate` and every entry in `norm_patterns` are
 * already normalized via `wc_safety_normalize_path`. `pattern_lens` may be NULL. */
bool wc_exclusion_matches_norm(const char *norm_candidate,
                               const char *const *norm_patterns,
                               const size_t *pattern_lens,
                               int count);

#endif /* WC_EXCLUSIONS_H */
