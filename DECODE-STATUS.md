# Crystal HD BCM70015 Decode Status

## Firmware
- Version: 1.54.0.0 (2010/11/30)
- File: bcm70015fw.bin (864276 bytes)
- Loading: SUCCESS (CMAC signature verified, step 7 Done RetVal:400019)
- Heartbeat: WORKING (firmware alive after load)

## What Works
- DtsDeviceOpen → firmware download via BCM_IOC_FW_DOWNLOAD ioctl
- DtsOpenDecoder(ES) → firmware accepts CHAN_OPEN
- DtsSetInputFormat(H.264 320x240)
- DtsStartDecoder → firmware accepts CHAN_ACTIVATE + START_VIDEO
- DtsStartCapture
- DtsProcInput → data sent, ipCnt=1 in driver stats

## What Fails
- DtsProcOutputNoCopy → BC_STS_TIMEOUT "No Active Channels"
- Driver stats: RLL=0, FLL=14, captured=0, ipCnt=1
- FETCH TIMEOUT in kernel log

## Root Cause
Firmware response to START_VIDEO command:
```
FW_START_VIDEO: status=0 delQ=0 relQ=0 chSts=0 picBuf=0
FW_RSP: 7376311a 00000005 00000000 00000000 00000000 00000000 00000000 00000000
```

Firmware accepts the command (status=0) but returns **zero delivery/release queue addresses**.
Without valid queue addresses, the hardware cannot deliver decoded frames.

## Conclusion
Firmware 1.54.0.0 (2010) for BCM70015 Flea does not properly initialize decode queues.
This is a firmware-level limitation, not a driver or library bug.

## Notes
- Link firmware (bcm70012fw.bin) cannot be used with Flea hardware (CMAC signature mismatch)
- Crystal HD firmware is NOT in linux-firmware package
- Crystal HD was removed from kernel staging in Linux 5.x
- The dbason firmware (1.54.0.0) is the latest publicly available version
