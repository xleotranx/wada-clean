#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_store_sqlite.h"
#include "wc_history.h"
#include "wc_platform.h"
#include "wc_safety.h"
#include "wc_types.h"
#include "wc_worker.h"

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

static bool make_dir_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    return CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_bytes_utf8(const char *path, size_t nbytes, DWORD extra_attrs) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    if (nbytes > 0) {
        char *buf = (char *)malloc(nbytes);
        if (!buf) {
            CloseHandle(h);
            return false;
        }
        memset(buf, 'A', nbytes);
        DWORD written = 0;
        ok = WriteFile(h, buf, (DWORD)nbytes, &written, NULL) && written == (DWORD)nbytes;
        free(buf);
    }
    CloseHandle(h);
    if (ok && extra_attrs != 0) {
        SetFileAttributesW(w, extra_attrs);
    }
    return ok;
}

static bool file_exists_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(w);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool dir_exists_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    DWORD attrs = GetFileAttributesW(w);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
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

static bool create_junction(const char *link_path, const char *target_path) {
    char cmd[WC_PATH_CAP * 3];
    snprintf(cmd, sizeof cmd, "cmd.exe /c mklink /J \"%s\" \"%s\" >NUL 2>&1", link_path, target_path);
    wchar_t wcmd[WC_PATH_CAP * 3];
    if (!to_wide(cmd, wcmd, (int)(sizeof wcmd / sizeof wcmd[0]))) return false;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

static void test_safety(void) {
    char out[WC_PATH_CAP];

    CHECK(wc_safety_normalize_path("c:/foo//bar/", out, sizeof out));
    CHECK(strcmp(out, "C:\\foo\\bar") == 0);

    CHECK(wc_safety_normalize_path("C:\\", out, sizeof out));
    CHECK(strcmp(out, "C:\\") == 0);

    CHECK(!wc_safety_normalize_path("C:\\foo\\..\\bar", out, sizeof out));
    CHECK(!wc_safety_normalize_path("\\\\server\\share\\x", out, sizeof out));
    CHECK(!wc_safety_normalize_path("", out, sizeof out));
    CHECK(!wc_safety_normalize_path("C:relative", out, sizeof out));

    /* Protected and drive roots must be rejected. */
    CHECK(!wc_safety_is_valid_target_root(NULL));
    CHECK(!wc_safety_is_valid_target_root(""));
    CHECK(!wc_safety_is_valid_target_root("C:\\"));
    CHECK(!wc_safety_is_valid_target_root("C:"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows"));
    CHECK(!wc_safety_is_valid_target_root("c:/windows/"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows\\System32"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Users"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Users\\alice"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Program Files"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Program Files (x86)"));
    CHECK(!wc_safety_is_valid_target_root("C:\\ProgramData"));

    /* Legitimate deeper roots must be accepted. */
    CHECK(wc_safety_is_valid_target_root("C:\\Windows\\Temp"));
    CHECK(wc_safety_is_valid_target_root("C:\\Users\\alice\\AppData\\Local\\Temp"));

    /* Path containment checks. */
    const char *root = "C:\\Users\\alice\\AppData\\Local\\Temp";
    CHECK(wc_safety_path_within(root, "c:\\users\\alice\\appdata\\local\\temp\\a.tmp"));
    CHECK(wc_safety_path_within(root, "C:\\Users\\alice\\AppData\\Local\\Temp\\sub\\b.tmp"));
    CHECK(!wc_safety_path_within(root, root));
    CHECK(!wc_safety_path_within(root, "C:\\Users\\alice\\AppData\\Local\\Temp2\\a.tmp"));
    CHECK(!wc_safety_path_within(root, "C:\\Users\\alice\\AppData\\Local\\Temp\\..\\a.tmp"));
}

static void test_format_bytes(void) {
    char buf[32];
    wc_format_bytes(0, buf, sizeof buf);
    CHECK(strcmp(buf, "0 B") == 0);
    wc_format_bytes(512, buf, sizeof buf);
    CHECK(strcmp(buf, "512 B") == 0);
    wc_format_bytes(1024, buf, sizeof buf);
    CHECK(strcmp(buf, "1.0 KB") == 0);
    wc_format_bytes(1536, buf, sizeof buf);
    CHECK(strcmp(buf, "1.5 KB") == 0);
    wc_format_bytes(1048576ULL, buf, sizeof buf);
    CHECK(strcmp(buf, "1.0 MB") == 0);
    wc_format_bytes(1073741824ULL, buf, sizeof buf);
    CHECK(strcmp(buf, "1.00 GB") == 0);
}

static void test_fs_scan_and_clean(const char *root) {
    char target[WC_PATH_CAP], sub[WC_PATH_CAP], outside[WC_PATH_CAP];
    char fa[WC_PATH_CAP], fb[WC_PATH_CAP], flocked[WC_PATH_CAP], fro[WC_PATH_CAP];
    char fsent[WC_PATH_CAP], link_path[WC_PATH_CAP];

    CHECK(join_path(target, sizeof target, root, "target"));
    CHECK(join_path(sub, sizeof sub, target, "sub"));
    CHECK(join_path(outside, sizeof outside, root, "outside"));
    CHECK(join_path(fa, sizeof fa, target, "a.bin"));
    CHECK(join_path(fb, sizeof fb, sub, "b.bin"));
    CHECK(join_path(flocked, sizeof flocked, target, "locked.bin"));
    CHECK(join_path(fro, sizeof fro, target, "readonly.bin"));
    CHECK(join_path(fsent, sizeof fsent, outside, "sentinel.txt"));
    CHECK(join_path(link_path, sizeof link_path, target, "link_out"));

    CHECK(make_dir_utf8(target));
    CHECK(make_dir_utf8(sub));
    CHECK(make_dir_utf8(outside));
    CHECK(write_bytes_utf8(fa, 100, 0));
    CHECK(write_bytes_utf8(fb, 250, 0));
    CHECK(write_bytes_utf8(flocked, 50, 0));
    CHECK(write_bytes_utf8(fro, 25, FILE_ATTRIBUTE_READONLY));
    CHECK(write_bytes_utf8(fsent, 12, 0));
    CHECK(create_junction(link_path, outside));

    volatile int cancel = 0;
    uint64_t files = 0, bytes = 0, skipped = 0;
    CHECK(wc_plat_scan_dir(target, &cancel, &files, &bytes));
    CHECK(files == 4);
    CHECK(bytes == 425);

    /* Dry-run must count accurately without deleting anything. */
    CHECK(wc_plat_clean_dir(target, true, &cancel, &files, &bytes, &skipped));
    CHECK(files == 4);
    CHECK(bytes == 425);
    CHECK(skipped == 0);
    CHECK(file_exists_utf8(fa));
    CHECK(file_exists_utf8(fb));
    CHECK(file_exists_utf8(flocked));
    CHECK(file_exists_utf8(fro));
    CHECK(file_exists_utf8(fsent));

    /* Lock locked.bin exclusively and run real clean. */
    wchar_t wlocked[WC_PATH_CAP];
    CHECK(to_wide(flocked, wlocked, WC_PATH_CAP));
    HANDLE lock_h = CreateFileW(wlocked, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(lock_h != INVALID_HANDLE_VALUE);

    CHECK(wc_plat_clean_dir(target, false, &cancel, &files, &bytes, &skipped));
    CHECK(files == 3);
    CHECK(bytes == 375);
    CHECK(skipped == 1);
    CHECK(!file_exists_utf8(fa));
    CHECK(!file_exists_utf8(fb));
    CHECK(!file_exists_utf8(fro));
    CHECK(file_exists_utf8(flocked));
    CHECK(dir_exists_utf8(target));
    CHECK(file_exists_utf8(fsent)); /* Sentinel behind junction MUST survive. */

    CloseHandle(lock_h);
}

static void test_history(const char *root) {
    char db_path[WC_PATH_CAP];
    CHECK(join_path(db_path, sizeof db_path, root, "history_test.db"));

    wc_history h;
    wada_err err;
    wada_err_clear(&err);
    CHECK(wc_history_open(&h, wada_store_sqlite(), db_path, &err) == WADA_OK);

    wc_history_entry e1;
    memset(&e1, 0, sizeof e1);
    e1.unix_sec = 1700000001LL;
    e1.dry_run = true;
    e1.files_cleaned = 10;
    e1.bytes_cleaned = 2048;
    e1.skipped_count = 0;
    snprintf(e1.summary, sizeof e1.summary, "run 1");
    CHECK(wc_history_append(&h, &e1, &err) == WADA_OK);

    wc_history_entry e2;
    memset(&e2, 0, sizeof e2);
    e2.unix_sec = 1700000002LL;
    e2.dry_run = false;
    e2.files_cleaned = 5;
    e2.bytes_cleaned = 4096;
    e2.skipped_count = 1;
    snprintf(e2.summary, sizeof e2.summary, "run 2");
    CHECK(wc_history_append(&h, &e2, &err) == WADA_OK);

    wc_history_entry listed[10];
    int count = 0;
    CHECK(wc_history_list(&h, listed, 10, &count, &err) == WADA_OK);
    CHECK(count == 2);
    CHECK(strcmp(listed[0].summary, "run 2") == 0);
    CHECK(!listed[0].dry_run);
    CHECK(listed[0].files_cleaned == 5);
    CHECK(listed[0].bytes_cleaned == 4096);
    CHECK(listed[0].skipped_count == 1);

    CHECK(strcmp(listed[1].summary, "run 1") == 0);
    CHECK(listed[1].dry_run);
    CHECK(listed[1].files_cleaned == 10);
    CHECK(listed[1].bytes_cleaned == 2048);

    wc_history_close(&h);
}

static void test_worker_async(const char *root) {
    char wdir[WC_PATH_CAP], f1[WC_PATH_CAP], f2[WC_PATH_CAP];
    CHECK(join_path(wdir, sizeof wdir, root, "worker_target"));
    CHECK(join_path(f1, sizeof f1, wdir, "one.tmp"));
    CHECK(join_path(f2, sizeof f2, wdir, "two.tmp"));
    CHECK(make_dir_utf8(wdir));
    CHECK(write_bytes_utf8(f1, 10, 0));
    CHECK(write_bytes_utf8(f2, 20, 0));

    wc_target_def def;
    memset(&def, 0, sizeof def);
    snprintf(def.id, sizeof def.id, "test.worker");
    snprintf(def.label, sizeof def.label, "Test Worker Target");
    def.category = WC_CAT_SYSTEM;
    def.kind = WC_TARGET_DIR_CONTENTS;
    snprintf(def.subpath, sizeof def.subpath, "%s", wdir);
    def.default_enabled = true;

    bool selected[1] = {true};
    wc_worker w;
    CHECK(wc_worker_init(&w));
    CHECK(wc_worker_start(&w, WC_JOB_ANALYZE, false, &def, selected, 1));

    wc_worker_snapshot snap;
    memset(&snap, 0, sizeof snap);
    for (int iter = 0; iter < 500; iter++) {
        if (!wc_worker_poll(&w, &snap)) break;
        Sleep(10);
    }

    CHECK(snap.state == WC_JOB_DONE);
    CHECK(snap.total_files_found == 2);
    CHECK(snap.total_bytes_found == 30);
    CHECK(snap.results[0].analyzed);
    CHECK(snap.results[0].files_found == 2);
    CHECK(snap.results[0].bytes_found == 30);

    wc_worker_destroy(&w);
}

int main(void) {
    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64];
    snprintf(leaf, sizeof leaf, "wada_clean_test_%lu", (unsigned long)GetCurrentProcessId());
    char test_root[WC_PATH_CAP];
    CHECK(join_path(test_root, sizeof test_root, temp_base, leaf));
    delete_tree_for_cleanup(test_root);
    CHECK(make_dir_utf8(test_root));

    test_safety();
    test_format_bytes();
    test_fs_scan_and_clean(test_root);
    test_history(test_root);
    test_worker_async(test_root);

    delete_tree_for_cleanup(test_root);

    if (g_failures > 0) {
        fprintf(stderr, "phase1 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("phase1 tests: %d checks passed\n", g_checks);
    return 0;
}
