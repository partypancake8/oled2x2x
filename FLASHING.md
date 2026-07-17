# Flashing the sealed 4-square (Feather ESP32-S3)

The 4-square box is sealed, so there is no access to the BOOT or RESET buttons.
The Feather ESP32-S3 also uses native USB (no USB-to-serial chip), and esptool's
normal "reset via RTS pin" does not actually reset this board. That means the
usual `arduino-cli upload` / `run.sh` flow gets stuck at "Connecting....." forever,
and even when a flash does succeed the chip stays in the ROM bootloader and the
sketch never starts (screens stay dark).

This note is the procedure that works without touching any buttons.

## The trick, in short

1. **1200-baud touch** to drop the running sketch into the ROM bootloader
   (works because the Arduino core's USB CDC reboots to bootloader when the host
   opens the port at 1200 baud, even if the main loop is locked up).
2. **`esptool ... --before no-reset write-flash ...`** to write the binaries while
   it sits in the bootloader.
3. **`esptool ... --after watchdog-reset`** to actually reboot into the app
   (RTS/hard-reset does not work on this board; the RTC watchdog reset does).

You can confirm which mode the board is in by its USB product name:
- `USB JTAG_serial debug unit`  = stuck in the ROM bootloader (app not running)
- `Feather ESP32_S3`            = the sketch is running

Check it with:
```bash
ioreg -p IOUSB -l -w 0 | grep -i '"USB Product Name"' | grep -iE 'Feather|JTAG'
```

## One-time setup

pyserial (for the touch) lives in the repo venv:
```bash
.venv/bin/pip install pyserial
```

Paths used below (adjust the esptool version if the core updates):
```
ESPTOOL=$HOME/Library/Arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool
VPY=$HOME/Active/GitHub/oled2x2x/.venv/bin/python3
BOOT0=$HOME/Library/Arduino15/packages/esp32/hardware/esp32/3.3.7/tools/partitions/boot_app0.bin
FQBN="esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"
```

## The 1200-baud touch script (touch.py)

```python
import serial, time, sys
port = sys.argv[1]
s = serial.Serial()
s.port = port
s.baudrate = 1200
s.open()
s.dtr = False
s.rts = False
time.sleep(0.12)
s.close()
# A "Device not configured" error here is fine — it means the board already
# rebooted into the bootloader before the port could close.
```

## Full flash steps

Replace `SKETCH` with the sketch folder name (e.g. `foursquareclock`).

```bash
SKETCH=foursquareclock
PORT=$(ls /dev/cu.usbmodem* | head -1)

# 1. compile
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

# find the build cache dir for this sketch
DIR=$(for d in "$HOME/Library/Caches/arduino/sketches"/*/; do \
  grep -q "$SKETCH" "$d/build.options.json" 2>/dev/null && echo "$d"; done)

# 2. touch into the bootloader
"$VPY" touch.py "$PORT"; sleep 3
PORT=$(ls /dev/cu.usbmodem* | head -1)

# 3. write the binaries (no-reset so it stays in the bootloader)
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 --before no-reset --after hard-reset \
  write-flash -z --flash-mode keep --flash-freq keep --flash-size keep \
  0x0    "$DIR/$SKETCH.ino.bootloader.bin" \
  0x8000 "$DIR/$SKETCH.ino.partitions.bin" \
  0xe000 "$BOOT0" \
  0x10000 "$DIR/$SKETCH.ino.bin"

# 4. boot into the app (RTS reset won't do it; watchdog reset will)
"$ESPTOOL" --chip esp32s3 --port "$PORT" --before no-reset --after watchdog-reset flash-id

# 5. confirm it's running the sketch
ioreg -p IOUSB -l -w 0 | grep -i '"USB Product Name"' | grep -iE 'Feather|JTAG'
```

If step 5 still shows `USB JTAG_serial debug unit`, just unplug and replug the USB
cable. A full power cycle clears the one-shot download flag and boots the sketch.

## Verifying WiFi / time sync

The clock sketches sync time over WiFi via NTP. To confirm the board joined the
network (and therefore can sync NTP), look it up in the ARP table by MAC:
```bash
arp -a | grep -i b8:f8:62
# -> esp32s3-d5ca3c.lan (192.168.86.69) at b8:f8:62:d5:ca:3c
```
If it has an IP, NTP will sync within a few seconds and the time will be accurate.

## Sketch reference

- `foursquareclock` — clock only, 2x2 mux. Hours + minutes/seconds on the top row,
  day-of-week + date on the bottom row. Same clock code as `oled1muxtest` with the
  screen rotation locked to the clock set. WiFi: Garland.
- `oled1muxtest` — full 2x2 dashboard that rotates through 3 sets (clock, then two
  animation/info sets).
- `justclock` — single OLED only, does NOT drive the mux, so it shows nothing on
  the 4-square. Do not flash this one to the box.
