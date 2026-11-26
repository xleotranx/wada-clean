#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc_disk.h"
#include "wc_platform.h"
#include "wc_types.h"

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

static bool write_bytes_utf8(const char *path, size_t nbytes) {
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
        memset(buf, 'B', nbytes);
        DWORD written = 0;
        ok = WriteFile(h, buf, (DWORD)nbytes, &written, NULL) && written == (DWORD)nbytes;
        free(buf);
    }
    CloseHandle(h);
    return ok;
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

static void test_apps_list_readonly(void) {
    wc_app_item *items = (wc_app_item *)calloc(WC_MAX_APPS, sizeof *items);
    CHECK(items != NULL);
    int count = -1;
    CHECK(wc_plat_list_apps(items, WC_MAX_APPS, &count));
    CHECK(count >= 0 && count <= WC_MAX_APPS);
    for (int i = 0; i < count; i++) {
        CHECK(items[i].name[0] != '\0');
        CHECK(items[i].uninstall_cmd[0] != '\0');
        if (i > 0) {
            CHECK(ascii_strcasecmp(items[i - 1].name, items[i].name) <= 0);
        }
    }
    free(items);
}

static void test_disk_top_insert(void) {
    wc_disk_entry arr[3];
    int count = 0;
    const uint64_t sizes[5] = {100, 500, 200, 50, 350};
    for (int i = 0; i < 5; i++) {
        wc_disk_entry e;
        memset(&e, 0, sizeof e);
        snprintf(e.name, sizeof e.name, "item_%d", i);
        e.bytes = sizes[i];
        wc_disk_insert_top(arr, &count, 3, &e);
    }
    CHECK(count == 3);
    CHECK(arr[0].bytes == 500);
    CHECK(arr[1].bytes == 350);
    CHECK(arr[2].bytes == 200);
}

static void test_disk_analyze_fixture(const char *root) {
    char scan_root[WC_PATH_CAP], dir_big[WC_PATH_CAP], dir_small[WC_PATH_CAP];
    char outside[WC_PATH_CAP], link_dir[WC_PATH_CAP];
    char f_big[WC_PATH_CAP], f_small[WC_PATH_CAP], f_root[WC_PATH_CAP], f_outside[WC_PATH_CAP];

    CHECK(join_path(scan_root, sizeof scan_root, root, "scan_root"));
    CHECK(join_path(dir_big, sizeof dir_big, scan_root, "dir_big"));
    CHECK(join_path(dir_small, sizeof dir_small, scan_root, "dir_small"));
    CHECK(join_path(outside, sizeof outside, root, "outside_dir"));
    CHECK(join_path(link_dir, sizeof link_dir, scan_root, "junc_outside"));

    CHECK(join_path(f_big, sizeof f_big, dir_big, "big.bin"));
    CHECK(join_path(f_small, sizeof f_small, dir_small, "small.bin"));
    CHECK(join_path(f_root, sizeof f_root, scan_root, "root.bin"));
    CHECK(join_path(f_outside, sizeof f_outside, outside, "huge_sentinel.bin"));

    CHECK(make_dir_utf8(scan_root));
    CHECK(make_dir_utf8(dir_big));
    CHECK(make_dir_utf8(dir_small));
    CHECK(make_dir_utf8(outside));
    CHECK(write_bytes_utf8(f_big, 500));
    CHECK(write_bytes_utf8(f_small, 50));
    CHECK(write_bytes_utf8(f_root, 200));
    CHECK(write_bytes_utf8(f_outside, 9999));
    CHECK(create_junction(link_dir, outside));

    wc_disk_worker w;
    CHECK(wc_disk_worker_init(&w));
    CHECK(wc_disk_worker_start(&w, scan_root));

    wc_disk_snapshot *snap = (wc_disk_snapshot *)calloc(1, sizeof *snap);
    CHECK(snap != NULL);
    for (int i = 0; i < 500; i++) {
        if (!wc_disk_worker_poll(&w, snap)) break;
        Sleep(10);
    }

    CHECK(snap->state == WC_JOB_DONE);
    CHECK(snap->total_bytes == 750);
    CHECK(snap->total_files == 3);
    CHECK(snap->total_dirs == 2);
    CHECK(snap->top_dir_count == 2);
    CHECK(strcmp(snap->top_dirs[0].name, "dir_big") == 0);
    CHECK(snap->top_dirs[0].bytes == 500);
    CHECK(strcmp(snap->top_dirs[1].name, "dir_small") == 0);
    CHECK(snap->top_dirs[1].bytes == 50);

    CHECK(snap->top_file_count == 3);
    CHECK(strcmp(snap->top_files[0].name, "big.bin") == 0);
    CHECK(snap->top_files[0].bytes == 500);
    CHECK(strcmp(snap->top_files[1].name, "root.bin") == 0);
    CHECK(snap->top_files[1].bytes == 200);
    CHECK(strcmp(snap->top_files[2].name, "small.bin") == 0);
    CHECK(snap->top_files[2].bytes == 50);

    free(snap);
    wc_disk_worker_destroy(&w);
}

int main(void) {
    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64];
    snprintf(leaf, sizeof leaf, "wada_p2_test_%lu", (unsigned long)GetCurrentProcessId());
    char test_root[WC_PATH_CAP];
    CHECK(join_path(test_root, sizeof test_root, temp_base, leaf));
    delete_tree_for_cleanup(test_root);
    CHECK(make_dir_utf8(test_root));

    test_apps_list_readonly();
    test_disk_top_insert();
    test_disk_analyze_fixture(test_root);

    delete_tree_for_cleanup(test_root);

    if (g_failures > 0) {
        fprintf(stderr, "phase2 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("phase2 tests: %d checks passed\n", g_checks);
    return 0;
}
