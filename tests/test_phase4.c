#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_store_sqlite.h"
#include "wc_cookies.h"
#include "wc_platform.h"
#include "wc_types.h"
#include "wc_updater.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        g_checks++;                                                                             \
        if (!(cond)) {                                                                          \
            g_failures++;                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                       \
    } while (0)

static bool to_wide(const char *s, wchar_t *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap) > 0;
}

static bool join_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + 1 + nlen + 1 > cap) return false;
    memcpy(out, dir, dlen);
    out[dlen] = '\\';
    memcpy(out + dlen + 1, name, nlen + 1);
    return true;
}

static bool make_dir_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    return CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static void delete_tree_for_cleanup(const char *dir) {
    wchar_t wpat[WC_PATH_CAP];
    char pat[WC_PATH_CAP];
    if (!join_path(pat, sizeof pat, dir, "*")) return;
    if (!to_wide(pat, wpat, WC_PATH_CAP)) return;

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            char name[WC_PATH_CAP];
            WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, name, sizeof name, NULL, NULL);
            char child[WC_PATH_CAP];
            if (!join_path(child, sizeof child, dir, name)) continue;
            wchar_t wchild[WC_PATH_CAP];
            if (!to_wide(child, wchild, WC_PATH_CAP)) continue;

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                RemoveDirectoryW(wchild);
            } else if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree_for_cleanup(child);
                RemoveDirectoryW(wchild);
            } else {
                SetFileAttributesW(wchild, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(wchild);
            }
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    wchar_t wdir[WC_PATH_CAP];
    if (to_wide(dir, wdir, WC_PATH_CAP)) {
        RemoveDirectoryW(wdir);
    }
}

static void test_winget_parser(void) {
    const char *sample =
        "   - \r   \\ \r   | \r   / \rName                               Id                           Version       Available     Source\r\n"
        "------------------------------------------------------------------------------------------------------\r\n"
        "Git                                Git.Git                      2.45.0        2.47.1        winget\r\n"
        "Visual Studio Code (User)          Microsoft.VisualStudioCode   < 1.94.2      1.95.0        winget\r\n"
        "7-Zip 24.08 (x64)                  7zip.7zip                    24.08         24.09         winget\r\n"
        "\r\n"
        "3 upgrades available.\r\n";

    wc_update_item items[8];
    int n = wc_updater_parse_winget_table(sample, items, 8);
    CHECK(n == 3);

    CHECK(strcmp(items[0].name, "Git") == 0);
    CHECK(strcmp(items[0].id, "Git.Git") == 0);
    CHECK(strcmp(items[0].current_version, "2.45.0") == 0);
    CHECK(strcmp(items[0].available_version, "2.47.1") == 0);
    CHECK(strcmp(items[0].source, "winget") == 0);

    CHECK(strcmp(items[1].name, "Visual Studio Code (User)") == 0);
    CHECK(strcmp(items[1].id, "Microsoft.VisualStudioCode") == 0);
    CHECK(strcmp(items[1].current_version, "< 1.94.2") == 0);
    CHECK(strcmp(items[1].available_version, "1.95.0") == 0);
    CHECK(strcmp(items[1].source, "winget") == 0);

    CHECK(strcmp(items[2].name, "7-Zip 24.08 (x64)") == 0);
    CHECK(strcmp(items[2].id, "7zip.7zip") == 0);

    CHECK(wc_updater_is_safe_id("Microsoft.VisualStudioCode"));
    CHECK(wc_updater_is_safe_id("7zip.7zip"));
    CHECK(!wc_updater_is_safe_id(""));
    CHECK(!wc_updater_is_safe_id("bad id with spaces"));
    CHECK(!wc_updater_is_safe_id("bad\"&calc"));
}

static void test_cookie_domain_matcher(void) {
    char dom[192];
    CHECK(wc_cookie_normalize_domain("https://Accounts.Google.COM/signin", dom, sizeof dom));
    CHECK(strcmp(dom, "accounts.google.com") == 0);

    CHECK(wc_cookie_normalize_domain(".github.com:443", dom, sizeof dom));
    CHECK(strcmp(dom, "github.com") == 0);

    CHECK(!wc_cookie_normalize_domain("", dom, sizeof dom));
    CHECK(!wc_cookie_normalize_domain("bad domain!", dom, sizeof dom));

    const char *allow[2] = {"google.com", "github.com"};
    CHECK(wc_cookie_domain_allowed("google.com", allow, 2));
    CHECK(wc_cookie_domain_allowed(".accounts.google.com", allow, 2));
    CHECK(wc_cookie_domain_allowed("api.GITHUB.com", allow, 2));
    CHECK(!wc_cookie_domain_allowed("notgoogle.com", allow, 2));
    CHECK(!wc_cookie_domain_allowed("tracker.adnetwork.example", allow, 2));
}

