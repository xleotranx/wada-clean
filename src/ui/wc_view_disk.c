#include "wc_product.h"

#include <cui.h>
#include <stdio.h>
#include <string.h>

#include "wada_app.h"
#include "wada_i18n.h"
#include "wc_platform.h"
#include "wc_safety.h"

void wc_disk_start_scan(wc_cleaner_state *st) {
    if (!st || st->disk_running || !st->disk_root[0]) return;
    if (wc_disk_worker_start(&st->disk_worker, st->disk_root)) {
        st->disk_running = true;
        wada_app_set_status(st->app, wada_tr("Disk analysis started"));
    } else {
        wada_app_set_status(st->app, wada_tr("Invalid folder path"));
    }
}

static void pick_preset_folder(wc_cleaner_state *st, const char *env_var, const char *sub) {
    char base[WC_PATH_CAP];
    if (!wc_plat_get_env(env_var, base, sizeof base) || !base[0]) return;
    if (sub && sub[0]) {
        char sep = (base[0] == '/') ? '/' : '\\';
        size_t blen = strlen(base);
        size_t slen = strlen(sub);
        if (blen + 1 + slen + 1 > sizeof st->disk_root) return;
        memcpy(st->disk_root, base, blen);
        st->disk_root[blen] = sep;
        memcpy(st->disk_root + blen + 1, sub, slen + 1);
    } else {
        snprintf(st->disk_root, sizeof st->disk_root, "%s", base);
    }
    wc_disk_start_scan(st);
}

