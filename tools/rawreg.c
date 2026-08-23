/* rawreg.c — read chip registers via /dev/crystalhd BCM_IOC_REG_RD (no lib open).
 * usage: rawreg <addr_hex> [<addr_hex> ...]
 * build: gcc -O2 -I/usr/include -I/usr/include/link -o rawreg rawreg.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef struct {
    uint32_t bFormatChange, resolution, channelId, ppbPtr;
    int32_t  ptsStcOffset;
    uint32_t zeroPanscanValid, dramOutBufAddr, yComponent;
} C011_PIB_stub;
#define C011_PIB C011_PIB_stub

#include <bc_dts_types.h>
#include <bc_dts_defs.h>
#include <bc_dts_glob_lnx.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <addr>...\n", argv[0]); return 1; }
    int fd = open("/dev/crystalhd", O_RDWR);
    if (fd < 0) { perror("/dev/crystalhd"); return 2; }

    for (int i = 1; i < argc; i++) {
        BC_IOCTL_DATA io;
        memset(&io, 0, sizeof(io));
        io.u.regAcc.Offset = strtoul(argv[i], NULL, 16);
        if (ioctl(fd, BCM_IOC_REG_RD, &io) < 0) { perror("ioctl(REG_RD)"); return 3; }
        printf("%08x: %08x\n", io.u.regAcc.Offset, io.u.regAcc.Value);
    }
    close(fd);
    return 0;
}
