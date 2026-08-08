# UART 검증 실행 안내

이 폴더는 STM32 IMU Node의 UART 정상, Command, 오류 및 복구 동작을 제3자가 재현할 수 있도록 원본 Log와 분석 결과를 보관한다.

## 1. 준비

- ST-Link V2와 USB-to-TTL을 Multi-port USB Hub에 동시에 연결한다.
- USB-to-TTL TXD는 STM32 PA10(RX), RXD는 PA9(TX), GND는 GND에 교차 연결한다.
- Firmware를 Flash한 뒤 Serial Port를 확인한다.

```bash
python3 -m pip install pyserial
python3 tools/run_uart_tests.py --help
```

Port를 생략했을 때 USB Serial 장치가 하나만 발견되면 자동 선택한다. 여러 장치가 있으면 `--port`를 명시한다.

```bash
python3 tools/run_uart_tests.py \
  --port /dev/cu.usbserial-0001 \
  --duration 60 \
  --burst-test \
  --recovery-test
```

`--recovery-test`는 사람이 현장에서 SDA Signal Jumper를 분리하고 다시 연결해야 하는 대화형 Test다. VCC와 GND는 건드리지 않는다.

60초 Log가 이미 PASS한 뒤 Command나 Recovery만 재시험할 때는 기존 Log를 덮어쓰지 않도록 `--skip-telemetry`를 사용한다.

```bash
python3 tools/run_uart_tests.py \
  --skip-telemetry \
  --burst-test \
  --recovery-test
```

## 2. 자동 생성 산출물

| 파일 | 내용 |
|---|---|
| `uart_60s_session.txt` | 60초 정상 Telemetry 원본 |
| `uart_command_session.txt` | `status`, Stream 제어, Invalid, Too-long, Burst Test |
| `uart_error_recovery.txt` | SDA 분리와 재연결 시 Offline/Recovered 흐름 |
| `evidence/uart_sensor_recovery_pass.png` | 경로 정보를 제거한 Sensor 자동 복구 공개용 Terminal Capture |
| `evidence/mpu6050_pose_validation_3step.png` | 평면·오른쪽 90°·앞쪽 90°의 MPU6050 자세 변화 Capture |

## 3. 60초 Log 분석

```bash
python3 tools/analyze_uart_log.py \
  docs/validation/uart_60s_session.txt \
  --output docs/validation/uart_60s_summary.md \
  --strict
```

분석기는 IMU Record 수, Sequence Gap, Timestamp 간격, 관측 Publish Rate와 Status 분포를 계산한다. `BOOT`, `ACK`, `ERR`, `STATUS`, `OK`는 제어 Record로 분리하며 Parsing 실패로 계산하지 않는다.

## 4. 개인정보와 원본 보존

- 원본 Log에는 이름, Token, 절대경로를 넣지 않는다.
- Screenshot에서는 Terminal 제목의 Username, Serial Port, 절대경로를 Crop 또는 가린다.
- 원본은 덮어쓰지 않고 수정본에 `_cropped` 또는 `_annotated` Suffix를 사용한다.
- Portfolio에는 실측값만 쓰고, 목표값이나 예상값을 결과처럼 쓰지 않는다.
