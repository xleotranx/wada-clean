#ifndef WC_RULES_H
#define WC_RULES_H

#include <stdbool.h>
#include <stddef.h>

#include "wc_types.h"

int wc_rules_count(void);
const wc_target_def *wc_rules_get(int index);

/* Resolves a target definition's normalized path using platform env vars.
 * Returns false for non-path targets (such as WC_TARGET_RECYCLE_BIN) or when
 * the target resolves to an invalid/unsafe root. */
bool wc_rules_resolve_path(const wc_target_def *def, char *out, size_t cap);

#endif /* WC_RULES_H */