static void test_cookie_allowlist_store(const char *root) {
    char db_path[WC_PATH_CAP];
    CHECK(join_path(db_path, sizeof db_path, root, "cookie_allow_test.db"));

    wc_cookies c;
    wada_err err;
    wada_err_clear(&err);
    CHECK(wc_cookies_open(&c, wada_store_sqlite(), db_path, &err) == WADA_OK);

    CHECK(wc_cookies_add(&c, "https://Google.com/home", &err) == WADA_OK);
    CHECK(wc_cookies_add(&c, ".github.com", &err) == WADA_OK);
    CHECK(wc_cookies_add(&c, "google.com", &err) == WADA_ERR_EXISTS);
    CHECK(wc_cookies_add(&c, "bad domain!", &err) == WADA_ERR_INVALID);

    wc_cookie_allow_item items[8];
    int count = 0;
    CHECK(wc_cookies_list(&c, items, 8, &count, &err) == WADA_OK);
    CHECK(count == 2);
    CHECK(strcmp(items[0].domain, "google.com") == 0);
    CHECK(strcmp(items[1].domain, "github.com") == 0);

    CHECK(wc_cookies_remove(&c, items[0].id, &err) == WADA_OK);
    CHECK(wc_cookies_list(&c, items, 8, &count, &err) == WADA_OK);
    CHECK(count == 1);
    CHECK(strcmp(items[0].domain, "github.com") == 0);

    wc_cookies_close(&c);
}

static int count_rows_in_db(const char *db_path, const char *table) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) return -1;
    char sql[128];
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM %s", table);
    sqlite3_stmt *stmt = NULL;
    int count = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return count;
}

static void test_cookie_db_pruning(const char *root) {
    const char *allow[1] = {"google.com"};

    /* 1. Chromium cookie DB */
    char chrome_db[WC_PATH_CAP];
    CHECK(join_path(chrome_db, sizeof chrome_db, root, "chromium_cookies.db"));
    {
        sqlite3 *db = NULL;
        CHECK(sqlite3_open(chrome_db, &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db,
                           "CREATE TABLE cookies (host_key TEXT NOT NULL, name TEXT NOT NULL);"
                           "INSERT INTO cookies VALUES ('.google.com', 'SID');"
                           "INSERT INTO cookies VALUES ('accounts.google.com', 'HSID');"
                           "INSERT INTO cookies VALUES ('.notgoogle.com', 'EVIL');"
                           "INSERT INTO cookies VALUES ('ads.tracker.net', 'ID');",
                           NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }

    uint64_t rem = 0, kept = 0;
    CHECK(wc_plat_prune_cookie_db(chrome_db, false, allow, 1, true, &rem, &kept));
    CHECK(rem == 2);
    CHECK(kept == 2);
    CHECK(count_rows_in_db(chrome_db, "cookies") == 4);

    CHECK(wc_plat_prune_cookie_db(chrome_db, false, allow, 1, false, &rem, &kept));
    CHECK(rem == 2);
    CHECK(kept == 2);
    CHECK(count_rows_in_db(chrome_db, "cookies") == 2);

    /* 2. Firefox cookie DB */
    char ff_db[WC_PATH_CAP];
    CHECK(join_path(ff_db, sizeof ff_db, root, "firefox_cookies.sqlite"));
    {
        sqlite3 *db = NULL;
        CHECK(sqlite3_open(ff_db, &db) == SQLITE_OK);
        CHECK(sqlite3_exec(db,
                           "CREATE TABLE moz_cookies (host TEXT NOT NULL, name TEXT NOT NULL);"
                           "INSERT INTO moz_cookies VALUES ('.mail.google.com', 'SESSION');"
                           "INSERT INTO moz_cookies VALUES ('tracker.example', 'TRK');",
                           NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }

    CHECK(wc_plat_prune_cookie_db(ff_db, true, allow, 1, false, &rem, &kept));
    CHECK(rem == 1);
    CHECK(kept == 1);
    CHECK(count_rows_in_db(ff_db, "moz_cookies") == 1);
}

int main(void) {
    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64];
    snprintf(leaf, sizeof leaf, "wada_p4_test_%lu", (unsigned long)GetCurrentProcessId());
    char test_root[WC_PATH_CAP];
    CHECK(join_path(test_root, sizeof test_root, temp_base, leaf));
    delete_tree_for_cleanup(test_root);
    CHECK(make_dir_utf8(test_root));

    test_winget_parser();
    test_cookie_domain_matcher();
    test_cookie_allowlist_store(test_root);
    test_cookie_db_pruning(test_root);

    delete_tree_for_cleanup(test_root);

    if (g_failures > 0) {
        fprintf(stderr, "phase4 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("phase4 tests: %d checks passed\n", g_checks);
    return 0;
}
