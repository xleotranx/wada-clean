#include "wc_rules.h"

#include <stdio.h>
#include <string.h>

#include "wc_platform.h"
#include "wc_safety.h"

#ifdef _WIN32

static const wc_target_def g_rules[] = {
    {
        "win.user_temp",
        "User Temp Files",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "TEMP",
        "",
        "",
        true,
        false,
    },
    {
        "win.recycle_bin",
        "Recycle Bin",
        WC_CAT_SYSTEM,
        WC_TARGET_RECYCLE_BIN,
        "",
        "",
        "",
        true,
        false,
    },
    {
        "win.thumb_cache",
        "Thumbnail Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "Microsoft\\Windows\\Explorer",
        "",
        true,
        false,
    },
    {
        "win.crash_dumps",
        "Crash Dumps",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "CrashDumps",
        "",
        true,
        false,
    },
    {
        "win.sys_temp",
        "Windows Temp",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "",
        "C:\\Windows\\Temp",
        "",
        false,
        true,
    },
    {
        "win.wu_cache",
        "Windows Update Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "",
        "C:\\Windows\\SoftwareDistribution\\Download",
        "",
        false,
        true,
    },
    {
        "browser.chrome_cache",
        "Chrome Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "Google\\Chrome\\User Data\\Default\\Cache\\Cache_Data",
        "chrome.exe",
        true,
        false,
    },
    {
        "browser.edge_cache",
        "Edge Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "Microsoft\\Edge\\User Data\\Default\\Cache\\Cache_Data",
        "msedge.exe",
        true,
        false,
    },
    {
        "browser.brave_cache",
        "Brave Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "BraveSoftware\\Brave-Browser\\User Data\\Default\\Cache\\Cache_Data",
        "brave.exe",
        true,
        false,
    },
    {
        "browser.firefox_cache",
        "Firefox Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "Mozilla\\Firefox\\Profiles",
        "firefox.exe",
        true,
        false,
    },
};

#else /* macOS / POSIX */

static const wc_target_def g_rules[] = {
    {
        "mac.user_caches",
        "User Caches",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Caches",
        "",
        true,
        false,
    },
    {
        "mac.trash",
        "Trash",
        WC_CAT_SYSTEM,
        WC_TARGET_RECYCLE_BIN,
        "",
        "",
        "",
        true,
        false,
    },
    {
        "mac.user_logs",
        "User Logs",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Logs",
        "",
        true,
        false,
    },
    {
        "mac.crash_reports",
        "Crash Reports",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Logs/DiagnosticReports",
        "",
        true,
        false,
    },
    {
        "mac.sys_caches",
        "System Caches",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "",
        "/Library/Caches",
        "",
        false,
        true,
    },
    {
        "browser.chrome_cache",
        "Chrome Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Caches/Google/Chrome/Default/Cache",
        "Google Chrome",
        true,
        false,
    },
    {
        "browser.edge_cache",
        "Edge Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Caches/Microsoft Edge/Default/Cache",
        "Microsoft Edge",
        true,
        false,
    },
    {
        "browser.safari_cache",
        "Safari Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Caches/com.apple.Safari",
        "Safari",
        true,
        false,
    },
    {
        "browser.firefox_cache",
        "Firefox Cache",
        WC_CAT_BROWSER,
        WC_TARGET_DIR_CONTENTS,
        "HOME",
        "Library/Caches/Firefox/Profiles",
        "firefox",
        true,
        false,
    },
};

#endif

int wc_rules_count(void) {
    return (int)(sizeof g_rules / sizeof g_rules[0]);
}

const wc_target_def *wc_rules_get(int index) {
    if (index < 0 || index >= wc_rules_count()) return NULL;
    return &g_rules[index];
}

bool wc_rules_resolve_path(const wc_target_def *def, char *out, size_t cap) {
    if (!def || !out || cap == 0) return false;
    out[0] = '\0';
    if (def->kind == WC_TARGET_RECYCLE_BIN) return false;

    char combined[WC_PATH_CAP];
    if (def->env_var[0]) {
        char base[WC_PATH_CAP];
        if (!wc_plat_get_env(def->env_var, base, sizeof base) || !base[0]) {
            return false;
        }
        if (def->subpath[0]) {
            char sep = (base[0] == '/') ? '/' : '\\';
            int n = snprintf(combined, sizeof combined, "%s%c%s", base, sep, def->subpath);
            if (n < 0 || (size_t)n >= sizeof combined) return false;
        } else {
            snprintf(combined, sizeof combined, "%s", base);
        }
    } else {
        if (!def->subpath[0]) return false;
        snprintf(combined, sizeof combined, "%s", def->subpath);
    }

    if (!wc_safety_normalize_path(combined, out, cap)) {
        out[0] = '\0';
        return false;
    }
    if (!wc_safety_is_valid_target_root(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}
