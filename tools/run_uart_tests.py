import serial
import time
import os
import sys

port = '/dev/cu.usbserial-0001'
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=0.1)
except Exception as e:
    print(f"Error opening serial port: {e}")
    sys.exit(1)

def read_until_quiet(duration=1.0):
    lines = []
    end_time = time.time() + duration
    while time.time() < end_time:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            lines.append(line)
            print(line)
    return lines

print("=== Starting 60s Telemetry Test ===")
ser.write(b"stream on\r\n")
telemetry_lines = []
start_time = time.time()
while time.time() - start_time < 60:
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if line:
        telemetry_lines.append(line)
        if len(telemetry_lines) % 50 == 0:
            print(f"Collected {len(telemetry_lines)} records...")

with open("docs/validation/uart_60s_session.txt", "w") as f:
    f.write("\n".join(telemetry_lines) + "\n")
print(f"Saved {len(telemetry_lines)} lines to uart_60s_session.txt")

print("\n=== Starting Command Test ===")
command_log = []

def send_cmd(cmd):
    print(f"Sending: {cmd}")
    command_log.append(f"> {cmd}")
    ser.write((cmd + "\r\n").encode())
    time.sleep(0.5)
    resp = read_until_quiet(0.5)
    command_log.extend(resp)

send_cmd("status")
send_cmd("stream off")
send_cmd("hello")
send_cmd("stream on")
send_cmd("1234567890123456789012345678901234567890") # Long command
send_cmd("status")

with open("docs/validation/uart_command_session.txt", "w") as f:
    f.write("\n".join(command_log) + "\n")
print("Saved command session log.")

ser.close()
print("\n[Done] Telemetry and Command tests finished.")
