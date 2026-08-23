/* rawdump.c — read BCM70015 DRAM via /dev/crystalhd BCM_IOC_MEM_RD
 * WITHOUT lib device open (no FW commands sent, no state pollution).
 * usage: rawdump <addr_hex> <nbytes> <outfile>
 * build:
 *   gcc -O2 -I/usr/include -I/usr/include/link -o rawdump rawdump.c
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
    if (argc < 4) {
        fprintf(stderr, "usage: %s <addr_hex> <nbytes> <outfile>\n", argv[0]);
        return 1;
    }
    uint32_t addr   = strtoul(argv[1], NULL, 16);
    uint32_t nbytes = strtoul(argv[2], NULL, 10);

    int fd = open("/dev/crystalhd", O_RDWR);
    if (fd < 0) { perror("/dev/crystalhd"); return 2; }

    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror("fopen"); return 3; }

    /* add_cdata lives right after the fixed-size message part */
    const uint32_t hdr_sz = sizeof(BC_IOCTL_MB);
    uint32_t chunk = 4096;
    uint8_t buf[8192];
    printf("sizeof(BC_IOCTL_MB)=%u\n", hdr_sz);

    for (uint32_t off = 0; off < nbytes; off += chunk) {
        uint32_t n = (nbytes - off) < chunk ? (nbytes - off) : chunk;
        n &= ~3u;
        if (!n) break;
        memset(buf, 0, hdr_sz);
        BC_IOCTL_DATA *p = (BC_IOCTL_DATA *)buf;
        p->u.devMem.StartOff  = addr + off;
        p->u.devMem.NumDwords = n / 4;
        if (ioctl(fd, BCM_IOC_MEM_RD, buf) < 0) {
            perror("ioctl(MEM_RD)");
            fclose(f); close(fd);
            return 4;
        }
        if (p->RetSts != BC_STS_SUCCESS) {
            fprintf(stderr, "MEM_RD @%#x sts=0x%x\n", addr + off, p->RetSts);
            fclose(f); close(fd);
            return 5;
        }
        fwrite(buf + hdr_sz, 1, n, f);
    }
    fclose(f); close(fd);
    printf("ok %u bytes @%#x -> %s\n", nbytes, addr, argv[3]);
    return 0;
}
