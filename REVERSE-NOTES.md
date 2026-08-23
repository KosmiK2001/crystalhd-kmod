# CrystalHD BCM70015 — Reverse Engineering Notes
_2026-08-23. Реверс Windows-драйвера BRCMHD64.sys (v03.07.00, Dell R286525) и bcmDIL.dll (3.17)._

## Источники
- Dell R286525.EXE (bcmDIL.dll 3.17.0 + BRCMHD64.sys x64, 2010-08-10) — `/tmp/dellx/R286525/`
- DriverPack repack brcmhd v2.53 (2009) — `~/Документы/Mimo_coding/crystalhd-win-driver/`
- Официальный Linux-драйвер в mainline-ядре НИКОГДА не поддерживал 70015 (только Link/70012).
- Все публичные Linux-деревья (dbason, osmc/crystalhd, rbraken/wetab) побайтово идентичны в RX-функциях.

## RX Workarounds (FLEA_WORK_AROUND_SIG = 0xF1EA)

Хендшейк (в новом .sys, после загрузки FW и детекта heartbeat):
1. Драйвер читает `SCRATCH_5` (`0x0e0058`).
2. Если биты [31:16] == `0xF1EA`, биты [15:0] — маска воркараундов, которые требует прошивка.
3. Дефолтная маска при отсутствии сигнатуры — `0x3`.

Биты (DriverFwShare.h):
- **bit 0 `RX_PIC_Q_STS_WRKARND`**: перед доверием к статусу очереди кадров — ~200 холостых
  чтений `MBOX_PCI2` (`0x0e0020`, он же RX_DMA_PIC_QSTS_MBOX), затем финальное чтение.
- **bit 1 `RX_DRAM_WRITE_WRKARND`**: после записи PIC_DELIVERY_HOST_INFO в DRAM чипа —
  чтение обратно и сравнение, до 10 циклов перезаписи (запись может не «прилипнуть»).
- bit 2 `RX_MBOX_WRITE_WRKARND`: в этом .sys не использовался.

### Реализация в Linux (crystalhd-kmod)
- Хендшейк добавлен в конец `crystalhd_flea_download_fw()` после `detect_fw_alive`.
- WA1 — в `crystalhd_flea_handle_PicQSts_intr()`.
- WA2 — в `crystalhd_flea_hw_fire_rxdma()` после `pfnDevDRAMWrite`.
- Поле `hw->FleaWorkAroundMask`.

**Результат на нашей плате: маска от прошивки = 0x0** — прошивка 1.54.0.0 воркараунды
не требует. Значит, Windows с этой же связкой работал(а?) тем же «голым» путём, что и мы,
и воркараунды нашей проблемы не объясняют. Реализация оставлена (безвредна при mask=0,
спасёт при других ревизиях FW).

## Сравнение протокола (Windows vs наш Linux-стек)
- `DtsFWStartVideo` в bcmDIL.dll строит START_VIDEO побайтно так же, как наша библиотека
  (все 36 слов, включая picInfo из DbgOptions bit6, displayTiming=1, userDataMode=1).
- INIT идентичен (0x40, 200MHz=0x0bebc200, 0x9600, 3, 1, ..., 2, 1).
- Пост-обработка ответа START_VIDEO в .sys: сохраняет resp word5/word6 (delQ/relQ) —
  те же поля, что и наш snoop.
- Постинг RX в .sys: PDI {ListIndex, DescY_lo, DescY_hi, DescUV_lo|0, DescUV_hi|0} —
  первые 5 слов совпадают с нашим layout (мы пишем ещё RxSeq+ChannelID, 7 слов).

## Ключевые технические факты о текущем затыке
- Прошивка ЖИВАЯ: принимает все команды (status=0), потребляет входные данные из DRAM-кольца
  (TX done показывает уменьшение DramSz ровно на размер данных), сигналит PicQSts bitmap.
- RX DMA падает UNDERRUN'ом (Y_RX_ERROR_STATUS 0x800=L0 / 0x2000=L1): дескриптор хоста имеет
  `sdram_buff_addr=0` — его должен заполнить firmware, но не заполняет. ByteCnt при underrun:
  lst0=40 байт, lst1=38440 байт.
- Ответ START_VIDEO содержит нули в picBuf/picRelBuf/picInfoDeliveryQ/picInfoReleaseQ/channelStatus
  при ЛЮБОМ picInfo (ON/OFF).
- PDI@borch+0x400 (0xd3400): прошивка НЕ переписывает наш блок (читает или игнорирует).
- borchStachAddr ≈ 0xd3000 ≈ размер прошивки 864276 байт (скраб до конца FW-образа).

## Открытые гипотезы (следующие шаги)
1. **Контрольный эксперимент под Windows**: если на этой карте декодирование не работает
   даже под виндой — железо/FW-ревизия не умеет decode, и гонять реверс бессмысленно.
   Если работает — разница в kernel-side init (первый кандидат: DDR init в crystalhd_flea_ddr.c).
2. Реверс обработчика START_VIDEO в bcm70015fw.bin (диспетчеризация без литералов команд,
   придётся копать от mailbox ARM1 чтения). radare2 установлен.
3. Сравнить полный flow DLL: DtsStartDecoder/DtsProcInput (PES-конвертер) — не все пути сверены.
