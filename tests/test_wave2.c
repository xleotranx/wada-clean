#include <stdio.h>
#include <string.h>

#include "wc_deep.h"

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

static void test_oem_inf_validator(void) {
    CHECK(wc_deep_is_safe_oem_inf("oem1.inf"));
    CHECK(wc_deep_is_safe_oem_inf("OEM242.INF"));
    CHECK(!wc_deep_is_safe_oem_inf("oem.inf"));
    CHECK(!wc_deep_is_safe_oem_inf("machine.inf"));
    CHECK(!wc_deep_is_safe_oem_inf("oem12.inf & calc"));
    CHECK(!wc_deep_is_safe_oem_inf("..\\oem12.inf"));
}

static void test_dism_parser(void) {
    const char *sample =
        "Deployment Image Servicing and Management tool\r\n"
        "Version: 10.0.22621.1\r\n"
        "\r\n"
        "Component Store (WinSxS) information:\r\n"
        "\r\n"
        "Windows Explorer Reported Size of Component Store : 12.40 GB\r\n"
        "\r\n"
        "Actual Size of Component Store : 8.15 GB\r\n"
        "\r\n"
        "    Shared with Windows : 5.20 GB\r\n"
        "    Backups and Disabled Features : 2.35 GB\r\n"
        "    Cache and Temporary Data : 614.20 MB\r\n"
        "\r\n"
        "Date of Last Cleanup : 2026-09-12 04:10:00\r\n"
        "Number of Reclaimable Packages : 7\r\n"
        "Component Store Cleanup Recommended : Yes\r\n";

    wc_winsxs_info info;
    CHECK(wc_deep_parse_dism_analyze(sample, &info));
    CHECK(info.analyzed);
    CHECK(info.cleanup_recommended);
    CHECK(info.reclaimable_packages == 7);
    CHECK(strcmp(info.actual_size_str, "8.15 GB") == 0);
    CHECK(strcmp(info.backups_size_str, "2.35 GB") == 0);
}

static void test_pnputil_parser(void) {
    const char *drivers =
        "Microsoft PnP Utility\r\n\r\n"
        "Published Name:     oem242.inf\r\n"
        "Original Name:      acpivpc.inf\r\n"
        "Provider Name:      Lenovo\r\n"
        "Class Name:         System\r\n"
        "Driver Version:     05/19/2026 15.11.30.14\r\n\r\n"
        "Published Name:     oem26.inf\r\n"
        "Original Name:      acpivpc.inf\r\n"
        "Provider Name:      Lenovo\r\n"
        "Class Name:         System\r\n"
        "Driver Version:     05/12/2025 15.11.30.10\r\n\r\n"
        "Published Name:     oem99.inf\r\n"
        "Original Name:      lonely_printer.inf\r\n"
        "Provider Name:      AcmePrint\r\n"
        "Class Name:         Printer\r\n"
        "Driver Version:     01/02/2024 1.0.0.1\r\n";

    const char *devices =
        "Instance ID:    ACPI\\VPC2004\\0\r\n"
        "Driver Name:    oem242.inf\r\n"
        "Instance ID:    ACPI\\PNP0C02\\1\r\n"
        "Driver Name:    machine.inf\r\n";

    wc_driver_item items[8];
    int n = wc_deep_parse_pnputil_drivers(drivers, devices, items, 8);
    CHECK(n == 3);

    CHECK(strcmp(items[0].published_name, "oem242.inf") == 0);
    CHECK(strcmp(items[0].date, "05/19/2026") == 0);
    CHECK(strcmp(items[0].version, "15.11.30.14") == 0);
    CHECK(items[0].in_use);
    CHECK(!items[0].unused_superseded);

    CHECK(strcmp(items[1].published_name, "oem26.inf") == 0);
    CHECK(!items[1].in_use);
    CHECK(items[1].unused_superseded);

    CHECK(strcmp(items[2].published_name, "oem99.inf") == 0);
    CHECK(!items[2].in_use);
    CHECK(!items[2].unused_superseded);
}

static void test_storage_parsers(void) {
    bool enabled = false;
    CHECK(wc_deep_parse_compact_query("The system is in the Compact state.", &enabled));
    CHECK(enabled);
    CHECK(wc_deep_parse_compact_query(
        "The system is not in the Compact state because Windows has determined...", &enabled));
    CHECK(!enabled);

    const char *vss_out =
        "Shadow Copy Storage association\r\n"
        "   For volume: (C:)\\\\?\\Volume{abc}\\\r\n"
        "   Used Shadow Copy Storage space: 4.50 GB (2%)\r\n"
        "   Allocated Shadow Copy Storage space: 5.00 GB (2%)\r\n"
        "   Maximum Shadow Copy Storage space: 10.0 GB (5%)\r\n";
    wc_storage_advisor adv;
    memset(&adv, 0, sizeof adv);
    CHECK(wc_deep_parse_vss_storage(vss_out, &adv));
    CHECK(adv.vss_known);
    CHECK(strcmp(adv.vss_used_str, "4.50 GB (2%)") == 0);
    CHECK(strcmp(adv.vss_alloc_str, "5.00 GB (2%)") == 0);
    CHECK(strcmp(adv.vss_max_str, "10.0 GB (5%)") == 0);
}

static void test_orphan_msi_filter(void) {
    const char *all_files[4] = {
        "C:\\Windows\\Installer\\1111.msi",
        "C:\\Windows\\Installer\\2222.msp",
        "C:\\Windows\\Installer\\3333.msi",
        "C:\\Windows\\Installer\\4444.msp",
    };
    const char *refs[2] = {
        "C:\\WINDOWS\\Installer\\1111.msi",
        "3333.msi",
    };
    int orphans[4] = {-1, -1, -1, -1};
    int n = wc_deep_filter_orphan_msi(all_files, 4, refs, 2, orphans, 4);
    CHECK(n == 2);
    CHECK(orphans[0] == 1);
    CHECK(orphans[1] == 3);
}

int main(void) {
    test_oem_inf_validator();
    test_dism_parser();
    test_pnputil_parser();
    test_storage_parsers();
    test_orphan_msi_filter();

    if (g_failures > 0) {
        fprintf(stderr, "wave2 tests: %d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("wave2 tests: %d checks passed\n", g_checks);
    return 0;
}
