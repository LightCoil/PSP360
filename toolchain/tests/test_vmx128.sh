#!/bin/bash
# test_vmx128.sh - проверка поддержки VMX128 на СОБРАННОМ ассемблере.
#
# Ожидаемые опкоды выведены НЕЗАВИСИМО из макросов кодирования
# opcodes/ppc-opc.c, добавленных патчем binutils-2.32-vmx128.diff:
#
#   #define OP(x)          (((uint64_t)(x) & 0x3f) << 26)
#   #define VX128(op,xop)  (OP(op) | ((xop) & 0x3d0))
#   #define VX128_1(op,xop)(OP(op) | ((xop) & 0x7f3))
#   #define VX128_2(op,xop)(OP(op) | ((xop) & 0x210))
#   #define VX128_3(op,xop)(OP(op) | ((xop) & 0x7f0))
#   #define VX128_5(op,xop)(OP(op) | ((xop) & 0x10))
#   #define X(op,xop)      (OP(op) | (((xop) & 0x3ff) << 1))
#
# В VMX128 НЕТ сдвига <<1: разделённые поля регистров VD128/VA128/VB128
# занимают биты 0..20, а расширенный опкод лежит в младших битах. Это
# принципиально отличается от обычного VX.
#
# Чтобы ожидаемое слово равнялось чистому опкоду, у всех VMX128-инструкций
# используются нулевые регистры (вклад полей = 0):
#   insert_vds128: ((v & 0x60) >> 3) | ((v & 0x1f) << 21)
#   insert_va128:  ((v & 0x40) << 4) |  (v & 0x20)     | ((v & 0x1f) << 16)
#   insert_vb128:  ((v & 0x60) >> 5) | ((v & 0x1f) << 11)
#   insert_vperm:  ((v & 0xe0) << 1) | ((v & 0x1f) << 16)
#
# Для обычных PowerPC и Cell load/store используются общеизвестные
# кодирования либо расчёт из макроса X(op,xop) при rD=rA=rB=1,
# что даёт фиксированную добавку 0x00210800.
#
# Использование: test_vmx128.sh [каталог bin]
set -u

BIN="${1:-/usr/local/xenon/bin}"

# Префикс инструментов зависит от канонизации цели в config.sub:
# --target=xenon -> xenon-as, --target=powerpc-eabi -> powerpc-eabi-as.
# Определяем по фактически установленным файлам.
PREFIX=""
for cand in "powerpc-eabi-" "xenon-"; do
  for exe in as objdump; do
    if [ -x "$BIN/${cand}${exe}" ] || [ -x "$BIN/${cand}${exe}.exe" ]; then
      PREFIX="$cand"; break
    fi
  done
  [ -n "$PREFIX" ] && break
done
[ -n "$PREFIX" ] || { echo "ассемблер не найден в $BIN"; ls "$BIN" | head -5; exit 1; }

