import sys, time
import serial

port = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 15

with serial.Serial(port, 115200, timeout=1) as s:
    s.dtr = False          # IO0 high: normaler Boot
    s.rts = True           # EN low: Reset
    time.sleep(0.1)
    s.rts = False
    end = time.time() + secs
    while time.time() < end:
        line = s.readline()
        if line:
            print(line.decode(errors="replace").rstrip(), flush=True)
