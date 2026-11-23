#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"
#include "wc_safety.h"

enum {
    CONFIRM_WINSXS = 1,
    CONFIRM_DRIVERS = 2,
    CONFIRM_MSI = 3,
    CONFIRM_HIBERNATE = 4,
    CONFIRM_VHDX = 5
};

static const char *const k_deep_tabs[] = {
    "OS & WinSxS",
    "DriverStore",
    "Storage Advisor",
    "Quarantine Vault",
    "WSL / Docker",
};

static void draw_winsxs_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    wc_deep_snapshot *ds = st->deep_snap;
    const wc_winsxs_info *ws = &ds->winsxs;
    bool busy = st->deep_running;
    bool confirm_ws = wada_ui_confirm_active(&st->deep_confirm, CONFIRM_WINSXS);
    bool confirm_msi = wada_ui_confirm_active(&st->deep_confirm, CONFIRM_MSI);

    wc_scroll_list(ui, "deep-os-list", 10) {
        /* 1. WinSxS Component Store */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_column(ui, .gap = 2) {
                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                        cui_text(ui, wada_tr("WinSxS Component Store (DISM)"),
                                 .font = CUI_FONT_MEDIUM, .size = 15);
                        cui_badge(ui, wada_tr("Admin"), .tone = CUI_TONE_WARNING,
                                  .size = CUI_SIZE_SM);
                    }
                    cui_text(ui,
                             wada_tr("Safely remove superseded Windows Update component backups via DISM."),
                             .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }

                if (ws->analyzed) {
                    cui_badge(ui,
                              wada_tr(ws->cleanup_recommended ? "Cleanup Recommended" : "Healthy"),
                              .tone = ws->cleanup_recommended ? CUI_TONE_WARNING
                                                              : CUI_TONE_SUCCESS,
                              .size = CUI_SIZE_SM);
                }
            }

            if (ws->analyzed) {
                cui_row(ui, .width = CUI_FILL, .gap = 12) {
                    cui_metric(ui, wada_tr("Actual Size"),
                               ws->actual_size_str[0] ? ws->actual_size_str : "—",
                               .width = CUI_GROW(1.0f));
                    cui_metric(ui, wada_tr("Backups & Disabled"),
                               ws->backups_size_str[0] ? ws->backups_size_str : "—",
                               .width = CUI_GROW(1.0f));
                    char pkg_buf[32];
                    snprintf(pkg_buf, sizeof pkg_buf, "%d", ws->reclaimable_packages);
                    cui_metric(ui, wada_tr("Reclaimable Packages"), pkg_buf,
                               .width = CUI_GROW(1.0f));
                }
            } else if (ws->raw_summary[0]) {
                cui_text(ui, ws->raw_summary, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_checkbox(ui,
                         wada_tr("Deep /ResetBase (cannot uninstall current Windows updates afterward)"),
                         &st->deep_reset_base, .disabled = busy);

            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                if (cui_button(ui, wada_tr("Analyze WinSxS & OS"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
                    wada_ui_confirm_reset(&st->deep_confirm);
                    if (wc_deep_worker_start_scan_all(&st->deep_worker)) {
                        st->deep_running = true;
                        wada_app_set_status(app, wada_tr("Analyzing deep OS targets…"));
                    }
                }
                if (cui_button(ui,
                               wada_tr(confirm_ws ? "Confirm WinSxS Clean" : "Clean WinSxS"),
                               .variant = confirm_ws ? CUI_BUTTON_DANGER : CUI_BUTTON_PRIMARY,
                               .icon = CUI_ICON_SPARKLES, .disabled = busy)) {
                    if (!st->deep_dry_run &&
                        !wada_ui_confirm_step(&st->deep_confirm, CONFIRM_WINSXS, 300)) {
                        wada_app_set_status(app, wada_tr("Click Confirm WinSxS Clean to run DISM."));
                    } else {
                        wada_ui_confirm_reset(&st->deep_confirm);
                        if (wc_deep_worker_start_clean_winsxs(&st->deep_worker, st->deep_reset_base,
                                                              st->deep_dry_run,
                                                              st->deep_restore_point)) {
                            st->deep_running = true;
                            wada_app_set_status(app, wada_tr("Running DISM component cleanup…"));
                        }
                    }
                }
            }
        }

        /* 2. Delivery Optimization Cache */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_column(ui, .gap = 2) {
                    cui_text(ui, wada_tr("Delivery Optimization Cache"), .font = CUI_FONT_MEDIUM,
                             .size = 15);
                    cui_text(ui,
                             wada_tr("Cached Windows Update files used for peer delivery on local networks."),
                             .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }
                if (cui_button(ui, wada_tr("Clear Cache"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_TRASH_2, .disabled = busy)) {
                    if (wc_deep_worker_start_clean_delivery_opt(&st->deep_worker,
                                                                st->deep_dry_run)) {
                        st->deep_running = true;
                        wada_app_set_status(app, wada_tr("Clearing Delivery Optimization cache…"));
                    }
                }
            }
            if (ds->advisor.delivery_opt_bytes > 0) {
                char sz[32], info[96];
                wc_format_bytes(ds->advisor.delivery_opt_bytes, sz, sizeof sz);
                wada_trf(info, sizeof info, "%s (%llu files)", sz,
                         (unsigned long long)ds->advisor.delivery_opt_files);
                cui_text(ui, info, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
            }
        }

        /* 3. Orphaned Windows Installers */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            uint64_t sel_bytes = 0;
            int sel_cnt = 0;
            const char *sel_paths[WC_MAX_ORPHAN_MSI];
            for (int i = 0; i < ds->msi_count; i++) {
                if (ds->msis[i].selected) {
                    sel_paths[sel_cnt++] = ds->msis[i].path;
                    sel_bytes += ds->msis[i].bytes;
                }
            }
            char msi_sz[32], msi_meta[96];
            wc_format_bytes(sel_bytes, msi_sz, sizeof msi_sz);
            wada_trf(msi_meta, sizeof msi_meta, "%d selected (%s)", sel_cnt, msi_sz);

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_column(ui, .gap = 2) {
                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                        cui_text(ui, wada_tr("Orphaned Installers (C:\\Windows\\Installer)"),
                                 .font = CUI_FONT_MEDIUM, .size = 15);
                        cui_badge(ui, wada_tr("Quarantine"), .tone = CUI_TONE_INFO,
                                  .size = CUI_SIZE_SM);
                    }
                    cui_text(ui,
                             wada_tr("Unreferenced .msi/.msp files verified against Windows Installer API; moved to Quarantine first."),
                             .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }

                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    cui_text(ui, msi_meta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                             .tabular = true);
                    if (cui_button(ui,
                                   wada_tr(confirm_msi ? "Confirm Quarantine"
                                                       : "Quarantine Selected"),
                                   .variant = confirm_msi ? CUI_BUTTON_DANGER : CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_ARCHIVE,
                                   .disabled = busy || sel_cnt == 0 || !st->quarantine.open)) {
                        if (!st->deep_dry_run &&
                            !wada_ui_confirm_step(&st->deep_confirm, CONFIRM_MSI, 300)) {
                            wada_app_set_status(
                                app, wada_tr("Click Confirm Quarantine to move orphaned installers."));
                        } else {
                            wada_ui_confirm_reset(&st->deep_confirm);
                            if (wc_deep_worker_start_quarantine_msi(&st->deep_worker, sel_paths,
                                                                    sel_cnt, st->deep_dry_run,
                                                                    st->deep_restore_point)) {
                                st->deep_running = true;
                                wada_app_set_status(app,
                                                    wada_tr("Moving orphaned installers to Quarantine…"));
                            }
                        }
                    }
                }
            }

            if (ds->msi_count == 0) {
                cui_text(ui,
                         wada_tr("No orphaned .msi/.msp files detected (run Analyze WinSxS & OS to scan)."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            } else {
                for (int i = 0; i < ds->msi_count; i++) {
                    wc_orphan_msi_item *it = &ds->msis[i];
                    char isz[32];
                    wc_format_bytes(it->bytes, isz, sizeof isz);
                    cui_id_scope(ui, it->name) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                            cui_checkbox(ui, it->name, &it->selected, .disabled = busy);
                            cui_text(ui, isz, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                                     .tabular = true);
                        }
                    }
                }
            }
        }
    }
}

