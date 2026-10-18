# Состояние Xenon-тулчейна

Фактическое состояние проверено **18 октября 2026**. Документ описывает
что реально есть, что проверено запуском, а что заблокировано.
Утверждения без проверки здесь не приводятся.

---

## 1. Краткий итог

| Уровень | Состояние |
|---|---|
| POSIX-компилятор для сборки | **УСТРАНЁН** — установлены `gcc`, `msys2-runtime-devel`, `gmp/mpfr/mpc/isl-devel` |
| Скрипт сборки | исправлен и протестирован (11/11) |
| Патч VMX128 для binutils 2.32 | перенесён, применяется чисто, **проверен на собранном ассемблере: 80/80** |
| binutils 2.32 (as, ld, objdump, readelf) | **СОБРАН и установлен** |
| Минимальная программа Xenon (ассемблерная) | **собрана и проверена**: ELF32 BE, EXEC, entry 0x82000000 |
| gcc 9.2.0 / newlib 3.1.0 | идёт сборка (скрипт запущен) |
| Упаковка ELF → XEX | **ЗАБЛОКИРОВАНО**: упаковщика NXE нет ни в проекте, ни в системе |
| Xenia canary | **установлена и запускается** (D3D12, RTX 4060) |

## 2. Как устранён блокер POSIX-компилятора

Прежний вывод «нужен Linux» был **неполным**. Диагностика показала:

* Shell — подсистема **MSYS2 MSYS** (`MSYSTEM=MSYS`,
  `OSTYPE=cygwin`, `uname -a` → `MSYS_NT-10.0-... Msys`).
* В подсистеме `msys` не было **ни C-компилятора, ни заголовков libc**:
  `/usr/include` содержал 18 файлов, все — заголовки binutils
  (`bfd.h`, `ctf.h`, `libiberty`, …). `libmsys-2.0.a` отсутствовал.
* Причина — **не установлен пакет `msys2-runtime-devel`**, а
  C-компилятора для подсистемы `msys` просто не было.

В репозитории `msys` доступны и `gcc`, и `msys2-runtime-devel`;
сеть (`repo.msys2.org`) доступна. Установлено:

```bash
pacman -S --needed --noconfirm gcc msys2-runtime-devel
pacman -S --needed --noconfirm gmp-devel mpfr-devel mpc-devel
pacman -S --needed --noconfirm isl-devel
```

Первая установка упёрлась в конфликт: `msys2-runtime` обновлялся, а
шесть файлов от старой установки Cygwin (`/usr/bin/newgrp.exe`,
`/usr/share/doc/Cygwin/*`) не принадлежали **ни одному пакету**
(проверено `pacman -Qo`). Они перезаписаны через `--overwrite` только
для этих файлов.

Результат:

```
gcc --version        : gcc.exe (Rev5, Built by MSYS2 project) 13.4.0
gcc -dumpmachine     : x86_64-pc-cygwin
макросы             : __unix__, __MSYS__, __CYGWIN__
unistd.h fcntl.h sys/wait.h : найдены
проба (pipe + fcntl(F_GETFD)): КОМПИЛЯЦИЯ УСПЕШНА
```

### 2.1 Побочно вскрытый дефект MSYS2

Утилиты `/usr/bin/expr` и `/usr/bin/diff` **молча ничего не делали**
(`diff` возвращал код 127, `expr` — «syntax error: unexpected
argument '3'»). Причина — устаревший и не согласованный сам с собой
рантайм `msys2-runtime 3.4.10`. После обновления до 3.6.3 обе
утилиты заработали. Это ломало скрипты `configure` (`expr`) и делало
невозможной проверку патчей (`diff`).

## 3. Состав каталога

```
toolchain/
  build-xenon-toolchain.sh      скрипт сборки (исправлен)
  .gitignore                    политика артефактов
  .build.lock.d/                замок от параллельной сборки (не в git)
  dist/                         архивы, НЕ в git (~183 МБ)
    CHECKSUMS.sha512            манифест, В git
  patches/                      патчи, В git
    binutils-2.32.diff          config.sub: цель xenon
    binutils-2.32-vmx128.diff   ПОРТ VMX128 с 2.28 на 2.32
    gcc-9.2.0.diff              config.sub + altivec.md
    newlib-3.1.0.diff           config.sub
  logs/                         логи, В git
  src/                          распакованные исходники, НЕ в git
  build/                        объекты сборки, НЕ в git
  tests/
    test_script.sh              11 проверок скрипта
    test_vmx128.sh              80 проверок ассемблера
    run_xenia.sh                запуск Xenia с логированием
```

