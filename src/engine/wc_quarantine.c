#include "wc_quarantine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc_platform.h"
#include "wc_safety.h"

static const char *path_basename(const char *path) {
    if (!path || !path[0]) return NULL;
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            last = p + 1;
        }
    }
    if (!last[0] || strcmp(last, ".") == 0 || strcmp(last, "..") == 0) return NULL;
    return last;
}

static bool join_path_sep(char *out, size_t cap, const char *dir, const char *name) {
    if (!out || cap == 0 || !dir || !name || !dir[0] || !name[0]) return false;
    char sep = (dir[0] == '/') ? '/' : '\\';
    size_t dlen = strlen(dir);
    bool has_sep = (dlen > 0 && (dir[dlen - 1] == '\\' || dir[dlen - 1] == '/'));
    int n = has_sep ? snprintf(out, cap, "%s%s", dir, name)
                    : snprintf(out, cap, "%s%c%s", dir, sep, name);
    return n > 0 && (size_t)n < cap;
}

static void parent_dir_of(const char *path, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!path) return;
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/') {
        len--;
    }
    if (len == 0) return;
    while (len > 1 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        if (len == 3 && path[1] == ':') break;
        len--;
    }
    if (len >= cap) len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static bool parse_record(const char *id, const char *buf, size_t len, wc_quarantine_item *out) {
    if (!id || !buf || !out) return false;
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "%s", id);

    const char *p = buf;
    const char *end = buf + len;
    char line[WC_PATH_CAP];

    if (!wada_records_next_line(&p, end, line, sizeof line) || strcmp(line, "v1") != 0) {
        return false;
    }
    if (!wada_records_next_line(&p, end, line, sizeof line)) return false;
    out->unix_sec = strtoll(line, NULL, 10);

    if (!wada_records_next_line(&p, end, line, sizeof line)) return false;
    out->bytes = strtoull(line, NULL, 10);

    if (!wada_records_next_line(&p, end, out->source_tag, sizeof out->source_tag)) return false;
    if (!wada_records_next_line(&p, end, out->orig_path, sizeof out->orig_path) ||
        !out->orig_path[0]) {
        return false;
    }
    if (!wada_records_next_line(&p, end, out->vault_path, sizeof out->vault_path) ||
        !out->vault_path[0]) {
        return false;
    }
    return true;
}

