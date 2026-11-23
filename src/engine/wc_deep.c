#include "wc_deep.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"
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

static const char *ascii_strcasestr(const char *hay, const char *needle) {
    if (!hay || !needle || !needle[0]) return NULL;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (ascii_strncasecmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

static void copy_trimmed(const char *src, size_t len, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!src) return;
    while (len > 0 && (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')) {
        src++;
        len--;
    }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' ||
                       src[len - 1] == '\n')) {
        len--;
    }
    if (len >= cap) len = cap - 1;
    if (len > 0) memcpy(out, src, len);
    out[len] = '\0';
}

bool wc_deep_is_safe_oem_inf(const char *name) {
    if (!name || !name[0]) return false;
    if (ascii_strncasecmp(name, "oem", 3) != 0) return false;
    const char *p = name + 3;
    int digits = 0;
    while (isdigit((unsigned char)*p)) {
        digits++;
        p++;
    }
    if (digits < 1 || digits > 6) return false;
    return ascii_strcasecmp(p, ".inf") == 0;
}

static bool extract_colon_value(const char *line, size_t len, const char *key, char *out,
                                size_t cap) {
    size_t klen = strlen(key);
    const char *trimmed = line;
    size_t tlen = len;
    while (tlen > 0 && (*trimmed == ' ' || *trimmed == '\t')) {
        trimmed++;
        tlen--;
    }
    if (tlen < klen || ascii_strncasecmp(trimmed, key, klen) != 0) return false;
    const char *after = trimmed + klen;
    size_t rem = tlen - klen;
    while (rem > 0 && (*after == ' ' || *after == '\t')) {
        after++;
        rem--;
    }
    if (rem == 0 || *after != ':') return false;
    copy_trimmed(after + 1, rem - 1, out, cap);
    return true;
}

bool wc_deep_parse_dism_analyze(const char *output, wc_winsxs_info *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!output || !output[0]) return false;

    bool found_any = false;
    const char *p = output;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char val[128];

        if (extract_colon_value(p, len, "Actual Size of Component Store", out->actual_size_str,
                                sizeof out->actual_size_str)) {
            found_any = true;
        } else if (extract_colon_value(p, len, "Backups and Disabled Features",
                                       out->backups_size_str, sizeof out->backups_size_str)) {
            found_any = true;
        } else if (extract_colon_value(p, len, "Number of Reclaimable Packages", val,
                                       sizeof val)) {
            out->reclaimable_packages = atoi(val);
            found_any = true;
        } else if (extract_colon_value(p, len, "Component Store Cleanup Recommended", val,
                                       sizeof val)) {
            out->cleanup_recommended = (ascii_strcasestr(val, "yes") != NULL);
            found_any = true;
        }

        if (!nl) break;
        p = nl + 1;
    }

    out->analyzed = found_any;
    if (found_any) {
        snprintf(out->raw_summary, sizeof out->raw_summary,
                 "Store: %s | Reclaimable backups: %s (%d pkgs)",
                 out->actual_size_str[0] ? out->actual_size_str : "-",
                 out->backups_size_str[0] ? out->backups_size_str : "-",
                 out->reclaimable_packages);
    }
    return found_any;
}

static int cmp_dotted_version(const char *va, const char *vb) {
    const char *pa = va ? va : "";
    const char *pb = vb ? vb : "";
    while (*pa || *pb) {
        unsigned long na = 0, nb = 0;
        if (*pa) {
            char *next = NULL;
            na = strtoul(pa, &next, 10);
            pa = next;
            while (*pa && *pa != '.') pa++;
            if (*pa == '.') pa++;
        }
        if (*pb) {
            char *next = NULL;
            nb = strtoul(pb, &next, 10);
            pb = next;
            while (*pb && *pb != '.') pb++;
            if (*pb == '.') pb++;
        }
        if (na > nb) return 1;
        if (na < nb) return -1;
    }
    return 0;
}

