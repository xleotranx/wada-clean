#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"

static void start_update_check(wc_cleaner_state *st, wada_app *app) {
    if (wc_updater_worker_start_check(&st->updater_worker)) {
        st->updater_running = true;
        wada_app_set_status(app, wada_tr("Checking for updates…"));
    }
}

void wc_view_updater_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);
    const wc_updater_snapshot *snap = &st->updater_snap;

    char count_text[64];
    wada_trf(count_text, sizeof count_text, "%d updates available", snap->count);

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Software Updater"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui,
                         wada_tr("Detect outdated Windows applications with winget and install updates safely."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
            if (snap->state == WC_JOB_DONE && snap->winget_available) {
                cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .tabular = true);
            }
        }

        /* While idle the empty state owns the primary action, so the toolbar
         * only appears once there is something to re-check or update. */
        bool show_check = snap->winget_available && (st->updater_running || snap->state != WC_JOB_IDLE);
        if (show_check || snap->count > 0) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
                if (show_check) {
                    if (cui_button(ui, wada_tr("Check for Updates"), .variant = CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_REFRESH_CW, .disabled = st->updater_running,
                                   .loading = st->updater_running && snap->op == WC_UPD_CHECK)) {
                        start_update_check(st, app);
                    }
                }

                if (snap->count > 0) {
                    if (cui_button(ui, wada_tr("Update All"), .variant = CUI_BUTTON_PRIMARY,
                                   .icon = CUI_ICON_DOWNLOAD, .disabled = st->updater_running,
                                   .loading = st->updater_running &&
                                              snap->op == WC_UPD_UPGRADE_ALL)) {
                        if (wc_updater_worker_start_upgrade_all(&st->updater_worker)) {
                            st->updater_running = true;
                            wada_app_set_status(app, wada_tr("Updating all packages…"));
                        }
                    }
                }

                if (st->updater_running) {
                    if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_GHOST,
                                   .icon = CUI_ICON_X)) {
                        wc_updater_worker_cancel(&st->updater_worker);
                    }
                }
            }
        }

        if (st->updater_running) {
            const char *lbl = (snap->op == WC_UPD_CHECK)
                                  ? wada_tr("Querying winget for updates…")
                                  : wada_tr("Installing updates via winget…");
            cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL, .label = lbl,
                         .tone = CUI_TONE_ACCENT);
        }

        if (!snap->winget_available) {
            cui_alert(ui, wada_tr("winget is not installed or unavailable on this PC."),
                      .tone = CUI_TONE_WARNING, .icon = CUI_ICON_CIRCLE_ALERT);
        } else if (snap->error_msg[0] && snap->state == WC_JOB_ERROR) {
            cui_alert(ui, snap->error_msg, .tone = CUI_TONE_DANGER, .icon = CUI_ICON_CIRCLE_ALERT);
        } else if (snap->state == WC_JOB_IDLE) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER,
                       .justify = CUI_JUSTIFY_CENTER, .gap = 10) {
                cui_icon(ui, CUI_ICON_DOWNLOAD, 34.0f, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, wada_tr("Check for app updates"), .font = CUI_FONT_MEDIUM,
                         .size = 15, .align = CUI_TEXT_CENTER);
                cui_text(ui,
                         wada_tr("Click Check for Updates to scan installed packages with winget."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED, .align = CUI_TEXT_CENTER,
                         .width = CUI_FILL);
                if (cui_button(ui, wada_tr("Check for Updates"), .variant = CUI_BUTTON_PRIMARY,
                               .icon = CUI_ICON_REFRESH_CW)) {
                    start_update_check(st, app);
                }
            }
        } else if (snap->count == 0 && !st->updater_running) {
            cui_empty_state(ui, wada_tr("All apps are up to date"),
                            .description = wada_tr("No pending updates were reported by winget."));
        } else {
            wc_scroll_list(ui, "updater-list", 6) {
                for (int i = 0; i < snap->count; i++) {
                    const wc_update_item *it = &snap->items[i];
                    char ver[140];
                    snprintf(ver, sizeof ver, "%s -> %s", it->current_version,
                             it->available_version);

                    cui_id_scope(ui, it->id) {
                        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                    cui_text(ui, it->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_badge(ui, it->source, .tone = CUI_TONE_NEUTRAL,
                                              .size = CUI_SIZE_SM);
                                }
                                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                    cui_text(ui, ver, .size = 12,
                                             .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                                    if (cui_button(ui, wada_tr("Update"),
                                                   .variant = CUI_BUTTON_OUTLINE,
                                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_DOWNLOAD,
                                                   .disabled = st->updater_running)) {
                                        if (wc_updater_worker_start_upgrade_one(&st->updater_worker,
                                                                                it->id)) {
                                            st->updater_running = true;
                                            wada_app_set_status(app,
                                                                wada_tr("Updating package…"));
                                        }
                                    }
                                }
                            }
                            cui_text(ui, it->id, .font = CUI_FONT_MONO, .size = 12,
                                     .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                        }
                    }
                }
            }
        }
    }
}