wada_status wc_quarantine_open(wc_quarantine *q, const wada_store_ops *ops, const char *db_path,
                               const char *vault_dir, wada_err *err) {
    if (!q || !ops || !db_path || !vault_dir || !vault_dir[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine open");
    }
    memset(q, 0, sizeof *q);

    if (!wc_safety_normalize_path(vault_dir, q->vault_dir, sizeof q->vault_dir) ||
        !wc_safety_is_valid_target_root(q->vault_dir)) {
        memset(q, 0, sizeof *q);
        return wada_err_set(err, WADA_ERR_INVALID, "invalid quarantine vault directory");
    }

    wada_status st = wada_records_open(&q->rec, ops, db_path, "quarantine/", err);
    if (st == WADA_OK) q->open = true;
    return st;
}

void wc_quarantine_close(wc_quarantine *q) {
    if (!q || !q->open) return;
    wada_records_close(&q->rec);
    q->open = false;
}

wada_status wc_quarantine_add(wc_quarantine *q, const char *orig_path, const char *source_tag,
                              bool dry_run, wc_quarantine_item *out_item, wada_err *err) {
    if (!q || !q->open || !orig_path || !orig_path[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine add");
    }

    char norm_orig[WC_PATH_CAP];
    if (!wc_safety_normalize_path(orig_path, norm_orig, sizeof norm_orig) ||
        !wc_safety_is_quarantine_safe_path(norm_orig)) {
        return wada_err_set(err, WADA_ERR_INVALID, "unsafe quarantine source path");
    }

    bool exists = false;
    bool is_dir = false;
    uint64_t bytes = 0;
    if (!wc_plat_path_info(norm_orig, &exists, &is_dir, &bytes) || !exists) {
        return wada_err_set(err, WADA_ERR_NOT_FOUND, "source path does not exist or is a link");
    }
    (void)is_dir;

    const char *base = path_basename(norm_orig);
    if (!base) {
        return wada_err_set(err, WADA_ERR_INVALID, "invalid file name");
    }

    char id[48];
    wada_records_fresh_id(&q->rec, id, sizeof id);

    char item_dir[WC_PATH_CAP];
    char vault_path[WC_PATH_CAP];
    if (!join_path_sep(item_dir, sizeof item_dir, q->vault_dir, id) ||
        !join_path_sep(vault_path, sizeof vault_path, item_dir, base)) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine path too long");
    }

    long long now = (long long)time(NULL);
    const char *tag = (source_tag && source_tag[0]) ? source_tag : "manual";

    if (out_item) {
        memset(out_item, 0, sizeof *out_item);
        snprintf(out_item->id, sizeof out_item->id, "%s", id);
        snprintf(out_item->orig_path, sizeof out_item->orig_path, "%s", norm_orig);
        snprintf(out_item->vault_path, sizeof out_item->vault_path, "%s", vault_path);
        snprintf(out_item->source_tag, sizeof out_item->source_tag, "%s", tag);
        out_item->bytes = bytes;
        out_item->unix_sec = now;
    }

    if (dry_run) return WADA_OK;

    if (!wc_plat_ensure_dir(item_dir)) {
        return wada_err_set(err, WADA_ERR_IO, "create quarantine directory");
    }
    if (!wc_plat_move_item(norm_orig, vault_path)) {
        (void)wc_plat_delete_item(item_dir);
        return wada_err_set(err, WADA_ERR_IO, "move item to quarantine");
    }

    char safe_tag[64];
    char safe_orig[WC_PATH_CAP];
    char safe_vault[WC_PATH_CAP];
    wada_records_sanitize_line(tag, safe_tag, sizeof safe_tag);
    wada_records_sanitize_line(norm_orig, safe_orig, sizeof safe_orig);
    wada_records_sanitize_line(vault_path, safe_vault, sizeof safe_vault);

    char buf[WC_PATH_CAP * 2 + 192];
    int n = snprintf(buf, sizeof buf, "v1\n%lld\n%llu\n%s\n%s\n%s",
                     now, (unsigned long long)bytes, safe_tag, safe_orig, safe_vault);
    if (n < 0 || (size_t)n >= sizeof buf) {
        (void)wc_plat_move_item(vault_path, norm_orig);
        (void)wc_plat_delete_item(item_dir);
        return wada_err_set(err, WADA_ERR, "encode quarantine record");
    }

    wada_status st = wada_records_put(&q->rec, id, buf, (size_t)n, err);
    if (st != WADA_OK) {
        (void)wc_plat_move_item(vault_path, norm_orig);
        (void)wc_plat_delete_item(item_dir);
        return st;
    }
    return WADA_OK;
}

typedef struct q_list_ctx {
    wc_quarantine_item *items;
    int cap;
    int count;
} q_list_ctx;

static bool q_list_visit(const char *id, wada_slice value, void *user) {
    q_list_ctx *ctx = (q_list_ctx *)user;
    if (ctx->count >= ctx->cap) return false;
    wc_quarantine_item item;
    if (parse_record(id, (const char *)value.p, value.n, &item)) {
        ctx->items[ctx->count++] = item;
    }
    return true;
}

static int cmp_q_newest(const void *pa, const void *pb) {
    const wc_quarantine_item *a = (const wc_quarantine_item *)pa;
    const wc_quarantine_item *b = (const wc_quarantine_item *)pb;
    if (b->unix_sec > a->unix_sec) return 1;
    if (b->unix_sec < a->unix_sec) return -1;
    return strcmp(b->id, a->id);
}

wada_status wc_quarantine_list(wc_quarantine *q, wc_quarantine_item *out, int max_items,
                               int *out_count, wada_err *err) {
    if (out_count) *out_count = 0;
    if (!q || !q->open || !out || max_items <= 0) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine list");
    }
    q_list_ctx ctx = {out, max_items, 0};
    wada_status st = wada_records_each(&q->rec, q_list_visit, &ctx, err);
    if (st == WADA_OK) {
        if (ctx.count > 1) {
            qsort(out, (size_t)ctx.count, sizeof out[0], cmp_q_newest);
        }
        if (out_count) *out_count = ctx.count;
    }
    return st;
}

