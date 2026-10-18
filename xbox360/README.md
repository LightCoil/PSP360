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

1. `psp360_core` — ядро эмуляции под `-mcpu=750 -m32` (Xenon PowerPC, VMX128)
2. `psp360_xex` — исполняемый файл, линкуется с `-lxenon -lxenon_xenos -lxenon_input -lxenon_sound`
3. ELF → XEX конвертацией `xenon-elf-to-xex`

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