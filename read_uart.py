import serial
import time
import sys
try:
    ser = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=1)
    ser.write(b'status\r\n')
    start_time = time.time()
    while time.time() - start_time < 3:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', 'replace').strip())
except Exception as e:
    print(f"Error: {e}")
