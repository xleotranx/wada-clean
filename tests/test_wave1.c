#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc_platform.h"
#include "wc_safety.h"
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
    int n = snprintf(out, cap, "%s\\%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

static bool make_dir_utf8(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    return CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool write_bytes_age(const char *path, size_t nbytes, int age_days) {
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
        memset(buf, 'W', nbytes);
        DWORD wr = 0;
        ok = WriteFile(h, buf, (DWORD)nbytes, &wr, NULL) && wr == (DWORD)nbytes;
        free(buf);
    }
    if (ok && age_days > 0) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
        uint64_t sub = (uint64_t)age_days * 24ULL * 3600ULL * 10000000ULL;
        t = (t > sub) ? (t - sub) : 0;
        ft.dwLowDateTime = (DWORD)(t & 0xffffffffULL);
        ft.dwHighDateTime = (DWORD)(t >> 32);
        SetFileTime(h, &ft, &ft, &ft);
    }
    CloseHandle(h);
    return ok;
}

static bool file_exists(const char *path) {
    wchar_t w[WC_PATH_CAP];
    if (!to_wide(path, w, WC_PATH_CAP)) return false;
    DWORD a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void delete_tree(const char *dir) {
    wchar_t wpat[WC_PATH_CAP];
    char pat[WC_PATH_CAP];
    if (!join_path(pat, sizeof pat, dir, "*") || !to_wide(pat, wpat, WC_PATH_CAP)) return;
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;
            char name[WC_PATH_CAP], child[WC_PATH_CAP];
            WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, name, sizeof name, NULL, NULL);
            if (!join_path(child, sizeof child, dir, name)) continue;
            wchar_t wc[WC_PATH_CAP];
            if (!to_wide(child, wc, WC_PATH_CAP)) continue;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_tree(child);
                RemoveDirectoryW(wc);
            } else {
                SetFileAttributesW(wc, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(wc);
            }
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    wchar_t wd[WC_PATH_CAP];
    if (to_wide(dir, wd, WC_PATH_CAP)) RemoveDirectoryW(wd);
}

static void test_wave1_safety(void) {
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows\\Installer"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows\\Installer\\abc.msi"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows\\WinSxS"));
    CHECK(!wc_safety_is_valid_target_root("C:\\Windows\\System32\\DriverStore"));

}

static void test_chromium_profiles(const char *root) {
    char ud[WC_PATH_CAP], def_p[WC_PATH_CAP], p1[WC_PATH_CAP], other[WC_PATH_CAP];
    char def_c[WC_PATH_CAP], def_cd[WC_PATH_CAP], p1_cc[WC_PATH_CAP], p1_js[WC_PATH_CAP];
    char f1[WC_PATH_CAP], f2[WC_PATH_CAP], fkeep1[WC_PATH_CAP], fkeep2[WC_PATH_CAP];

    CHECK(join_path(ud, sizeof ud, root, "UserData"));
    CHECK(join_path(def_p, sizeof def_p, ud, "Default"));
    CHECK(join_path(p1, sizeof p1, ud, "Profile 1"));
    CHECK(join_path(other, sizeof other, ud, "Crashpad"));
    CHECK(join_path(def_c, sizeof def_c, def_p, "Cache"));
    CHECK(join_path(def_cd, sizeof def_cd, def_c, "Cache_Data"));
    CHECK(join_path(p1_cc, sizeof p1_cc, p1, "Code Cache"));
    CHECK(join_path(p1_js, sizeof p1_js, p1_cc, "js"));
    CHECK(join_path(f1, sizeof f1, def_cd, "f_001"));
    CHECK(join_path(f2, sizeof f2, p1_js, "script.bin"));
    CHECK(join_path(fkeep1, sizeof fkeep1, def_p, "Bookmarks"));
    CHECK(join_path(fkeep2, sizeof fkeep2, other, "metadata"));

    CHECK(make_dir_utf8(ud) && make_dir_utf8(def_p) && make_dir_utf8(p1) &&
          make_dir_utf8(other) && make_dir_utf8(def_c) && make_dir_utf8(def_cd) &&
          make_dir_utf8(p1_cc) && make_dir_utf8(p1_js));
    CHECK(write_bytes_age(f1, 100, 0));
    CHECK(write_bytes_age(f2, 250, 0));
    CHECK(write_bytes_age(fkeep1, 80, 0));
    CHECK(write_bytes_age(fkeep2, 90, 0));

    volatile int cancel = 0;
    uint64_t files = 0, bytes = 0, skipped = 0;
    CHECK(wc_plat_scan_target(WC_TARGET_CHROMIUM_PROFILES, ud, &cancel, NULL, NULL, &files, &bytes));
    CHECK(files == 2);
    CHECK(bytes == 350);

    CHECK(wc_plat_clean_target(WC_TARGET_CHROMIUM_PROFILES, ud, false, &cancel, NULL, NULL, &files,
                               &bytes, &skipped));
    CHECK(files == 2 && bytes == 350 && skipped == 0);
    CHECK(!file_exists(f1));
    CHECK(!file_exists(f2));
    CHECK(file_exists(fkeep1));
    CHECK(file_exists(fkeep2));
}

int main(void) {
    char temp_base[WC_PATH_CAP];
    CHECK(wc_plat_get_env("TEMP", temp_base, sizeof temp_base));

    char leaf[64], root[WC_PATH_CAP];
    snprintf(leaf, sizeof leaf, "wada_wave1_test_%lu", (unsigned long)GetCurrentProcessId());
    CHECK(join_path(root, sizeof root, temp_base, leaf));
    delete_tree(root);
    CHECK(make_dir_utf8(root));

    test_wave1_safety();
    test_chromium_profiles(root);

    delete_tree(root);

    if (g_failures > 0) {
        fprintf(stderr, "wave1 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("wave1 tests: %d checks passed\n", g_checks);
    return 0;
}
