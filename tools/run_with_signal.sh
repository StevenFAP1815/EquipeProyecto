#!/bin/bash
set -u

if [ "$#" -ne 4 ]; then
    echo "Uso: tools/run_with_signal.sh <delivery_sim> <config.json> <segundos_antes_de_SIGINT> <carpeta_de_salida>"
    exit 2
fi

BIN="$1"
CONFIG="$2"
SECONDS_BEFORE_SIGNAL="$3"
OUT="$4"
mkdir -p "$OUT"

"$BIN" "$CONFIG" --log "$OUT/events.log" > "$OUT/stdout.txt" 2> "$OUT/stderr.txt" &
PID=$!

until grep -q "Simulation has started" "$OUT/stdout.txt" 2>/dev/null; do
    sleep 0.1
done
sleep "$SECONDS_BEFORE_SIGNAL"

START=$(date +%s.%N)
kill -INT "$PID"
wait "$PID"
EXIT_CODE=$?
END=$(date +%s.%N)

{
    echo "comando: $BIN $CONFIG --log $OUT/events.log"
    echo "SIGINT enviado a los ${SECONDS_BEFORE_SIGNAL} s de la linea de inicio"
    echo "codigo de salida: $EXIT_CODE"
    echo "segundos desde la senal hasta la salida: $(echo "$END - $START" | bc)"
} > "$OUT/run-info.txt"
cat "$OUT/run-info.txt"
