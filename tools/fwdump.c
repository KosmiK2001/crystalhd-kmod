/* fwdump.c — dump BCM70015 device DRAM via DtsDevMemRd
 * usage: fwdump <addr_hex> <nbytes_dec> <outfile>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <bc_dts_types.h>
#include <bc_dts_defs.h>

extern BC_STATUS DtsDeviceOpen(HANDLE *hDevice, uint32_t mode);
extern BC_STATUS DtsDeviceClose(HANDLE hDevice);
extern BC_STATUS DtsDevMemRd(HANDLE hDevice, uint32_t *Buffer, uint32_t BuffSz,
                             uint32_t Offset);

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <addr_hex> <nbytes> <outfile>\n", argv[0]);
        return 1;
    }
    uint32_t addr  = strtoul(argv[1], NULL, 16);
    uint32_t nbytes = strtoul(argv[2], NULL, 10);
    const char *outfn = argv[3];

    HANDLE h = NULL;
    BC_STATUS st = DtsDeviceOpen(&h, DTS_PLAYBACK_MODE);
    if (st != BC_STS_SUCCESS) {
        fprintf(stderr, "DtsDeviceOpen failed: 0x%x\n", st);
        return 2;
    }

    FILE *f = fopen(outfn, "wb");
    if (!f) { perror("fopen"); return 3; }

    /* chunked: driver limits ioctl size; use 4KB chunks */
    uint32_t chunk = 4096;
    uint8_t *buf = malloc(chunk);
    for (uint32_t off = 0; off < nbytes; off += chunk) {
        uint32_t n = (nbytes - off) < chunk ? (nbytes - off) : chunk;
        n &= ~3u;
        if (!n) break;
        st = DtsDevMemRd(h, (uint32_t *)buf, n, addr + off);
        if (st != BC_STS_SUCCESS) {
            fprintf(stderr, "DtsDevMemRd @0x%x failed: 0x%x\n", addr + off, st);
            fclose(f); free(buf); DtsDeviceClose(h);
            return 4;
        }
        fwrite(buf, 1, n, f);
    }
    fclose(f); free(buf); DtsDeviceClose(h);
    printf("dumped %u bytes from 0x%x to %s\n", nbytes, addr, outfn);
    return 0;
}
