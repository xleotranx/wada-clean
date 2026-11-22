#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "wc_platform.h"

typedef struct chromium_def {
    const char *label;
    const char *rel;
    const char *process_name;
} chromium_def;

static const chromium_def g_chromium[] = {
    {"Chrome", "Library/Application Support/Google/Chrome", "Google Chrome"},
    {"Edge", "Library/Application Support/Microsoft Edge", "Microsoft Edge"},
    {"Brave", "Library/Application Support/BraveSoftware/Brave-Browser", "Brave Browser"},
};

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void add_db(wc_cookie_db *out, int max_items, int *count, const char *browser,
                   const char *profile, const char *path, const char *process_name,
                   bool is_firefox) {
    if (*count >= max_items) return;
    wc_cookie_db *item = &out[*count];
    memset(item, 0, sizeof *item);
    snprintf(item->browser, sizeof item->browser, "%s", browser);
    snprintf(item->profile, sizeof item->profile, "%s", profile);
    snprintf(item->path, sizeof item->path, "%s", path);
    snprintf(item->process_name, sizeof item->process_name, "%s", process_name);
    item->is_firefox = is_firefox;
    (*count)++;
}

static void scan_chromium(const char *home, const chromium_def *def, wc_cookie_db *out,
                          int max_items, int *count) {
    char base[WC_PATH_CAP];
    if (snprintf(base, sizeof base, "%s/%s", home, def->rel) >= (int)sizeof base) return;

    DIR *dir = opendir(base);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char path[WC_PATH_CAP];
        snprintf(path, sizeof path, "%s/%s/Network/Cookies", base, entry->d_name);
        if (!file_exists(path)) {
            snprintf(path, sizeof path, "%s/%s/Cookies", base, entry->d_name);
        }
        if (file_exists(path)) {
            add_db(out, max_items, count, def->label, entry->d_name, path, def->process_name,
                   false);
        }
    }
    closedir(dir);
}

static void scan_firefox(const char *home, wc_cookie_db *out, int max_items, int *count) {
    char base[WC_PATH_CAP];
    if (snprintf(base, sizeof base, "%s/Library/Application Support/Firefox/Profiles", home) >=
        (int)sizeof base) {
        return;
    }

    DIR *dir = opendir(base);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char path[WC_PATH_CAP];
        snprintf(path, sizeof path, "%s/%s/cookies.sqlite", base, entry->d_name);
        if (file_exists(path)) {
            add_db(out, max_items, count, "Firefox", entry->d_name, path, "firefox", true);
        }
    }
    closedir(dir);
}

static int compare_db(const void *a, const void *b) {
    const wc_cookie_db *x = (const wc_cookie_db *)a;
    const wc_cookie_db *y = (const wc_cookie_db *)b;
    int by_browser = strcmp(x->browser, y->browser);
    if (by_browser != 0) return by_browser;
    return strcmp(x->profile, y->profile);
}

int wc_plat_list_cookie_dbs(wc_cookie_db *out, int max_items) {
    if (!out || max_items <= 0) return 0;
    if (max_items > WC_MAX_COOKIE_DBS) max_items = WC_MAX_COOKIE_DBS;

    const char *home = getenv("HOME");
    if (!home || !home[0]) return 0;

    int count = 0;
    for (size_t i = 0; i < sizeof g_chromium / sizeof g_chromium[0]; i++) {
        scan_chromium(home, &g_chromium[i], out, max_items, &count);
    }
    scan_firefox(home, out, max_items, &count);

    if (count > 1) qsort(out, (size_t)count, sizeof out[0], compare_db);
    return count;
}
