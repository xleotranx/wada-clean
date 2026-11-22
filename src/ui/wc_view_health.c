#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_safety.h"

void wc_view_health_draw(wc_cleaner_state *st, wada_app *app) {
    if (!st->startup_loaded) wc_startup_refresh(st);

    cui_ctx *ui = wada_app_ui(app);

    int enabled_startup = 0;
    for (int i = 0; i < st->startup_count; i++) {
        if (st->startup_items[i].enabled) enabled_startup++;
    }

    float drive_used_frac = 0.0f;
    char drive_value[48];
    char drive_desc[96];
    if (st->sys_drive_ok && st->sys_drive_total > 0) {
        char free_str[32], total_str[32];
        wc_format_bytes(st->sys_drive_free, free_str, sizeof free_str);
        wc_format_bytes(st->sys_drive_total, total_str, sizeof total_str);
        uint64_t used = st->sys_drive_total - st->sys_drive_free;
        drive_used_frac = (float)((double)used / (double)st->sys_drive_total);
        snprintf(drive_value, sizeof drive_value, "%s", free_str);
        wada_trf(drive_desc, sizeof drive_desc, "%s free of %s", free_str, total_str);
    } else {
        snprintf(drive_value, sizeof drive_value, "%s", wada_tr("Unavailable"));
        drive_desc[0] = '\0';
    }

    char junk_value[48];
    char junk_desc[96];
    if (st->has_analyzed) {
        wc_format_bytes(st->snap.total_bytes_found, junk_value, sizeof junk_value);
        wada_trf(junk_desc, sizeof junk_desc, "%s across %llu files", junk_value,
                 (unsigned long long)st->snap.total_files_found);
    } else {
        snprintf(junk_value, sizeof junk_value, "%s", wada_tr("Not scanned"));
        snprintf(junk_desc, sizeof junk_desc, "%s", wada_tr("Run checkup to analyze"));
    }

    char startup_value[64];
    wada_trf(startup_value, sizeof startup_value, "%d enabled / %d total", enabled_startup,
             st->startup_count);

    char last_value[48];
    char last_desc[64];
    if (st->history_count > 0) {
        const wc_history_entry *last = &st->history_entries[0];
        char sz[32];
        wc_format_bytes(last->bytes_cleaned, sz, sizeof sz);
        wada_trf(last_value, sizeof last_value, "%s freed", sz);
        wada_ui_format_when(last->unix_sec, last_desc, sizeof last_desc);
    } else {
        snprintf(last_value, sizeof last_value, "%s", wada_tr("Never"));
        snprintf(last_desc, sizeof last_desc, "%s", wada_tr("No clean runs recorded yet."));
    }

    bool attention = (st->has_analyzed && st->snap.total_bytes_found > 0) ||
                     (drive_used_frac > 0.85f) || enabled_startup > 8;

    const char *hero_badge = "System Ready";
    cui_tone hero_tone = CUI_TONE_SUCCESS;
    const char *hero_msg = "Click Run Health Check to scan junk targets and startup impact.";
    if (st->running) {
        hero_badge = "Analyzing System…";
        hero_tone = CUI_TONE_INFO;
        hero_msg = "Scanning system and browser targets…";
    } else if (st->has_analyzed && attention) {
        hero_badge = "Attention Suggested";
        hero_tone = CUI_TONE_WARNING;
        hero_msg = "Review the recommendations below to keep your PC clean and responsive.";
    } else if (st->has_analyzed) {
        hero_badge = "System Ready";
        hero_tone = CUI_TONE_SUCCESS;
        hero_msg = "Your system looks clean and responsive.";
    }

    cui_column(ui, .id = "health-scroll", .width = CUI_FILL, .height = CUI_FILL, .scroll_y = true,
               .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("PC Health Check"), .font = CUI_FONT_DISPLAY, .size = 24);
            cui_text(ui,
                     wada_tr("One-click overview of disk space, reclaimable junk, startup impact, and clean history."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Hero banner */
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
                cui_column(ui, .width = CUI_GROW(1.0f), .gap = 6) {
                    cui_badge(ui, wada_tr(hero_badge), .tone = hero_tone, .dot = true);
                    cui_text(ui, wada_tr(hero_msg), .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }

                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                    if (!st->running) {
                        if (cui_button(ui, wada_tr("Run Health Check"),
                                       .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SHIELD)) {
                            wc_health_run_checkup(st);
                        }
                    } else {
                        if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_OUTLINE,
                                       .icon = CUI_ICON_X)) {
                            wc_cleaner_cancel_job(st);
                        }
                    }

                    if (st->has_analyzed && st->snap.total_bytes_found > 0 && !st->running) {
                        if (cui_button(ui, wada_tr("Go to Cleaner"), .variant = CUI_BUTTON_OUTLINE,
                                       .icon = CUI_ICON_SPARKLES)) {
                            st->active_view = WC_VIEW_CLEANER;
                        }
                    }
                }
            }

            if (st->running) {
                float frac = st->snap.total_selected > 0
                                 ? (float)st->snap.completed_targets /
                                       (float)st->snap.total_selected
                                 : 0.0f;
                const char *cur = st->snap.current_label[0] ? wada_tr(st->snap.current_label) : "";
                char prog_lbl[160];
                wada_trf(prog_lbl, sizeof prog_lbl, "Scanning: %s", cur);
                cui_progress(ui, frac, .width = CUI_FILL, .label = prog_lbl, .show_value = true,
                             .tone = CUI_TONE_ACCENT);
            }
        }

        /* 2x2 Summary grid */
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_card(ui, .width = CUI_GROW(1.0f), .pad = 14, .gap = 6) {
                cui_text(ui, wada_tr("System Drive (C:)"), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, drive_value, .font = CUI_FONT_DISPLAY, .size = 24, .tabular = true);
                if (st->sys_drive_ok && st->sys_drive_total > 0) {
                    cui_progress(ui, drive_used_frac, .width = CUI_FILL, .height = 6.0f,
                                 .tone = drive_used_frac > 0.85f ? CUI_TONE_WARNING
                                                                 : CUI_TONE_ACCENT);
                    cui_text(ui, drive_desc, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
            }
            cui_card(ui, .width = CUI_GROW(1.0f), .pad = 14, .gap = 6) {
                cui_text(ui, wada_tr("Reclaimable Junk"), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, junk_value, .font = CUI_FONT_DISPLAY, .size = 24, .tabular = true);
                cui_text(ui, junk_desc, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_card(ui, .width = CUI_GROW(1.0f), .pad = 14, .gap = 6) {
                cui_text(ui, wada_tr("Startup Programs"), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, startup_value, .font = CUI_FONT_DISPLAY, .size = 24, .tabular = true);
            }
            cui_card(ui, .width = CUI_GROW(1.0f), .pad = 14, .gap = 6) {
                cui_text(ui, wada_tr("Last Clean"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, last_value, .font = CUI_FONT_DISPLAY, .size = 24, .tabular = true);
                cui_text(ui, last_desc, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        /* Recommendations & quick actions */
        cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 8) {
            cui_text(ui, wada_tr("Recommendations"), .font = CUI_FONT_MEDIUM, .size = 15);

            if (st->has_analyzed && st->snap.total_bytes_found > 0) {
                char sz[32], msg[160];
                wc_format_bytes(st->snap.total_bytes_found, sz, sizeof sz);
                wada_trf(msg, sizeof msg,
                         "Reclaimable junk detected (%s). Open Junk Cleaner to preview and remove it.",
                         sz);
                cui_alert(ui, msg, .tone = CUI_TONE_INFO, .icon = CUI_ICON_SPARKLES);
            }
            if (enabled_startup > 8) {
                char msg[160];
                wada_trf(msg, sizeof msg,
                         "%d startup items are enabled. Disabling unused ones can speed up boot times.",
                         enabled_startup);
                cui_alert(ui, msg, .tone = CUI_TONE_WARNING, .icon = CUI_ICON_ROCKET);
            }
            if (!st->is_admin) {
                cui_alert(ui,
                          wada_tr("Running as Standard User. Restart as Administrator to scan protected system folders."),
                          .tone = CUI_TONE_NEUTRAL, .icon = CUI_ICON_SHIELD);
            }
            if ((!st->has_analyzed || st->snap.total_bytes_found == 0) && enabled_startup <= 8 &&
                st->is_admin) {
                cui_text(ui,
                         wada_tr("Everything looks good! Check back periodically or configure Exclusions to protect paths."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_row(ui, .width = CUI_FILL, .gap = 8) {
                if (cui_button(ui, wada_tr("Review Junk Cleaner"), .variant = CUI_BUTTON_OUTLINE,
                               .size = CUI_SIZE_SM)) {
                    st->active_view = WC_VIEW_CLEANER;
                }
                if (cui_button(ui, wada_tr("Manage Startup Items"), .variant = CUI_BUTTON_OUTLINE,
                               .size = CUI_SIZE_SM)) {
                    st->active_view = WC_VIEW_STARTUP;
                }
                if (cui_button(ui, wada_tr("Manage Exclusions"), .variant = CUI_BUTTON_OUTLINE,
                               .size = CUI_SIZE_SM)) {
                    st->active_view = WC_VIEW_EXCLUSIONS;
                }
                if (!st->is_admin) {
                    if (cui_button(ui, wada_tr("Restart as Administrator"),
                                   .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                                   .icon = CUI_ICON_SHIELD)) {
                        wc_relaunch_admin(st, app);
                    }
                }
            }
        }
    }
}
