#!/bin/sh
# Compile (and optionally flash) the RECALL firmware with the Arduino IDE's bundled CLI.
#   scripts/build-recall.sh [--bench] [--upload]
# --bench records to internal flash and adds serial test commands. Never use it with participants.
set -e
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc"
BENCH=0
UPLOAD=0
for arg in "$@"; do
  case "$arg" in
    --bench) BENCH=1 ;;
    --upload) UPLOAD=1 ;;
    *) echo "usage: $0 [--bench] [--upload]" >&2; exit 2 ;;
  esac
done
BUILD="${BUILD_PATH:-/tmp/recall-build-$BENCH}"

"$CLI" compile --fqbn "$FQBN" --build-path "$BUILD" \
  --build-property "compiler.cpp.extra_flags=-DRECALL_BENCH=$BENCH" \
  --build-property "compiler.c.extra_flags=-DRECALL_BENCH=$BENCH" RECALL
if [ "$UPLOAD" = 1 ]; then
  PORT="${PORT:-$(ls /dev/cu.usbmodem* | head -1)}"
  "$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" RECALL
fi
