#!/bin/bash
# run_xenia.sh - запуск Xenia canary на указанном файле с логированием.
#
# Ничего не предполагает заранее: запускает эмулятор, сохраняет вывод,
# код завершения и ищет файлы журнала Xenia. Отсутствие результата
# фиксируется как отсутствие результата, а не как успех.
#
# Использование: run_xenia.sh <файл.xex|файл.elf32> [доп. аргументы...]
set -u

XENIA="${XENIA:-/d/xenia/xenia_canary.exe}"
TARGET="$1"; shift || true

if [ ! -f "$XENIA" ]; then
  echo "Xenia не найдена: $XENIA"
  echo "Ожидается официальный артефакт xenia_canary_windows.zip"
  exit 1
fi
if [ ! -f "$TARGET" ]; then
  echo "Файл не найден: $TARGET"
  exit 1
fi

TS="$(date +%Y%m%d-%H%M%S)"
LOGDIR="${TMPDIR:-/tmp}/xenia-$TS"
mkdir -p "$LOGDIR"

echo "=== Xenia: $XENIA"
"$XENIA" --version >"$LOGDIR/version.txt" 2>&1 || true
echo "--- версия (если отвечает) ---"
head -5 "$LOGDIR/version.txt" 2>/dev/null
echo
echo "=== целевой файл ==="
ls -la "$TARGET"
echo "сигнатура: $(od -An -c -N4 "$TARGET" | tr -s ' ')"
echo

# Xenia может быть собрана как GUI-приложение, тогда stdout пуст.
# Пробуем несколько режимов и сохраняем всё, что удалось получить.
run_attempt() {
  local name="$1"; shift
  local out="$LOGDIR/$name.out" err="$LOGDIR/$name.err"
  echo "--- попытка [$name]: $* ---"
  timeout 60 "$XENIA" "$@" >"$out" 2>"$err"
  local rc=$?
  echo "    код завершения: $rc"
  echo "    stdout: $(wc -c <"$out") байт, stderr: $(wc -c <"$err") байт"
  [ -s "$out" ] && head -20 "$out" | sed 's/^/      /'
  [ -s "$err" ] && head -20 "$err" | sed 's/^/      /'
  return $rc
}

run_attempt "01-headless-nogpu" --headless --no_gpu "$TARGET"
run_attempt "02-headless"        --headless "$TARGET"
run_attempt "03-log2"            --log_level=2 --headless --no_gpu "$TARGET"

echo
echo "=== поиск журналов Xenia ==="
found=0
for d in "$LOCALAPPDATA" "$APPDATA" "$HOME" /c/msys64/home/Rapture; do
  [ -d "$d" ] || continue
  find "$d" -maxdepth 4 -iname '*.log' -newermt '-10 minutes' 2>/dev/null \
    | head -10 | while read -r f; do echo "  $f"; done
done
[ -d "$LOGDIR" ] && echo "  наши логи: $LOGDIR"
echo
echo "=== ИТОГ ==="
echo "логи сохранены в: $LOGDIR"
ls -la "$LOGDIR"
