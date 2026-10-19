/* Copyright (c) 2026 j0shua-SYSON. MIT licensed.
 * Test entry point. Production authentication is outside this component. */
#include "../../../tools/guest_hle_files.h"
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc!=3 || getuid()!=0) return 2;
    long maximum=sysconf(_SC_OPEN_MAX);
    for (int fd=3;fd<maximum;fd++) close(fd);
    return s5lbox_hle_files(0,argv[1],argv[2],65534,65534);
}
