#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wc_platform.h"

static bool ends_with(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t suflen = strlen(suffix);
    if (slen < suflen) return false;
    return strcmp(s + slen - suflen, suffix) == 0;
}

static bool plist_has_disabled_true(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long fsz = ftell(f);
    if (fsz <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    const long max_sz = 256L * 1024L;
    size_t cap = (size_t)(fsz > max_sz ? max_sz : fsz);
    char *buf = (char *)malloc(cap + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    buf[n] = '\0';

    bool disabled = false;
    const char *k = strstr(buf, "<key>Disabled</key>");
    if (k) {
        k += strlen("<key>Disabled</key>");
        while (*k == ' ' || *k == '\t' || *k == '\r' || *k == '\n') k++;
        disabled = (strncmp(k, "<true/>", 7) == 0);
    }
    free(buf);
    return disabled;
}

static void scan_launch_agents(const char *dir, wc_startup_source src, bool needs_admin,
                               wc_startup_item *out, int max_items, int *count) {
    DIR *dp = opendir(dir);
    if (!dp) return;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL && *count < max_items) {
        if (de->d_name[0] == '.') continue;
        if (!ends_with(de->d_name, ".plist")) continue;

        char full[WC_PATH_CAP];
        int fn = snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
        if (fn < 0 || (size_t)fn >= sizeof full) continue;

        wc_startup_item item;
        memset(&item, 0, sizeof item);
        size_t nlen = strlen(de->d_name) - 6; /* strip ".plist" */
        if (nlen >= sizeof item.name) nlen = sizeof item.name - 1;
        memcpy(item.name, de->d_name, nlen);
        item.name[nlen] = '\0';
        snprintf(item.command, sizeof item.command, "%s", full);
        item.source = src;
        item.needs_admin = needs_admin;
        item.enabled = !plist_has_disabled_true(full);
        out[(*count)++] = item;
    }
    closedir(dp);
}

bool wc_plat_list_startup(wc_startup_item *out, int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    int count = 0;
    char home[WC_PATH_CAP];
    if (wc_plat_get_env("HOME", home, sizeof home) && home[0]) {
        char user_dir[WC_PATH_CAP];
        int n = snprintf(user_dir, sizeof user_dir, "%s/Library/LaunchAgents", home);
        if (n > 0 && (size_t)n < sizeof user_dir) {
            scan_launch_agents(user_dir, WC_STARTUP_USER_FOLDER, false, out, max_items, &count);
        }
    }
    scan_launch_agents("/Library/LaunchAgents", WC_STARTUP_COMMON_FOLDER, true, out, max_items,
                       &count);

    if (out_count) *out_count = count;
    return true;
}

bool wc_plat_set_startup_enabled(const wc_startup_item *item, bool enabled) {
    if (!item || !item->command[0]) return false;

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const char *op = enabled ? "load" : "unload";
        char *argv[] = {"launchctl", (char *)op, "-w", (char *)item->command, NULL};
        execvp("launchctl", argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
