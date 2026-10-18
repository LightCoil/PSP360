#!/bin/bash
# build-xenon-toolchain.sh - сборка кросс-тулчейна для Xbox 360 (Xenon).
#
# Происхождение: скрипт LibXenon (Free60Project/libxenon), публичное
# достояние. Эта версия исправляет работу с путями, проверку аргументов,
# обработку ошибок, проверку зависимостей и обязательность VMX128.
#
# Ключевые отличия от исходной версии LibXenon:
#   * все пути считаются от расположения скрипта, а не от текущего
#     каталога - скрипт одинаково работает из корня репозитория,
#     из toolchain/ и из любого другого места;
#   * архивы ищутся в dist/, патчи - в patches/;
#   * `set -Eeuo pipefail`, ненулевой код возврата при любой ошибке;
#   * исправлен порядок перенаправления `2>&1 >> log` -> `>> log 2>&1`
#     (в исходной версии stderr уходил в терминал и терялся из лога);
#   * VMX128-патч ОБЯЗАТЕЛЕН: отсутствие или неприменение - ошибка сборки;
#   * предварительная проверка зависимостей с понятными сообщениями;
#   * офлайн-режим: если архивы уже в dist/, сеть не используется.

set -Eeuo pipefail

# ---------------------------------------------------------------------------
# Пути. Всё вычисляется от каталога скрипта.
# ---------------------------------------------------------------------------
SCRIPT_PATH="${BASH_SOURCE[0]}"
while [ -L "$SCRIPT_PATH" ]; do
    SCRIPT_PATH="$(readlink "$SCRIPT_PATH")"
done
SCRIPT_DIR="$(cd -- "$(dirname -- "$SCRIPT_PATH")" && pwd -P)"
TOOLCHAIN_DIR="$SCRIPT_DIR"
# Корень проекта - на уровень выше toolchain/
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"

DIST_DIR="$TOOLCHAIN_DIR/dist"
PATCH_DIR="$TOOLCHAIN_DIR/patches"
LOG_DIR="$TOOLCHAIN_DIR/logs"
BUILD_DIR="$TOOLCHAIN_DIR/build"
WORK_DIR="$TOOLCHAIN_DIR/src"

LOGFILE="$LOG_DIR/build.log"
PREFIX="${PREFIX:-/usr/local/xenon}"
PARALLEL="${PARALLEL:-}"
# Число потоков сборки; -j1 означает последовательную сборку
JOBS="${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 1)}"
# 1 - не обращаться к сети, использовать только dist/
OFFLINE="${OFFLINE:-0}"

TARGET=xenon

BINUTILS_DL="https://ftp.gnu.org/gnu/binutils/binutils-2.32.tar.gz"
GCC_DL="https://ftp.gnu.org/gnu/gcc/gcc-9.2.0/gcc-9.2.0.tar.gz"
NEWLIB_DL="https://sourceware.org/pub/newlib/newlib-3.1.0.tar.gz"

ZLIB_DL="https://zlib.net/fossils/zlib-1.2.11.tar.gz"
LIBPNG_DL="https://download.sourceforge.net/libpng/libpng-1.5.10.tar.xz"
BZIP2_DL="https://sourceware.org/pub/bzip2/bzip2-1.0.6.tar.gz"
FREETYPE_DL="https://download.savannah.gnu.org/releases/freetype/freetype-2.10.4.tar.gz"

BINUTILS=$(basename "$BINUTILS_DL" .tar.gz)
GCC=$(basename "$GCC_DL" .tar.gz)
NEWLIB=$(basename "$NEWLIB_DL" .tar.gz)
ZLIB=$(basename "$ZLIB_DL" .tar.gz)
LIBPNG=$(basename "$LIBPNG_DL" .tar.xz)
BZIP2=$(basename "$BZIP2_DL" .tar.gz)
FREETYPE=$(basename "$FREETYPE_DL" .tar.gz)

