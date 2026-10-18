#!/bin/bash
# Проверки скрипта сборки Xenon-тулчейна.
# Запуск:  bash toolchain/tests/test_script.sh
#
# Покрывает:
#   1) синтаксис bash
#   2) поведение без аргумента и с неизвестным аргументом
#   3) независимость от рабочего каталога (в т.ч. путь с пробелом)
#   4) отказ при отсутствии обязательного патча
#   5) отказ при отсутствии архива в офлайн-режиме
#   6) сверка контрольных сумм
#
# Тесты НЕ запускают сборку: она слишком долгая. Проверяется поведение
# до начала длительных этапов.

set -u

S="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)/build-xenon-toolchain.sh"
TC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
DIST="$TC/dist"
PATCHES="$TC/patches"

pass=0
fail=0

chk() {   # chk <описание> <ожидаемый код> <команда...>
    local desc="$1" want="$2"; shift 2
    out="$("$@" 2>&1)"; rc=$?
    if [ "$rc" = "$want" ]; then
        printf '  OK    %-58s rc=%s\n' "$desc" "$rc"; pass=$((pass+1))
    else
        printf '  FAIL  %-58s rc=%s (ожидался %s)\n' "$desc" "$rc" "$want"
        fail=$((fail+1))
        printf '%s\n' "$out" | head -6 | sed 's/^/          /'
    fi
}

echo "=== [1] синтаксис bash ==="
if bash -n "$S"; then
    printf '  OK    bash -n: синтаксис корректен\n'; pass=$((pass+1))
else
    printf '  FAIL  bash -n: есть ошибки синтаксиса\n'; fail=$((fail+1))
fi

echo
echo "=== [2] проверка аргументов ==="
chk "без аргумента -> usage + ненулевой код" 1 bash "$S"
chk "неизвестный аргумент -> ошибка"          2 bash "$S" заведомо_нет_такого_режима
chk "--help -> usage + ненулевой код"         1 bash "$S" --help

echo
echo "=== [3] независимость от рабочего каталога ==="
for d in "$TC" "$TC/.." /tmp "/d/xbox 360"; do
    [ -d "$d" ] || mkdir -p "$d" 2>/dev/null || continue
    out=$(cd "$d" && bash "$S" заведомо_нет_такого_режима 2>&1); rc=$?
    if [ "$rc" = "2" ]; then
        printf '  OK    cwd=%-46s rc=2\n' "$d"; pass=$((pass+1))
    else
        printf '  FAIL  cwd=%-46s rc=%s\n' "$d" "$rc"; fail=$((fail+1))
    fi
done

echo
echo "=== [4] отсутствие обязательного патча ==="
mv "$PATCHES/binutils-2.32-vmx128.diff" "$PATCHES/.vmx128.bak"
out=$(OFFLINE=1 bash "$S" toolchain 2>&1); rc=$?
mv "$PATCHES/.vmx128.bak" "$PATCHES/binutils-2.32-vmx128.diff"
if [ "$rc" != "0" ] && printf '%s' "$out" | grep -q 'binutils-2.32-vmx128.diff'; then
    printf '  OK    VMX128-патч отсутствует -> отказ, rc=%s\n' "$rc"; pass=$((pass+1))
    printf '%s\n' "$out" | grep 'Обязательный патч' | sed 's/^/          /'
else
    printf '  FAIL  rc=%s\n' "$rc"; fail=$((fail+1))
    printf '%s\n' "$out" | tail -6
fi

echo
echo "=== [5] отсутствие архива в офлайн-режиме ==="
mv "$DIST/newlib-3.1.0.tar.gz" "$DIST/.newlib.bak"
out=$(OFFLINE=1 bash "$S" toolchain 2>&1); rc=$?
mv "$DIST/.newlib.bak" "$DIST/newlib-3.1.0.tar.gz"
if [ "$rc" != "0" ] && printf '%s' "$out" | grep -q 'newlib-3.1.0.tar.gz'; then
    printf '  OK    архив отсутствует + OFFLINE=1 -> отказ, rc=%s\n' "$rc"
    pass=$((pass+1))
    printf '%s\n' "$out" | grep -i 'Офлайн-режим' | sed 's/^/          /'
else
    printf '  FAIL  rc=%s\n' "$rc"; fail=$((fail+1))
    printf '%s\n' "$out" | tail -6
fi

echo
echo "=== [6] сверка контрольных сумм ==="
out=$(OFFLINE=1 bash "$S" toolchain 2>&1)
if printf '%s' "$out" | grep -q 'Архив на месте и цел: dist/binutils-2.32.tar.gz'; then
    printf '  OK    суммы совпали, сборка дошла до следующего этапа\n'
    pass=$((pass+1))
    printf '%s\n' "$out" | grep 'dist/binutils-2.32.tar.gz' | head -1 | sed 's/^/          /'
elif printf '%s' "$out" | grep -q 'Контрольная сумма'; then
    printf '  FAIL  сумма не совпала\n'; fail=$((fail+1))
    printf '%s\n' "$out" | grep -A2 'Контрольная сумма' | sed 's/^/          /'
else
    printf '  FAIL  проверка сумм не дала результата\n'; fail=$((fail+1))
fi

echo
echo "==================================================="
printf 'ИТОГО: успешно %d, неудачно %d\n' "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