int wc_deep_parse_pnputil_drivers(const char *drivers_out, const char *devices_out,
                                  wc_driver_item *out, int max_items) {
    if (!drivers_out || !out || max_items <= 0) return 0;

    char (*used_infs)[32] = (char (*)[32])calloc((size_t)(WC_MAX_DRIVERS * 2), 32);
    int used_count = 0;

    if (devices_out && used_infs) {
        const char *p = devices_out;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char val[32];
            if (extract_colon_value(p, len, "Driver Name", val, sizeof val)) {
                if (wc_deep_is_safe_oem_inf(val)) {
                    bool dup = false;
                    for (int i = 0; i < used_count; i++) {
                        if (ascii_strcasecmp(used_infs[i], val) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup && used_count < WC_MAX_DRIVERS * 2) {
                        memcpy(used_infs[used_count++], val, 32);
                    }
                }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }

    int count = 0;
    wc_driver_item cur;
    memset(&cur, 0, sizeof cur);
    bool have_cur = false;

    const char *p = drivers_out;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char val[160];

        if (extract_colon_value(p, len, "Published Name", val, sizeof cur.published_name)) {
            if (have_cur && wc_deep_is_safe_oem_inf(cur.published_name) && count < max_items) {
                out[count++] = cur;
            }
            memset(&cur, 0, sizeof cur);
            memcpy(cur.published_name, val, sizeof cur.published_name);
            have_cur = true;
        } else if (have_cur) {
            if (extract_colon_value(p, len, "Original Name", cur.original_name,
                                    sizeof cur.original_name)) {
            } else if (extract_colon_value(p, len, "Provider Name", cur.provider,
                                           sizeof cur.provider)) {
            } else if (extract_colon_value(p, len, "Class Name", cur.class_name,
                                           sizeof cur.class_name)) {
            } else if (extract_colon_value(p, len, "Driver Version", val, sizeof val)) {
                const char *sp = strchr(val, ' ');
                if (sp) {
                    copy_trimmed(val, (size_t)(sp - val), cur.date, sizeof cur.date);
                    copy_trimmed(sp + 1, strlen(sp + 1), cur.version, sizeof cur.version);
                } else {
                    copy_trimmed(val, strlen(val), cur.version, sizeof cur.version);
                }
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    if (have_cur && wc_deep_is_safe_oem_inf(cur.published_name) && count < max_items) {
        out[count++] = cur;
    }

    for (int i = 0; i < count; i++) {
        for (int u = 0; u < used_count; u++) {
            if (ascii_strcasecmp(out[i].published_name, used_infs[u]) == 0) {
                out[i].in_use = true;
                break;
            }
        }
    }

    /* Mark superseded only when NOT bound to any device AND another installed
     * package with the same Original Name is active or strictly newer. */
    for (int i = 0; i < count; i++) {
        if (out[i].in_use || !out[i].original_name[0]) continue;
        for (int j = 0; j < count; j++) {
            if (i == j) continue;
            if (ascii_strcasecmp(out[i].original_name, out[j].original_name) != 0) continue;
            int vcmp = cmp_dotted_version(out[j].version, out[i].version);
            if (out[j].in_use || vcmp > 0) {
                out[i].unused_superseded = true;
                break;
            }
        }
    }

    free(used_infs);
    return count;
}

bool wc_deep_parse_compact_query(const char *output, bool *out_enabled) {
    if (!output || !out_enabled) return false;
    if (ascii_strcasestr(output, "not in the Compact state") != NULL) {
        *out_enabled = false;
        return true;
    }
    if (ascii_strcasestr(output, "in the Compact state") != NULL) {
        *out_enabled = true;
        return true;
    }
    return false;
}

bool wc_deep_parse_vss_storage(const char *output, wc_storage_advisor *out) {
    if (!output || !out) return false;
    out->vss_used_str[0] = '\0';
    out->vss_alloc_str[0] = '\0';
    out->vss_max_str[0] = '\0';
    out->vss_known = false;

    if (ascii_strcasestr(output, "No items found") != NULL) {
        snprintf(out->vss_used_str, sizeof out->vss_used_str, "0 B");
        snprintf(out->vss_alloc_str, sizeof out->vss_alloc_str, "0 B");
        snprintf(out->vss_max_str, sizeof out->vss_max_str, "Unconfigured");
        out->vss_known = true;
        return true;
    }

    bool found = false;
    const char *p = output;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);

        if (extract_colon_value(p, len, "Used Shadow Copy Storage space", out->vss_used_str,
                                sizeof out->vss_used_str)) {
            found = true;
        } else if (extract_colon_value(p, len, "Allocated Shadow Copy Storage space",
                                       out->vss_alloc_str, sizeof out->vss_alloc_str)) {
            found = true;
        } else if (extract_colon_value(p, len, "Maximum Shadow Copy Storage space",
                                       out->vss_max_str, sizeof out->vss_max_str)) {
            found = true;
        }

        if (!nl) break;
        p = nl + 1;
    }
    out->vss_known = found;
    return found;
}

static const char *leaf_name(const char *path) {
    if (!path) return "";
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '\\' || *p == '/') last = p + 1;
    }
    return last;
}