wada_status wc_quarantine_restore(wc_quarantine *q, const char *id, wada_err *err) {
    if (!q || !q->open || !id || !id[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine restore");
    }

    char buf[WC_PATH_CAP * 2 + 192];
    size_t len = 0;
    wada_status st = wada_records_get(&q->rec, id, buf, sizeof buf, &len, err);
    if (st != WADA_OK) return st;

    wc_quarantine_item item;
    if (!parse_record(id, buf, len, &item)) {
        return wada_err_set(err, WADA_ERR_INVALID, "corrupt quarantine record");
    }
    if (!wc_safety_is_quarantine_safe_path(item.orig_path)) {
        return wada_err_set(err, WADA_ERR_INVALID, "unsafe restore destination");
    }

    bool dst_exists = false;
    if (wc_plat_path_info(item.orig_path, &dst_exists, NULL, NULL) && dst_exists) {
        return wada_err_set(err, WADA_ERR_EXISTS, "original path already exists");
    }

    bool src_exists = false;
    if (!wc_plat_path_info(item.vault_path, &src_exists, NULL, NULL) || !src_exists) {
        return wada_err_set(err, WADA_ERR_NOT_FOUND, "quarantined file missing");
    }

    char orig_parent[WC_PATH_CAP];
    parent_dir_of(item.orig_path, orig_parent, sizeof orig_parent);
    if (orig_parent[0] && !wc_plat_ensure_dir(orig_parent)) {
        return wada_err_set(err, WADA_ERR_IO, "create restore destination directory");
    }

    if (!wc_plat_move_item(item.vault_path, item.orig_path)) {
        return wada_err_set(err, WADA_ERR_IO, "move item from quarantine");
    }

    char item_dir[WC_PATH_CAP];
    parent_dir_of(item.vault_path, item_dir, sizeof item_dir);
    if (item_dir[0] && wc_safety_path_within(q->vault_dir, item_dir)) {
        (void)wc_plat_delete_item(item_dir);
    }

    return wada_records_del(&q->rec, id, err);
}

wada_status wc_quarantine_delete(wc_quarantine *q, const char *id, wada_err *err) {
    if (!q || !q->open || !id || !id[0]) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine delete");
    }

    char buf[WC_PATH_CAP * 2 + 192];
    size_t len = 0;
    wada_status st = wada_records_get(&q->rec, id, buf, sizeof buf, &len, err);
    if (st != WADA_OK) return st;

    wc_quarantine_item item;
    if (parse_record(id, buf, len, &item)) {
        bool exists = false;
        if (wc_plat_path_info(item.vault_path, &exists, NULL, NULL) && exists) {
            if (!wc_plat_delete_item(item.vault_path)) {
                return wada_err_set(err, WADA_ERR_IO, "delete quarantined file");
            }
        }
        char item_dir[WC_PATH_CAP];
        parent_dir_of(item.vault_path, item_dir, sizeof item_dir);
        if (item_dir[0] && wc_safety_path_within(q->vault_dir, item_dir)) {
            (void)wc_plat_delete_item(item_dir);
        }
    }

    return wada_records_del(&q->rec, id, err);
}

wada_status wc_quarantine_purge_older_than(wc_quarantine *q, long long max_age_sec,
                                           long long now_sec, int *out_removed, wada_err *err) {
    if (out_removed) *out_removed = 0;
    if (!q || !q->open || max_age_sec < 0) {
        return wada_err_set(err, WADA_ERR_INVALID, "quarantine purge");
    }
    if (now_sec <= 0) now_sec = (long long)time(NULL);

    wc_quarantine_item *items = (wc_quarantine_item *)calloc(WC_MAX_QUARANTINE, sizeof *items);
    if (!items) return wada_err_set(err, WADA_ERR_NOMEM, "quarantine purge");

    int count = 0;
    wada_status st = wc_quarantine_list(q, items, WC_MAX_QUARANTINE, &count, err);
    if (st != WADA_OK) {
        free(items);
        return st;
    }

    int removed = 0;
    for (int i = 0; i < count; i++) {
        long long age = now_sec - items[i].unix_sec;
        if (age >= max_age_sec) {
            if (wc_quarantine_delete(q, items[i].id, err) == WADA_OK) {
                removed++;
            }
        }
    }
    free(items);
    if (out_removed) *out_removed = removed;
    return WADA_OK;
}