static void draw_drivers_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    wc_deep_snapshot *ds = st->deep_snap;
    bool busy = st->deep_running;
    bool confirming = wada_ui_confirm_active(&st->deep_confirm, CONFIRM_DRIVERS);

    const char *sel_names[WC_MAX_DRIVERS];
    int sel_cnt = 0;
    for (int i = 0; i < ds->driver_count; i++) {
        if (ds->drivers[i].selected && !ds->drivers[i].in_use) {
            sel_names[sel_cnt++] = ds->drivers[i].published_name;
        }
    }

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_BETWEEN,
            .gap = 8) {
        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
            if (cui_button(ui, wada_tr("Scan Drivers"), .variant = CUI_BUTTON_OUTLINE,
                           .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
                wada_ui_confirm_reset(&st->deep_confirm);
                if (wc_deep_worker_start_scan_drivers(&st->deep_worker)) {
                    st->deep_running = true;
                    wada_app_set_status(app, wada_tr("Scanning DriverStore packages…"));
                }
            }
            cui_checkbox(ui, wada_tr("Unused / superseded only"), &st->deep_drivers_unused_only,
                         .disabled = busy);
        }

        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
            if (cui_button(ui, wada_tr("Select Superseded"), .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .disabled = busy || ds->driver_count == 0)) {
                for (int i = 0; i < ds->driver_count; i++) {
                    ds->drivers[i].selected =
                        (!ds->drivers[i].in_use && ds->drivers[i].unused_superseded);
                }
                wada_ui_confirm_reset(&st->deep_confirm);
            }
            if (cui_button(ui, wada_tr("Clear"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = busy || sel_cnt == 0)) {
                for (int i = 0; i < ds->driver_count; i++) ds->drivers[i].selected = false;
                wada_ui_confirm_reset(&st->deep_confirm);
            }
            if (cui_button(ui,
                           wada_tr(confirming ? "Confirm Remove Drivers" : "Remove Selected"),
                           .variant = confirming ? CUI_BUTTON_DANGER : CUI_BUTTON_PRIMARY,
                           .icon = CUI_ICON_TRASH_2, .disabled = busy || sel_cnt == 0)) {
                if (!st->deep_dry_run &&
                    !wada_ui_confirm_step(&st->deep_confirm, CONFIRM_DRIVERS, 300)) {
                    wada_app_set_status(app, wada_tr("Click Confirm Remove Drivers to proceed."));
                } else {
                    wada_ui_confirm_reset(&st->deep_confirm);
                    if (wc_deep_worker_start_remove_drivers(&st->deep_worker, sel_names, sel_cnt,
                                                            st->deep_dry_run,
                                                            st->deep_restore_point)) {
                        st->deep_running = true;
                        wada_app_set_status(app, wada_tr("Removing selected drivers…"));
                    }
                }
            }
        }
    }

    if (ds->driver_count == 0) {
        cui_empty_state(ui, wada_tr("No drivers scanned yet"),
                        .description = wada_tr("Click Scan Drivers to list third-party OEM packages in DriverStore."));
        return;
    }

    wc_scroll_list(ui, "deep-drivers-list", 6) {
        for (int i = 0; i < ds->driver_count; i++) {
            wc_driver_item *d = &ds->drivers[i];
            if (st->deep_drivers_unused_only && d->in_use) continue;

            char title[192];
            snprintf(title, sizeof title, "%s — %s", d->published_name,
                     d->original_name[0] ? d->original_name : d->provider);

            char meta[224];
            snprintf(meta, sizeof meta, "%s · %s · %s %s",
                     d->provider[0] ? d->provider : "-",
                     d->class_name[0] ? d->class_name : "-",
                     d->version[0] ? d->version : "-",
                     d->date[0] ? d->date : "");

            cui_id_scope(ui, d->published_name) {
                cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 3) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                            .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                        cui_checkbox(ui, title, &d->selected, .disabled = busy || d->in_use);
                        cui_badge(ui,
                                  wada_tr(d->in_use
                                              ? "In Use"
                                              : (d->unused_superseded ? "Superseded" : "Unused")),
                                  .tone = d->in_use
                                              ? CUI_TONE_SUCCESS
                                              : (d->unused_superseded ? CUI_TONE_WARNING
                                                                      : CUI_TONE_NEUTRAL),
                                  .size = CUI_SIZE_SM);
                    }
                    cui_text(ui, meta, .size = 12, .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                }
            }
        }
    }
}