## 4. Как запустить

```bash
# сборка тулчейна (нужна подсистема msys в MSYS2, см. раздел 2)
JOBS=8 ./toolchain/build-xenon-toolchain.sh toolchain

# только из локальных архивов, без сети
OFFLINE=1 ./toolchain/build-xenon-toolchain.sh toolchain

# проверки
bash toolchain/tests/test_script.sh          # 11/11
bash toolchain/tests/test_vmx128.sh          # 80/80
bash xbox360/tests/min/build_min_xenon.sh    # минимальная программа
```

Параллельный запуск блокируется: скрипт создаёт
`toolchain/.build.lock.d`. Без этого две сборки делили каталог
`build/` и портили друг другу результат — configure начинал видеть
`conftest.c: No such file or directory`. Это наблюдалось фактически.

## 5. Исправленные ошибки скрипта

| Дефект | Исправление |
|---|---|
| Пути от текущего каталога, архивы не в `dist/` | Все пути от `SCRIPT_DIR`; проверено из 4 каталогов, включая с пробелом |
| Нет аргумента → Usage и **код 0** | код 1 / код 2 для неизвестного режима |
| VMX128-патч пропускался молча | обязателен; `verify_vmx128` останавливает сборку |
| `2>&1 >> log` терял stderr | `>> log 2>&1` |
| `cat f \| patch` | `patch < f` |
| `[ "$X" != "0" ] && cmd` обрывал скрипт при `set -e` | заменено на `if` |
| Повторное нанесение патча обрывало сборку | идемпотентность по маркеру (`apply_patch`) |
| `cube()`: при установленном git писал «git не нужен» | логика исправлена |
| Ожидалось `powerpc-eabi-as`, получается `xenon-as` | `detect_tool_prefix` по фактическим файлам |
| Нет проверки gmp/mpfr/mpc/isl | проверяются **заголовки**; GMP без заголовков роняет configure |
| Нет защиты от параллельной сборки | `acquire_lock` |
| `/opt/local` для gmp/mpfr, опечатка `-enable-interwork` | убраны / исправлено |

Сохраняются все режимы LibXenon: `toolchain`, `libxenon`, `libs`,
`zlib`, `bzip2`, `libpng`, `freetype`, `bin2s`, `filesystems`,
`cube`, `clean`, `env-cmd`.

### Про VMX128

VMX128 в binutils 2.32 отсутствует (в `opcodes/ppc-opc.c` нет ни
одной мнемоники `v*128`). Патч 2.28 на 2.32 неприменим по существу:

* `struct powerpc_operand` в 2.32: `{bitm, shift, insert, extract,
  flags}`, в 2.28 было `{shift, shift_mask, …}`;
* `bfd/elfxx-ppc.c` в 2.32 отсутствует (поглощён `elf32-ppc.c`);
* **маска `0x4000000000ull` в 2.32 уже занята — это
  `PPC_OPCODE_7450`.** Наивный перенос тихо сломал бы 7450.

Создан `patches/binutils-2.32-vmx128.diff` (374 строки): маска
`0x400000000000ull` (первый свободный бит), опция `vmx128` в
`ppc_opts[]`, справка `-mvmx128`, функции `insert/extract_vds128`,
`va128`, `vb128`, `vperm` с типами `uint64_t/int64_t`, операнды
`VD128/VA128/VB128/VC128/VPERM128/VD3D*`, макросы `VX128*`,
**88 инструкций**, `CELL|PPCVEC128` для
`lvlx/stvlx/lvrx/stvrx/lvlxl/stvlxl/lvrxl/stvrxl`.

**GCC не генерирует VMX128.** `gcc-9.2.0.diff` содержит только
`config.sub`, отключение `TARGET_ALTIVEC` и одну правку
`libstdc++-v3/configure`. Патч binutils даёт ассемблер, принимающий
VMX128; автоматически компилятор его не выдаст. Ни один исходник
PSP360 VMX128 сейчас не использует, поэтому патч GCC не разрабатывался.

