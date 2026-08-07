import serial, time
try:
    ser = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=1)
    print("Listening for 60 seconds...")
    end = time.time() + 60
    while time.time() < end:
        line = ser.readline()
        if line:
            print(f"RECEIVED: {line.decode('utf-8', 'replace').strip()}")
            if b"MPU6050_FOUND" in line or b"SENSOR_RECOVERED" in line or b"FOUND" in line:
                print("SUCCESS! Sending stream on command!")
                ser.write(b'\r\nstream on\r\n')
    print("Finished listening.")
except Exception as e:
    print(f"Error: {e}")
