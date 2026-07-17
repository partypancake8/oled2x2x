import serial, time, sys
port = sys.argv[1]
try:
    s = serial.Serial()
    s.port = port
    s.baudrate = 1200
    s.open()
    s.dtr = False
    s.rts = False
    time.sleep(0.12)
    s.close()
    print("1200bps touch sent to", port)
except Exception as e:
    print("touch error:", e)