AS="$BIN/${PREFIX}as"
OD="$BIN/${PREFIX}objdump"
WORK="${TMPDIR:-/tmp}/vmx128.$$"
pass=0; fail=0
ok()  { printf '  OK    %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail+1)); }
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK"

echo "=== 0. инструменты (префикс '${PREFIX}') ==="
echo "  $("$AS" --version 2>&1 | head -1)"

# Первое машинное слово .text в виде 8 hex-символов.
first_word() {
  "$OD" -d "$1" 2>/dev/null \
    | sed -n 's/^ *[0-9a-f]\{1,8\}:\t\([0-9a-f][0-9a-f ]*[0-9a-f]\).*/\1/p' \
    | head -1 | tr -d ' \n' | tr 'A-F' 'a-f'
}

# check <строка-исходника> <ожид. hex> <комментарий> <флаги>
check() {
  local src="$1" want="$2" note="$3" flags="$4"
  local tag; tag="$(printf '%s' "$src" | tr ' \t,' '___' | cut -c1-28)"
  {
    echo "	.text"
    echo "	.globl t"
    echo "t:"
    echo "	$src"
  } > "$WORK/one.s"

  if ! "$AS" $flags -o "$WORK/one.o" "$WORK/one.s" >"$WORK/err" 2>&1; then
    bad "[$src] ассемблер отверг: $(grep -v '^ *$' "$WORK/err" | head -1)"
    return
  fi
  local got; got="$(first_word "$WORK/one.o")"
  if [ -z "$got" ]; then bad "[$src] не удалось извлечь машинное слово"; return; fi
  if [ "$got" = "$want" ]; then ok "[$src] = 0x$got  ($note)"
  else bad "[$src]: ожидалось 0x$want, получено 0x$got  ($note)"; fi
}

Z="0"          # нулевые регистры: вклад полей = 0
VMX128_CASES=(
  "vperm128 $Z,$Z,$Z,$Z|14000000|VX128_2(5,0)"
  "vaddfp128 $Z,$Z,$Z|14000010|VX128(5,16)"
  "vsubfp128 $Z,$Z,$Z|14000050|VX128(5,80)"
  "vmulfp128 $Z,$Z,$Z|14000090|VX128(5,144)"
  "vmaddfp128 $Z,$Z,$Z,$Z|140000d0|VX128(5,208)"
  "vmaddcfp128 $Z,$Z,$Z,$Z|14000110|VX128(5,272)"
  "vnmsubfp128 $Z,$Z,$Z,$Z|14000150|VX128(5,336)"
  "vmsum3fp128 $Z,$Z,$Z|14000190|VX128(5,400)"
  "vmsum4fp128 $Z,$Z,$Z|140001d0|VX128(5,464)"
  "vpkshss128 $Z,$Z,$Z|14000200|VX128(5,512)"
  "vand128 $Z,$Z,$Z|14000210|VX128(5,528)"
  "vpkshus128 $Z,$Z,$Z|14000240|VX128(5,576)"
  "vandc128 $Z,$Z,$Z|14000250|VX128(5,592)"
  "vpkswss128 $Z,$Z,$Z|14000280|VX128(5,640)"
  "vnor128 $Z,$Z,$Z|14000290|VX128(5,656)"
  "vpkswus128 $Z,$Z,$Z|140002c0|VX128(5,704)"
  "vor128 $Z,$Z,$Z|140002d0|VX128(5,720)"
  "vpkuhum128 $Z,$Z,$Z|14000300|VX128(5,768)"
  "vxor128 $Z,$Z,$Z|14000310|VX128(5,784)"
  "vpkuhus128 $Z,$Z,$Z|14000340|VX128(5,832)"
  "vsel128 $Z,$Z,$Z,$Z|14000350|VX128(5,848)"
  "vpkuwum128 $Z,$Z,$Z|14000380|VX128(5,896)"
  "vslo128 $Z,$Z,$Z|14000390|VX128(5,912)"
  "vpkuwus128 $Z,$Z,$Z|140003c0|VX128(5,960)"
  "vsro128 $Z,$Z,$Z|140003d0|VX128(5,976)"
  "vsldoi128 $Z,$Z,$Z,0|10000010|VX128_5(4,16)"
  "vcmpeqfp128 $Z,$Z,$Z|18000000|VX128(6,0)"
  "vcmpeqfp128. $Z,$Z,$Z|18000040|VX128(6,64)"
  "vrlw128 $Z,$Z,$Z|18000050|VX128(6,80)"
  "vcmpgefp128 $Z,$Z,$Z|18000080|VX128(6,128)"
  "vslw128 $Z,$Z,$Z|180000d0|VX128(6,208)"
  "vsraw128 $Z,$Z,$Z|18000150|VX128(6,336)"
  "vsrw128 $Z,$Z,$Z|180001d0|VX128(6,464)"
  "vmaxfp128 $Z,$Z,$Z|18000280|VX128(6,640)"
  "vminfp128 $Z,$Z,$Z|180002c0|VX128(6,704)"
  "vmrghw128 $Z,$Z,$Z|18000300|VX128(6,768)"
  "vmrglw128 $Z,$Z,$Z|18000340|VX128(6,832)"
  "vupkhsb128 $Z,$Z|18000380|VX128(6,896)"
  "vupklsb128 $Z,$Z|180003c0|VX128(6,960)"
  "lvx128 $Z,$Z,$Z|100000c3|VX128_1(4,195)"
  "stvx128 $Z,$Z,$Z|100001c3|VX128_1(4,451)"
  "lvsl128 $Z,$Z,$Z|10000003|VX128_1(4,3)"
  "lvsr128 $Z,$Z,$Z|10000043|VX128_1(4,67)"
  "lvewx128 $Z,$Z,$Z|10000083|VX128_1(4,131)"
  "stvewx128 $Z,$Z,$Z|10000183|VX128_1(4,387)"
  "lvxl128 $Z,$Z,$Z|100002c3|VX128_1(4,707)"
  "stvxl128 $Z,$Z,$Z|100003c3|VX128_1(4,963)"
  "lvlx128 $Z,$Z,$Z|10000403|VX128_1(4,1027)"
  "lvrx128 $Z,$Z,$Z|10000443|VX128_1(4,1091)"
  "stvlx128 $Z,$Z,$Z|10000503|VX128_1(4,1283)"
  "stvrx128 $Z,$Z,$Z|10000543|VX128_1(4,1347)"
  "lvlxl128 $Z,$Z,$Z|10000603|VX128_1(4,1539)"
  "lvrxl128 $Z,$Z,$Z|10000643|VX128_1(4,1603)"
  "stvlxl128 $Z,$Z,$Z|10000703|VX128_1(4,1795)"
  "stvrxl128 $Z,$Z,$Z|10000743|VX128_1(4,1859)"
)

# X(31,xop) при rD=rA=rB=1 добавляет 0x00210800
# (поле VD на бите 21 = 0x00200000, RA на 16 = 0x00010000, RB на 11 = 0x00000800)
CELL_ADD=00210800
PPC_CASES=(
  "nop|60000000|ori r0,r0,0"
  "li 3,1|38600001|addi r3,r0,1"
  "lis 3,0x1234|3c601234|addis r3,r0,0x1234"
  "blr|4e800020|branch to link register"
  "mtctr 3|7c6903a6|move to CTR"
  "lvlx 1,1,1|7c210c0e|X(31,519) + $CELL_ADD"
  "lvrx 1,1,1|7c210c4e|X(31,551) + $CELL_ADD"
  "stvlx 1,1,1|7c210d0e|X(31,647) + $CELL_ADD"
  "stvrx 1,1,1|7c210d4e|X(31,679) + $CELL_ADD"
  "lvlxl 1,1,1|7c210e0e|X(31,775) + $CELL_ADD"
  "lvrxl 1,1,1|7c210e4e|X(31,807) + $CELL_ADD"
  "stvlxl 1,1,1|7c210f0e|X(31,903) + $CELL_ADD"
  "stvrxl 1,1,1|7c210f4e|X(31,935) + $CELL_ADD"
)

echo
echo "=== 1. VMX128 при -mvmx128 (${#VMX128_CASES[@]} инструкций) ==="
for c in "${VMX128_CASES[@]}"; do
  check "${c%%|*}" "$(echo "$c" | cut -d'|' -f2)" "$(echo "$c" | cut -d'|' -f3)" \
        "-mvmx128 -maltivec -a32"
done

echo
echo "=== 2. обычный PowerPC и Cell load/store (${#PPC_CASES[@]}) ==="
for c in "${PPC_CASES[@]}"; do
  check "${c%%|*}" "$(echo "$c" | cut -d'|' -f2)" "$(echo "$c" | cut -d'|' -f3)" \
        "-mcell -maltivec -a32"
done

echo
echo "=== 3. вклад полей регистров (insert-функции патча) ==="
# vaddfp128 1,2,3 = 0x14000010 | vd<<21 | va<<16 | vb<<11
#                = 0x14000010 | 0x00200000 | 0x00020000 | 0x00001800
check "vaddfp128 1,2,3" "14221810" "vd=1,va=2,vb=3 -> 0x00200000|0x00020000|0x00001800" \
      "-mvmx128 -maltivec -a32"
# vperm128 1,2,3,4: vd=1 va=2 vb=3 vc=4 (поле vc на битах 6..8)
check "vperm128 1,2,3,4" "14221900" "vd/va/vb/vc = 1/2/3/4 -> 0x00200000|0x00020000|0x00001800|0x100" \
      "-mvmx128 -maltivec -a32"

echo
echo "=== 4. дизассемблер распознаёт VMX128 ==="
{
  echo "	.text"; echo "	.globl _start"; echo "_start:"
  echo "	vaddfp128 1,2,3"; echo "	lvx128 4,5,6"; echo "	vxor128 7,8,9"
} > "$WORK/d.s"
if "$AS" -mvmx128 -maltivec -a32 -o "$WORK/d.o" "$WORK/d.s" 2>"$WORK/derr"; then
  dis="$("$OD" -d "$WORK/d.o" 2>/dev/null)"
  for w in vaddfp128 lvx128 vxor128; do
    if printf '%s' "$dis" | grep -q "$w"; then ok "objdump показывает $w"
    else bad "objdump не показал $w"; fi
  done
  printf '%s\n' "$dis" | sed -n '6,9p' | sed 's/^/        /'
else
  bad "сборка для дизассемблера не удалась: $(head -1 "$WORK/derr")"
fi

echo
echo "=== 5. без -mvmx128 VMX128-инструкции отвергаются ==="
for m in vaddfp128 lvx128 vxor128 vperm128 vmsum3fp128; do
  printf '\t.text\n\t%s 0,0,0\n' "$m" > "$WORK/n.s"
  if "$AS" -mcell -maltivec -a32 -o "$WORK/n.o" "$WORK/n.s" >/dev/null 2>&1; then
    bad "$m принята БЕЗ -mvmx128 - неверно"
  else
    ok "$m отвергнута без -mvmx128"
  fi
done

echo
echo "=== 6. формат объекта: 32-битный big-endian PowerPC ==="
printf '\t.text\n\tnop\n' > "$WORK/f.s"
"$AS" -a32 -o "$WORK/f.o" "$WORK/f.s" 2>/dev/null
hdr="$("$BIN/${PREFIX}readelf" -h "$WORK/f.o" 2>/dev/null)"
if printf '%s' "$hdr" | grep -q 'ELF32'; then ok "объект ELF32"
else bad "объект не ELF32"; fi
if printf '%s' "$hdr" | grep -q 'big endian'; then ok "порядок байтов big endian"
else bad "порядок байтов не big endian"; fi
printf '%s' "$hdr" | grep -E 'Machine|Type' | sed 's/^/        /'

echo
echo "==================================================="
printf 'ИТОГО: успешно %d, неудачно %d\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1



