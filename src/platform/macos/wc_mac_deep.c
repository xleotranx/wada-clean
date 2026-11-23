#include "wc_deep.h"
#include "wc_platform.h"

#include <stdio.h>
#include <string.h>

bool wc_plat_dism_analyze(const volatile int *cancel_flag, wc_winsxs_info *out) {
    (void)cancel_flag;
    if (out) {
        memset(out, 0, sizeof *out);
        snprintf(out->raw_summary, sizeof out->raw_summary, "Windows-only feature");
    }
    return false;
}

bool wc_plat_dism_cleanup(bool reset_base, bool dry_run, const volatile int *cancel_flag,
                          char *out_summary, size_t summary_cap) {
    (void)reset_base;
    (void)dry_run;
    (void)cancel_flag;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_delivery_opt_clean(bool dry_run, uint64_t *out_freed_bytes, char *out_summary,
                                size_t summary_cap) {
    (void)dry_run;
    if (out_freed_bytes) *out_freed_bytes = 0;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_list_orphan_msi(wc_orphan_msi_item *out, int max_items, int *out_count) {
    (void)out;
    (void)max_items;
    if (out_count) *out_count = 0;
    return false;
}

bool wc_plat_list_drivers(const volatile int *cancel_flag, wc_driver_item *out, int max_items,
                          int *out_count) {
    (void)cancel_flag;
    (void)out;
    (void)max_items;
    if (out_count) *out_count = 0;
    return false;
}

bool wc_plat_remove_driver(const char *published_name, bool dry_run, char *out_summary,
                           size_t summary_cap) {
    (void)published_name;
    (void)dry_run;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_storage_advisor_query(wc_storage_advisor *out) {
    if (out) memset(out, 0, sizeof *out);
    return false;
}

bool wc_plat_set_compact_os(bool enable, bool dry_run, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap) {
    (void)enable;
    (void)dry_run;
    (void)cancel_flag;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_set_hibernation(bool enable, bool dry_run, char *out_summary, size_t summary_cap) {
    (void)enable;
    (void)dry_run;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_vss_resize(int max_percent, bool dry_run, char *out_summary, size_t summary_cap) {
    (void)max_percent;
    (void)dry_run;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}

bool wc_plat_create_restore_point(const char *description) {
    (void)description;
    return false;
}

bool wc_plat_list_vhdx(wc_vhdx_item *out, int max_items, int *out_count) {
    (void)out;
    (void)max_items;
    if (out_count) *out_count = 0;
    return false;
}

bool wc_plat_compact_vhdx(const char *path, bool dry_run, const volatile int *cancel_flag,
                          uint64_t *out_before, uint64_t *out_after, char *out_summary,
                          size_t summary_cap) {
    (void)path;
    (void)dry_run;
    (void)cancel_flag;
    if (out_before) *out_before = 0;
    if (out_after) *out_after = 0;
    if (out_summary && summary_cap > 0) snprintf(out_summary, summary_cap, "Windows-only feature");
    return false;
}
