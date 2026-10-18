# Состояние тулчейна Xbox 360 — проверено 18.10.2026

## Короткий ответ

**Собранного тулчейна на этой машине нет. Ни одного исполняемого файла.**

Сборка тулчейна **запускалась 21.06.2025 и оборвалась на этапе configure/make**,
не дойдя до компиляции ни одного бинарника. Всё, что сохранилось, — это
исходники и рецепт сборки.

## Что проверено

| Что искали | Где | Результат |
|------------|-----|-----------|
| `xenon-gcc.exe`, `xenon-g++.exe` | C:, D:, E:, J: | не найдены |
| `powerpc-eabi-gcc.exe`, `powerpc-eabi-g++.exe` | C:, D:, E:, J: | не найдены |
| `powerpc-eabi-ld.exe`, `-as.exe`, `-objcopy.exe` | C:, D:, E:, J: | не найдены |
| `xenon-elf-to-xex.exe` | C:, D:, E:, J: | не найден |
| `nxe-stage0.exe` (NewXeXEra) | C:, D:, E:, J: | не найден |
| Каталог `toolchain/bin` | `D:\PSP360\libxenon-master\toolchain` | **отсутствует** |
| Исполняемые файлы в `toolchain/build` | там же | **0 штук** |

## Доказательства того, что сборка не завершилась

`toolchain/build.log` (39 байт, весь файл):

```
patching file binutils-2.32/config.sub
```

Обрыв на первом же шаге скрипта.

`toolchain/build/build.log` заканчивается так:

```
make[1]: Entering directory '/h/tools/New XeX Era/libxenon-master/toolchain/build'
mkdir -p -- ./libiberty
Configuring in ./libiberty
mkdir -p -- ./intl
Configuring in ./intl
mkdir -p -- ./bfd
Configuring in ./bfd
make[2]: Entering directory '.../build/libiberty'
make[2]: Leaving directory '.../build/libiberty'
make[1]: Leaving directory '.../build'
```

Configure отработал (`config.log`, `config.status`, `Makefile` на 410 КБ
сгенерированы), но после этого `make` ушёл в подкаталоги и вышел.
Ни `as`, ни `ld`, ни `objcopy` не собраны.

Дополнительно: VMX128-патч **не применён** к распакованным исходникам —
в `binutils-2.32/gas/config/tc-ppc.c` ноль вхождений `vmx128`/`efp`.

Обратите внимание на путь в логе: `/h/tools/New XeX Era/...` — сборка шла
с диска `H:`, которого сейчас в системе нет.

## Что было скопировано в psp360Phoenix

### `toolchain/` — рецепт сборки Xenon-тулчейна

```
toolchain/
  patches/                        ← ЦЕННОЕ, воспроизвести нельзя
    binutils-2.28-vmx128.diff     19 822 байт  ← поддержка VMX128 (векторный
                                              блок Xbox 360)
    binutils-2.32.diff                330
    binutils-2.28.diff                392
    binutils-2.22.diff                367
    gcc-9.2.0.diff                  6 504
    gcc-6.3.0.diff                  7 209
    gcc-4.7.1.diff                  7 229
    newlib-3.1.0.diff                 467
    newlib-2.5.0.diff                 445
    newlib-1.20.0.diff                482
  build-xenon-toolchain.sh       16 960 байт  ← скрипт сборки
  dist/                                        ← для пересборки офлайн
    gcc-9.2.0.tar.gz             124 304 709
    binutils-2.32.tar.gz          41 123 367
    newlib-3.1.0.tar.gz           17 958 952
  logs/
    toolchain-build.log                  39   ← обрыв на config.sub
    binutils-configure.log             4 489
    binutils-config.log                26 667
    binutils-config.status             33 302
```

### `third_party/xex_writer/` — библиотека записи XEX (23 файла)

`xex_writer.hpp`, `xex_writer.cpp`, `xex_writer_full.cpp`, CMakeLists,
тесты, плюс 11 документов `.docx` с описанием формата XEX2.
Рабочий код. **Не переписывать — использовать.**

### `third_party/x360make/` — собственный билдер проекта

```
crt/crt0.S          62 байта   ← точка входа для Xenon
crt/xex.ld          137        ← линкер-скрипт XEX
src/  main, core_build, downloader, gui, locale, logger, packer, unzip
include/  соответствующие заголовки
```

## Что НЕ копировалось и почему

| Путь | Размер | Причина |
|------|--------|---------|
| `D:\PSP360\libxenon-master\toolchain\binutils-2.32\` | 220 МБ | распакованные публичные исходники; патчи к ним не применены, состояние = pristine. Восстанавливается `tar xf dist/binutils-2.32.tar.gz` |
| `.../toolchain\build\` | ~500 КБ | Makefile на 410 КБ регенерируется через `./configure`; логи уже скопированы |

Итого в `psp360Phoenix` перенесено **~184 МБ** (из них 183 МБ — публичные
tarball-ы) и **~470 КБ** действительно уникального кода и знаний.

## Важное замечание о патчах

`binutils-2.28-vmx128.diff` написан для **binutils 2.28**, а в наличии лежит
tarball **binutils-2.32**. То есть самый ценный патч (поддержка VMX128)
потребует переноса на 2.32 — либо сборки на версии 2.28. Это отдельная задача,
и до неё тулчейн работать не будет: Xenon — это PowerPC 750 с векторным
расширением VMX128, без него компилятор генерирует неверный код для
векторных операций.

## Как собрать тулчейн из того, что сохранено

Требуется MSYS2 или Cygwin с make, autoconf, automake, bison, flex, texinfo.

```bash
cd toolchain
bash build-xenon-toolchain.sh
```

Скрипт сам распакует tarball-ы из `dist/` и применит патчи из `patches/`.
Ожидаемое время: 40–70 минут.

Либо проще — поставить готовый devkitXenon:
<https://github.com/ConsolePioneer/libxenon> (нужна сборка тулчейна)
или готовый пакет <https://wii.leseratte10.de/devkitPro/>.

## Где ещё можно поискать исходники NewXeXEra

Проект `H:\tools\NewXeXEra\modern_toolchain` (Python, «новый язык XEX»)
утрачен, но его **содержимое сохранилось в векторном индексе Continue**:

```
C:\Users\Rapture\.continue\index\lancedb\
    fileh3AtoolsNewXeXEramodern_toolchainmastervectordb_TransformersJsEmbeddingsProviderall-MiniLM-L6-v2500.lance\
        data\   246 сегментов, 224 МБ
```

Колонка `contents` содержит текст исходных файлов **без сжатия** —
читается обычным поиском по байтам. Проверено.