static void draw_storage_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    const wc_storage_advisor *adv = &st->deep_snap->advisor;
    bool busy = st->deep_running;
    bool confirm_hib = wada_ui_confirm_active(&st->deep_confirm, CONFIRM_HIBERNATE);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_BETWEEN) {
        cui_text(ui, wada_tr("Adjust OS-level compression, hibernation, and shadow copy storage limits."),
                 .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        if (cui_button(ui, wada_tr("Refresh"), .variant = CUI_BUTTON_OUTLINE,
                       .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
            if (wc_deep_worker_start_query_storage(&st->deep_worker)) {
                st->deep_running = true;
                wada_app_set_status(app, wada_tr("Querying storage configuration…"));
            }
        }
    }

    wc_scroll_list(ui, "deep-storage-list", 10) {
        /* CompactOS */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 6) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    cui_text(ui, wada_tr("CompactOS System Compression"), .font = CUI_FONT_MEDIUM,
                             .size = 15);
                    cui_badge(ui,
                              wada_tr(!adv->compact_os_known
                                          ? "Unknown"
                                          : (adv->compact_os_enabled ? "Enabled" : "Disabled")),
                              .tone = adv->compact_os_enabled ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }
                if (cui_button(ui,
                               wada_tr(adv->compact_os_enabled ? "Disable CompactOS"
                                                               : "Enable CompactOS"),
                               .variant = CUI_BUTTON_OUTLINE, .disabled = busy)) {
                    if (wc_deep_worker_start_set_compact_os(&st->deep_worker,
                                                            !adv->compact_os_enabled,
                                                            st->deep_dry_run)) {
                        st->deep_running = true;
                        wada_app_set_status(app, wada_tr("Updating CompactOS state…"));
                    }
                }
            }
            cui_text(ui,
                     wada_tr("Compresses Windows system binaries on SSDs (typically saves 2-4 GB with negligible CPU cost)."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Hibernation (hiberfil.sys) */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 6) {
            char hib_str[32];
            if (adv->hiberfil_present) {
                wc_format_bytes(adv->hiberfil_bytes, hib_str, sizeof hib_str);
            } else {
                snprintf(hib_str, sizeof hib_str, "%s", wada_tr("Off"));
            }
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    cui_text(ui, wada_tr("Hibernation File (hiberfil.sys)"), .font = CUI_FONT_MEDIUM,
                             .size = 15);
                    cui_badge(ui, hib_str,
                              .tone = adv->hiberfil_present ? CUI_TONE_INFO : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }
                if (cui_button(ui,
                               wada_tr(adv->hiberfil_present
                                           ? (confirm_hib ? "Confirm Disable"
                                                          : "Disable Hibernation")
                                           : "Enable Hibernation"),
                               .variant = (adv->hiberfil_present && confirm_hib)
                                              ? CUI_BUTTON_DANGER
                                              : CUI_BUTTON_OUTLINE,
                               .disabled = busy)) {
                    bool enable = !adv->hiberfil_present;
                    if (!enable && !st->deep_dry_run &&
                        !wada_ui_confirm_step(&st->deep_confirm, CONFIRM_HIBERNATE, 300)) {
                        wada_app_set_status(app, wada_tr("Click Confirm Disable to turn off hibernation."));
                    } else {
                        wada_ui_confirm_reset(&st->deep_confirm);
                        if (wc_deep_worker_start_set_hibernate(&st->deep_worker, enable,
                                                               st->deep_dry_run)) {
                            st->deep_running = true;
                            wada_app_set_status(app, wada_tr("Updating hibernation setting…"));
                        }
                    }
                }
            }
            cui_text(ui,
                     wada_tr("Reclaims RAM-sized space if you only use Sleep (disables Fast Startup)."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* VSS Shadow Copy Storage */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_text(ui, wada_tr("Volume Shadow Copy Storage (Restore Points)"),
                         .font = CUI_FONT_MEDIUM, .size = 15);
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    if (cui_button(ui, wada_tr("Cap to 5%"), .variant = CUI_BUTTON_OUTLINE,
                                   .size = CUI_SIZE_SM, .disabled = busy)) {
                        if (wc_deep_worker_start_resize_vss(&st->deep_worker, 5, st->deep_dry_run)) {
                            st->deep_running = true;
                        }
                    }
                    if (cui_button(ui, wada_tr("Cap to 10%"), .variant = CUI_BUTTON_OUTLINE,
                                   .size = CUI_SIZE_SM, .disabled = busy)) {
                        if (wc_deep_worker_start_resize_vss(&st->deep_worker, 10,
                                                            st->deep_dry_run)) {
                            st->deep_running = true;
                        }
                    }
                }
            }
            if (adv->vss_known) {
                cui_row(ui, .width = CUI_FILL, .gap = 12) {
                    cui_metric(ui, wada_tr("Used"),
                               adv->vss_used_str[0] ? adv->vss_used_str : "—",
                               .width = CUI_GROW(1.0f));
                    cui_metric(ui, wada_tr("Allocated"),
                               adv->vss_alloc_str[0] ? adv->vss_alloc_str : "—",
                               .width = CUI_GROW(1.0f));
                    cui_metric(ui, wada_tr("Max Limit"),
                               adv->vss_max_str[0] ? adv->vss_max_str : "—",
                               .width = CUI_GROW(1.0f));
                }
            } else {
                cui_text(ui, wada_tr("Click Refresh (requires Administrator) to query shadow storage."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }
}

static void draw_quarantine_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    uint64_t total_bytes = 0;
    for (int i = 0; i < st->quarantine_count; i++) {
        total_bytes += st->quarantine_items[i].bytes;
    }
    char sz[32], summary[96];
    wc_format_bytes(total_bytes, sz, sizeof sz);
    wada_trf(summary, sizeof summary, "%d items (%s)", st->quarantine_count, sz);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_BETWEEN,
            .gap = 8) {
        cui_text(ui, summary, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
            if (cui_button(ui, wada_tr("Refresh"), .variant = CUI_BUTTON_OUTLINE,
                           .size = CUI_SIZE_SM, .icon = CUI_ICON_REFRESH_CW,
                           .disabled = st->running || st->deep_running)) {
                wc_quarantine_reload(st);
            }
            if (cui_button(ui, wada_tr("Purge >14 days"), .variant = CUI_BUTTON_OUTLINE,
                           .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH_2,
                           .disabled = !st->quarantine.open || st->quarantine_count == 0 ||
                                       st->running || st->deep_running)) {
                wada_err err;
                wada_err_clear(&err);
                int removed = 0;
                if (wc_quarantine_purge_older_than(&st->quarantine, WC_QUARANTINE_DEFAULT_TTL_SEC,
                                                   0, &removed, &err) == WADA_OK) {
                    wc_quarantine_reload(st);
                    wada_app_set_status(app, wada_tr("Expired quarantine items purged"));
                } else {
                    wada_ui_status_err(app, st->status, sizeof st->status, &err);
                }
            }
        }
    }

    if (st->quarantine_count == 0) {
        cui_empty_state(ui, wada_tr("Quarantine Vault is empty"),
                        .description = wada_tr("Items removed safely by Old Downloads, Orphaned Installers, or Leftovers appear here for 14 days."));
        return;
    }

    wc_scroll_list(ui, "deep-quarantine-list", 6) {
        for (int i = 0; i < st->quarantine_count; i++) {
            const wc_quarantine_item *q = &st->quarantine_items[i];
            char qsz[32], when[32];
            wc_format_bytes(q->bytes, qsz, sizeof qsz);
            wada_ui_format_when(q->unix_sec, when, sizeof when);

            cui_id_scope(ui, q->id) {
                cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                            .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                            cui_badge(ui, q->source_tag, .tone = CUI_TONE_INFO,
                                      .size = CUI_SIZE_SM);
                            cui_text(ui, qsz, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                                     .tabular = true);
                            cui_text(ui, when, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        }
                        cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 4) {
                            if (cui_button(ui, wada_tr("Restore"), .variant = CUI_BUTTON_OUTLINE,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_UNDO_2,
                                           .disabled = st->running || st->deep_running)) {
                                wada_err err;
                                wada_err_clear(&err);
                                if (wc_quarantine_restore(&st->quarantine, q->id, &err) == WADA_OK) {
                                    wc_quarantine_reload(st);
                                    wada_app_set_status(app, wada_tr("Item restored"));
                                    break;
                                } else {
                                    wada_ui_status_err(app, st->status, sizeof st->status, &err);
                                }
                            }
                            if (cui_button(ui, wada_tr("Delete"), .variant = CUI_BUTTON_GHOST,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH_2,
                                           .disabled = st->running || st->deep_running)) {
                                wada_err err;
                                wada_err_clear(&err);
                                if (wc_quarantine_delete(&st->quarantine, q->id, &err) == WADA_OK) {
                                    wc_quarantine_reload(st);
                                    wada_app_set_status(app, wada_tr("Quarantine item deleted"));
                                    break;
                                } else {
                                    wada_ui_status_err(app, st->status, sizeof st->status, &err);
                                }
                            }
                        }
                    }
                    cui_text(ui, q->orig_path, .font = CUI_FONT_MONO, .size = 12,
                             .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                }
            }
        }
    }
}