# Контрольные суммы исходников (SHA-512).
#
# ПРОВЕРЕНО 18.10.2026 сверкой с официальными перечнями sourceware:
#   https://sourceware.org/pub/binutils/releases/sha512.sum
#   https://sourceware.org/pub/gcc/releases/gcc-9.2.0/sha512.sum
#   https://sourceware.org/pub/newlib/sha512.sum
# SHA-512 выбран потому, что именно в этом формате upstream публикует
# подписи. Значения НЕ придуманы: каждое сверено с перечнем выше.
BINUTILS_SHA512="0dbf26e648096f267715d645fed563690a83c55b67cc6da9b7991114aa816465207887688979b6bcdd247491fe45cf9b4827215229a1229563ba1eb2e5053783"
GCC_SHA512="55fead9cac2374b18134c17a143fc9317f67be834589303d31a7c3a6878e6bef22a0590fda902a07cb60f802df035e67975a8ab6a641048e0baa89af439a46ca"
NEWLIB_SHA512="efc4c3ab7153387780d141386bca5d3e20c9d25ae3e6b87cf94c8df9d301ce5926dacdff9bd33aeb9781559d933c3d0ae77f4e5b46120d90792f75dbfde702c7"

# патчи, обязательные для toolchain
PATCH_BINUTILS_BASE="binutils-2.32.diff"
PATCH_BINUTILS_VMX128="binutils-2.32-vmx128.diff"
PATCH_GCC="gcc-9.2.0.diff"
PATCH_NEWLIB="newlib-3.1.0.diff"

# ---------------------------------------------------------------------------
# Вывод и обработка ошибок
# ---------------------------------------------------------------------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RED=$'\033[0;31m'; C_GRN=$'\033[0;32m'; C_YEL=$'\033[0;33m'
    C_BLU=$'\033[0;36m'; C_OFF=$'\033[0m'
else
    C_RED=''; C_GRN=''; C_YEL=''; C_BLU=''; C_OFF=''
fi

mkdir -p "$LOG_DIR" "$BUILD_DIR" "$WORK_DIR" "$DIST_DIR"

say()  { printf '%s[+]%s %s\n' "$C_BLU" "$C_OFF" "$*"; }
info() { printf '%s[*]%s %s\n' "$C_OFF"  "$C_OFF" "$*"; }
warn() { printf '%s[!]%s %s\n' "$C_YEL" "$C_OFF" "$*" >&2; }
err()  { printf '%s[x]%s %s\n' "$C_RED" "$C_OFF" "$*" >&2; }
ok()   { printf '%s[+]%s %s\n' "$C_GRN" "$C_OFF" "$*"; }

# Лог: stdout И stderr идут в файл. Порядок перенаправления важен -
# `cmd 2>&1 >> f` отправляет stderr в терминал, а не в файл.
run_logged() {
    "$@" >>"$LOGFILE" 2>&1
}

on_error() {
    local rc=$? line=$1 cmd=$2
    err "СБОЙ (код $rc) на строке $line: $cmd"
    err "Подробности: $LOGFILE"
    exit "$rc"
}
trap 'on_error "$LINENO" "$BASH_COMMAND"' ERR

fail() {
    err "$*"
    err "Подробности: $LOGFILE"
    exit 1
}

usage() {
    cat >&2 <<EOF
Использование: $(basename "$0") <режим>

Режимы сборки тулчейна:
  toolchain          binutils + gcc + newlib + gcc (2nd pass)
  libxenon           библиотека libxenon (нужен готовый тулчейн)
  libs               zlib + bzip2 + libpng + freetype + bin2s + файловые системы
  zlib               только zlib
  bzip2              только bzip2
  libpng             только libpng
  freetype           только freetype
  bin2s              только bin2s
  filesystems        файловые системы (fat-xenon, ext2fs-xenon, xtaflib)
  cube               собрать пример cube

Служебные:
  clean              удалить распакованные исходники и каталог build
  env-cmd            установить /usr/local/bin/xenon-env
  help               эта справка

Переменные окружения:
  PREFIX             префикс установки (по умолчанию /usr/local/xenon)
  JOBS               число параллельных задач make (по умолчанию nproc)
  OFFLINE=1          не обращаться к сети, использовать только dist/
  NO_COLOR=1         без цветов

Примеры:
  $(basename "$0") toolchain                 # полная сборка тулчейна
  OFFLINE=1 $(basename "$0") toolchain        # только из dist/, без сети
  PREFIX=\$HOME/xenon $(basename "$0") toolchain
EOF
}

# ---------------------------------------------------------------------------
# Проверка зависимостей
# ---------------------------------------------------------------------------
need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        err "Отсутствует обязательная программа: $1"
        err "$2"
        exit 1
    fi
}

