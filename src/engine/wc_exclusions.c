#include "wc_exclusions.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

static int ascii_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

wada_status wc_exclusions_open(wc_exclusions *ex, const wada_store_ops *ops, const char *db_path,
                               wada_err *err) {
    if (!ex || !ops || !db_path) return wada_err_set(err, WADA_ERR_INVALID, "exclusions open");
    memset(ex, 0, sizeof *ex);
    wada_status st = wada_records_open(&ex->rec, ops, db_path, "excl/", err);
    if (st == WADA_OK) ex->open = true;
    return st;
}

void wc_exclusions_close(wc_exclusions *ex) {
    if (!ex || !ex->open) return;
    wada_records_close(&ex->rec);
    ex->open = false;
}

typedef struct dup_check_ctx {
    const char *norm_path;
    bool found;
} dup_check_ctx;

static bool parse_record_path(wada_slice value, char *out, size_t cap) {
    const char *p = (const char *)value.p;
    const char *end = p + value.n;
    char line[WC_PATH_CAP];
    if (!wada_records_next_line(&p, end, line, sizeof line) || strcmp(line, "v1") != 0) {
        return false;
    }
    if (!wada_records_next_line(&p, end, out, cap) || !out[0]) {
        return false;
    }
    return true;
}

static bool dup_visit(const char *id, wada_slice value, void *user) {
    (void)id;
    dup_check_ctx *ctx = (dup_check_ctx *)user;
    char path[WC_PATH_CAP];
    if (parse_record_path(value, path, sizeof path)) {
        if (ascii_strcasecmp(path, ctx->norm_path) == 0) {
            ctx->found = true;
            return false;
        }
    }
    return true;
}

wada_status wc_exclusions_add(wc_exclusions *ex, const char *raw_path, wada_err *err) {
    if (!ex || !ex->open || !raw_path || !raw_path[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "invalid exclusion path");
    }

    char norm[WC_PATH_CAP];
    if (!wc_safety_normalize_path(raw_path, norm, sizeof norm)) {
        return wada_err_set(err, WADA_ERR_INVALID, "invalid exclusion path");
    }
    bool is_win = (isalpha((unsigned char)norm[0]) && norm[1] == ':' && norm[2] == '\\' &&
                   norm[3] != '\0');
    bool is_posix = (norm[0] == '/' && norm[1] != '\0');
    if (!is_win && !is_posix) {
        return wada_err_set(err, WADA_ERR_INVALID, "exclusion must be a full path");
    }

    dup_check_ctx dup = {norm, false};
    wada_status st = wada_records_each(&ex->rec, dup_visit, &dup, err);
    if (st != WADA_OK) return st;
    if (dup.found) {
        return wada_err_set(err, WADA_ERR_EXISTS, "path is already excluded");
    }

    char id[48];
    wada_records_fresh_id(&ex->rec, id, sizeof id);

    char safe_path[WC_PATH_CAP];
    wada_records_sanitize_line(norm, safe_path, sizeof safe_path);

    char buf[WC_PATH_CAP + 16];
    int n = snprintf(buf, sizeof buf, "v1\n%s", safe_path);
    if (n < 0 || (size_t)n >= sizeof buf) {
        return wada_err_set(err, WADA_ERR, "encode exclusion");
    }
    return wada_records_put(&ex->rec, id, buf, (size_t)n, err);
}

wada_status wc_exclusions_remove(wc_exclusions *ex, const char *id, wada_err *err) {
    if (!ex || !ex->open || !id || !id[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "exclusion remove");
    }
    return wada_records_del(&ex->rec, id, err);
}

typedef struct list_ctx {
    wc_exclusion_item *out;
    int max_items;
    int count;
} list_ctx;

static bool list_visit(const char *id, wada_slice value, void *user) {
    list_ctx *ctx = (list_ctx *)user;
    if (ctx->count >= ctx->max_items) return false;

    wc_exclusion_item item;
    memset(&item, 0, sizeof item);
    snprintf(item.id, sizeof item.id, "%s", id);
    if (parse_record_path(value, item.path, sizeof item.path)) {
        ctx->out[ctx->count++] = item;
    }
    return true;
}

wada_status wc_exclusions_list(wc_exclusions *ex, wc_exclusion_item *out, int max_items,
                               int *out_count, wada_err *err) {
    if (out_count) *out_count = 0;
    if (!ex || !ex->open || !out || max_items <= 0) {
        return wada_err_set(err, WADA_ERR_INVALID, "exclusions list");
    }
    list_ctx ctx = {out, max_items, 0};
    wada_status st = wada_records_each(&ex->rec, list_visit, &ctx, err);
    if (st == WADA_OK && out_count) *out_count = ctx.count;
    return st;
}

static bool norm_path_matches_one(const char *nc, size_t clen, const char *np, size_t plen) {
    if (plen == 0) return false;
    char sep = (np[0] == '/') ? '/' : '\\';
    if (clen == plen && ascii_strcasecmp(nc, np) == 0) {
        return true;
    }
    if (clen > plen && nc[plen] == sep && ascii_strncasecmp(nc, np, plen) == 0) {
        return true;
    }
    return false;
}

bool wc_exclusion_matches_norm(const char *norm_candidate, const char *const *norm_patterns,
                               const size_t *pattern_lens, int count) {
    if (!norm_candidate || !norm_candidate[0] || !norm_patterns || count <= 0) return false;
    size_t clen = strlen(norm_candidate);

    for (int i = 0; i < count; i++) {
        const char *np = norm_patterns[i];
        if (!np || !np[0]) continue;
        size_t plen = pattern_lens ? pattern_lens[i] : strlen(np);
        if (norm_path_matches_one(norm_candidate, clen, np, plen)) {
            return true;
        }
    }
    return false;
}

bool wc_exclusion_matches(const char *candidate, const char *const *patterns, int count) {
    if (!candidate || !candidate[0] || !patterns || count <= 0) return false;

    char nc[WC_PATH_CAP];
    if (!wc_safety_normalize_path(candidate, nc, sizeof nc)) return false;
    size_t clen = strlen(nc);

    for (int i = 0; i < count; i++) {
        if (!patterns[i] || !patterns[i][0]) continue;
        char np[WC_PATH_CAP];
        if (!wc_safety_normalize_path(patterns[i], np, sizeof np)) continue;
        if (norm_path_matches_one(nc, clen, np, strlen(np))) {
            return true;
        }
    }
    return false;
}