## 6. Целевая архитектура: `cell` против `750`

Проверено на собранном ассемблере:

```
as --target-help:
  -m750cl, -mgekko, -mbroadway   generate code for PowerPC 750cl/Gekko/Broadway
  -mcell                         generate code for Cell Broadband Engine
```

* **`-mcell` существует, `-mcpu=cell` ассемблер отвергает.**
  На уровне gas опция называется `-m<имя>`; `-mcpu=` — это форма GCC.
  У `build_xex.ps1` (компиляция через GCC) `-mcpu=cell` корректна.
* **`-m750` не существует** — есть `750cl`/`gekko`/`broadway`.
  Прежнее `-mcpu=750` в build_xex.ps1 отверг бы ассемблер.
* По `ppc_opts[]`: `cell = PPC | _64 | _POWER4 | _CELL | _ALTIVEC`,
  `750 = PPC_OPCODE_7450`. Маска `PPC_OPCODE_CELL` **не входит** в 750,
  а именно за ней закреплены 128-битные load/store из патча VMX128.

Итоговый набор флагов:

* компиляция: `-mcpu=cell -mpowerpc64 -m32`
* ассемблирование: `-mcell -maltivec -mregnames -a32` (`-mvmx128` при необходимости)
* линковка: `-m elf32ppc --oformat elf32-powerpc`
  (`-m32` линкеру **недопустим**: «unrecognised emulation mode: 32»)

### 6.1 Ошибка в скрипте линковки

`OUTPUT_ARCH(powerpc)` в binutils 2.32 заставляет линкер выбрать
**64-битный** вывод: PowerPC собирается с
`--enable-64-bit-default`, включённым по умолчанию на 64-битном хосте.
Линковка падала с

```
powerpc:common architecture of input file `...o' is incompatible
with powerpc:common64 output
```

Проверенная матрица на собранном binutils 2.32:

| Скрипт | Результат |
|---|---|
| `OUTPUT_FORMAT("elf32-powerpc")` | работает |
| `OUTPUT_FORMAT(...)` + `OUTPUT_ARCH(powerpc)` | **падает** |
| `OUTPUT_FORMAT(...)` + `OUTPUT_ARCH(powerpc:common)` | работает |

В `xbox360/tests/min/xenon_min.ld` используется `powerpc:common`.
Комментарии в скриптах линковки пишутся как `/* … */`; символ `#`
комментарием не является.

## 7. Результаты сборки

| Инструмент | Версия | Путь |
|---|---|---|
| `xenon-as` | GNU assembler (GNU Binutils) 2.32 | `/usr/local/xenon/bin/xenon-as.exe` |
| `xenon-ld` | GNU ld (GNU Binutils) 2.32 | `/usr/local/xenon/bin/xenon-ld.exe` |
| `xenon-objdump` | GNU objdump (GNU Binutils) 2.32 | `/usr/local/xenon/bin/xenon-objdump.exe` |
| `xenon-readelf` | GNU readelf (GNU Binutils) 2.32 | `/usr/local/xenon/bin/xenon-readelf.exe` |
| `xenon-nm`, `xenon-objcopy`, `xenon-size`, `xenon-strip`, `xenon-ar`, `xenon-ranlib` | 2.32 | там же |

Префикс имён определяется тем, как `config.sub` канонизировал цель:
`--target=xenon` даёт `xenon-*`. Скрипт и тесты определяют префикс по
фактически установленным файлам, а не предполагают.

## 8. Результаты тестов ассемблера VMX128

`toolchain/tests/test_vmx128.sh` — **80 успешно, 0 неудачно**.

Ожидаемые опкоды выведены **независимо** из макросов кодирования
`opcodes/ppc-opc.c` патча, а не из вывода самого ассемблера:

```
#define OP(x)           (((uint64_t)(x) & 0x3f) << 26)
#define VX128(op,xop)   (OP(op) | ((xop) & 0x3d0))
#define VX128_1(op,xop) (OP(op) | ((xop) & 0x7f3))
#define X(op,xop)       (OP(op) | (((xop) & 0x3ff) << 1))
```

Ключевая особенность VMX128: **нет сдвига `<<1`** — разделённые поля
регистров `VD128/VA128/VB128` занимают биты 0..20, а расширенный
опкод лежит в младших битах.