check_build_deps() {
    info "Проверка зависимостей для сборки тулчейна..."
    local missing=0

    # --- MSYS2: хостовый gcc лежит в подсистеме mingw64, а не в msys -----
    # В подсистеме msys2 gcc по умолчанию отсутствует. Если он есть
    # рядом, но не в PATH - добавляем и сообщаем об этом явно.
    if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
        if [ -x /mingw64/bin/gcc.exe ]; then
            info "Обнаружена подсистема MSYS2 mingw64: добавляю /mingw64/bin в PATH"
            export PATH="/mingw64/bin:$PATH"
            info "gcc: $(gcc --version 2>&1 | head -1)"
        fi
    fi

    local t
    for t in make tar gzip patch sed awk grep find; do
        if ! command -v "$t" >/dev/null 2>&1; then
            err "Отсутствует: $t"
            missing=1
        fi
    done
    # Хостовый C-компилятор обязателен: им собирается сам тулчейн.
    if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
        err "Отсутствует хостовый C-компилятор (cc или gcc)"
        err "  MSYS2:      pacman -S --needed gcc make flex bison texinfo gmp mpfr mpc"
        err "  Fedora:     sudo dnf install gcc make flex bison texinfo gmp-devel mpfr-devel libmpc-devel"
        err "  Debian/Ub.: sudo apt install build-essential flex bison texinfo libgmp-dev libmpfr-dev libmpc-dev"
        missing=1
    fi
    for t in flex bison makeinfo; do
        if ! command -v "$t" >/dev/null 2>&1; then
            err "Отсутствует: $t (нужен для сборки binutils/gcc)"
            err "  MSYS2: pacman -S --needed flex bison texinfo"
            missing=1
        fi
    done
    if [ "$missing" -ne 0 ]; then
        err ""
        err "Установите перечисленные компоненты и повторите сборку."
        err "Скрипт НЕ пытается устанавливать пакеты сам: apt/dnf/pacman"
        err "требуют изменений системы и явного согласия."
        exit 1
    fi
    ok "Все обязательные зависимости на месте (JOBS=$JOBS)"
}

check_download_deps() {
    need wget "Нужен для загрузки исходников. MSYS2: pacman -S wget; Debian: apt install wget" \
        || true
}

# ---------------------------------------------------------------------------
# Работа с архивами и патчами
# ---------------------------------------------------------------------------

# archive_ok <файл> <ожидаемый sha512 или пусто>
# 0 - цел и сумма совпала; 1 - повреждён/отсутствует; 2 - сумма не совпала
archive_ok() {
    local f="$1" want="${2:-}"
    [ -f "$f" ] || return 1
    gzip -t "$f" 2>/dev/null || return 1
    if [ -n "$want" ]; then
        local got
        got="$(sha512sum "$f" | awk '{print $1}')"
        [ "$got" = "$want" ] || return 2
    fi
    return 0
}

# ensure_archive <basename> <url> <sha512>
ensure_archive() {
    local name="$1" url="$2" want="${3:-}"
    local dest="$DIST_DIR/$name"

    if [ -f "$dest" ]; then
        local rc=0
        archive_ok "$dest" "$want" || rc=$?
        if [ $rc -eq 0 ]; then
            ok "Архив на месте и цел: dist/$name ($(stat -c %s "$dest") байт)"
            return 0
        elif [ $rc -eq 2 ]; then
            err "Контрольная сумма dist/$name не совпадает с ожидаемой."
            err "  ожидалось: $want"
            err "  получено:  $(sha512sum "$dest" | awk '{print $1}')"
            err "Удалите файл и повторите, либо проверьте источник."
            exit 1
        else
            warn "dist/$name повреждён (gzip не проходит проверку) - будет загружен заново"
            rm -f "$dest"
        fi
    fi

    if [ "$OFFLINE" != "0" ]; then
        err "Офлайн-режим (OFFLINE=1), но архив отсутствует: dist/$name"
        err "Положите его в $DIST_DIR вручную и повторите."
        exit 1
    fi

    info "Загрузка $name из $url"
    mkdir -p "$DIST_DIR"
    ( cd "$DIST_DIR" && wget -c "$url" ) >>"$LOGFILE" 2>&1 \
        || fail "Не удалось загрузить $name (см. $LOGFILE)"

    local rc=0
    archive_ok "$dest" "$want" || rc=$?
    if [ $rc -eq 2 ]; then
        err "Контрольная сумма загруженного $name не совпадает с ожидаемой."
        err "  ожидалось: $want"
        err "  получено:  $(sha512sum "$dest" | awk '{print $1}')"
        exit 1
    elif [ $rc -ne 0 ]; then
        fail "Загруженный $name повреждён (gzip -t не проходит)"
    fi
    ok "Загружен и проверен: dist/$name"
}

