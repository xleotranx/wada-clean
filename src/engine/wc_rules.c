#include "wc_rules.h"

#include <stdio.h>
#include <string.h>

#include "wc_platform.h"
#include "wc_safety.h"

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
        WC_RISK_SAFE,
    },
    {
        "win.recycle_bin",
        "Recycle Bin",
        WC_CAT_SYSTEM,
        WC_TARGET_RECYCLE_BIN,
        "",
        "",
        "",
        false,
        false,
        WC_RISK_SAFE,
    },
    {
        "win.thumb_cache",
        "Thumbnail Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_THUMB_CACHE,
        "LOCALAPPDATA",
        "Microsoft\\Windows\\Explorer",
        "",
        true,
        false,
        WC_RISK_SAFE,
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
        WC_RISK_SAFE,
    },
    {
        "win.d3d_cache",
        "DirectX Shader Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "D3DSCache",
        "",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "win.nvidia_cache",
        "NVIDIA Shader Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "NVIDIA\\DXCache",
        "",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "win.amd_cache",
        "AMD Shader Cache",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "AMD\\DxCache",
        "",
        true,
        false,
        WC_RISK_SAFE,
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
        WC_RISK_SAFE,
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
        WC_RISK_SAFE,
    },
    {
        "win.wer_reports",
        "Windows Error Reporting",
        WC_CAT_SYSTEM,
        WC_TARGET_DIR_CONTENTS,
        "ProgramData",
        "Microsoft\\Windows\\WER",
        "",
        false,
        true,
        WC_RISK_SAFE,
    },

    {
        "browser.chrome_cache",
        "Chrome Cache (All Profiles)",
        WC_CAT_BROWSER,
        WC_TARGET_CHROMIUM_PROFILES,
        "LOCALAPPDATA",
        "Google\\Chrome\\User Data",
        "chrome.exe",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "browser.edge_cache",
        "Edge Cache (All Profiles)",
        WC_CAT_BROWSER,
        WC_TARGET_CHROMIUM_PROFILES,
        "LOCALAPPDATA",
        "Microsoft\\Edge\\User Data",
        "msedge.exe",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "browser.brave_cache",
        "Brave Cache (All Profiles)",
        WC_CAT_BROWSER,
        WC_TARGET_CHROMIUM_PROFILES,
        "LOCALAPPDATA",
        "BraveSoftware\\Brave-Browser\\User Data",
        "brave.exe",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "browser.firefox_cache",
        "Firefox Cache",
        WC_CAT_BROWSER,
        WC_TARGET_FIREFOX_PROFILES,
        "LOCALAPPDATA",
        "Mozilla\\Firefox\\Profiles",
        "firefox.exe",
        true,
        false,
        WC_RISK_SAFE,
    },
    {
        "app.vscode_cache",
        "VS Code Cache",
        WC_CAT_APPS,
        WC_TARGET_DIR_CONTENTS,
        "APPDATA",
        "Code\\Cache\\Cache_Data",
        "Code.exe",
        false,
        false,
        WC_RISK_SAFE,
    },
    {
        "app.discord_cache",
        "Discord Cache",
        WC_CAT_APPS,
        WC_TARGET_DIR_CONTENTS,
        "APPDATA",
        "discord\\Cache\\Cache_Data",
        "Discord.exe",
        false,
        false,
        WC_RISK_SAFE,
    },
    {
        "app.slack_cache",
        "Slack Cache",
        WC_CAT_APPS,
        WC_TARGET_DIR_CONTENTS,
        "APPDATA",
        "Slack\\Cache\\Cache_Data",
        "slack.exe",
        false,
        false,
        WC_RISK_SAFE,
    },
    {
        "app.spotify_cache",
        "Spotify Cache",
        WC_CAT_APPS,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "Spotify\\Storage",
        "Spotify.exe",
        false,
        false,
        WC_RISK_SAFE,
    },
    {
        "dev.npm_cache",
        "npm Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "npm-cache",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.pnpm_store",
        "pnpm Store Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "pnpm\\store",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.pip_cache",
        "pip Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "pip\\Cache",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.uv_cache",
        "uv Python Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "uv\\cache",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.nuget_cache",
        "NuGet Packages Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "USERPROFILE",
        ".nuget\\packages",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.cargo_cache",
        "Cargo Registry Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "USERPROFILE",
        ".cargo\\registry\\cache",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.gradle_cache",
        "Gradle Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "USERPROFILE",
        ".gradle\\caches",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
    {
        "dev.go_build_cache",
        "Go Build Cache",
        WC_CAT_DEV,
        WC_TARGET_DIR_CONTENTS,
        "LOCALAPPDATA",
        "go-build",
        "",
        false,
        false,
        WC_RISK_MODERATE,
    },
};



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
