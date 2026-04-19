#!/usr/bin/env bash
set -euo pipefail

FQBN="esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"

# Sketches available:
#   oled1test      — original single-screen test (no mux)
#   oled1muxtest   — four-screen mux test: channels 0-3, pin-swap detection
#   oled4mux       — full 4-screen mux dashboard
#   justclock      — clock only + 10 s metrics screen every 60 s
SKETCH="${1:-oled1muxtest}"
SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)/$SKETCH"

if [[ ! -d "$SKETCH_DIR" ]]; then
  echo "ERROR: Sketch directory not found: $SKETCH_DIR"
  echo "Usage: ./run.sh [oled1test|oled1muxtest|oled4mux|justclock]"
  exit 1
fi
echo ">>> Sketch: $SKETCH"

# Kill any processes holding a serial port (arduino-cli monitor, screen, minicom, etc.)
echo ">>> Killing any existing serial monitor processes..."
pkill -f "arduino-cli monitor" 2>/dev/null || true
pkill -f "screen /dev/cu"      2>/dev/null || true
pkill -f "minicom"             2>/dev/null || true
sleep 2   # wait for port to fully release before we touch it

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
echo ">>> Upload port: $PORT"
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH_DIR"
echo ">>> Uploaded: $SKETCH"

echo ">>> Done. Waiting for port to re-enumerate..."
sleep 2
# Re-detect port after CDC re-enumerate
PORT2=$(arduino-cli board list 2>/dev/null | awk '/Serial Port \(USB\)/ {print $1}' | head -1)
if [[ -z "$PORT2" ]]; then PORT2="$PORT"; fi
echo ">>> Monitoring serial on $PORT2 (Ctrl+C to quit)..."
arduino-cli monitor --port "$PORT2" --config baudrate=115200
