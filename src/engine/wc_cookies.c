#include "wc_cookies.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int ascii_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

bool wc_cookie_normalize_domain(const char *in, char *out, size_t cap) {
    if (!in || !out || cap < 2) return false;
    out[0] = '\0';

    const char *p = in;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (ascii_strncasecmp(p, "https://", 8) == 0) p += 8;
    else if (ascii_strncasecmp(p, "http://", 7) == 0) p += 7;

    while (*p == '.') p++;

    char buf[192];
    size_t n = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' &&
           *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '.' || c == '-')) {
            return false;
        }
        if (n + 1 >= sizeof buf || n + 1 >= cap) return false;
        buf[n++] = (char)tolower(c);
        p++;
    }

    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p != '\0') return false;
    }

    while (n > 0 && buf[n - 1] == '.') n--;
    if (n == 0) return false;
    if (buf[0] == '-' || buf[n - 1] == '-') return false;

    /* Reject consecutive dots "..". */
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '.' && buf[i - 1] == '.') return false;
    }

    buf[n] = '\0';
    memcpy(out, buf, n + 1);
    return true;
}

bool wc_cookie_domain_allowed(const char *host_key, const char *const *allowlist, int count) {
    if (!host_key || !host_key[0] || !allowlist || count <= 0) return false;

    char nh[192];
    if (!wc_cookie_normalize_domain(host_key, nh, sizeof nh)) return false;
    size_t hlen = strlen(nh);

    for (int i = 0; i < count; i++) {
        if (!allowlist[i] || !allowlist[i][0]) continue;
        char np[192];
        if (!wc_cookie_normalize_domain(allowlist[i], np, sizeof np)) continue;
        size_t plen = strlen(np);

        if (hlen == plen && strcmp(nh, np) == 0) return true;
        if (hlen > plen && nh[hlen - plen - 1] == '.' &&
            strcmp(nh + (hlen - plen), np) == 0) {
            return true;
        }
    }
    return false;
}

wada_status wc_cookies_open(wc_cookies *c, const wada_store_ops *ops, const char *db_path,
                            wada_err *err) {
    if (!c || !ops || !db_path) return wada_err_set(err, WADA_ERR_INVALID, "cookies open");
    memset(c, 0, sizeof *c);
    wada_status st = wada_records_open(&c->rec, ops, db_path, "cookie_allow/", err);
    if (st == WADA_OK) c->open = true;
    return st;
}

void wc_cookies_close(wc_cookies *c) {
    if (!c || !c->open) return;
    wada_records_close(&c->rec);
    c->open = false;
}

static bool parse_domain_record(wada_slice value, char *out, size_t cap) {
    const char *p = (const char *)value.p;
    const char *end = p + value.n;
    char line[192];
    if (!wada_records_next_line(&p, end, line, sizeof line) || strcmp(line, "v1") != 0) {
        return false;
    }
    if (!wada_records_next_line(&p, end, out, cap) || !out[0]) {
        return false;
    }
    return true;
}

typedef struct cookie_dup_ctx {
    const char *norm_domain;
    bool found;
} cookie_dup_ctx;

static bool cookie_dup_visit(const char *id, wada_slice value, void *user) {
    (void)id;
    cookie_dup_ctx *ctx = (cookie_dup_ctx *)user;
    char dom[192];
    if (parse_domain_record(value, dom, sizeof dom)) {
        if (strcmp(dom, ctx->norm_domain) == 0) {
            ctx->found = true;
            return false;
        }
    }
    return true;
}

wada_status wc_cookies_add(wc_cookies *c, const char *raw_domain, wada_err *err) {
    if (!c || !c->open || !raw_domain || !raw_domain[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "invalid domain");
    }

    char norm[192];
    if (!wc_cookie_normalize_domain(raw_domain, norm, sizeof norm)) {
        return wada_err_set(err, WADA_ERR_INVALID, "invalid domain");
    }

    cookie_dup_ctx dup = {norm, false};
    wada_status st = wada_records_each(&c->rec, cookie_dup_visit, &dup, err);
    if (st != WADA_OK) return st;
    if (dup.found) {
        return wada_err_set(err, WADA_ERR_EXISTS, "domain is already allowlisted");
    }

    char id[48];
    wada_records_fresh_id(&c->rec, id, sizeof id);

    char buf[224];
    int n = snprintf(buf, sizeof buf, "v1\n%s", norm);
    if (n < 0 || (size_t)n >= sizeof buf) {
        return wada_err_set(err, WADA_ERR, "encode cookie domain");
    }
    return wada_records_put(&c->rec, id, buf, (size_t)n, err);
}

wada_status wc_cookies_remove(wc_cookies *c, const char *id, wada_err *err) {
    if (!c || !c->open || !id || !id[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "cookie remove");
    }
    return wada_records_del(&c->rec, id, err);
}

typedef struct cookie_list_ctx {
    wc_cookie_allow_item *out;
    int max_items;
    int count;
} cookie_list_ctx;

static bool cookie_list_visit(const char *id, wada_slice value, void *user) {
    cookie_list_ctx *ctx = (cookie_list_ctx *)user;
    if (ctx->count >= ctx->max_items) return false;

    wc_cookie_allow_item item;
    memset(&item, 0, sizeof item);
    snprintf(item.id, sizeof item.id, "%s", id);
    if (parse_domain_record(value, item.domain, sizeof item.domain)) {
        ctx->out[ctx->count++] = item;
    }
    return true;
}

wada_status wc_cookies_list(wc_cookies *c, wc_cookie_allow_item *out, int max_items,
                            int *out_count, wada_err *err) {
    if (out_count) *out_count = 0;
    if (!c || !c->open || !out || max_items <= 0) {
        return wada_err_set(err, WADA_ERR_INVALID, "cookies list");
    }
    cookie_list_ctx ctx = {out, max_items, 0};
    wada_status st = wada_records_each(&c->rec, cookie_list_visit, &ctx, err);
    if (st == WADA_OK && out_count) *out_count = ctx.count;
    return st;
}
