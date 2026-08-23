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

## Реверс прошивки bcm70015fw.bin (2026-08-23, второй заход — ПРОРЫВ)

- **Найден ARM-вид регистров моста**: адреса PCIe `0x0e00xxx` из ARM видны как
  `0x100e0xxx` (+0x10000000). Доказательство — literal pool Thumb-функций в
  области **0x4b240–0x4b300**, содержащий подряд:
  - `0x100e001c` (MBOX_ARM1 = FW_CMD_POST)
  - `0x100e0018` (MBOX_PCI1 = FW_CMD_RESP)
  - `0x100e0020` (MBOX_PCI2 = RX_DMA_PIC_QSTS)
  - плюс слова вида 0x50209cxx (MISC1 DMA-регистры).
- Значит, обработчик хост-команд прошивки — Thumb-код вокруг 0x4b240.
- Прочее подтверждено: IRQ-диспетчер 0x6ef0 читает статус с 0xFFFFF000,
  периферия ARM-вида в диапазоне 0x10xxxxxx; трассировочные буферы в DDR @0xd2000;
  таблицы трасс {str_ptr, module_id, level} (блок 0x6e100).

### Результат третьего прохода (тоже 23.08)
- Область 0x4b000+ — НЕ код, а **table-driven конфигурация**: упакованные записи
  «регистр/данные», включая полные ARM-вида адреса (0x100e1a24, 0x100e3600,
  0x100e2438, 0x100e2030...) и серии с шагом mailbox-оффсетов
  (…0010, …0014, …0018, …001c, …0020).
- На буфер 0x4b000 ссылаются дескрипторы памяти в таблице ~0x2e700-0x2e900
  ({ptr, size, flags, ...}) — прошивка копирует/использует его generic-кодом.
- Поиск ссылок: ни Thumb LDR/ADR, ни ARM LDR из соседнего кода на пул не указывают —
  доступ идёт через сохранённый где-то базовый указатель.

### Следующий шаг реверса
1. Найти код-владелец таблицы 0x4b000: искать в FW код, копирующий/парсящий её
   (generic-интерпретатор записей {reg,data}); владелец = командный процессор.