# require_patch <имя файла> - патч обязателен, отсутствие - ошибка
require_patch() {
    local name="$1"
    local p="$PATCH_DIR/$name"
    if [ ! -f "$p" ]; then
        err "Обязательный патч отсутствует: patches/$name"
        err "Каталог патчей: $PATCH_DIR"
        exit 1
    fi
    ok "Патч найден: patches/$name ($(wc -c <"$p") байт)"
}

# apply_patch <путь-к-каталогу-источника> <имя патча> <strip>
apply_patch() {
    local srcdir="$1" name="$2" strip="${3:-p1}"
    local p="$PATCH_DIR/$name"
    info "Применение патча $name"
    ( cd "$srcdir" && patch "-$strip" --batch --forward < "$p" ) \
        >>"$LOGFILE" 2>&1 \
        || fail "Патч $name не применился к $srcdir (см. $LOGFILE)"
    ok "Патч $name применён"
}

# verify_vmx128 <каталог binutils> - обязательная проверка VMX128
verify_vmx128() {
    local b="$1"
    local opc="$b/opcodes/ppc-opc.c"
    local hdr="$b/include/opcode/ppc.h"
    local dis="$b/opcodes/ppc-dis.c"

    if [ ! -f "$opc" ] || [ ! -f "$hdr" ] || [ ! -f "$dis" ]; then
        fail "verify_vmx128: нет исходников binutils в $b"
    fi

    # 1) маска opcode должна существовать ровно один раз
    local nmask
    nmask="$(grep -c '^#define PPC_OPCODE_VMX_128' "$hdr" || true)"
    if [ "$nmask" -ne 1 ]; then
        fail "VMX128: маска PPC_OPCODE_VMX_128 не определена (найдено $nmask)"
    fi

    # 2) маска не должна совпадать с уже занятой в binutils 2.32.
    #    0x4000000000ull в 2.32 - это PPC_OPCODE_7450, его брать нельзя.
    local mask
    mask="$(sed -n 's/^#define PPC_OPCODE_VMX_128[[:space:]]*\(0x[0-9a-fA-F]*ull\)/\1/p' "$hdr")"
    if [ -z "$mask" ]; then
        fail "VMX128: не удалось разобрать значение маски PPC_OPCODE_VMX_128"
    fi
    if grep -q "^#define PPC_OPCODE_7450[[:space:]]*0x4000000000ull" "$hdr"; then
        if [ "$mask" = "0x4000000000ull" ]; then
            fail "VMX128: маска $mask уже занята PPC_OPCODE_7450 в binutils 2.32"
        fi
    fi

    # 3) опция vmx128 должна быть в таблице машин
    local nopt
    nopt="$(grep -c '{ "vmx128"' "$dis" || true)"
    if [ "$nopt" -ne 1 ]; then
        fail "VMX128: опция -mvmx128 не зарегистрирована в ppc_opts[] ($nopt)"
    fi

    # 4) инструкции должны реально попасть в таблицу
    local ninsn
    ninsn="$(grep -c 'PPCVEC128,' "$opc" || true)"
    if [ "$ninsn" -lt 50 ]; then
        fail "VMX128: в таблице найдено $ninsn инструкций, ожидалось >= 50"
    fi

    # 5) обязательные опорные функции вставки/извлечения
    local fn
    for fn in insert_vds128 extract_vds128 insert_va128 extract_va128 \
              insert_vb128 extract_vb128 insert_vperm extract_vperm; do
        if ! grep -q "^${fn} (" "$opc"; then
            fail "VMX128: отсутствует функция $fn"
        fi
    done

    ok "VMX128 проверен: маска=$mask, опций=1, инструкций=$ninsn"
}

# ---------------------------------------------------------------------------
# Установка
# ---------------------------------------------------------------------------
check_priv() {
    mkdir -p "$PREFIX"
    if [ ! -w "$PREFIX" ]; then
        err "Нет прав на запись в префикс: $PREFIX"
        err "Укажите другой префикс: PREFIX=\$HOME/xenon $0 $MODE"
        exit 1
    fi
}

do_chown_prefix() {
    # chown требует root в Linux и может отсутствовать в MSYS2/Cygwin.
    if [ "$(uname -s 2>/dev/null || echo unknown)" = "Linux" ] && [ "${EUID:-$(id -u)}" -eq 0 ]; then
        chown -R "$(id -un):$(id -gn)" "$PREFIX" >>"$LOGFILE" 2>&1 || \
            warn "chown не удался (не критично)"
    fi
}

