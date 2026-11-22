#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_platform.h"

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

static int cmp_app_name(const void *pa, const void *pb) {
    const wc_app_item *a = (const wc_app_item *)pa;
    const wc_app_item *b = (const wc_app_item *)pb;
    return ascii_strcasecmp(a->name, b->name);
}

static bool ends_with(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t suflen = strlen(suffix);
    if (slen < suflen) return false;
    return strcmp(s + slen - suflen, suffix) == 0;
}

static void scan_apps_dir(const char *dir, bool is_user, wc_app_item *out, int max_items,
                          int *count) {
    DIR *dp = opendir(dir);
    if (!dp) return;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL && *count < max_items) {
        if (de->d_name[0] == '.') continue;
        if (!ends_with(de->d_name, ".app")) continue;

        char full[WC_PATH_CAP];
        int fn = snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        if (fn < 0 || (size_t)fn >= sizeof full) continue;

        wc_app_item item;
        memset(&item, 0, sizeof item);
        size_t nlen = strlen(de->d_name) - 4; /* strip ".app" */
        if (nlen >= sizeof item.name) nlen = sizeof item.name - 1;
        memcpy(item.name, de->d_name, nlen);
        item.name[nlen] = '\0';
        snprintf(item.uninstall_cmd, sizeof item.uninstall_cmd, "%s", full);
        item.is_user_install = is_user;
        out[(*count)++] = item;
    }
    closedir(dp);
}

bool wc_plat_list_apps(wc_app_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    int count = 0;
    scan_apps_dir("/Applications", false, out, max_items, &count);

    char home[WC_PATH_CAP];
    if (wc_plat_get_env("HOME", home, sizeof home) && home[0]) {
        char user_apps[WC_PATH_CAP];
        int n = snprintf(user_apps, sizeof user_apps, "%s/Applications", home);
        if (n > 0 && (size_t)n < sizeof user_apps) {
            scan_apps_dir(user_apps, true, out, max_items, &count);
        }
    }

    if (count > 1) {
        qsort(out, (size_t)count, sizeof out[0], cmp_app_name);
    }
    if (out_count) *out_count = count;
    return true;
}

bool wc_plat_launch_uninstaller(const char *uninstall_cmd) {
    if (!uninstall_cmd || !uninstall_cmd[0]) return false;
    return wc_plat_open_in_explorer(uninstall_cmd, true);
}
