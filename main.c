#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "LibFS.h"

void
usage(char *prog) {
    fprintf(stderr, "usage: %s <disk image file>\n", prog);
    exit(1);
}

int
main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
    }
    char *path = argv[1];

    FS_Boot(path);
    FS_Sync();
    test_all();
    return 0;
}