static void draw_vhdx_tab(wc_cleaner_state *st, cui_ctx *ui, wada_app *app) {
    wc_deep_snapshot *ds = st->deep_snap;
    bool busy = st->deep_running;
    bool confirming = wada_ui_confirm_active(&st->deep_confirm, CONFIRM_VHDX);

    const char *sel_paths[WC_MAX_VHDX];
    int sel_cnt = 0;
    for (int i = 0; i < ds->vhdx_count; i++) {
        if (ds->vhdxs[i].selected) sel_paths[sel_cnt++] = ds->vhdxs[i].path;
    }

    cui_alert(ui,
              wada_tr("Compacting runs 'wsl --shutdown' and stops running WSL/Docker containers."),
              .tone = CUI_TONE_WARNING, .icon = CUI_ICON_TRIANGLE_ALERT);

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_BETWEEN,
            .gap = 8) {
        if (cui_button(ui, wada_tr("Scan Virtual Disks"), .variant = CUI_BUTTON_OUTLINE,
                       .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
            wada_ui_confirm_reset(&st->deep_confirm);
            if (wc_deep_worker_start_scan_vhdx(&st->deep_worker)) {
                st->deep_running = true;
                wada_app_set_status(app, wada_tr("Scanning WSL/Docker virtual disks…"));
            }
        }
        if (cui_button(ui, wada_tr(confirming ? "Confirm Compact" : "Compact Selected"),
                       .variant = confirming ? CUI_BUTTON_DANGER : CUI_BUTTON_PRIMARY,
                       .icon = CUI_ICON_LAYERS, .disabled = busy || sel_cnt == 0)) {
            if (!st->deep_dry_run &&
                !wada_ui_confirm_step(&st->deep_confirm, CONFIRM_VHDX, 300)) {
                wada_app_set_status(app, wada_tr("Click Confirm Compact to shut down WSL and shrink disks."));
            } else {
                wada_ui_confirm_reset(&st->deep_confirm);
                if (wc_deep_worker_start_compact_vhdx(&st->deep_worker, sel_paths, sel_cnt,
                                                      st->deep_dry_run)) {
                    st->deep_running = true;
                    wada_app_set_status(app, wada_tr("Compacting virtual hard disks…"));
                }
            }
        }
    }

    if (ds->vhdx_count == 0) {
        cui_empty_state(ui, wada_tr("No WSL/Docker virtual disks found"),
                        .description = wada_tr("Click Scan Virtual Disks to discover Docker Desktop and WSL2 .vhdx files."));
        return;
    }

    wc_scroll_list(ui, "deep-vhdx-list", 6) {
        for (int i = 0; i < ds->vhdx_count; i++) {
            wc_vhdx_item *v = &ds->vhdxs[i];
            char sz[32];
            wc_format_bytes(v->bytes, sz, sizeof sz);
            cui_id_scope(ui, v->path) {
                cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                            .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                        cui_checkbox(ui, v->label, &v->selected, .disabled = busy);
                        cui_text(ui, sz, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                                 .tabular = true);
                    }
                    cui_text(ui, v->path, .font = CUI_FONT_MONO, .size = 12,
                             .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                }
            }
        }
    }
}

