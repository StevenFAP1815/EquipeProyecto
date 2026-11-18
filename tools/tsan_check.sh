#!/bin/bash
set -u

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Uso: tools/tsan_check.sh <config.json> [segundos_antes_de_SIGINT]"
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="$1"
SECONDS_BEFORE_SIGNAL="${2:-3}"
BUILD="${BUILD_DIR:-$HOME/eq-tsan-check}"
OUT="$BUILD/tsan-check"
mkdir -p "$OUT"

if ! cmake -S "$ROOT" -B "$BUILD" -DENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$OUT/cmake.txt" 2>&1; then
    cat "$OUT/cmake.txt"
    exit 1
fi
cmake --build "$BUILD" -j 8 > "$OUT/build.txt" 2>&1
BUILD_CODE=$?
grep -Ei "warning|error" "$OUT/build.txt"
if [ "$BUILD_CODE" -ne 0 ]; then
    exit 1
fi

"$BUILD/tests/delivery_tests" > "$OUT/tests.txt" 2>&1
TESTS_CODE=$?
tail -2 "$OUT/tests.txt"
echo "avisos de TSan en los tests: $(grep -c 'WARNING: ThreadSanitizer' "$OUT/tests.txt")"

bash "$ROOT/tools/run_with_signal.sh" "$BUILD/delivery_sim" "$CONFIG" "$SECONDS_BEFORE_SIGNAL" "$OUT/run"
echo "avisos de TSan en la simulacion: $(grep -c 'WARNING: ThreadSanitizer' "$OUT/run/stderr.txt")"
python3 "$ROOT/tools/check_event_log.py" "$OUT/run/events.log" "$OUT/run/stdout.txt" | tail -1

exit "$TESTS_CODE"
