#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wc_platform.h"

#define WC_MAC_PLIST_NAME "com.wadaclean.scheduled.plist"

static bool resolve_plist_path(char *out, size_t cap) {
    char home[WC_PATH_CAP];
    if (!wc_plat_get_env("HOME", home, sizeof home) || !home[0]) return false;
    int n = snprintf(out, cap, "%s/Library/LaunchAgents/%s", home, WC_MAC_PLIST_NAME);
    return n > 0 && (size_t)n < cap;
}

static bool run_launchctl(const char *op, const char *plist_path) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        char *argv[] = {"launchctl", (char *)op, "-w", (char *)plist_path, NULL};
        execvp("launchctl", argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool wc_plat_schedule_query(wc_schedule_info *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->freq = WC_SCHED_OFF;
    out->hour = 3;
    out->minute = 0;

    char plist[WC_PATH_CAP];
    if (!resolve_plist_path(plist, sizeof plist)) return false;

    FILE *f = fopen(plist, "rb");
    if (!f) {
        out->active = false;
        return true;
    }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    out->active = true;
    out->freq = strstr(buf, "<key>Weekday</key>") ? WC_SCHED_WEEKLY : WC_SCHED_DAILY;

    const char *hk = strstr(buf, "<key>Hour</key>");
    if (hk) {
        const char *iv = strstr(hk, "<integer>");
        if (iv) out->hour = atoi(iv + 9);
    }
    snprintf(out->next_run, sizeof out->next_run, "%02d:%02d", out->hour, out->minute);
    return true;
}

static bool xml_escape_str(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return false;
    size_t pos = 0;
    for (const char *p = in; *p; p++) {
        const char *rep = NULL;
        size_t rlen = 0;
        switch (*p) {
        case '&':
            rep = "&amp;";
            rlen = 5;
            break;
        case '<':
            rep = "&lt;";
            rlen = 4;
            break;
        case '>':
            rep = "&gt;";
            rlen = 4;
            break;
        case '"':
            rep = "&quot;";
            rlen = 6;
            break;
        case '\'':
            rep = "&apos;";
            rlen = 6;
            break;
        default:
            break;
        }
        if (rep) {
            if (pos + rlen >= cap) return false;
            memcpy(out + pos, rep, rlen);
            pos += rlen;
        } else {
            if (pos + 1 >= cap) return false;
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return true;
}

bool wc_plat_schedule_set(wc_schedule_freq freq, int hour, int minute) {
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;

    char plist[WC_PATH_CAP];
    if (!resolve_plist_path(plist, sizeof plist)) return false;

    if (freq == WC_SCHED_OFF) {
        (void)run_launchctl("unload", plist);
        unlink(plist);
        return true;
    }

    char home[WC_PATH_CAP];
    if (wc_plat_get_env("HOME", home, sizeof home) && home[0]) {
        char la_dir[WC_PATH_CAP];
        snprintf(la_dir, sizeof la_dir, "%s/Library/LaunchAgents", home);
        mkdir(la_dir, 0755);
    }

    char exe[WC_PATH_CAP];
    uint32_t exe_sz = (uint32_t)sizeof exe;
    if (_NSGetExecutablePath(exe, &exe_sz) != 0) return false;

    char esc_exe[WC_PATH_CAP * 2];
    if (!xml_escape_str(exe, esc_exe, sizeof esc_exe)) return false;

    (void)run_launchctl("unload", plist);

    FILE *f = fopen(plist, "wb");
    if (!f) return false;

    fprintf(f,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "  <key>Label</key><string>com.wadaclean.scheduled</string>\n"
            "  <key>ProgramArguments</key>\n"
            "  <array><string>%s</string><string>--scheduled-clean</string></array>\n"
            "  <key>StartCalendarInterval</key>\n"
            "  <dict>\n",
            esc_exe);
    if (freq == WC_SCHED_WEEKLY) {
        fprintf(f, "    <key>Weekday</key><integer>0</integer>\n");
    }
    fprintf(f,
            "    <key>Hour</key><integer>%d</integer>\n"
            "    <key>Minute</key><integer>%d</integer>\n"
            "  </dict>\n"
            "</dict>\n"
            "</plist>\n",
            hour, minute);
    fclose(f);

    return run_launchctl("load", plist);
}

bool wc_plat_tray_show(const char *tooltip) {
    (void)tooltip;
    return false;
}

void wc_plat_tray_hide(void) {
}

void wc_plat_tray_notify(const char *title, const char *message) {
    (void)title;
    (void)message;
}
