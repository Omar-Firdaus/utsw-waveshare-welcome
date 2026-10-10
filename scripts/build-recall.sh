#!/bin/sh
# Compile (and optionally flash) the RECALL firmware with the Arduino IDE's bundled CLI.
set -e
cd "$(dirname "$0")/.."
CLI="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc"
BUILD="${BUILD_PATH:-/tmp/recall-build}"

"$CLI" compile --fqbn "$FQBN" --build-path "$BUILD" RECALL
if [ "$1" = "--upload" ]; then
  PORT="${PORT:-$(ls /dev/cu.usbmodem* | head -1)}"
  "$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" RECALL
fi