# ---------------------------------------------------------------------------
# Сборка тулчейна
# ---------------------------------------------------------------------------
toolchain_install() {
    check_priv
    check_build_deps
    do_chown_prefix

    : > "$LOGFILE"
    say "Сборка Xenon-тулчейна"
    info "корень проекта:   $PROJECT_ROOT"
    info "каталог скрипта:  $TOOLCHAIN_DIR"
    info "архивы:           $DIST_DIR"
    info "патчи:            $PATCH_DIR"
    info "лог:              $LOGFILE"
    info "префикс:          $PREFIX"
    info "потоков:          $JOBS"
    # ВАЖНО: конструкция вида `[ cond ] && cmd` отдельной строкой
    # возвращает 1 при ложном cond и обрывает скрипт из-за set -e.
    if [ "$OFFLINE" != "0" ]; then
        info "режим:            ОФЛАЙН (сеть не используется)"
    else
        info "режим:            онлайн (архивы берутся из dist/ либо загружаются)"
    fi

    # --- проверка и получение обязательных входных данных ---------------
    require_patch "$PATCH_BINUTILS_BASE"
    require_patch "$PATCH_BINUTILS_VMX128"
    require_patch "$PATCH_GCC"
    require_patch "$PATCH_NEWLIB"

    [ "$OFFLINE" = "0" ] && check_download_deps || true
    ensure_archive "$BINUTILS.tar.gz" "$BINUTILS_DL" "$BINUTILS_SHA512"
    ensure_archive "$GCC.tar.gz"     "$GCC_DL"     "$GCC_SHA512"
    ensure_archive "$NEWLIB.tar.gz"  "$NEWLIB_DL"  "$NEWLIB_SHA512"

    # Манифест контрольных сумм отслеживается git (в отличие от самих
    # архивов) - по нему видно, что именно должно лежать в dist/.
    if [ ! -f "$DIST_DIR/CHECKSUMS.sha512" ]; then
        warn "Нет файла dist/CHECKSUMS.sha512 - манифеста ожидаемых архивов"
    fi

    # --- binutils --------------------------------------------------------
    local SRC="$WORK_DIR/$BINUTILS"
    if [ ! -d "$SRC" ]; then
        say "Распаковка $BINUTILS"
        rm -rf "$SRC" "$SRC.tmp"
        mkdir -p "$SRC.tmp"
        ( cd "$SRC.tmp" && tar xzf "$DIST_DIR/$BINUTILS.tar.gz" ) >>"$LOGFILE" 2>&1 \
            || fail "Распаковка $BINUTILS не удалась"
        mv "$SRC.tmp/$BINUTILS" "$SRC"
        rmdir "$SRC.tmp"
        ok "Распаковано $(( $(find "$SRC" -type f | wc -l) )) файлов"
    else
        info "Дерево $BINUTILS уже распаковано (используется повторно)"
    fi

    apply_patch "$SRC" "$PATCH_BINUTILS_BASE" p1
    apply_patch "$SRC" "$PATCH_BINUTILS_VMX128" p1
    verify_vmx128 "$SRC"

    local BD="$BUILD_DIR/binutils"
    say "configure binutils (--target=$TARGET)"
    rm -rf "$BD"; mkdir -p "$BD"
    ( cd "$BD" && "$SRC/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --disable-werror \
        --disable-gdb --disable-sim --disable-readline ) \
        >>"$LOGFILE" 2>&1 || fail "configure binutils не удался (см. $LOGFILE)"
    ok "configure binutils: target=$(grep -m1 '^target=' "$BD/Makefile" || echo '?')"

    say "Сборка binutils (это долго)..."
    ( cd "$BD" && make "-j$JOBS" ) >>"$LOGFILE" 2>&1 \
        || fail "Сборка binutils не удалась (см. $LOGFILE)"
    ( cd "$BD" && make install ) >>"$LOGFILE" 2>&1 \
        || fail "Установка binutils не удалась"

    local AS="$PREFIX/bin/powerpc-eabi-as"
    [ -x "$AS" ] || fail "Ассемблер не найден после установки: $AS"
    ok "Ассемблер собран: $($AS --version 2>&1 | head -1)"

    # --- gcc (первый проход) --------------------------------------------
    local GSRC="$WORK_DIR/$GCC"
    if [ ! -d "$GSRC" ]; then
        say "Распаковка $GCC"
        rm -rf "$GSRC" "$GSRC.tmp"; mkdir -p "$GSRC.tmp"
        ( cd "$GSRC.tmp" && tar xzf "$DIST_DIR/$GCC.tar.gz" ) >>"$LOGFILE" 2>&1 \
            || fail "Распаковка $GCC не удалась"
        mv "$GSRC.tmp/$GCC" "$GSRC"; rmdir "$GSRC.tmp"
    fi
    apply_patch "$GSRC" "$PATCH_GCC" p1

    local GBD="$BUILD_DIR/gcc1"
    say "configure gcc (проход 1: только C, без заголовков)"
    rm -rf "$GBD"; mkdir -p "$GBD"
    ( cd "$GBD" && "$GSRC/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --enable-languages=c --without-headers --with-newlib \
        --disable-shared --disable-nls --disable-libssp \
        --disable-decimal-float --disable-threads \
        --enable-altivec --with-cpu=cell ) \
        >>"$LOGFILE" 2>&1 || fail "configure gcc (1) не удался"
    say "Сборка gcc (проход 1)..."
    ( cd "$GBD" && make "-j$JOBS" all-gcc ) >>"$LOGFILE" 2>&1 \
        || fail "Сборка gcc (1) не удалась"
    ( cd "$GBD" && make install-gcc ) >>"$LOGFILE" 2>&1 \
        || fail "Установка gcc (1) не удалась"
    ok "gcc (проход 1) установлен"

    # --- newlib ----------------------------------------------------------
    local NSRC="$WORK_DIR/$NEWLIB"
    if [ ! -d "$NSRC" ]; then
        say "Распаковка $NEWLIB"
        rm -rf "$NSRC" "$NSRC.tmp"; mkdir -p "$NSRC.tmp"
        ( cd "$NSRC.tmp" && tar xzf "$DIST_DIR/$NEWLIB.tar.gz" ) >>"$LOGFILE" 2>&1 \
            || fail "Распаковка $NEWLIB не удалась"
        mv "$NSRC.tmp/$NEWLIB" "$NSRC"; rmdir "$NSRC.tmp"
    fi
    apply_patch "$NSRC" "$PATCH_NEWLIB" p1

    local NBD="$BUILD_DIR/newlib"
    say "Сборка newlib"
    rm -rf "$NBD"; mkdir -p "$NBD"
    ( cd "$NBD" && "$NSRC/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --disable-nls --enable-newlib-io-long-long \
        CFLAGS="-DHAVE_BLKSIZE -O2" ) >>"$LOGFILE" 2>&1 \
        || fail "configure newlib не удался"
    ( cd "$NBD" && make "-j$JOBS" ) >>"$LOGFILE" 2>&1 \
        || fail "Сборка newlib не удалась"
    ( cd "$NBD" && make install ) >>"$LOGFILE" 2>&1 \
        || fail "Установка newlib не удалась"
    ok "newlib установлен"

    # --- gcc (второй проход: C и C++) -----------------------------------
    say "Сборка gcc (проход 2: C и C++)"
    rm -rf "$GBD"
    mkdir -p "$GBD"
    ( cd "$GBD" && "$GSRC/configure" \
        --target="$TARGET" --prefix="$PREFIX" \
        --enable-languages=c,c++ --with-newlib \
        --disable-shared --disable-nls --disable-libssp \
        --disable-decimal-float --disable-threads \
        --disable-libquadmath --disable-libatomic \
        --disable-libgomp --disable-libitm \
        --enable-altivec --enable-cxx-flags="-G0" \
        --with-cpu=cell ) >>"$LOGFILE" 2>&1 \
        || fail "configure gcc (2) не удался"
    ( cd "$GBD" && make "-j$JOBS" ) >>"$LOGFILE" 2>&1 \
        || fail "Сборка gcc (2) не удалась"
    ( cd "$GBD" && make install ) >>"$LOGFILE" 2>&1 \
        || fail "Установка gcc (2) не удалась"

    local GCCBIN="$PREFIX/bin/powerpc-eabi-gcc"
    [ -x "$GCCBIN" ] || fail "Компилятор не найден после установки: $GCCBIN"
    ok "Компилятор собран: $($GCCBIN --version 2>&1 | head -1)"

    verify_toolchain
    all_done
}

# ---------------------------------------------------------------------------
# Проверка собранного тулчейна
# ---------------------------------------------------------------------------
verify_toolchain() {
    say "Проверка собранного тулчейна"
    local t
    for t in powerpc-eabi-as powerpc-eabi-ld powerpc-eabi-gcc; do
        local f="$PREFIX/bin/$t"
        if [ ! -x "$f" ]; then
            fail "Нет $f"
        fi
        info "$t: $("$f" --version 2>&1 | head -1)"
    done

    # Реальная проверка: ассемблер обязан собрать объект целевой архитектуры
    local TD="$BUILD_DIR/verify"
    rm -rf "$TD"; mkdir -p "$TD"
    cat > "$TD/t.s" <<'EOF'
	.text
	.globl _start
_start:
	lis 3, 1
	ori 3, 3, 0x1234
	mtctr 3
	bctr
EOF
    if "$PREFIX/bin/powerpc-eabi-as" -mregnames -o "$TD/t.o" "$TD/t.s" \
            >>"$LOGFILE" 2>&1; then
        ok "Ассемблер собрал объект для целевой архитектуры"
    else
        fail "Ассемблер не смог собрать контрольный объект"
    fi
    if command -v od >/dev/null 2>&1; then
        info "Первые байты объекта: $(od -An -tx1 -N16 "$TD/t.o")"
    fi
}

# ---------------------------------------------------------------------------
# Остальные режимы (перенесены из LibXenon с исправлением ошибок)
# ---------------------------------------------------------------------------
libxenon_install() {
    check_priv
    : >> "$LOGFILE"
    say "Сборка libxenon"
    local dir="$PROJECT_ROOT/libxenon/ports/xenon"
    [ -d "$dir" ] || fail "Каталог не найден: $dir"
    run_logged make -C "$dir" clean || true
    run_logged make -C "$dir" "PARALLEL=-j$JOBS" libxenon.a \
        || fail "Сборка libxenon.a не удалась"
    run_logged make -C "$dir" install || fail "Установка libxenon не удалась"
    ok "libxenon установлен"
}

cross_lib_install() {
    # $1 - имя, $2 - флаг VX/DQ для unpack.c, $3 - URL, $4 - имя архива
    local name="$1" flag="$2" url="$3" arc="$4"
    check_priv
    ensure_archive "$arc" "$url" ""
    say "Сборка $name"
    local src="$WORK_DIR/${arc%.tar.*}"
    rm -rf "$src"
    ( cd "$WORK_DIR" && tar xf "$DIST_DIR/$arc" ) >>"$LOGFILE" 2>&1 \
        || fail "Распаковка $arc не удалась"
    (
        cd "$src"
        CFLAGS="-O2 -DBUILD_SHARED_LIBS=0 -D$flag" \
        CC="$PREFIX/bin/powerpc-eabi-gcc" \
        AR="$PREFIX/bin/powerpc-eabi-ar" \
        RANLIB="$PREFIX/bin/powerpc-eabi-ranlib" \
        ./configure --host=powerpc-eabi --prefix="$PREFIX" --disable-shared \
            >>"$LOGFILE" 2>&1
        make "-j$JOBS" >>"$LOGFILE" 2>&1
        make install >>"$LOGFILE" 2>&1
    ) || fail "Сборка $name не удалась"
    ok "$name установлен"
}

zlib_install()     { cross_lib_install zlib    "Z_HAVE_UNALIGNED" "$ZLIB_DL"     "$ZLIB.tar.gz"; }
bzip2_install()   { cross_lib_install bzip2   "_LARGEFILE_SOURCE" "$BZIP2_DL"   "$BZIP2.tar.gz"; }
libpng_install()  { cross_lib_install libpng  "PNG_ARM_NEON_OPT=0" "$LIBPNG_DL" "$LIBPNG.tar.xz"; }
freetype_install(){ cross_lib_install freetype "FT2_BUILD_LIBRARY=ft2" "$FREETYPE_DL" "$FREETYPE.tar.gz"; }

bin2s_install() {
    check_priv
    say "Сборка bin2s"
    local dir="$PROJECT_ROOT/libxenon/ports/xenon"
    [ -f "$dir/bin2s.c" ] || fail "Не найден $dir/bin2s.c"
    run_logged cc "$dir/bin2s.c" -o "$PREFIX/bin/bin2s" \
        || fail "Сборка bin2s не удалась"
    chmod a+x "$PREFIX/bin/bin2s"
    ok "bin2s установлен"
}

filesystems_install() {
    check_priv
    need git "Нужен git для получения файловых систем. MSYS2: pacman -S git"
    say "Сборка файловых систем"
    local fs i
    for fs in fat-xenon ext2fs-xenon xtaflib; do
        info "$fs"
        if [ ! -d "$PROJECT_ROOT/$fs" ]; then
            if [ "$OFFLINE" != "0" ]; then
                fail "Офлайн-режим: нет каталога $fs"
            fi
            run_logged git clone --depth 1 "https://github.com/Free60Project/$fs.git" \
                "$PROJECT_ROOT/$fs" || fail "Не удалось получить $fs"
        fi
        run_logged make -C "$PROJECT_ROOT/$fs" "-j$JOBS" \
            || fail "Сборка $fs не удалась"
        run_logged make -C "$PROJECT_ROOT/$fs" install \
            || fail "Установка $fs не удалась"
    done
    ok "Файловые системы установлены"
}

libs_install() {
    zlib_install
    bzip2_install
    libpng_install
    freetype_install
    bin2s_install
    filesystems_install
}

cube() {
    # В исходной версии LibXenon здесь перепутана логика проверки git:
    # при git, УСТАНОВЛЕННОМ (RETVAL=0), выполнялась ветка "git не нужен".
    local dir="$PROJECT_ROOT/devkitxenon/examples/xenon/graphics/cube"
    if ! command -v git >/dev/null 2>&1; then
        err "Для режима cube нужен git."
        err "Установите git и запустите: $0 toolchain && $0 libxenon && $0 cube"
        return 1
    fi
    [ -d "$dir" ] || { err "Каталог не найден: $dir"; return 1; }
    say "Сборка примера cube"
    run_logged make -C "$dir" "-j$JOBS" || fail "Сборка cube не удалась"
    ok "cube собран: $dir/cube.elf32"
}

clean_all() {
    check_priv
    say "Очистка"
    local d
    for d in "$BINUTILS" "$GCC" "$NEWLIB" "$ZLIB" "$LIBPNG" "$BZIP2" "$FREETYPE"; do
        rm -rf "${WORK_DIR:?}/$d"
    done
    rm -rf "$BUILD_DIR/binutils" "$BUILD_DIR/gcc1" "$BUILD_DIR/verify"
    ok "Распакованные исходники и каталог сборки binutils/gcc удалены"
    info "Архивы в dist/ ОСТАВЛЕНЫ намеренно (позволяют повторную сборку без сети)"
}

env_cmd() {
    check_priv
    local dst="/usr/local/bin/xenon-env"
    mkdir -p "$(dirname "$dst")"
    cat > "$dst" <<EOF
#!/bin/bash
export DEVKITXENON="$PREFIX"
export PATH="\${PATH:+\${PATH}:}\$DEVKITXENON/bin:\$DEVKITXENON/usr/bin"
echo "\$PATH"
bash
EOF
    chmod 755 "$dst"
    ok "Установлен $dst"
}

all_done() {
    echo
    ok "Все этапы завершены успешно."
    echo
    info "Тулчейн: $PREFIX"
    info "Лог:     $LOGFILE"
    echo
    echo "Добавьте в ~/.bashrc:"
    echo "  export DEVKITXENON=\"$PREFIX\""
    echo "  export PATH=\"\${PATH:+\${PATH}:}\$DEVKITXENON/bin:\$DEVKITXENON/usr/bin\""
    echo
}

# ---------------------------------------------------------------------------
# Диспетчер аргументов
# ---------------------------------------------------------------------------
MODE="${1:-}"
if [ $# -gt 0 ]; then shift; fi

case "$MODE" in
    ""|help|-h|--help)
        usage
        exit 1   # без аргумента - ошибка, не успех
        ;;
    toolchain)   toolchain_install   ;;
    libxenon)    libxenon_install    ;;
    libs)        libs_install        ;;
    zlib)        zlib_install        ;;
    bzip2)       bzip2_install       ;;
    libpng)      libpng_install      ;;
    freetype)    freetype_install    ;;
    bin2s)       bin2s_install       ;;
    filesystems) filesystems_install  ;;
    cube)        cube                ;;
    clean)       clean_all           ;;
    env-cmd)     env_cmd             ;;
    *)
        err "Неизвестный режим: '$MODE'"
        echo >&2
        usage
        exit 2
        ;;
esac
