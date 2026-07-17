#!/usr/bin/env bash
# Flash the sealed 4-square (Feather ESP32-S3) from the Linux PC.
# Linux port of FLASHING.md: 1200-baud touch -> no-reset write -> watchdog-reset boot.
# Usage: ./flash-linux.sh [sketch]   (default: foursquareclock)
set -e
cd "$(dirname "$0")"

SKETCH=${1:-foursquareclock}
ESPTOOL=$HOME/.arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool
BOOT0=$HOME/.arduino15/packages/esp32/hardware/esp32/3.3.10/tools/partitions/boot_app0.bin
FQBN="esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"
VPY=$HOME/flowclock/.venv/bin/python3

PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
[ -z "$PORT" ] && { echo "No /dev/ttyACM* — data cable not detected"; exit 1; }

~/.local/bin/arduino-cli compile --fqbn "$FQBN" "$SKETCH"

DIR=$(for d in "$HOME/.cache/arduino/sketches"/*/; do
  grep -q "$SKETCH" "$d/build.options.json" 2>/dev/null && echo "$d"; done | head -1)
[ -z "$DIR" ] && { echo "build dir not found"; exit 1; }

"$VPY" ../touch.py "$PORT" 2>/dev/null || "$VPY" touch.py "$PORT" || true
sleep 3
PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)

"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 --before no-reset --after hard-reset \
  write-flash -z --flash-mode keep --flash-freq keep --flash-size keep \
  0x0     "$DIR/$SKETCH.ino.bootloader.bin" \
  0x8000  "$DIR/$SKETCH.ino.partitions.bin" \
  0xe000  "$BOOT0" \
  0x10000 "$DIR/$SKETCH.ino.bin"

# RTS reset doesn't boot this board; the watchdog reset does
"$ESPTOOL" --chip esp32s3 --port "$PORT" --before no-reset --after watchdog-reset flash-id >/dev/null

sleep 2
lsusb | grep -i 303a || true
echo "If lsusb still shows 'JTAG serial debug unit', unplug/replug the box once."
