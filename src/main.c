#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "wada_boot.h"
#include "wada_instance.h"
#include "wada_store_sqlite.h"
#include "wc_exclusions.h"
#include "wc_history.h"
#include "wc_platform.h"
#include "wc_product.h"
#include "wc_rules.h"
#include "wc_safety.h"

typedef struct sched_excl_ctx {
    wc_exclusion_item items[WC_MAX_EXCLUSIONS];
    const char *ptrs[WC_MAX_EXCLUSIONS];
    int count;
} sched_excl_ctx;

static bool sched_is_excluded(const char *path, void *user) {
    const sched_excl_ctx *ctx = (const sched_excl_ctx *)user;
    if (!ctx || ctx->count <= 0) return false;
    return wc_exclusion_matches(path, ctx->ptrs, ctx->count);
}

static void resolve_clean_data_dir(char *out, size_t cap) {
    const char *env = getenv("WADA_DATA_DIR");
    if (env && env[0]) {
        snprintf(out, cap, "%s", env);
        return;
    }
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (local && local[0]) {
        snprintf(out, cap, "%s\\Wada Clean", local);
        return;
    }
#else
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(out, cap, "%s/Library/Application Support/Wada Clean", home);
        return;
    }
#endif
    snprintf(out, cap, ".wada-Wada Clean");
}

static int run_scheduled_clean(bool dry_run) {
    char data_dir[640];
    resolve_clean_data_dir(data_dir, sizeof data_dir);
#ifdef _WIN32
    _mkdir(data_dir);
#else
    mkdir(data_dir, 0755);
#endif

    wada_err err;
    wada_err_clear(&err);
    wada_instance *inst = wada_instance_acquire(data_dir, &err);
    if (!inst) return 2;

    char db_path[700];
    snprintf(db_path, sizeof db_path, "%s" WADA_PATH_SEP "wada.db", data_dir);

    const wada_store_ops *ops = wada_store_sqlite();
    wc_exclusions ex;
    wc_history hist;
    if (wc_exclusions_open(&ex, ops, db_path, &err) != WADA_OK ||
        wc_history_open(&hist, ops, db_path, &err) != WADA_OK) {
        wc_history_close(&hist);
        wc_exclusions_close(&ex);
        wada_instance_release(inst);
        return 1;
    }

    sched_excl_ctx *ectx = (sched_excl_ctx *)calloc(1, sizeof *ectx);
    if (ectx) {
        int ec = 0;
        if (wc_exclusions_list(&ex, ectx->items, WC_MAX_EXCLUSIONS, &ec, &err) == WADA_OK) {
            ectx->count = ec;
            for (int i = 0; i < ec; i++) ectx->ptrs[i] = ectx->items[i].path;
        }
    }
    wc_exclude_fn excl_fn = (ectx && ectx->count > 0) ? sched_is_excluded : NULL;

    bool is_admin = wc_plat_is_admin();
    volatile int cancel = 0;
    uint64_t total_files = 0, total_bytes = 0, total_skipped = 0;
    int ran = 0;

    int nrules = wc_rules_count();
    for (int i = 0; i < nrules; i++) {
        const wc_target_def *def = wc_rules_get(i);
        if (!def || !def->default_enabled) continue;
        if (def->needs_admin && !is_admin) continue;

        if (def->kind == WC_TARGET_RECYCLE_BIN) {
            uint64_t files = 0, bytes = 0;
            wc_plat_query_recycle_bin(&files, &bytes);
            if (dry_run || files == 0 || wc_plat_empty_recycle_bin()) {
                total_files += files;
                total_bytes += bytes;
            } else {
                total_skipped += files;
            }
            ran++;
        } else {
            if (def->process_name[0] && wc_plat_process_running(def->process_name)) {
                total_skipped++;
                continue;
            }
            char path[WC_PATH_CAP];
            if (!wc_rules_resolve_path(def, path, sizeof path)) continue;
            uint64_t f = 0, b = 0, s = 0;
            if (wc_plat_clean_dir_ex(path, dry_run, &cancel, excl_fn, ectx, &f, &b, &s)) {
                total_files += f;
                total_bytes += b;
                total_skipped += s;
            }
            ran++;
        }
    }

    char size_buf[32];
    wc_format_bytes(total_bytes, size_buf, sizeof size_buf);

    wc_history_entry entry;
    memset(&entry, 0, sizeof entry);
    entry.unix_sec = (long long)time(NULL);
    entry.dry_run = dry_run;
    entry.files_cleaned = total_files;
    entry.bytes_cleaned = total_bytes;
    entry.skipped_count = total_skipped;
    snprintf(entry.summary, sizeof entry.summary, "Scheduled: %s (%d targets)", size_buf, ran);
    wc_history_append(&hist, &entry, &err);

    free(ectx);
    wc_history_close(&hist);
    wc_exclusions_close(&ex);
    wada_instance_release(inst);
    return 0;
}

int main(int argc, char **argv) {
    bool dry_run = false;
    bool scheduled_clean = false;

    char **filtered = (char **)calloc((size_t)(argc + 1), sizeof *filtered);
    int out_argc = 0;

    if (filtered && argc > 0) {
        filtered[out_argc++] = argv[0];
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") == 0) {
                dry_run = true;
                wc_cleaner_set_initial_dry_run(true);
            } else if (strcmp(argv[i], "--scheduled-clean") == 0) {
                scheduled_clean = true;
            } else {
                filtered[out_argc++] = argv[i];
            }
        }
    }

    if (scheduled_clean) {
        free(filtered);
        return run_scheduled_clean(dry_run);
    }

    const wada_product *products[] = { &wc_cleaner_product };
    wada_boot_desc desc = {
        "Wada Clean",
        1120,
        740,
        false,
        wada_store_sqlite(),
        products,
        1,
        "cleaner",
        "Wada Clean",
        NULL,
    };
    int rc = wada_boot_run(&desc, filtered ? out_argc : argc, filtered ? filtered : argv);
    free(filtered);

    if (wc_cleaner_take_relaunch_admin()) {
        wc_plat_relaunch_as_admin();
    }
    return rc;
}
