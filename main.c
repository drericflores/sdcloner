// main.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// forward decl from engine
int sdcloner_clone(const char* src_disk, const char* dest_disk, uint64_t dest_capacity_hint);

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,"Usage:\n"
                "  %s <SRC_DISK>                # save image locally (raw, compressed)\n"
                "  %s <SRC_DISK> <DEST_DISK>    # clone to destination\n"
                "  %s <SRC_DISK> --hint <GB>    # image sized for smaller future card\n", argv[0], argv[0], argv[0]);
        return 1;
    }
    const char* src = argv[1];
    const char* dest = NULL;
    uint64_t hint=0;

    if (argc >= 3 && strcmp(argv[2],"--hint")==0 && argc>=4) {
        hint = (uint64_t)atoll(argv[3]) * 1024ULL*1024ULL*1024ULL;
    } else if (argc >= 3) {
        dest = argv[2];
    }

    return sdcloner_clone(src, dest, hint);
}