int wc_deep_filter_orphan_msi(const char *const *all_files, int all_count,
                              const char *const *referenced, int ref_count,
                              int *out_orphan_indices, int max_out) {
    if (!all_files || all_count <= 0 || !out_orphan_indices || max_out <= 0) return 0;

    int orphans = 0;
    for (int i = 0; i < all_count && orphans < max_out; i++) {
        const char *cand = all_files[i];
        if (!cand || !cand[0]) continue;
        const char *cand_leaf = leaf_name(cand);

        bool matched = false;
        for (int j = 0; j < ref_count; j++) {
            const char *ref = referenced ? referenced[j] : NULL;
            if (!ref || !ref[0]) continue;
            if (ascii_strcasecmp(cand, ref) == 0 ||
                (cand_leaf[0] && ascii_strcasecmp(cand_leaf, leaf_name(ref)) == 0)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            out_orphan_indices[orphans++] = i;
        }
    }
    return orphans;
}

static void deep_worker_entry(void *user) {
    wc_deep_worker *w = (wc_deep_worker *)user;
    wc_deep_snapshot *res = (wc_deep_snapshot *)calloc(1, sizeof *res);
    if (!res) {
        wc_mutex_lock(w->mu);
        w->snap->state = WC_JOB_ERROR;
        snprintf(w->snap->error_msg, sizeof w->snap->error_msg, "Out of memory");
        w->running = false;
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return;
    }

    wc_mutex_lock(w->mu);
    *res = *w->snap;
    wc_deep_op op = w->job_op;
    bool dry_run = w->job_dry_run;
    bool bool_arg = w->job_bool_arg;
    bool restore_pt = w->job_restore_point;
    int int_arg = w->job_int_arg;
    int ncount = w->job_name_count;
    int pcount = w->job_path_count;
    wc_quarantine *q = w->quarantine;
    wc_mutex_unlock(w->mu);

    res->op = op;
    res->dry_run = dry_run;
    res->output_summary[0] = '\0';
    res->error_msg[0] = '\0';

    bool ok = true;
    if (op == WC_DEEP_SCAN_ALL) {
        wc_plat_dism_analyze(&w->cancel_flag, &res->winsxs);
        if (!w->cancel_flag) {
            int mc = 0;
            wc_plat_list_orphan_msi(res->msis, WC_MAX_ORPHAN_MSI, &mc);
            res->msi_count = mc;
        }
        if (!w->cancel_flag) {
            wc_plat_storage_advisor_query(&res->advisor);
        }
    } else if (op == WC_DEEP_CLEAN_WINSXS) {
        if (!dry_run && restore_pt) {
            wc_plat_create_restore_point("Wada Clean WinSxS Component Cleanup");
        }
        ok = wc_plat_dism_cleanup(bool_arg, dry_run, &w->cancel_flag, res->output_summary,
                                  sizeof res->output_summary);
        if (ok && !dry_run && !w->cancel_flag) {
            wc_plat_dism_analyze(&w->cancel_flag, &res->winsxs);
        }
    } else if (op == WC_DEEP_CLEAN_DELIVERY_OPT) {
        uint64_t freed = 0;
        ok = wc_plat_delivery_opt_clean(dry_run, &freed, res->output_summary,
                                        sizeof res->output_summary);
        if (ok && !dry_run) {
            wc_plat_storage_advisor_query(&res->advisor);
        }
    } else if (op == WC_DEEP_QUARANTINE_MSI) {
        if (!dry_run && restore_pt) {
            wc_plat_create_restore_point("Wada Clean Orphaned Installer Cleanup");
        }
        int moved = 0, failed = 0;
        uint64_t bytes = 0;
        for (int i = 0; i < pcount && !w->cancel_flag; i++) {
            wc_quarantine_item qitem;
            wada_err err;
            wada_err_clear(&err);
            if (q && q->open &&
                wc_quarantine_add(q, w->job_paths[i], "orphan_msi", dry_run, &qitem, &err) ==
                    WADA_OK) {
                moved++;
                bytes += qitem.bytes;
            } else {
                failed++;
            }
        }
        char sz[32];
        wc_format_bytes(bytes, sz, sizeof sz);
        snprintf(res->output_summary, sizeof res->output_summary,
                 dry_run ? "Dry-run: %d orphaned packages (%s) would move to Quarantine"
                         : "Quarantined %d orphaned packages (%s), %d failed",
                 moved, sz, failed);
        if (!dry_run && !w->cancel_flag) {
            int mc = 0;
            wc_plat_list_orphan_msi(res->msis, WC_MAX_ORPHAN_MSI, &mc);
            res->msi_count = mc;
        }
        ok = (moved > 0 || failed == 0);
    } else if (op == WC_DEEP_SCAN_DRIVERS) {
        int dc = 0;
        ok = wc_plat_list_drivers(&w->cancel_flag, res->drivers, WC_MAX_DRIVERS, &dc);
        res->driver_count = dc;
    } else if (op == WC_DEEP_REMOVE_DRIVERS) {
        if (!dry_run && restore_pt) {
            wc_plat_create_restore_point("Wada Clean DriverStore Pruning");
        }
        int removed = 0, failed = 0;
        for (int i = 0; i < ncount && !w->cancel_flag; i++) {
            char one_msg[128];
            if (wc_plat_remove_driver(w->job_names[i], dry_run, one_msg, sizeof one_msg)) {
                removed++;
            } else {
                failed++;
            }
        }
        snprintf(res->output_summary, sizeof res->output_summary,
                 dry_run ? "Dry-run: %d driver packages would be removed"
                         : "Removed %d driver packages, %d skipped/failed",
                 removed, failed);
        if (!dry_run && !w->cancel_flag) {
            int dc = 0;
            wc_plat_list_drivers(&w->cancel_flag, res->drivers, WC_MAX_DRIVERS, &dc);
            res->driver_count = dc;
        }
        ok = (removed > 0 || failed == 0);
    } else if (op == WC_DEEP_QUERY_STORAGE) {
        ok = wc_plat_storage_advisor_query(&res->advisor);
    } else if (op == WC_DEEP_SET_COMPACT_OS) {
        ok = wc_plat_set_compact_os(bool_arg, dry_run, &w->cancel_flag, res->output_summary,
                                    sizeof res->output_summary);
        if (ok && !dry_run) wc_plat_storage_advisor_query(&res->advisor);
    } else if (op == WC_DEEP_SET_HIBERNATE) {
        ok = wc_plat_set_hibernation(bool_arg, dry_run, res->output_summary,
                                     sizeof res->output_summary);
        if (ok && !dry_run) wc_plat_storage_advisor_query(&res->advisor);
    } else if (op == WC_DEEP_RESIZE_VSS) {
        ok = wc_plat_vss_resize(int_arg, dry_run, res->output_summary, sizeof res->output_summary);
        if (ok && !dry_run) wc_plat_storage_advisor_query(&res->advisor);
    } else if (op == WC_DEEP_SCAN_VHDX) {
        int vc = 0;
        ok = wc_plat_list_vhdx(res->vhdxs, WC_MAX_VHDX, &vc);
        res->vhdx_count = vc;
    } else if (op == WC_DEEP_COMPACT_VHDX) {
        int done = 0, failed = 0;
        uint64_t saved = 0;
        for (int i = 0; i < pcount && !w->cancel_flag; i++) {
            uint64_t before = 0, after = 0;
            char one_msg[128];
            if (wc_plat_compact_vhdx(w->job_paths[i], dry_run, &w->cancel_flag, &before, &after,
                                     one_msg, sizeof one_msg)) {
                done++;
                if (before > after) saved += (before - after);
            } else {
                failed++;
            }
        }
        char sz[32];
        wc_format_bytes(saved, sz, sizeof sz);
        snprintf(res->output_summary, sizeof res->output_summary,
                 dry_run ? "Dry-run: %d virtual disks would be compacted"
                         : "Compacted %d virtual disks (%s reclaimed), %d failed",
                 done, sz, failed);
        if (!dry_run && !w->cancel_flag) {
            int vc = 0;
            wc_plat_list_vhdx(res->vhdxs, WC_MAX_VHDX, &vc);
            res->vhdx_count = vc;
        }
        ok = (done > 0 || failed == 0);
    }

    if (w->cancel_flag) {
        res->state = WC_JOB_CANCELLED;
    } else if (ok) {
        res->state = WC_JOB_DONE;
    } else {
        res->state = WC_JOB_ERROR;
        snprintf(res->error_msg, sizeof res->error_msg, "%.191s",
                 res->output_summary[0] ? res->output_summary : "Deep operation failed");
    }

    wc_mutex_lock(w->mu);
    *w->snap = *res;
    w->running = false;
    w->dirty = true;
    wc_mutex_unlock(w->mu);

    free(res);
}

bool wc_deep_worker_init(wc_deep_worker *w) {
    if (!w) return false;
    memset(w, 0, sizeof *w);
    w->mu = wc_mutex_create();
    w->snap = (wc_deep_snapshot *)calloc(1, sizeof *w->snap);
    if (!w->mu || !w->snap) {
        wc_mutex_destroy(w->mu);
        free(w->snap);
        w->mu = NULL;
        w->snap = NULL;
        return false;
    }
    w->snap->state = WC_JOB_IDLE;
    w->dirty = true;
    return true;
}

void wc_deep_worker_destroy(wc_deep_worker *w) {
    if (!w) return;
    wc_deep_worker_cancel(w);
    wc_thread *t = NULL;
    if (w->mu) {
        wc_mutex_lock(w->mu);
        t = w->thread;
        w->thread = NULL;
        wc_mutex_unlock(w->mu);
    }
    if (t) wc_thread_join(t);
    if (w->mu) {
        wc_mutex_destroy(w->mu);
        w->mu = NULL;
    }
    free(w->snap);
    w->snap = NULL;
}

void wc_deep_worker_set_quarantine(wc_deep_worker *w, wc_quarantine *q) {
    if (!w || !w->mu) return;
    wc_mutex_lock(w->mu);
    w->quarantine = q;
    wc_mutex_unlock(w->mu);
}

void wc_deep_worker_cancel(wc_deep_worker *w) {
    if (!w) return;
    w->cancel_flag = 1;
}

bool wc_deep_worker_poll(wc_deep_worker *w, wc_deep_snapshot *out) {
    if (!w || !w->mu || !w->snap) return false;
    wc_thread *done_thread = NULL;

    wc_mutex_lock(w->mu);
    if (!w->running && w->thread) {
        done_thread = w->thread;
        w->thread = NULL;
        w->dirty = true;
    }
    bool running = w->running;
    if (out && (running || w->dirty || out->state != w->snap->state)) {
        *out = *w->snap;
        if (!running) w->dirty = false;
    }
    wc_mutex_unlock(w->mu);

    if (done_thread) wc_thread_join(done_thread);
    return running;
}

static bool start_deep_job(wc_deep_worker *w, wc_deep_op op, bool dry_run, bool bool_arg,
                           bool restore_point, int int_arg, const char *const *names,
                           int name_count, const char *const *paths, int path_count) {
    if (!w || !w->mu || !w->snap) return false;

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }
    wc_thread *old = w->thread;
    w->thread = NULL;
    wc_mutex_unlock(w->mu);

    if (old) wc_thread_join(old);

    wc_mutex_lock(w->mu);
    if (w->running) {
        wc_mutex_unlock(w->mu);
        return false;
    }

    w->job_op = op;
    w->job_dry_run = dry_run;
    w->job_bool_arg = bool_arg;
    w->job_restore_point = restore_point;
    w->job_int_arg = int_arg;
    w->job_name_count = 0;
    if (names && name_count > 0) {
        for (int i = 0; i < name_count && w->job_name_count < WC_MAX_DRIVERS; i++) {
            if (wc_deep_is_safe_oem_inf(names[i])) {
                snprintf(w->job_names[w->job_name_count++], 32, "%s", names[i]);
            }
        }
    }
    w->job_path_count = 0;
    if (paths && path_count > 0) {
        for (int i = 0; i < path_count && w->job_path_count < WC_MAX_ORPHAN_MSI; i++) {
            if (paths[i] && paths[i][0]) {
                snprintf(w->job_paths[w->job_path_count++], WC_PATH_CAP, "%s", paths[i]);
            }
        }
    }

    w->snap->op = op;
    w->snap->state = WC_JOB_RUNNING;
    w->snap->dry_run = dry_run;
    w->snap->reset_base = bool_arg;
    w->snap->create_restore_point = restore_point;
    w->snap->error_msg[0] = '\0';
    w->snap->output_summary[0] = '\0';

    w->cancel_flag = 0;
    w->running = true;
    w->dirty = true;

    wc_thread *t = wc_thread_start(deep_worker_entry, w);
    if (!t) {
        w->running = false;
        w->snap->state = WC_JOB_ERROR;
        snprintf(w->snap->error_msg, sizeof w->snap->error_msg, "Failed to start deep worker");
        w->dirty = true;
        wc_mutex_unlock(w->mu);
        return false;
    }
    w->thread = t;
    wc_mutex_unlock(w->mu);
    return true;
}

