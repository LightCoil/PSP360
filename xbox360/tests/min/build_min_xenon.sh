#!/bin/bash
# build_min_xenon.sh - сборка и проверка минимальной программы Xenon.
#
# Ничего не заменяет заглушками: собирается настоящий объект целевым
# GCC, линкуется настоящим линкером целевым скриптом, результат
# проверяется целевым readelf/objdump.
#
# Использование: build_min_xenon.sh [префикс bin]
set -u

BIN="${1:-/usr/local/xenon/bin}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
OUT="$HERE/out"
mkdir -p "$OUT"

PREFIX=""
for c in "xenon-" "powerpc-eabi-" ""; do
  if [ -x "$BIN/${c}gcc" ] || [ -x "$BIN/${c}gcc.exe" ]; then PREFIX="$c"; break; fi
done
[ -n "$PREFIX" ] || { echo "gcc не найден в $BIN"; exit 1; }
T() { local f="$BIN/${PREFIX}$1"; [ -f "$f" ] && echo "$f" || echo "$f.exe"; }
GCC="$(T gcc)"; GXX="$(T g++)"; AS="$(T as)"; LD="$(T ld)"; RE="$(T readelf)"; OD="$(T objdump)"

echo "=== 0. инструменты (префикс '${PREFIX}') ==="
for x in "$GCC" "$AS" "$LD" "$RE" "$OD"; do
  printf '  %-46s ' "$x"
  if [ -x "$x" ] || [ -f "$x" ]; then echo "$("$x" --version 2>&1 | head -1)"; else echo "ОТСУТСТВУЕТ"; fi
done

# Флаги цели. Обоснование в docs/TOOLCHAIN_STATE.md раздел 6:
#   -mcpu=cell     маска PPC_OPCODE_CELL нужна для 128-битных load/store
#                  VMX128; в 750 она не входит
#   -mpowerpc64    ABI powerpc64 (тулчейн и libxenon собраны с ним)
#   -m32           32-битный мультилиб, Xenon - 32-битная система
#
# Для ЛИНКЕРА флаги другие: -m32 там недопустим ("unrecognised emulation
# mode: 32"). Режим эмуляции задаётся как -m elf32ppc, формат вывода -
# как --oformat elf32-powerpc.
CFLAGS=(-mcpu=cell -mpowerpc64 -m32 -O2 -ffreestanding -fno-builtin
       -Wall -Wno-unused-parameter)
LDFLAGS=(-m elf32ppc --oformat elf32-powerpc
         -T "$HERE/xenon_min.ld" -nostdlib -static)

echo
echo "=== 1. компиляция C целевым GCC ==="
if "$GXX" "${CFLAGS[@]}" -c "$HERE/xenon_min.c" -o "$OUT/xenon_min.o" 2>"$OUT/cc.log"; then
  echo "  OK: $OUT/xenon_min.o"
else
  echo "  ОТКАЗ компиляции:"; sed 's/^/    /' "$OUT/cc.log" | head -20; exit 2
fi

echo
echo "=== 2. ассемблирование целевым ассемблером ==="
if "$AS" -mcell -mregnames -a32 -o "$OUT/xenon_min_asm.o" "$HERE/xenon_min.S" 2>"$OUT/as.log"; then
  echo "  OK: $OUT/xenon_min_asm.o"
else
  echo "  ОТКАЗ ассемблирования:"; sed 's/^/    /' "$OUT/as.log" | head -20; exit 3
fi

echo
echo "=== 3. проверка объекта целевым readelf ==="
"$RE" -h "$OUT/xenon_min.o" | grep -E 'Class|Data|Machine|Type|Entry|Flags' | sed 's/^/    /'
hdr="$("$RE" -h "$OUT/xenon_min.o")"
check_hdr() { printf '  %-28s ' "$1"; if printf '%s' "$hdr" | grep -q "$2"; then echo "OK"; else echo "НЕ СООТВЕТСТВУЕТ"; return 1; fi; }
rc=0
check_hdr "ELF32"            'ELF32'              || rc=1
check_hdr "big endian"       'big endian'         || rc=1
check_hdr "машина PowerPC"   'PowerPC'            || rc=1
check_hdr "перемещаемый"     'REL'                || rc=1
[ $rc -eq 0 ] || exit 4

echo
echo "=== 4. линковка целевым линкером со скриптом xenon_min.ld ==="
if "$LD" "${LDFLAGS[@]}" -o "$OUT/xenon_min.elf" \
      "$OUT/xenon_min.o" "$OUT/xenon_min_asm.o" 2>"$OUT/ld.log"; then
  echo "  OK: $OUT/xenon_min.elf"
else
  echo "  ОТКАЗ линковки:"; sed 's/^/    /' "$OUT/ld.log" | head -20; exit 5
fi

echo
echo "=== 5. проверка исполнимого ELF целевым readelf ==="
"$RE" -h "$OUT/xenon_min.elf" | grep -E 'Class|Data|Machine|Type|Entry|Flags' | sed 's/^/    /'
ehdr="$("$RE" -h "$OUT/xenon_min.elf")"
rc=0
printf '  %-28s ' "тип EXEC"
printf '%s' "$ehdr" | grep -q 'EXEC' && echo OK || { echo "НЕ EXEC"; rc=1; }
printf '  %-28s ' "точка входа"
ep="$(printf '%s' "$ehdr" | sed -n 's/.*Entry point address: *//p')"
echo "$ep"
case "$ep" in
  0x8200*) echo "    в диапазоне образа 0x82000000: OK" ;;
  *) echo "    ВНЕ диапазона образа: ОШИБКА"; rc=1 ;;
esac
[ $rc -eq 0 ] || exit 6

echo
echo "=== 6. секции и дизассемблирование ==="
"$RE" -S "$OUT/xenon_min.elf" | sed 's/^/    /'
echo "  --- дизассемблирование точки входа ---"
"$OD" -d "$OUT/xenon_min.elf" | sed -n '/<_start>/,+14p' | sed 's/^/    /'

echo
echo "=== 7. байты первых инструкций (проверка порядка байтов) ==="
od -An -tx1 -N16 "$OUT/xenon_min.elf" | sed 's/^/    /'

echo
echo "==================================================="
echo "ИТОГ: минимальная программа собрана и проверена"
echo "  ELF:    $OUT/xenon_min.elf"
exit 0


