#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_safety.h"

static int history_target_count(const wc_history_entry *entry) {
    int count = 0;
    const char *p = entry->details;
    while (p && *p) {
        count++;
        const char *nl = strchr(p, '\n');
        p = nl ? nl + 1 : p + strlen(p);
    }
    return count;
}

static void draw_history_detail(wc_cleaner_state *st, cui_ctx *ui) {
    if (st->history_selected < 0 || st->history_selected >= st->history_count) {
        st->history_open = false;
        return;
    }
    const wc_history_entry *entry = &st->history_entries[st->history_selected];

    char when[48];
    wada_ui_format_when(entry->unix_sec, when, sizeof when);
    char freed[32];
    wc_format_bytes(entry->bytes_cleaned, freed, sizeof freed);
    char files_str[32];
    snprintf(files_str, sizeof files_str, "%llu", (unsigned long long)entry->files_cleaned);
    char skipped_str[32];
    snprintf(skipped_str, sizeof skipped_str, "%llu", (unsigned long long)entry->skipped_count);
    int targets = history_target_count(entry);
    char targets_str[32];
    if (targets > 0) {
        snprintf(targets_str, sizeof targets_str, "%d", targets);
    } else {
        snprintf(targets_str, sizeof targets_str, "—");
    }

    char status[96];
    wada_trf(status, sizeof status, "%s · %s", when,
             wada_tr(entry->dry_run ? "Dry Run" : "Clean"));

    char summary_line[192];
    if (entry->dry_run) {
        wada_trf(summary_line, sizeof summary_line, "Preview: %s across %llu files.", freed,
                 (unsigned long long)entry->files_cleaned);
    } else {
        wada_trf(summary_line, sizeof summary_line, "Freed %s across %llu files.", freed,
                 (unsigned long long)entry->files_cleaned);
    }

    char copy[WC_HISTORY_DETAIL_CAP + 512];
    int cn = snprintf(copy, sizeof copy, "%s — %s\n%s\n%s: %s\n%s: %llu\n%s: %llu\n",
                      wada_tr(entry->dry_run ? "Preview run" : "Clean run"), status, summary_line,
                      wada_tr("Freed"), freed, wada_tr("Files"),
                      (unsigned long long)entry->files_cleaned, wada_tr("Skipped"),
                      (unsigned long long)entry->skipped_count);
    if (cn > 0 && (size_t)cn < sizeof copy && entry->details[0]) {
        size_t used = (size_t)cn;
        snprintf(copy + used, sizeof copy - used, "%s", entry->details);
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_START,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 10) {
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 10) {
                if (cui_button(ui, wada_tr("Back"), .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_CHEVRON_LEFT)) {
                    st->history_open = false;
                }
                cui_column(ui, .gap = 2) {
                    cui_text(ui, wada_tr(entry->dry_run ? "Preview run" : "Clean run"),
                             .font = CUI_FONT_DISPLAY, .size = 24);
                    cui_text(ui, status, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }
            }
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                cui_copy_button(ui, "history-copy", copy);
                cui_text(ui, wada_tr("Copy details"), .size = 12,
                         .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, wada_tr("Freed"), freed, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Files"), files_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Skipped"), skipped_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Targets"), targets_str, .width = CUI_GROW(1.0f));
        }

        if (summary_line[0]) {
            cui_text(ui, summary_line, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        if (entry->details[0] == '\0') {
            cui_alert(ui, wada_tr("This run predates detailed history; only totals are available."),
                      .tone = CUI_TONE_INFO, .icon = CUI_ICON_INFO);
        } else {
            cui_text(ui, wada_tr("Per-target results"), .font = CUI_FONT_MEDIUM, .size = 15);
            wc_scroll_list(ui, "history-detail", 6) {
                const char *p = entry->details;
                while (p && *p) {
                    const char *nl = strchr(p, '\n');
                    size_t len = nl ? (size_t)(nl - p) : strlen(p);
                    char line[WC_HISTORY_DETAIL_CAP];
                    if (len >= sizeof line) len = sizeof line - 1;
                    memcpy(line, p, len);
                    line[len] = '\0';
                    p = nl ? nl + 1 : p + strlen(p);

                    char *fields[6];
                    int nf = 0;
                    char *cur = line;
                    while (nf < 6) {
                        fields[nf++] = cur;
                        char *bar = strchr(cur, '|');
                        if (!bar) break;
                        *bar = '\0';
                        cur = bar + 1;
                    }
                    if (nf < 5) continue;

                    const char *label = fields[1][0] ? fields[1] : fields[0];
                    uint64_t f = strtoull(fields[2], NULL, 10);
                    uint64_t b = strtoull(fields[3], NULL, 10);
                    uint64_t s = strtoull(fields[4], NULL, 10);
                    const char *note = nf >= 6 ? fields[5] : "";
                    char size_str[32];
                    wc_format_bytes(b, size_str, sizeof size_str);
                    char meta[160];
                    wada_trf(meta, sizeof meta, "%s · %llu files · %llu skipped", size_str,
                             (unsigned long long)f, (unsigned long long)s);

                    cui_id_scope(ui, fields[0]) {
                        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4, .muted = true) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                cui_text(ui, wada_tr(label), .font = CUI_FONT_MEDIUM, .size = 13);
                                cui_text(ui, meta, .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                            }
                            if (note[0]) {
                                bool warn = strstr(note, "skipped") != NULL ||
                                            strstr(note, "Administrator") != NULL ||
                                            strstr(note, "Access") != NULL ||
                                            strstr(note, "Failed") != NULL ||
                                            strstr(note, "Browser") != NULL;
                                cui_text(ui, wada_tr(note), .size = 12,
                                         .color = warn ? CUI_COLOR_WARNING : CUI_COLOR_TEXT_MUTED);
                            }
                        }
                    }
                }
            }
        }
    }
}