bool wc_deep_worker_start_scan_all(wc_deep_worker *w) {
    return start_deep_job(w, WC_DEEP_SCAN_ALL, false, false, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_clean_winsxs(wc_deep_worker *w, bool reset_base, bool dry_run,
                                       bool create_restore_point) {
    return start_deep_job(w, WC_DEEP_CLEAN_WINSXS, dry_run, reset_base, create_restore_point, 0,
                          NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_clean_delivery_opt(wc_deep_worker *w, bool dry_run) {
    return start_deep_job(w, WC_DEEP_CLEAN_DELIVERY_OPT, dry_run, false, false, 0, NULL, 0, NULL,
                          0);
}

bool wc_deep_worker_start_quarantine_msi(wc_deep_worker *w, const char *const *paths, int count,
                                         bool dry_run, bool create_restore_point) {
    if (!paths || count <= 0) return false;
    return start_deep_job(w, WC_DEEP_QUARANTINE_MSI, dry_run, false, create_restore_point, 0, NULL,
                          0, paths, count);
}

bool wc_deep_worker_start_scan_drivers(wc_deep_worker *w) {
    return start_deep_job(w, WC_DEEP_SCAN_DRIVERS, false, false, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_remove_drivers(wc_deep_worker *w, const char *const *oem_names,
                                         int count, bool dry_run, bool create_restore_point) {
    if (!oem_names || count <= 0) return false;
    return start_deep_job(w, WC_DEEP_REMOVE_DRIVERS, dry_run, false, create_restore_point, 0,
                          oem_names, count, NULL, 0);
}

bool wc_deep_worker_start_query_storage(wc_deep_worker *w) {
    return start_deep_job(w, WC_DEEP_QUERY_STORAGE, false, false, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_set_compact_os(wc_deep_worker *w, bool enable, bool dry_run) {
    return start_deep_job(w, WC_DEEP_SET_COMPACT_OS, dry_run, enable, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_set_hibernate(wc_deep_worker *w, bool enable, bool dry_run) {
    return start_deep_job(w, WC_DEEP_SET_HIBERNATE, dry_run, enable, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_resize_vss(wc_deep_worker *w, int max_percent, bool dry_run) {
    if (max_percent < 1 || max_percent > 50) return false;
    return start_deep_job(w, WC_DEEP_RESIZE_VSS, dry_run, false, false, max_percent, NULL, 0, NULL,
                          0);
}

bool wc_deep_worker_start_scan_vhdx(wc_deep_worker *w) {
    return start_deep_job(w, WC_DEEP_SCAN_VHDX, false, false, false, 0, NULL, 0, NULL, 0);
}

bool wc_deep_worker_start_compact_vhdx(wc_deep_worker *w, const char *const *paths, int count,
                                       bool dry_run) {
    if (!paths || count <= 0) return false;
    return start_deep_job(w, WC_DEEP_COMPACT_VHDX, dry_run, false, false, 0, NULL, 0, paths,
                          count);
}