Примеры (все нулевые регистры, вклад полей = 0):

| Инструкция | Ожидалось | Подтверждено ассемблером |
|---|---|---|
| `vaddfp128` | `0x14000010` | ✔ |
| `vsubfp128` | `0x14000050` | ✔ |
| `vmulfp128` | `0x14000090` | ✔ |
| `vxor128` | `0x14000310` | ✔ |
| `vsldoi128` | `0x10000010` | ✔ |
| `lvx128` | `0x100000c3` | ✔ |
| `stvx128` | `0x100001c3` | ✔ |
| `vcmpeqfp128` | `0x18000000` | ✔ |
| `vmaxfp128` | `0x18000280` | ✔ |
| `lvlx128` | `0x10000403` | ✔ |
| `stvrxl128` | `0x10000743` | ✔ |

Покрытие: 55 VMX128-инструкций, 13 обычных PowerPC/Cell, вклад полей
регистров для `insert`-функций патча (`vaddfp128 1,2,3` =
`0x14221810` = `0x14000010 | 0x00200000 | 0x00020000 | 0x00001800`),
распознавание дизассемблером, **отказ VMX128-инструкций без
`-mvmx128`**, формат объекта (ELF32, big endian, PowerPC).

Инструкции Cell (`lvlx`, `stvlx`, `lvrx`, `stvrx`, `lvlxl`,
`stvlxl`, `lvrxl`, `stvrxl`) — область, где патч менял маску с
`CELL` на `CELL|PPCVEC128` — проверены отдельно; их кодирование
не изменилось.

Пример дизассемблирования:

```
00000000 <_start>:
   0:  14 22 18 10   vaddfp128 v1,v2,v3
   4:  10 85 30 c3   lvx128    v4,r5,r6
   8:  14 22 19 00   vperm128  v1,v2,v3,v36
```

## 9. Минимальная программа Xenon

`xbox360/tests/min/` — минимальная программа, не зависящая от
libxenon, системных вызовов и графики. Диагностика идёт через
аппаратный порт UART Xenon (`0x8008200`), а не через `printf`, чтобы
результат не зависел от наличия libc с нужными syscall'ами.

Ассемблерная часть собрана и проверена полностью:

```
Class:  ELF32
Data:   2's complement, big endian
Type:   EXEC (Executable file)
Machine: PowerPC
Entry point address: 0x82000000

[ 1] .text   PROGBITS  82000000  010000  000040  00  AX
[ 2] .rodata PROGBITS  82000040  010040  000010  00   A
[ 3] .data   PROGBITS  82000050  010050  000010  00  WA
[ 4] .bss    NOBITS    82000060  010060  004000  00  WA

82000000: 3c 80 80 08   lis   r4,-32760
82000004: 60 84 82 00   ori   r4,r4,33280
82000008: 38 a0 00 4f   li    r5,79            # символ 'O'
8200000c: 98 a4 00 00   stb   r5,0(r4)
82000018: 48 00 00 19   bl    82000030 <min_check>
```

Компиляция C-части целевым GCC выполняется тем же скриптом; её
результат будет добавлен по факту завершения сборки gcc.

`third_party/x360make/crt/crt0.S` и `crt/xex.ld` существуют, но это
минимальные 62 и 137 байт: `crt0.S` вызывает `main` и зацикливается,
`xex.ld` размещает `. = 0x82000000`. Как готовые они не годятся —
используется собственный скрипт линковки из раздела 6.

## 10. Упаковка XEX — что установлено на самом деле

`D:\xbox 360\xextool.exe` опознан по его собственному выводу:

```
XexTool v6.3 - xorloser 2006-2011 (Build Fri Oct 14 16:31:54 2011)
Usage: XexTool <options> <xex filename>
  -l = print extended info list about xex file
  -p <xexp filename> = patch xex with xexp
  -b <base filename>  = dump basefile from xex
  -o <xex filename>   = output altered xex to a new file
  -r = remove xex limitations ...
  -x = xml output options ...
```

**Это редактор и анализатор существующих XEX, а не упаковщик ELF.**
На входе всегда `<xex filename>`. Ни один его ключ не превращает ELF
или NXE в XEX.

