# Crystal HD BCM70015 Decode Status

_Обновлено 2026-08-23 после реверса Windows-драйвера. См. также REVERSE-NOTES.md._

## Firmware
- Version: 1.54.0.0 (2010/11/30), bcm70015fw.bin (864276 bytes)
- Loading: SUCCESS, heartbeat alive
- **RX workaround mask advertised via SCRATCH_5: 0x0** — прошивка не требует воркараундов

## What Works (подтверждено трассировкой на ядре 6.18)
- Полная командная цепочка INIT → CHAN_OPEN → INPUT_PARAMS → ACTIVATE → START_VIDEO,
  все ответы status=0, команда START_VIDEO корректна до последнего слова (36 слов сверены
  с построением в bcmDIL.dll 3.17 — идентично).
- Прошивка ПОТРЕБЛЯЕТ входные данные: после DtsProcInput её счётчик DramSz в DRAM-кольце
  уменьшается ровно на размер поданных байт.
- Прошивка сигналит о кадрах: PicQSts bitmap=1 (MBOX_PCI2) приходит 3+ раза.

## What Fails
- RX DMA (доставка кадров в хост): Y Rx engine стартует и сразу падает UNDERRUN'ом
  (Y_RX_ERROR_STATUS: 0x800 = L0, 0x2000 = L1). Дескриптор имеет `sdram_buff_addr=0`,
  который должна заполнить прошивка, но не заполняет. ByteCnt: lst0=40, lst1=38440 байт.
- Следствие: FETCH TIMEOUT в цикле выборки кадров, декодированные кадры не доходят до хоста.
- Ответ START_VIDEO: picBuf/picRelBuf/picInfoDeliveryQ/picInfoReleaseQ/channelStatus = 0
  при любом picInfo (ON и OFF).

## Root Cause (уточнён)
Не «мёртвая прошивка» — она активна и обрабатывает вход. Выходной конвейер прошивки
не инициализируется: адреса очередей не выдаются, дескрипторы не патчатся.
Протокол нашего стека побайтово совпадает с Windows-стеком Broadcom (сверены INIT,
START_VIDEO, постинг RX), публичных отличий нет — все Linux-деревья (dbason/OSMC/wetab)
идентичны, mainline-ядро 70015 никогда не поддерживало.

## Открытые гипотезы
1. Контрольный тест под Windows на этой же карте: работает ли decode вообще?
   Нет → HW/FW-ревизия не умеет decode. Да → разница в kernel-side init
   (кандидат №1: DDR init, crystalhd_flea_ddr.c).
2. Реверс обработчика START_VIDEO в bcm70015fw.bin (ARM, диспетчеризация без литералов).
3. Сверить PES-конвертер / DtsProcInput flow с bcmDIL.dll.

## Notes
- Link firmware (bcm70012fw.bin) несовместима с Flea (CMAC signature mismatch).
- В linux-firmware прошивки нет; dbason repo — единственный источник (FW 1.54.0.0).
- crystalhd удалён из kernel staging (там был только BCM70012/Link).
