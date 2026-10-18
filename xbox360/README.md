# Целевая сборка под Xbox 360

## Статус: заблокировано

Скрипты в этом каталоге подготовлены, но **запустить их на этой машине
невозможно** — отсутствует собранный тулчейн devkitXenon / libxenon.

Проверено 18.10.2026:

| Что искали | Где | Результат |
|------------|-----|-----------|
| `xenon-gcc.exe`, `xenon-g++.exe` | C:, D:, E:, J: | не найдены |
| `powerpc-eabi-gcc.exe` | C:, D:, E:, J: | не найдены |
| `xenon-elf-to-xex.exe` | C:, D:, E:, J: | не найдены |
| `nxe-stage0.exe` (NewXeXEra) | C:, D:, E:, J: | не найден; проект был на диске `H:`, который отсутствует |
| Каталог с бинарниками тулчейна | D:\PSP360\libxenon-master\toolchain | только исходники, каталога `bin/` нет |

## Как установить тулчейн

### Вариант A — готовый devkitXenon (рекомендуется)

```powershell
git clone https://github.com/ConsolePioneer/libxenon.git D:\libxenon
cd D:\libxenon\toolchain
bash build-xenon-toolchain.sh
```

Либо готовый Windows-пакет с devkitPro:
https://wii.leseratte10.de/devkitPro/ — распаковать так, чтобы появился
`...\devkitxenon\bin\powerpc-eabi-g++.exe`.

Проверка после установки:

```powershell
& D:\libxenon\toolchain\bin\powerpc-eabi-g++.exe --version
```

### Вариант B — сборка из исходников, уже лежащих на диске

В `D:\PSP360\libxenon-master\toolchain` есть:
`binutils-2.32.tar.gz`, `gcc-9.2.0.tar.gz`, `newlib-3.1.0.tar.gz`,
патчи и скрипт `build-xenon-toolchain`. Нужна среда MSYS2/Cygwin с make и
autotools. Сборка занимает 30–60 минут.

## Целевые файлы сборки

После установки тулчейна:

```powershell
cd D:\psp360Phoenix
powershell -ExecutionPolicy Bypass -File .\xbox360\build_xex.ps1
```

Скрипт собирает:

1. `psp360_core` — ядро эмуляции под `-mcpu=cell -mpowerpc64 -m32`
   (Xenon PowerPC). Флаг `-mcpu=750`, стоявший здесь раньше, был неверен:
   маска `PPC_OPCODE_CELL`, за которой закреплены 128-битные load/store
   VMX128, **не входит** в 750, а самой опции `-m750` в gas нет
   (есть `750cl`/`gekko`/`broadway`). Подробности — в
   `../docs/TOOLCHAIN_STATE.md`, раздел 6.
2. `psp360_xex` — исполняемый файл, линкуется с
   `-lxenon -lxenon_xenos -lxenon_input -lxenon_sound`
3. ELF → XEX — **отдельным явным шагом**.

### Про упаковку в XEX

Инструмента `xenon-elf-to-xex` в тулчейне LibXenon **нет**. Реальная
цепочка: ELF → NXE (PE-образ) → XEX, причём последний шаг выполняет
утилита `xextool`. Поэтому:

```powershell
powershell -ExecutionPolicy Bypass -File .\xbox360\build_xex.ps1 `
    -ElfToXex "D:\xbox 360\xextool.exe"
```

Без `-ElfToXex` скрипт соберёт ELF и **явно** сообщит, что шаг упаковки
пропущен. Переименованный в `.xex` ELF рабочим XEX не является.

Ключи:

| Ключ | Назначение |
|---|---|
| `-ToolchainRoot` | префикс тулчейна (иначе ищется через `DEVKITXENON`) |
| `-LibxenonRoot` | каталог libxenon |
| `-Config` | `Release` (по умолчанию) или `Debug` |
| `-UseVmx128` | добавить `-mvmx128`. По умолчанию выключено: ни один исходник PSP360 VMX128 не использует |
| `-ElfToXex` | путь к инструменту упаковки |

## Что ещё потребуется для реального XEX

Ядро эмуляции готово к компиляции под Xenon, но само по себе XEX не даст
работающей игры. Отсутствует:

- **GPU (Graphics Engine SceGu)** — без него PSP ничего не рисует.
  Это крупнейший пробел, отдельный трек работ.
- Подсистемы Xenos (видео) и XAudio (звук) — сейчас только хостовые заглушки.
- Маппинг контроллера Xbox 360 на кнопки PSP.
- Диспетчер системных вызовов — в `core/syscall_defs.h` есть таблица номеров,
  но обработчики ещё не реализованы.
- MMU / трансляция адресного пространства PSP.
- Эмуляция потоков PSP и таймеров.

Порядок работы по этапам — в `../docs/ROADMAP.md`.