2. Расшифровать формат упакованных записей (биты адреса vs данные vs флаги).
3. Через диспетчер выйти на START_VIDEO и условие нулевых очередей.
1. Дизассемблировать Thumb-код вокруг пула 0x4b240 (r2 -a arm -b 16, искать
   функции, чьи literal pool'ы упираются в этот диапазон).
2. Найти чтение MBOX_ARM1 → получение адреса команды → диспетчер по cmd id
   (ожидаемо switch по старшему байту/word[0]).
3. В ветке START_VIDEO (0x7376311a → вероятно индекс 0x11a) найти аллокатор
   picBuf/picInfoDeliveryQ и условие, при котором он возвращает нули.

## Реверс прошивки bcm70015fw.bin (2026-08-23, первый заход)

- Архитектура: ARM (гибрид ARM32 + Thumb), база загрузки 0, вектора на 0x0.
  IRQ-хендлер C-level: 0x1dc → get_active_irq = заглушка `mov r0,0` (0x20348) —
  командный интерфейс работает ПОЛЛИНГОМ, не через прерывания.
- Констант команд (0x737631xx) в бинарнике НЕТ ни как dword-литералов, ни как
  movw-иммедиатов — диспетчеризация вычисляемая (индекс/смещение от base+0x73763000).
- ARM-вид регистров моста (mailbox'ов) не найден: PCIe-адреса 0x0e00xx не встречаются
  ни литералами, ни movw. Вероятно, другой базовый адрес периферии + смещения.
- Строки API: BXVD_Open / BXVD_StartDecode / BXVD_OpenChannel / BXVD_P_HostCmdSendInit /
  BXVD_RSP_INITIALIZE и т.п.; реестры трассировки {str_ptr, module_id, level}
  (например, блок 0x6e100, модуль 0x26d04); таблица имён {ptr,val,0} @0xcb620+.
- Эксперимент DEBUG_SETUP (cmd=0x73763006, paramMask=0xFF, debugARCs=all):
  принят прошивкой (status=0), поведение декодирования не изменилось —
  ARC-трасса не маршрутизируется к хосту.

Оценка: полный реверс обработчика START_VIDEO требует выделенной сессии
(построить r2-проект, найти поллинг mailbox из главного цикла, проследить аллокатор кадров).

## Открытые гипотезы (следующие шаги)
1. **Контрольный эксперимент под Windows**: если на этой карте декодирование не работает
   даже под виндой — железо/FW-ревизия не умеет decode, и гонять реверс бессмысленно.
   Если работает — разница в kernel-side init (первый кандидат: DDR init в crystalhd_flea_ddr.c).
2. Реверс обработчика START_VIDEO в bcm70015fw.bin (диспетчеризация без литералов команд,
   придётся копать от mailbox ARM1 чтения). radare2 установлен.
3. Сравнить полный flow DLL: DtsStartDecoder/DtsProcInput (PES-конвертер) — не все пути сверены.

## Снимок стенда DMA (2026-08-23)
После постинга буфера захвата:
- Y Rx engine (SW_DESC_LIST_CTRL_STS 0x502050): RUN, переносит частично
  (lst0=40 байт, lst1=38440 байт), затем UNDERRUN (y_err 0x800/0x2000).
- HIF/UV Rx engine (HIF_DMA_CTRL 0x5020b0): RUN выставлен, но UVcnt0=UVcnt1=0 —
  НОЛЬ байтов перенесено, ни Done ни Error. Молчаливый висяк.
- Пакет захвата остаётся с rx_waiting_uv_intr навсегда → freeq/actq пустеют → FETCH TIMEOUT.
- Дескрипторы хоста: sdram_buff_addr=0 (не запатчен FW), xfer_size=1020 dw (4080 B),
  buff_addr_low корректный, next_desc валиден.

Вывод: выходной конвейер FW (аллокатор кадров + патч дескрипторов) не функционирует.
Входной конвейер и командный интерфейс полностью работоспособны.

## Реверс bcm70015fw.bin (2026-08-23, четвёртый заход — ПРОРЫВ: полная карта командного слоя)

### Метод (воспроизводимый)
1. **Динамический дамп DDR**: утилита `tools/fwdump.c` (DtsDevMemRd → BCM_IOC_MEM_RD)
   и `tools/probe.c` (открывает девайс, стартует декодер и снимает состояние DRAM
   ДО/ПОСЛЕ START_VIDEO в одном процессе — важно: каждое DtsDeviceOpen шлёт
   DEBUG_SETUP+INIT и затирает слоты очереди команд!).
2. **Дифф DDR со статическим образом**: FW исполняется прямо из DRAM@0 (копия файла
   байт-в-байт живая: .data модифицируется). Изменённые страницы = рантайм.
3. **ADR-скан**: ARM-код 32-битный, строки адресуются ТОЛЬКО pc-relative
   (`add rX, pc, #imm`), поэтому прямых литералов на строки нет. Скан пар
   «инструкция→цель в области строк» даёт полную карту функция→строка.

### Структура FW
- Каталог логов BDBG {ptr,line,module} @0x6d600–0x6e100; строки @0x394–0x8f90;
  ARC-образ AVD с ELF-символами внутри файла (~0xc1000 в DDR).
- Sharedmem base = `*(0x100F6004)+1` (регистр скраба BORCH_END; драйвер пишет туда
  GetScrubEndAddr(buffSz) — 0xd3000). FW зануляет base..+0xA00 при старте.

### Карта функций ARM-слоя SMP (все адреса = файловые = DRAM)
- `SMP_CmdApi_ProcessHstCmd` @0x5f2c. rsp = cmd+0x114; memset(rsp,0,0x100);
  rsp[0]=echo eCmd; dispatch бинарным поиском по eCmd:
  - CH_OPEN 0x73763100 (+0xfc от 0x73763004) → 0x62cc
  - INPUT_PARAMS 0x73763108 → 0x662c → bl 0x3f68
  - ACTIVATE 0x73763102 → 0x6450; STATUS +1→0x6478; FLUSH +2→0x6498;
    TRICK_PLAY +3→0x64c4; TS_PIDS +4→0x64ec; PS_STREAM_ID +5→0x660c;
    CH_CLOSE 0x73763101 → 0x6428; DROP(0x10E)→0x6654
  - **START_VIDEO 0x7376311A (+0x12) → 0x667c → bl SMP_CmdIf_DecStart@0x4630**
  - STOP_VIDEO (+0x13) → 0x66a4 → bl 0x4288
- `SMP_CmdIf_DecStart` @0x4630: r7=[cmd+0x1c]=channelId; глобал таблицы каналов
  [0x3de8]=0xd1ff4 ([+4]=база массива); запись канала stride 0x1CC
  (**in-use флаг +0xC4**, state +0xD2, hXVDDecode +0x18);
  вызывает SMP_StartVideo@0xdf0 → BXVD_StartDecode([chan+0xD4] handle, bl 0xc0bc),
  затем XPT Playback Settings (bl 0x1bf04/0x1ba4c с [chan+0xBC]).
- Глобалы: очередь хост-команд 0xd5124 (+4 head/+8 count), рабочие очереди
  0xd5164/0xd5174/0xd5134; main loop @0x8d98 поллит; воркер диспетчера @0x9048;
  hostCommIntHandler @0x8d48 читает MBOX_ARM1 (0x100e0000+0x1c) → адрес команды.
- RX: `StartRxDmaList` @0x77e0 программирует Y Rx DMA блок **0x10502000**:
  list0 lo/hi=+0x40/+0x44, list1 lo/hi=+0x48/+0x4C, START-бит |=1 в lo-регистре;
  вызывается из RX-обработчика (чтение PDI) инструкцией @0x8818 c &chanCtx[0x188].

### Командный интерфейс (наблюдаемое рантайм-поведение)
- Хост пишет 24 слова в base+0x100 (=0xd3100), адрес — в MBOX_ARM1.
- FW копирует команду в очередь слотов **шаг 0x218** (0x100 cmd + 0x100 rsp + meta,
  первый наблюдённый слот 0xd5430), обрабатывает воркером, ответ пишет в
  base+0x200 (0xd3200), адрес ответа — в MBOX_PCI1.
- PDI (PIC_DELIVERY_HOST_INFO, 6 слов без Reserved) хост пишет в base+0x400
  (0xd3400), канал — в MBOX_ARM2 (0x100e0024).

### ГЛАВНЫЙ ВЫВОД сессии: нули в ответе START_VIDEO — НЕ БАГ
`DecStart` заполняет в ответе ТОЛЬКО rsp[1]=sequence и rsp[2]=status
(str r,[r4,4] / str r,[r4,8] @0x4910–0x4918); остальные поля (picBuf/picRelBuf/
picInfoDeliveryQ/picInfoReleaseQ/channelStatus) остаются нулями после memset'а
диспетчера. Эта FW (1.54.0.0) В ПРИНЦИПЕ не возвращает очереди через START_VIDEO —
flea-схема delivery работает через PDI/MBOX_ARM2, а не через delQ/relQ из ответа.
Драйверу эти поля не нужны (hw->pib_del_Q_addr используется только link-путём 70012).

### Следующий шаг (конкретно)
Ищем, кто патчит word0 дескриптора (sdram_buff_addr) и почему Y Rx получает
underrun: трассировать обработчик PDI (функция вокруг 0x83xx–0x88xx: bl 0x7bd4,
bl 0x7898, bl 0x82d0, bl 0xe110), поля chanCtx+0xE0..0x1CC. Проверить:
заполняет ли FW chanCtx[0x188] {list,descLo,descHi} из нашего PDI (дампить
chanCtx до/после поста захвата — база канала узнать из CHANNEL_OPEN rsp или
глобала 0xd1ff4+4). PicQSts bitmap уже сигналится — значит до ветки delivery FW
доходит; вопрос в аллокаторе SDRAM-буфера кадра.

## Эксперимент force_sdram_addr (2026-08-23, вечер)
Гипотеза «FW должен патчить word0 дескриптора» ОПРОВЕРГНУТА:
- В kmod-dbg добавлен module_param `force_sdram_addr` (прописывает word0 всех
  RX-дескрипторов перед постингом PDI).
- insmod ... force_sdram_addr=0x00a34000 (адрес кадрового буфера из chanCtx+0x120):
  поведение ИДЕНТИЧНО базовому — те же bytecnt (lst0=40, lst1=38440), те же
  underrun 0x800/0x2000, FETCH TIMEOUT. => underrun НЕ зависит от word0.
- Интерпретация: Y Rx engine черпает данные не из произвольной SDRAM-адресации
  дескриптора, а из выходного потока декодера; UNDERRUN = декодер не выдаёт кадр.
  Вход при этом полностью потребляется (TX done растёт), PicQSts сигналится.
- Контекст канала ch0 @0xd3a00 (снят rawdump'ом во время FETCH TIMEOUT,
  инструмент tools/rawdump.c читает /dev/crystalhd без lib-open и не порит state):
  - +0xC4 word = 01 01 01 (in-use/c5/c6 флаги все взведены), +0xD2=1 (playing)
  - +0x188: {list=1, DescY=0x9a7d0000, DescUV=0x9a7d3fe0} — наш PDI прочитан FW!
  - +0x178/+0x180 = 1 (флаги BVN-ветки доставки)
  - +0xBC=0xd8280 (hXVDDecode), +0xD4=0xe5eec (BXVD handle)
  - +0x120/+0x124 = 0x00a34000/0x00a4b000 (кандидаты кадровых буферов SDRAM)
  - глобал [0xd221c] = 0xf1ea0000 (FLEA_WORK_AROUND_SIG, маска FW=0)
- Вывод: командный слой, PDI, программирование DMA-движков FW выполняет ПРАВИЛЬНО;
  затык — выше: AVD/ARC ядро не производит кадров (или его выход не подключен к
  Y Rx входу). Кандидаты проверки:
  1. Жив ли ARC: heartbeat/статус-регистры AVD, счётчики Decode_Count/PPB.
  2. DramLog ARC-ядра (строки DramLogControl/DramLogCmd в FW; консольные команды
     dumpall/version — ARC console через UART/порт моста).
  3. Сравнить регистры AVD-блока с референсом из Windows-прогона (Vista SSD).

## Проверка живости AVD/ARC (2026-08-23, поздний вечер)
Инструменты: tools/rawdump.c (DDR через /dev/crystalhd без lib-open),
tools/rawreg.c (чтение чиповых регистров через BCM_IOC_REG_RD).
- CLK_PM_CTRL (0x070004) = 0x03000000 → DIS_AVD_CLK=0 (клок AVD включён).
- IP_SHIM CPU_ID (0x860010) = 0x302 читается; AVD_CLK_GATE (0x860014) = 0x0e:
  загейчены ТОЛЬКО clk_mp4/clk_vc1_db/clk_vc1; clk_avc (H.264) ВКЛЮЧЁН —
  FW гейтит по запрошенному кодеку, это норма.
- BVN_INT_REG (0x86000c) = 0 во время декода.
- ARM UART (0x0f3000..08): CTL=0x00730001 активен, DATA не выдаёт потока;
  канал логов недоступен. SMP_CmdIf_DebugSetup в этой FW = NOT IMPLEMENTED
  (строка @0x5b34) — DEBUG_SETUP бесполезен, логи FW не получить в принципе.
- Регистры ARC CPUCORE (0x844000+) читаются нулями (не маппятся через этот window).

### Текущая рабочая гипотеза
Вход потребляется TX'ом полностью, но ни одного кадра не производится:
подозрение на PES-парсинг. Для FLEA lib ВСЕГДА ставит BC_STREAM_TYPE_PES и
заворачивает ES→PES (libcrystalhd_if.cpp DtsSetInputFormat, DtsSetPESConverter,
libcrystalhd_parser.cpp). Если наш PES-конвертер формирует пакеты, которые
RAVE/PES-парсер FW молча отбрасывает (stream id, flags, stuffing), получаем
ровно наблюдаемую картину. Next:
1. Сверить байты PES-пакетов наши vs bcmDIL.dll 3.17 (Windows flow) на одном файле.
2. Прочитать RAVE-контекст (CDB/ITB valid/read указатели) из памяти FW во время
   теста: chanCtx ch0 @0xd3a00 (+0xBC=hXVDDecode 0xd8280) → xpt/rave структуры;
   если указатели стоят — данные не доходят до парсера; если двигаются — парсер,
   а затык дальше (AVD).
3. Контрольный прогон под Windows/Vista (решает вопрос «железо или мы» одним тестом).
