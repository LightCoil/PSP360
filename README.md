# Сборка и запуск

## Что требуется

| Компонент | Назначение | Обязателен |
|-----------|-----------|-------------|
| MSVC (Visual Studio 2022) | компиляция | да, для хоста |
| CMake ≥ 3.16 | система сборки | да |
| GoogleTest | модульные тесты | нет (тесты отключатся) |

Для целевой сборки под Xbox 360 дополнительно требуется **devkitXenon / libxenon** —
на этой машине его нет, см. раздел «Состояние тулчейна».

## Хост-сборка (работает прямо сейчас)

```powershell
cd D:\psp360Phoenix

# Конфигурация (однократно)
cmake -S . -B build -G "Visual Studio 17 2022"

# Сборка
cmake --build build --config Release

# Модульные тесты
.\build\Release\psp360_tests.exe

# Хост-раннер (выполняет тестовую программу и печатает регистры)
.\build\Release\psp360_host.exe
```

Через CTest:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

> **Важно:** GoogleTest в `D:\googletest\build\lib\Debug` собран с
> `MD_DynamicRelease`, поэтому тесты линкуются только в конфигурации
> **Release**. В Debug будет ошибка `LNK2038` (несовпадение RuntimeLibrary).

## Структура

```
psp360Phoenix/
  CMakeLists.txt
  core/           модель процессора, память, декодер, логгер, номера syscall
    cpu_state.{h,cpp}   CPU Allegrex: GPR, HI/LO, VFPU vpr[128][4],
                        COP0 (Status, Cause, EPC, EBase), заготовка LL/SC
    memory.{h,cpp}      RAM/VRAM/Scratchpad с проверкой границ и big-endian
    decoder.{h,cpp}     декодирование MIPS32 + VFPU
    logger.{h,cpp}      журналирование без внешних зависимостей
    syscall_defs.h      таблица системных вызовов PSP
  interp/         интерпретатор
    interpreter.{h,cpp} цикл Step/Run, модель pc/nextPC, слоты задержки
    ops_arith.cpp       арифметика, умножение/деление, mfhi/mflo
    ops_logic.cpp       and/or/xor/nor/slt, andi/ori/xori/lui
    ops_shift.cpp       сдвиги, syscall, break
    ops_branch.cpp      beq/bne/blez/bgtz/j/jal/jr/jalr + slot задержки
    ops_mem.cpp         lb/lh/lw/lbu/lhu/sb/sh/sw (+ lwl/lwr/swl/swr)
    ops_cop.cpp         COP0 (mfc0/mtc0)
  vfpu/           обработчик VFPU (mov/neg/abs/add/sub/mul/div/min/max)
  Loader/         PBP -> ELF -> загрузка сегментов в память
  tests/          129 тестов GoogleTest
  host/main.cpp   хостовый раннер
  xbox360/        скрипты целевой сборки (требуют devkitXenon)
  docs/ARCHITECTURE_NOTES.md  проверенные раскладки Allegrex и MIPS32
                              с указанием первоисточников
```

## Архитектурные решения

**Проверенные раскладки.** Всё, что касается раскладки полей, взято из
первоисточников, а не восстановлено по памяти: PSP CPU Overview (Hitmen)
и исходники PPSSPP. Выводы и ссылки — в `docs/ARCHITECTURE_NOTES.md`.

**Порядок байтов — little-endian.** Allegrex работает в LE-режиме
(Config.LEL), ELF-образы PSP — MIPSEL. Сборка приведением указателя к
`uint16_t*`/`uint32_t*` намеренно не используется: она зависит от платформы,
а код должен собираться и для MSVC-хоста, и для PowerPC-хоста Xbox 360.

**VFPU.** В отличие от обычного сопроцессора, VFPU на Allegrex разнесён по
нескольким опкодам MIPS: `0x18` (vadd/vsub/vdiv), `0x19` (vmul), `0x1B`
(vmin/vmax), `0x34` (vmov/vabs/vneg). COP2 (`0x12`) — это перенос регистров
(lv.s, sv.s, mftv), а не векторные операции. Поля регистров семибитные:
приёмник — биты 6..0, источник 1 — 14..8, источник 2 — 22..16; биты 15 и 7
несут размер вектора (.s/.p/.t/.q) и в поля регистров не входят.

**Маска прерываний.** Отдельного регистра Interrupt Mask на Allegrex нет:
IM[7:0] занимает биты 15..8 регистра Status. Хранить её отдельным полем
нельзя — тогда гостевой код не смог бы включить прерывания.

**Адрес возврата JAL/JALR** определяется адресом самой инструкции
(`d.pc + 8`), а не изменяемым `cpu.nextPC`. Иначе `Step()` и `StepOne()`
давали бы разные значения.

