#include "wc_product.h"

#include <cui.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static const char *source_label(wc_startup_source src) {
    switch (src) {
    case WC_STARTUP_HKCU_RUN:
        return "HKCU Run";
    case WC_STARTUP_HKLM_RUN:
        return "HKLM Run";
    case WC_STARTUP_USER_FOLDER:
        return "User Startup Folder";
    case WC_STARTUP_COMMON_FOLDER:
        return "Common Startup Folder";
    }
    return "Startup";
}

void wc_startup_refresh(wc_cleaner_state *st) {
    if (!st || !st->startup_items) return;
    st->startup_loaded = true;
    st->startup_count = 0;
    wc_plat_list_startup(st->startup_items, WC_MAX_STARTUP, &st->startup_count);
}

void wc_view_startup_draw(wc_cleaner_state *st, wada_app *app) {
    if (!st->startup_loaded) wc_startup_refresh(st);

    cui_ctx *ui = wada_app_ui(app);
    int enabled_count = 0;
    int shown_count = 0;
    for (int i = 0; i < st->startup_count; i++) {
        if (st->startup_items[i].enabled) enabled_count++;
        if (str_contains_ci(st->startup_items[i].name, st->startup_search) ||
            str_contains_ci(st->startup_items[i].command, st->startup_search)) {
            shown_count++;
        }
    }

    char count_text[64];
    wada_trf(count_text, sizeof count_text, "%d enabled / %d total", enabled_count,
             st->startup_count);

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                .justify = CUI_JUSTIFY_BETWEEN, .gap = 12) {
            cui_column(ui, .gap = 2) {
                cui_text(ui, wada_tr("Startup Manager"), .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui,
                         wada_tr("Disable or re-enable startup programs safely without deleting them."),
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
            if (cui_button(ui, wada_tr("Refresh"), .variant = CUI_BUTTON_OUTLINE,
                           .icon = CUI_ICON_REFRESH_CW)) {
                wc_startup_refresh(st);
                wada_app_set_status(app, wada_tr("Startup list refreshed"));
            }
        }

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 12) {
            cui_search(ui, "startup-search", st->startup_search, sizeof st->startup_search,
                       .placeholder = wada_tr("Search startup programs…"), .width = CUI_FILL);
            cui_text(ui, count_text, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }

        if (shown_count == 0) {
            cui_empty_state(ui, wada_tr("No startup items found"),
                            .description = wada_tr("Try clearing your search filter or refreshing."));
        } else {
            wc_scroll_list(ui, "startup-list", 6) {
                for (int i = 0; i < st->startup_count; i++) {
                    wc_startup_item *item = &st->startup_items[i];
                    if (!str_contains_ci(item->name, st->startup_search) &&
                        !str_contains_ci(item->command, st->startup_search)) {
                        continue;
                    }

                    char scope_key[160];
                    snprintf(scope_key, sizeof scope_key, "su-%d-%s", i, item->name);
                    bool locked = item->needs_admin && !st->is_admin;

                    cui_id_scope(ui, scope_key) {
                        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                    .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                    cui_text(ui, item->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_badge(ui, wada_tr(source_label(item->source)),
                                              .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                    if (locked) {
                                        cui_badge(ui, wada_tr("Admin"), .tone = CUI_TONE_WARNING,
                                                  .size = CUI_SIZE_SM);
                                    }
                                }

                                bool next = item->enabled;
                                if (cui_switch(ui, wada_tr(item->enabled ? "Enabled" : "Disabled"),
                                               &next, .disabled = locked)) {
                                    if (wc_plat_set_startup_enabled(item, next)) {
                                        item->enabled = next;
                                        wada_app_set_status(app, wada_tr("Updated startup item"));
                                    } else {
                                        wada_app_set_status(
                                            app, wada_tr("Could not update startup item"));
                                    }
                                }
                            }

                            if (item->command[0]) {
                                cui_text(ui, item->command, .size = 11,
                                         .color = CUI_COLOR_TEXT_MUTED, .nowrap = true);
                            }
                        }
                    }
                }
            }
        }
    }
}
