#!/usr/bin/env bash
# Compile justclockc3 and flash it to the C3 4-square OVER WIFI (no cable needed).
# Device: justclock-c3 @ 192.168.86.56 (DHCP — falls back to MAC lookup if it moved)
set -e
cd "$(dirname "$0")"

IP=192.168.86.56
ping -c1 -W2 "$IP" >/dev/null 2>&1 || {
  IP=$(ip neigh | grep -i "ac:a7:04:d6:75:20" | awk '{print $1}' | head -1)
  [ -z "$IP" ] && { echo "C3 not found on LAN (is it powered?)"; exit 1; }
  echo "IP moved, using $IP"
}

~/.local/bin/arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc .

DIR=$(for d in "$HOME/.cache/arduino/sketches"/*/; do
  grep -q justclockc3 "$d/build.options.json" 2>/dev/null && echo "$d"; done | head -1)

OTAP=$(grep OTA_PASS secrets.h | cut -d'"' -f2)
python3 ~/.arduino15/packages/esp32/hardware/esp32/3.3.10/tools/espota.py \
  -i "$IP" -p 3232 --auth="$OTAP" -f "$DIR/justclockc3.ino.bin"
echo "OTA flash sent — clock reboots itself in a few seconds."