Проверка на файле `D:\PSP360\test_min.xex` (ручной эксперимент
предыдущей сессии, 114 байт) дала **пустой вывод** — файл не
разбирается даже этим инструментом, то есть он нерабочий.

Отсутствуют:
* упаковщик ELF → NXE (PE): в дереве `libxenon-master` его нет
  (проверено поиском по Makefile, `.sh`, `.py`, `.cpp`);
* `D:\Xbox 360 SDK\bin\win32\powerpc-eabi-g++` — **файл нулевого
  размера**, заглушка; настоящего XDK на машине нет;
* `D:\libxenon\toolchain` и `D:\devkitpro\devkitxenon` из списка
  кандидатов в `build_xex.ps1` не существуют.

По спецификации Free60 XEX2 по смещению `PE data offset` обязан
лежать **PE/NXE-образ**. Открытый тулчейн LibXenон такого образа не
производит: его штатный результат — `cube.elf32`, который запускается
через `xell`, а не как XEX. Сборка XEX — путь Microsoft XDK.

`third_party/xex_writer` приведён к спецификации **контейнера**
(28/28 тестов), но его полезная нагрузка — секции ELF, а не PE-образ,
поэтому полученный файл Xenia не загрузит. Проверено: собственный
`xex_writer` используется как независимый инспектор формата, не как
упаковщик.

## 11. Xenia

Официальный архит `xenia_canary_windows.zip` найден в загрузках
пользователя и распакован в `D:\xenia` (`LICENSE`: «Copyright (c)
2015, Ben Vanik»).

**Xenia запускается на этой машине:**

```
i> Build: canary_experimental@f819f2385 on Jun 24 2025
i> Setup: Initializing Memory... Exports... Processor... Audio...
i> Setup: Initializing Graphics...
i> DXGI adapter: NVIDIA GeForce RTX 4060 (vendor 0x10DE, device 0x2882)
i> Direct3D 12 device and OS features: ... tier 4
i> SDL OnControllerDeviceAdded: "Xbox 360 Controller"
i> Setup: Initializing VFS... Kernel... Starting graphics_system...
```

Важно: Xenia на Windows использует **D3D12, а не Vulkan**. Отсутствие
Vulkan-драйвера (есть только загрузчик `vulkan-1.dll`, ни ICD-файлов,
ни записей в реестре) **не является блокером**.

Xenia — GUI-приложение, вывода в консоль не даёт; логи пишутся по
флагу `--log_file=`.

При передаче Windows-пути Xenia интерпретирует его как виртуальный:
`Loading module game:\test_min.xex` при аргументе
`D:\PSP360\test_min.xex`. Файл нужно размещать в content root
(`D:\xenia\content`) и передавать имя.

## 12. Контрольные суммы исходников

Сверены с официальными перечнями sourceware (SHA-512); все три
локальных файла совпали:

| Файл | Источник |
|---|---|
| binutils-2.32.tar.gz | `sourceware.org/pub/binutils/releases/sha512.sum` |
| gcc-9.2.0.tar.gz | `sourceware.org/pub/gcc/releases/gcc-9.2.0/sha512.sum` |
| newlib-3.1.0.tar.gz | `sourceware.org/pub/newlib/sha512.sum` |

Это выявило ошибку в предыдущем отчёте: значения были внесены по
памяти, и проверка их отвергла. Исправлены на проверенные.

## 13. Что осталось нерешённым

1. **Упаковка в XEX** — требуется построитель NXE (PE). Отсутствует
   в проекте и в системе. Следующий шаг: либо реализовать
   конвертер ELF → PE32 Xbox, либо получить XDK.
2. **gcc 9.2.0 и newlib 3.1.0** — сборка запущена; результат будет
   зафиксирован по её завершении.
3. **Запуск XEX в Xenia** — зависит от пункта 1. Отдельно нужно
   учесть, что libxenon опирается на MMIO и низкоуровневые регистры
   Xenon: даже корректный XEX может потребовать поддержки, которой у
   конкретной сборки Xenia нет.
4. **Интеграция PSP360 для Xbox 360** — не начиналась: нет ни
   тулчейна с C++, ни libxenon в рабочем виде
   (`libxenon-master` содержит только `libxenon`, `devkitxenon`
   неполон).