void wc_view_disk_draw(wc_cleaner_state *st, wada_app *app) {
    cui_ctx *ui = wada_app_ui(app);

    char total_str[32], files_str[32], dirs_str[32];
    wc_format_bytes(st->disk_snap.total_bytes, total_str, sizeof total_str);
    snprintf(files_str, sizeof files_str, "%llu", (unsigned long long)st->disk_snap.total_files);
    snprintf(dirs_str, sizeof dirs_str, "%llu", (unsigned long long)st->disk_snap.total_dirs);

    const char *tab_labels[2] = {wada_tr("Largest Folders"), wada_tr("Largest Files")};

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 12) {
        cui_column(ui, .gap = 2) {
            cui_text(ui, wada_tr("Disk Space Analyzer"), .font = CUI_FONT_DISPLAY, .size = 24);
            cui_text(ui, wada_tr("Find large folders and files without modifying anything on disk."),
                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        }

        /* Path bar + Scan button */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
            cui_input(ui, "disk-root-input", st->disk_root, sizeof st->disk_root,
                      .placeholder = wada_tr("Folder path to analyze…"), .width = CUI_FILL,
                      .disabled = st->disk_running);
            if (!st->disk_running) {
                if (cui_button(ui, wada_tr("Scan"), .variant = CUI_BUTTON_PRIMARY,
                               .icon = CUI_ICON_SEARCH, .disabled = !st->disk_root[0])) {
                    wc_disk_start_scan(st);
                }
            } else {
                if (cui_button(ui, wada_tr("Cancel"), .variant = CUI_BUTTON_OUTLINE,
                               .icon = CUI_ICON_X)) {
                    wc_disk_worker_cancel(&st->disk_worker);
                }
            }
        }

        /* Quick folder presets */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 6) {
            cui_text(ui, wada_tr("Quick scan:"), .size = 12, .color = CUI_COLOR_TEXT_MUTED);
#ifdef _WIN32
            if (cui_button(ui, wada_tr("Home"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = st->disk_running)) {
                pick_preset_folder(st, "USERPROFILE", NULL);
            }
            if (cui_button(ui, wada_tr("Downloads"), .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .disabled = st->disk_running)) {
                pick_preset_folder(st, "USERPROFILE", "Downloads");
            }
            if (cui_button(ui, wada_tr("AppData"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = st->disk_running)) {
                pick_preset_folder(st, "LOCALAPPDATA", NULL);
            }
#else
            if (cui_button(ui, wada_tr("Home"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = st->disk_running)) {
                pick_preset_folder(st, "HOME", NULL);
            }
            if (cui_button(ui, wada_tr("Downloads"), .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .disabled = st->disk_running)) {
                pick_preset_folder(st, "HOME", "Downloads");
            }
            if (cui_button(ui, wada_tr("AppData"), .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .disabled = st->disk_running)) {
                pick_preset_folder(st, "HOME", "Library/Caches");
            }
#endif
        }

        if (st->disk_running) {
            char scanning_lbl[128];
            wada_trf(scanning_lbl, sizeof scanning_lbl, "Scanning %llu files (%s)…",
                     (unsigned long long)st->disk_snap.total_files, total_str);
            cui_progress(ui, 0.0f, .indeterminate = true, .width = CUI_FILL, .label = scanning_lbl,
                         .tone = CUI_TONE_ACCENT);
            if (st->disk_snap.current_dir[0]) {
                cui_text(ui, st->disk_snap.current_dir, .size = 11, .color = CUI_COLOR_TEXT_MUTED,
                         .nowrap = true);
            }
        }

        /* Summary metrics */
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, wada_tr("Total Size"), total_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Files"), files_str, .width = CUI_GROW(1.0f));
            cui_metric(ui, wada_tr("Folders"), dirs_str, .width = CUI_GROW(1.0f));
        }

        cui_tabs(ui, "disk-tabs", tab_labels, 2, &st->disk_tab);

        if (st->disk_snap.state == WC_JOB_IDLE) {
            cui_empty_state(ui, wada_tr("Ready to analyze"),
                            .description = wada_tr("Choose a folder above and click Scan to see what uses disk space."));
        } else if (st->disk_tab == 0) {
            if (st->disk_snap.top_dir_count == 0) {
                cui_empty_state(ui, wada_tr("No subfolders found"),
                                .description = wada_tr("Try scanning a parent folder or checking Largest Files."));
            } else {
                cui_scroll(ui, .id = "disk-dirs", .width = CUI_FILL, .height = CUI_FILL, .gap = 6) {
                    for (int i = 0; i < st->disk_snap.top_dir_count; i++) {
                        const wc_disk_entry *e = &st->disk_snap.top_dirs[i];
                        float ratio = st->disk_snap.total_bytes > 0
                                          ? (float)((double)e->bytes /
                                                    (double)st->disk_snap.total_bytes)
                                          : 0.0f;
                        char sz[32], info[96];
                        wc_format_bytes(e->bytes, sz, sizeof sz);
                        wada_trf(info, sizeof info, "%s (%llu files)", sz,
                                 (unsigned long long)e->file_count);

                        cui_id_scope(ui, e->path) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 6) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                    cui_text(ui, e->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                        cui_text(ui, info, .size = 12,
                                                 .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                                        if (cui_button(ui, wada_tr("Open"),
                                                       .variant = CUI_BUTTON_GHOST,
                                                       .size = CUI_SIZE_SM,
                                                       .icon = CUI_ICON_EXTERNAL_LINK)) {
                                            wc_plat_open_in_explorer(e->path, false);
                                        }
                                    }
                                }
                                cui_progress(ui, ratio, .width = CUI_FILL, .height = 6.0f,
                                             .tone = CUI_TONE_ACCENT);
                            }
                        }
                    }
                }
            }
        } else {
            if (st->disk_snap.top_file_count == 0) {
                cui_empty_state(ui, wada_tr("No files found"),
                                .description = wada_tr("No regular files were found in this folder."));
            } else {
                cui_scroll(ui, .id = "disk-files", .width = CUI_FILL, .height = CUI_FILL, .gap = 6) {
                    for (int i = 0; i < st->disk_snap.top_file_count; i++) {
                        const wc_disk_entry *e = &st->disk_snap.top_files[i];
                        char sz[32];
                        wc_format_bytes(e->bytes, sz, sizeof sz);

                        cui_id_scope(ui, e->path) {
                            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 3) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                        .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                                    cui_text(ui, e->name, .font = CUI_FONT_MEDIUM, .size = 13);
                                    cui_row(ui, .align = CUI_ALIGN_CENTER, .gap = 8) {
                                        cui_text(ui, sz, .size = 12,
                                                 .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
                                        if (cui_button(ui, wada_tr("Reveal"),
                                                       .variant = CUI_BUTTON_GHOST,
                                                       .size = CUI_SIZE_SM,
                                                       .icon = CUI_ICON_EXTERNAL_LINK)) {
                                            wc_plat_open_in_explorer(e->path, true);
                                        }
                                    }
                                }
                                cui_text(ui, e->path, .size = 11, .color = CUI_COLOR_TEXT_MUTED,
                                         .nowrap = true);
                            }
                        }
                    }
                }
            }
        }
    }
}
