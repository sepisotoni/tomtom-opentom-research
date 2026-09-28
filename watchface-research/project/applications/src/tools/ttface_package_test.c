#include <stdio.h>

#include "ttface_package.h"

int
main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s package.ttface staging-directory\n",
                argv[0]);
        return 2;
    }
    return ttface_unpack_archive(argv[1], argv[2]) ? 0 : 1;
}
