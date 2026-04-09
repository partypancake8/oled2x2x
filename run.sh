#!/usr/bin/env bash
set -euo pipefail

FQBN="esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"
SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)/oled4mux"

# Auto-detect port via arduino-cli (most reliable)
PORT=$(arduino-cli board list 2>/dev/null | awk '/Serial Port \(USB\)/ {print $1}' | head -1)
# Fallback: glob common macOS USB-serial names
if [[ -z "$PORT" ]]; then
  PORT=$(ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* /dev/cu.usbserial* 2>/dev/null | head -1 || true)
fi
if [[ -z "$PORT" ]]; then
  echo "ERROR: No ESP32 serial port found. Plug in the board and try again."
  exit 1
fi
echo ">>> Port: $PORT"

echo ">>> Compiling..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"

echo ">>> Uploading..."
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH_DIR"

echo ">>> Done. Monitoring serial (Ctrl+C to quit)..."
arduino-cli monitor --port "$PORT" --config baudrate=115200