void wc_view_deep_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);
    if (!st->deep_snap) return;

    const char *tab_labels[5];
    for (int i = 0; i < 5; i++) tab_labels[i] = wada_tr(k_deep_tabs[i]);

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Deep Clean"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui,
                         wada_tr("Clean WinSxS, old drivers, orphaned installers, storage policies, and virtual disks."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 10) {
                cui_switch(ui, wada_tr("Restore point"), &st->deep_restore_point,
                           .disabled = st->deep_running);
                if (cui_switch(ui, wada_tr("Preview only"), &st->deep_dry_run,
                               .disabled = st->deep_running)) {
                    wada_ui_confirm_reset(&st->deep_confirm);
                }
                if (st->deep_running) {
                    if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_GHOST,
                                   .icon = CUI_ICON_X)) {
                        wc_deep_worker_cancel(&st->deep_worker);
                    }
                }
            }
        }

        if (!st->is_admin) {
            cui_alert(ui,
                      wada_tr("Standard User mode: Restart as Administrator for WinSxS, DriverStore, and Storage Advisor changes."),
                      .tone = CUI_TONE_WARNING, .icon = CUI_ICON_SHIELD);
        }

        if (st->deep_running) {
            cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL,
                         .label = wada_tr("Running deep maintenance task…"),
                         .tone = CUI_TONE_ACCENT);
        } else if (st->deep_snap->error_msg[0]) {
            cui_alert(ui, st->deep_snap->error_msg, .tone = CUI_TONE_DANGER,
                      .icon = CUI_ICON_CIRCLE_ALERT);
        } else if (st->deep_snap->output_summary[0]) {
            cui_alert(ui, st->deep_snap->output_summary, .tone = CUI_TONE_INFO,
                      .icon = CUI_ICON_INFO);
        }

        cui_tabs(ui, "deep-tabs", tab_labels, 5, &st->deep_tab);

        if (st->deep_tab == 0) {
            draw_winsxs_tab(st, ui, app);
        } else if (st->deep_tab == 1) {
            draw_drivers_tab(st, ui, app);
        } else if (st->deep_tab == 2) {
            draw_storage_tab(st, ui, app);
        } else if (st->deep_tab == 3) {
            draw_quarantine_tab(st, ui, app);
        } else {
            draw_vhdx_tab(st, ui, app);
        }
    }
}
