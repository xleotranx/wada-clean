#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"

static void reload_schedule(wc_cleaner_state *st) {
    st->sched_loaded = true;
    wc_plat_schedule_query(&st->sched_info);
    st->sched_freq_idx = (int)st->sched_info.freq;
    st->sched_hour = (double)st->sched_info.hour;
}

void wc_view_schedule_draw(wc_cleaner_state *st, wada_app *app) {
    if (!st->sched_loaded) reload_schedule(st);

    cui_ctx *ui = wada_app_ui(app);
    const char *freq_labels[3] = {wada_tr("Off"), wada_tr("Daily"), wada_tr("Weekly")};

    cui_column(ui, .id = "schedule-scroll", .width = CUI_FILL, .height = CUI_FILL,
               .scroll_y = true, .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Automation & Tray"), .font = CUI_FONT_DISPLAY, .size = 24);
            cui_text(ui,
                     wada_tr("Configure Windows Task Scheduler cleanups and system tray notifications."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Scheduled Cleaning Card */
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 12) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                    cui_text(ui, wada_tr("Scheduled Cleaning"), .font = CUI_FONT_MEDIUM, .size = 15);
                    cui_badge(ui, wada_tr(st->sched_info.active ? "Active" : "Off"),
                              .tone = st->sched_info.active ? CUI_TONE_SUCCESS
                                                            : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }
                if (st->sched_info.active && st->sched_info.next_run[0]) {
                    char next_msg[96];
                    wada_trf(next_msg, sizeof next_msg, "Next run: %s", st->sched_info.next_run);
                    cui_text(ui, next_msg, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }
            }

            cui_text(ui,
                     wada_tr("Runs safe non-admin temp cleanups silently via Windows Task Scheduler."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 16) {
                cui_segmented(ui, "sched-freq", freq_labels, 3, &st->sched_freq_idx);
                cui_number(ui, "sched-hour", &st->sched_hour, 0.0, 23.0,
                           .label = wada_tr("Run Hour (24h)"), .step = 1.0, .decimals = 0,
                           .disabled = st->sched_freq_idx == 0);
            }

            cui_row(ui, .width = CUI_FILL, .gap = 8) {
                if (cui_button(ui, wada_tr("Save Schedule"), .variant = CUI_BUTTON_PRIMARY,
                               .icon = CUI_ICON_CLOCK)) {
                    int h = (int)st->sched_hour;
                    if (wc_plat_schedule_set((wc_schedule_freq)st->sched_freq_idx, h, 0)) {
                        reload_schedule(st);
                        wada_app_set_status(app, wada_tr("Schedule updated"));
                    } else {
                        wada_app_set_status(app, wada_tr("Failed to update schedule"));
                    }
                }
                if (cui_button(ui, wada_tr("Refresh"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_REFRESH_CW)) {
                    reload_schedule(st);
                }
            }
        }

        /* System Tray Card */
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_text(ui, wada_tr("System Tray"), .font = CUI_FONT_MEDIUM, .size = 15);
            cui_text(ui,
                     wada_tr("Show the Wada Clean icon while the app is open. It is removed when Wada Clean closes."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            bool next_tray = st->tray_enabled;
            if (cui_switch(ui, wada_tr("Show tray icon"), &next_tray)) {
                if (next_tray) {
                    if (wc_plat_tray_show("Wada Clean")) {
                        st->tray_enabled = true;
                        wada_app_set_status(app, wada_tr("Tray icon enabled"));
                    } else {
                        wada_app_set_status(app, wada_tr("Failed to show tray icon"));
                    }
                } else {
                    wc_plat_tray_hide();
                    st->tray_enabled = false;
                    wada_app_set_status(app, wada_tr("Tray icon hidden"));
                }
            }

            cui_row(ui, .width = CUI_FILL, .gap = 8) {
                if (cui_button(ui, wada_tr("Send Test Notification"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_BELL, .disabled = !st->tray_enabled)) {
                    wc_plat_tray_notify(wada_tr("Wada Clean"),
                                        wada_tr("System tray notifications are working."));
                }
            }
        }
    }
}
