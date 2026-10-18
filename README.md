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
    cpu_state.{h,cpp}   CPU Allegrex: GPR, HI/LO, VFPU vpr[128][4], COP0, LL/SC
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
    ops_cop.cpp         COP0 (mfc0/mtc0), COP2 -> VFPU
  vfpu/           обработчик VFPU (mov/neg/abs/add/sub/mul/div/min/max)
  Loader/         PBP -> ELF -> загрузка сегментов в память
  tests/          58 тестов GoogleTest
  host/main.cpp   хостовый раннер
  xbox360/        скрипты целевой сборки (требуют devkitXenon)
```

## Архитектурные решения

**Поля CPUState публичные.** Так было в снапшоте проекта от 10.06.2025, и это
даёт прямой доступ к `vpr[128][4]` без геттеров. Позднейшая версия проекта
перевела их в private и потеряла векторные регистры — из-за чего обработчик
VFPU перестал компилироваться. Здесь сохранены оба пути: прямой доступ к полям
и безопасные `Reg()` / `SetReg()`, которые additionally учитывают, что `$0`
всегда читается как ноль.

**Модель pc/nextPC.** Слот задержки MIPS реализован сдвигом `pc`/`nextPC` в
`Interpreter::Step()` **до** диспетчеризации. Инструкция ветвления лишь
выставляет `nextPC = цель`, поэтому инструкция в слоте задержки всегда
выполняется между ней и целью — автоматически, без отдельного флага.

**Память.** Реальное выделение (`calloc`), три отображаемые области с
проверкой принадлежности и границ, big-endian как на PSP. В предыдущих
версиях указатели просто присваивались фиктивным адресам `0xEC000000`.

## Состояние тулчейна (проверено 18.10.2026)

На машине **нет собранного тулчейна для Xbox 360**. Что есть:

| Путь | Что это |
|------|---------|
| `D:\PSP360\libxenon-master\toolchain` | исходники binutils-2.32 / gcc-9.2.0 / newlib для самостоятельной сборки. Каталога `bin/` нет |
| `D:\Халявы Не Работает\X360make-main` | собственный билдер (`crt0.S`, core_build, downloader, gui) |
| `D:\Халявы Не Работает\xex_writer` | библиотека записи XEX (23 файла) |
| `H:\tools\NewXeXEra\modern_toolchain` | **диск H: отсутствует в системе.** Проект был на Python (CMake+ninja+googletest), это «новый язык XEX», а не компилятор |
| `J:\Tools\new xex language` | папка существует, но **пуста** |

Восстановить исходники NewXeXEra можно из векторного индекса Continue
(`C:\Users\Rapture\.continue\index\lancedb\...NewXeXEra...lance`, 224 МБ):
колонка `contents` хранит текст чанков без сжатия. Это отдельная задача.

Целевая сборка в `xbox360/` подготовлена, но запустить её без devkitXenon
невозможно. См. `xbox360/README.md`.