**Исключения.** Переполнение `ADD`/`SUB`/`ADDI` возбуждает исключение
Arithmetic Overflow (ExcCode 12) с записью EPC и BD, а не останавливает
эмулятор. `ADDU`/`ADDIU`/`SUBU` исключения не возбуждают по определению.

## Архитектурные решения

**Поля CPUState публичные.** Так было в снапшоте проекта от 10.06.2025, и это
даёт прямой доступ к `vpr[128][4]` без геттеров. Позднейшая версия проекта
перевела их в private и потеряла векторные регистры — из-за чего обработчик
VFPU перестал компилироваться. Здесь сохранены оба пути: прямой доступ к полям
и безопасные `Reg()` / `SetReg()`, которые учитывают, что `$0` всегда
читается как ноль.

**Модель pc/nextPC.** Слот задержки MIPS реализован сдвигом `pc`/`nextPC` в
`Interpreter::Step()` **до** диспетчеризации. Инструкция ветвления лишь
выставляет `nextPC = цель`, поэтому инструкция в слоте задержки всегда
выполняется между ней и целью — автоматически, без отдельного флага.
Признак нахождения в слоте задержки хранится в `CPUState::inDelaySlot` и
нужен для корректного EPC при исключении (BD = 1, EPC указывает на ветвление).

**Память.** Реальное выделение (`calloc`), три отображаемые области с
проверкой принадлежности и границ, little-endian как на PSP. В предыдущих
версиях указатели просто присваивались фиктивным адресам `0xEC000000`.

## Проверка

```powershell
# Полная пересборка с нуля
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# Под AddressSanitizer
cmake -S . -B build-asan -G "Visual Studio 17 2022" `
      -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi /INCREMENTAL:NO" `
      -DCMAKE_EXE_LINKER_FLAGS="/INCREMENTAL:NO"
cmake --build build-asan --config Release
.\build-asan\Release\psp360_tests.exe
```

Sanitizer-прогон: 129 тестов и отдельный набор из ~12 000 граничных случаев
загрузчика и памяти — без диагностик ASan.

## Состояние тулчейна (проверено 18.10.2026)

На машине **нет собранного тулчейна для Xbox 360**, и собрать его здесь
нельзя. Что есть и что проверено:

| Путь | Что это | Состояние |
|------|---------|-----------|
| `toolchain/build-xenon-toolchain.sh` | скрипт сборки | **исправлен**, 11/11 проверок |
| `toolchain/patches/binutils-2.32-vmx128.diff` | перенос VMX128 с binutils 2.28 на 2.32 | **применяется чисто**, 88 инструкций |
| `toolchain/dist/` | binutils-2.32 / gcc-9.2.0 / newlib-3.1.0 | SHA-512 сверены с sourceware |
| `toolchain/src/`, `toolchain/build/` | распакованные исходники и объекты | не отслеживаются git |
| `third_party/xex_writer/` | запись контейнера XEX2 | контейнер приведён к спецификации, 28/28 тестов |
| `D:\PSP360\libxenon-master` | исходники LibXenon, тот же toolchain | предыдущая провалившаяся попытка сборки |
| `D:\xbox 360\xextool.exe` | упаковщик NXE → XEX | пригоден, если появится NXE |
| `D:\devkitpro` | devkitA64 (Switch) | PowerPC/Xenon там нет |

**Чего нет:** Docker, Podman, WSL, Hyper-V, Cygwin, готового тулчейна;
**Xenia не установлена**.

**Почему сборка заблокирована.** Сборка binutils доходит до `libiberty`
и падает на отсутствии POSIX-функций (`wait`, `fork`, `pipe`, `fcntl`).
Подсистема `msys` в MSYS2 не содержит ни C-компилятора, ни POSIX-заголовков;
подсистема `mingw64` содержит gcc, но его рантайм нативный для Windows.
Кросс-тулчейн для PowerPC требует POSIX-хост.

Что при этом удалось проверить: `configure --target=xenon` проходит,
`config.sub` знает `xenon`, цель разрешается в `powerpc64--linux-gnu`.

Подробности, включая решение по `cell` против `750` и по версиям
патчей: **`docs/TOOLCHAIN_STATE.md`**.

Восстановить исходники NewXeXEra можно из векторного индекса Continue
(`C:\Users\Rapture\.continue\index\lancedb\...NewXeXEra...lance`, 224 МБ):
колонка `contents` хранит текст чанков без сжатия. Это отдельная задача.

Целевая сборка в `xbox360/` подготовлена и разбирается на флаги,
проверенные по binutils, но запустить её без тулчейна невозможно.
См. `xbox360/README.md`.