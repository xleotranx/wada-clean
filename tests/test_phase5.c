#include <stdio.h>
#include <string.h>

#include "wc_exclusions.h"
#include "wc_safety.h"
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

static void test_posix_safety(void) {
    char out[WC_PATH_CAP];

    CHECK(wc_safety_normalize_path("/Users/alice//Library/Caches/", out, sizeof out));
    CHECK(strcmp(out, "/Users/alice/Library/Caches") == 0);

    CHECK(wc_safety_normalize_path("/", out, sizeof out));
    CHECK(strcmp(out, "/") == 0);

    CHECK(!wc_safety_normalize_path("/Users/../etc", out, sizeof out));
    CHECK(!wc_safety_normalize_path("//server/share/x", out, sizeof out));

    CHECK(!wc_safety_is_valid_target_root("/"));
    CHECK(!wc_safety_is_valid_target_root("/System"));
    CHECK(!wc_safety_is_valid_target_root("/System/Library"));
    CHECK(!wc_safety_is_valid_target_root("/Library"));
    CHECK(!wc_safety_is_valid_target_root("/Applications"));
    CHECK(!wc_safety_is_valid_target_root("/Applications/Safari.app"));
    CHECK(!wc_safety_is_valid_target_root("/Users"));
    CHECK(!wc_safety_is_valid_target_root("/Users/alice"));
    CHECK(!wc_safety_is_valid_target_root("/usr"));
    CHECK(!wc_safety_is_valid_target_root("/usr/bin"));
    CHECK(!wc_safety_is_valid_target_root("/bin/sh"));
    CHECK(!wc_safety_is_valid_target_root("/etc"));
    CHECK(!wc_safety_is_valid_target_root("/etc/hosts"));
    CHECK(!wc_safety_is_valid_target_root("/private/etc/ssl"));
    CHECK(!wc_safety_is_valid_target_root("/dev/null"));

    CHECK(wc_safety_is_valid_target_root("/Users/alice/Library/Caches"));
    CHECK(wc_safety_is_valid_target_root("/Library/Caches"));
    CHECK(wc_safety_is_valid_target_root("/tmp/wada_clean_test"));

    CHECK(wc_safety_is_thumbcache_file("thumbcache_256.db"));
    CHECK(!wc_safety_is_thumbcache_file("sub/thumbcache_256.db"));
    CHECK(!wc_safety_is_thumbcache_file("other.db"));

    const char *root = "/Users/alice/Library/Caches";
    CHECK(wc_safety_path_within(root, "/Users/alice/Library/Caches/com.apple.Safari"));
    CHECK(wc_safety_path_within(root, "/users/alice/library/caches/a/b.cache"));
    CHECK(!wc_safety_path_within(root, root));
    CHECK(!wc_safety_path_within(root, "/Users/alice/Library/Caches2/foo"));
}

static void test_posix_exclusions(void) {
    const char *patterns[2] = {
        "/Users/alice/Library/Caches/com.important.app",
        "/tmp/keep.log",
    };
    CHECK(wc_exclusion_matches("/Users/alice/Library/Caches/com.important.app", patterns, 2));
    CHECK(wc_exclusion_matches("/users/alice/library/caches/com.important.app/data.db", patterns,
                               2));
    CHECK(wc_exclusion_matches("/tmp/keep.log", patterns, 2));
    CHECK(!wc_exclusion_matches("/Users/alice/Library/Caches/com.important.app2/x", patterns, 2));
    CHECK(!wc_exclusion_matches("/tmp/other.log", patterns, 2));

    const size_t lens[2] = {strlen(patterns[0]), strlen(patterns[1])};
    CHECK(wc_exclusion_matches_norm("/Users/alice/Library/Caches/com.important.app", patterns,
                                    lens, 2));
    CHECK(wc_exclusion_matches_norm("/users/alice/library/caches/com.important.app/data.db",
                                    patterns, lens, 2));
    CHECK(!wc_exclusion_matches_norm("/Users/alice/Library/Caches/com.important.app2/x", patterns,
                                     lens, 2));
}

static void test_brew_outdated_parser(void) {
    const char *sample =
        "git (2.45.0) < 2.47.1\n"
        "visual-studio-code (1.94.2) != 1.95.0\n"
        "\n"
        "cmake (3.29.0) < 3.30.2 [pinned at 3.29]\n";

    wc_update_item items[8];
    int n = wc_updater_parse_brew_outdated(sample, items, 8);
    CHECK(n == 3);

    CHECK(strcmp(items[0].id, "git") == 0);
    CHECK(strcmp(items[0].current_version, "2.45.0") == 0);
    CHECK(strcmp(items[0].available_version, "2.47.1") == 0);
    CHECK(strcmp(items[0].source, "brew") == 0);

    CHECK(strcmp(items[1].id, "visual-studio-code") == 0);
    CHECK(strcmp(items[1].current_version, "1.94.2") == 0);
    CHECK(strcmp(items[1].available_version, "1.95.0") == 0);
    CHECK(strcmp(items[1].source, "brew") == 0);

    CHECK(strcmp(items[2].id, "cmake") == 0);
    CHECK(strcmp(items[2].current_version, "3.29.0") == 0);
    CHECK(strcmp(items[2].available_version, "3.30.2") == 0);
}

int main(void) {
    test_posix_safety();
    test_posix_exclusions();
    test_brew_outdated_parser();

    if (g_failures > 0) {
        fprintf(stderr, "phase5 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("phase5 tests: %d checks passed\n", g_checks);
    return 0;
}
