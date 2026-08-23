/* probe.c — BCM70015: decode init + DRAM state capture around START_VIDEO
 * usage: probe <h264file>
 * dumps:
 *   /tmp/probeA.bin  DRAM 0xd0000..0xe0000 after device open
 *   /tmp/probeB.bin  same region right after DtsStartDecoder
 *   /tmp/probeScan.bin  0x0..0x200000 after DtsStartDecoder
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <bc_dts_types.h>

#define C011_PIB_stub_typedef
typedef struct {
    uint32_t bFormatChange, resolution, channelId, ppbPtr;
    int32_t  ptsStcOffset;
    uint32_t zeroPanscanValid, dramOutBufAddr, yComponent;
} C011_PIB_stub;
#define C011_PIB C011_PIB_stub

#include <bc_dts_defs.h>
#include <bc_dts_glob_lnx.h>

extern BC_STATUS DtsDeviceOpen(HANDLE *hDevice, uint32_t mode);
extern BC_STATUS DtsDeviceClose(HANDLE hDevice);
extern BC_STATUS DtsDevMemRd(HANDLE hDevice, uint32_t *Buffer, uint32_t BuffSz,
                             uint32_t Offset);
extern BC_STATUS DtsOpenDecoder(HANDLE hDevice, uint32_t StreamType);
extern BC_STATUS DtsSetInputFormat(HANDLE hDevice, BC_INPUT_FORMAT *pInputFormat);
extern BC_STATUS DtsStartDecoder(HANDLE hDevice);

static HANDLE hDev = NULL;

static int dump_range(uint32_t addr, uint32_t nbytes, const char *fn)
{
    FILE *f = fopen(fn, "wb");
    if (!f) { perror(fn); return -1; }
    uint8_t buf[4096];
    for (uint32_t off = 0; off < nbytes; off += sizeof(buf)) {
        uint32_t n = (nbytes - off) < sizeof(buf) ? (nbytes - off) : sizeof(buf);
        n &= ~3u;
        BC_STATUS st = DtsDevMemRd(hDev, (uint32_t *)buf, n, addr + off);
        if (st != BC_STS_SUCCESS) {
            fprintf(stderr, "memrd @%#x failed 0x%x\n", addr + off, st);
            fclose(f); return -1;
        }
        fwrite(buf, 1, n, f);
    }
    fclose(f);
    printf("dumped %s (%#x @%#x)\n", fn, nbytes, addr);
    return 0;
}

int main(void)
{
    BC_STATUS st = DtsDeviceOpen(&hDev, DTS_PLAYBACK_MODE);
    if (st != BC_STS_SUCCESS) { fprintf(stderr, "open dev: 0x%x\n", st); return 1; }

    if (dump_range(0xd0000, 0x10000, "/tmp/probeA.bin")) return 2;

    st = DtsOpenDecoder(hDev, BC_STREAM_TYPE_ES);
    if (st != BC_STS_SUCCESS) { fprintf(stderr, "open dec: 0x%x\n", st); return 3; }

    BC_INPUT_FORMAT fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSubtype       = BC_MSUBTYPE_H264;
    fmt.width          = 320;
    fmt.height         = 240;
    fmt.startCodeSz    = 3;
    fmt.Progressive    = 1;
    st = DtsSetInputFormat(hDev, &fmt);
    if (st != BC_STS_SUCCESS) { fprintf(stderr, "set fmt: 0x%x\n", st); return 4; }

    st = DtsStartDecoder(hDev);
    printf("DtsStartDecoder: 0x%x\n", st);

    dump_range(0xd0000, 0x10000, "/tmp/probeB.bin");
    dump_range(0x0, 0x200000, "/tmp/probeScan.bin");

    DtsDeviceClose(hDev);
    return 0;
}
