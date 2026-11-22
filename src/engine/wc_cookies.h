#ifndef WC_COOKIES_H
#define WC_COOKIES_H

#include <stdbool.h>
#include <stddef.h>

#include "wada_records.h"
#include "wc_types.h"

typedef struct wc_cookies {
    wada_records rec;
    bool open;
} wc_cookies;

wada_status wc_cookies_open(wc_cookies *c, const wada_store_ops *ops, const char *db_path,
                            wada_err *err);
void wc_cookies_close(wc_cookies *c);

wada_status wc_cookies_add(wc_cookies *c, const char *raw_domain, wada_err *err);
wada_status wc_cookies_remove(wc_cookies *c, const char *id, wada_err *err);
wada_status wc_cookies_list(wc_cookies *c, wc_cookie_allow_item *out, int max_items,
                            int *out_count, wada_err *err);

/* Pure domain normalization & matching helpers. */
bool wc_cookie_normalize_domain(const char *in, char *out, size_t cap);
bool wc_cookie_domain_allowed(const char *host_key, const char *const *allowlist, int count);

#endif /* WC_COOKIES_H */
