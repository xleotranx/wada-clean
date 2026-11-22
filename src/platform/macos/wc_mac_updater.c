#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wc_platform.h"
#include "wc_updater.h"

static bool run_cmd_capture(char *const argv[], const volatile int *cancel_flag, char *out_buf,
                            size_t out_cap, int *out_exit) {
    if (out_buf && out_cap > 0) out_buf[0] = '\0';
    if (out_exit) *out_exit = 1;

    int fds[2];
    if (pipe(fds) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fds[1]);
    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

    size_t pos = 0;
    bool cancelled = false;
    int status = 0;

    for (;;) {
        if (cancel_flag && *cancel_flag) {
            cancelled = true;
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            break;
        }

        char chunk[4096];
        for (;;) {
            ssize_t n = read(fds[0], chunk, sizeof chunk);
            if (n > 0) {
                if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                    size_t cp = (size_t)n;
                    if (pos + cp >= out_cap) cp = (out_cap - 1) - pos;
                    memcpy(out_buf + pos, chunk, cp);
                    pos += cp;
                }
            } else {
                break;
            }
        }

        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            for (;;) {
                ssize_t n = read(fds[0], chunk, sizeof chunk);
                if (n <= 0) break;
                if (out_buf && out_cap > 1 && pos < out_cap - 1) {
                    size_t cp = (size_t)n;
                    if (pos + cp >= out_cap) cp = (out_cap - 1) - pos;
                    memcpy(out_buf + pos, chunk, cp);
                    pos += cp;
                }
            }
            break;
        }
        usleep(50000);
    }

    close(fds[0]);
    if (out_buf && out_cap > 0) out_buf[pos] = '\0';
    if (!cancelled && out_exit && WIFEXITED(status)) *out_exit = WEXITSTATUS(status);
    return !cancelled;
}

bool wc_plat_winget_available(void) {
    char *const argv[] = {"brew", "--version", NULL};
    char buf[256];
    int code = 1;
    return run_cmd_capture(argv, NULL, buf, sizeof buf, &code) && code == 0;
}

bool wc_plat_winget_list_upgrades(const volatile int *cancel_flag, wc_update_item *out,
                                  int max_items, int *out_count) {
    if (out_count) *out_count = 0;
    if (!out || max_items <= 0) return false;

    const size_t cap = 128 * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    char *const argv[] = {"brew", "outdated", "--verbose", NULL};
    int code = 1;
    bool ok = run_cmd_capture(argv, cancel_flag, buf, cap, &code);
    if (!ok) {
        free(buf);
        return false;
    }

    int parsed = wc_updater_parse_brew_outdated(buf, out, max_items);
    free(buf);
    if (out_count) *out_count = parsed;
    return (code == 0 || parsed > 0);
}

bool wc_plat_winget_upgrade(const char *package_id, const volatile int *cancel_flag,
                            char *out_summary, size_t summary_cap) {
    if (out_summary && summary_cap > 0) out_summary[0] = '\0';

    const size_t cap = 64 * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;

    int code = 1;
    bool ok = false;
    if (package_id && package_id[0]) {
        if (!wc_updater_is_safe_id(package_id)) {
            free(buf);
            return false;
        }
        char *const argv[] = {"brew", "upgrade", (char *)package_id, NULL};
        ok = run_cmd_capture(argv, cancel_flag, buf, cap, &code);
    } else {
        char *const argv[] = {"brew", "upgrade", NULL};
        ok = run_cmd_capture(argv, cancel_flag, buf, cap, &code);
    }

    if (out_summary && summary_cap > 0) {
        snprintf(out_summary, summary_cap, "%s",
                 (ok && code == 0) ? "brew upgrade complete" : "brew upgrade failed");
    }
    free(buf);
    return ok && code == 0;
}