static bool hub_card(cui_ctx *ui, const char *id, const char *title, const char *desc,
                     const char *icon, const char *action) {
    bool clicked = false;
    cui_id_scope(ui, id) {
        cui_card(ui, .width = CUI_GROW(1.0f), .pad = 16, .gap = 8) {
            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                cui_icon(ui, icon, 16.0f, CUI_COLOR_TEXT_SECONDARY);
                cui_text(ui, wada_tr(title), .font = CUI_FONT_MEDIUM, .size = 16);
            }
            cui_text(ui, wada_tr(desc), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            if (cui_button(ui, wada_tr(action), .variant = CUI_BUTTON_OUTLINE,
                           .size = CUI_SIZE_SM)) {
                clicked = true;
            }
        }
    }
    return clicked;
}

void wc_view_tools_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);
    if (st->history_open) {
        draw_history_detail(st, ui);
        return;
    }

    cui_column(ui, .id = "tools-scroll", .width = CUI_FILL, .height = CUI_FILL, .scroll_y = true,
               .pad = 20, .gap = 14) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Tools"), .font = CUI_FONT_DISPLAY, .size = 26);
            cui_text(ui, wada_tr("Everything else. Nothing here is needed for a normal clean."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            if (hub_card(ui, "tools-apps", "Apps", "Uninstall apps and free space.",
                         CUI_ICON_PACKAGE, "Open")) {
                st->active_view = WC_VIEW_APPS;
            }
            if (hub_card(ui, "tools-space", "Space", "See what is using your disk.",
                         CUI_ICON_FOLDER_OPEN, "Open")) {
                st->active_view = WC_VIEW_DISK;
            }
        }

        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_text(ui, wada_tr("History"), .font = CUI_FONT_MEDIUM, .size = 16);
            if (st->history_count == 0) {
                cui_text(ui, wada_tr("No clean runs recorded yet."), .size = 13,
                         .color = CUI_COLOR_TEXT_MUTED);
            } else {
                for (int i = 0; i < st->history_count && i < 6; i++) {
                    const wc_history_entry *e = &st->history_entries[i];
                    char when[32], size_str[32], detail[96];
                    wada_ui_format_when(e->unix_sec, when, sizeof when);
                    wc_format_bytes(e->bytes_cleaned, size_str, sizeof size_str);
                    wada_trf(detail, sizeof detail, "%s | %llu files | %llu skipped", size_str,
                             (unsigned long long)e->files_cleaned,
                             (unsigned long long)e->skipped_count);
                    cui_id_scope(ui, e->id) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                            cui_column(ui, .gap = 2) {
                                cui_text(ui, when, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                                cui_text(ui, detail, .size = 12,
                                         .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                            }
                            cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 6) {
                                cui_badge(ui, wada_tr(e->dry_run ? "Dry Run" : "Clean"),
                                          .tone = e->dry_run ? CUI_TONE_INFO : CUI_TONE_SUCCESS,
                                          .size = CUI_SIZE_SM);
                                if (cui_button(ui, wada_tr("View"), .variant = CUI_BUTTON_GHOST,
                                               .size = CUI_SIZE_SM)) {
                                    st->history_selected = i;
                                    st->history_open = true;
                                }
                            }
                        }
                    }
                    if (i + 1 < st->history_count && i < 5) cui_separator(ui);
                }
            }
        }

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 10) {
            if (cui_button(ui, wada_tr("About"), .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_INFO)) {
                st->about_open = true;
            }
            if (st->data_dir[0] &&
                cui_button(ui, wada_tr("Open data folder"), .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_FOLDER_OPEN)) {
                wc_plat_open_in_explorer(st->data_dir, false);
            }
        }
    }
}
