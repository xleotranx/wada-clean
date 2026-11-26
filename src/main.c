#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wada_boot.h"
#include "wada_store_sqlite.h"
#include "wc_product.h"

int main(int argc, char **argv) {
    char **filtered = (char **)calloc((size_t)(argc + 1), sizeof *filtered);
    int out_argc = 0;

    if (filtered && argc > 0) {
        filtered[out_argc++] = argv[0];
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") == 0) {
                wc_cleaner_set_initial_dry_run(true);
            } else {
                filtered[out_argc++] = argv[i];
            }
        }
    }

    const wada_product *products[] = { &wc_cleaner_product };
    wada_boot_desc desc = {
        "Wada Clean",
        1120,
        740,
        false,
        wada_store_sqlite(),
        products,
        1,
        "cleaner",
        "Wada Clean",
        NULL,
    };
    int rc = wada_boot_run(&desc, filtered ? out_argc : argc, filtered ? filtered : argv);
    free(filtered);

    if (wc_cleaner_take_relaunch_admin()) {
        wc_plat_relaunch_as_admin();
    }
    return rc;
}
