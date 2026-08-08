# UART Development and Reproduction Log

## Environment

| Item | Configuration |
|---|---|
| Host | macOS |
| MCU | STM32F103C8T6 Blue Pill |
| Sensor | MPU6050 호환 IMU, WHO_AM_I 0x72 확인 |
| Debug | ST-Link V2, SWD |
| Serial | USB-to-TTL, USART1 115200 8N1 |
| Host USB | Multi-port USB Hub, ST-Link와 TTL 동시 연결 |
| Firmware Branch | `feat/uart-diagnostic-interface` |

## Wiring

### USB-to-TTL → STM32

| USB-to-TTL | STM32 |
|---|---|
| TXD, 보라색 | PA10, RX |
| RXD, 흰색 | PA9, TX |
| GND, 파란색 | GND |
| 5V, 빨간색 | 5V |

### MPU6050 → STM32

| MPU6050 | STM32 |
|---|---|
| VCC, 주황색 | 3.3V |
| GND, 갈색 | GND |
| SCL, 초록색 | PB6 |
| SDA, 노란색 | PB7 |
| AD0, 빨간색 | GND |

## Reproduction Procedure

1. ST-Link와 USB-to-TTL을 USB Hub에 동시에 연결한다.
2. STM32CubeIDE 또는 CMake로 Clean Build한다.
3. ST-Link로 Firmware를 Flash한다.
4. `/dev/cu.*`에서 USB Serial Port를 확인한다.
5. `tools/run_uart_tests.py`로 60초, Command, Burst, Recovery Test를 실행한다.
6. `tools/analyze_uart_log.py`로 60초 Session을 분석한다.
7. Test Matrix의 Actual Result, Status, Evidence를 실측값으로 갱신한다.

## Build Verification

- 2026-08-08: CMake + `arm-none-eabi-gcc` Release Build 성공.
- RAM: 2,352 bytes / 20KB, 11.48%.
- I2C Recovery 수정 후 Flash: 17,180 bytes / 64KB, 26.21%.
- 2026-08-09: STM32CubeProgrammer CLI로 최신 Working Tree HEX를 Flash, Verify, Software Reset 완료.
- Flash Target: STM32F101/F102/F103 Medium-density, Device ID 0x410, 64KB, 3.29V.
- Flash 직후 Smoke Test: Sensor OK, Stream OFF, Overflow 0, Too-long Error 1회, Stream on/off 및 10Hz IMU 정상.
- Smoke Test 첫 IMU의 `t_ms=19,357`로 Reset 후 신규 Firmware 실행을 확인함.

## Hardware Validation Session

- Date/Time: 2026-08-08
- Firmware Commit: 1차 Session은 신규 Parser 수정본 Flash 전 Firmware. 2026-08-09 최신 Working Tree를 다시 Flash함.
- Serial Port는 공개 문서에 기록하지 않음
- 60초 Record 수: 600
- Sequence: 8~607, Gap 0
- Interval Min/Avg/Max: 100/100.00/100ms, 관측 Rate 10.000Hz
- Command Test: status와 stream on/off, Invalid, Too-long, Burst 모두 PASS.
- Offline/Recovery Test: FAIL. Offline 및 Recovered Record가 없고 IMU가 계속 발행되어 SDA 단절 시점이 Capture 구간보다 늦었던 것으로 판단.
- 자세 변화: Roll Range 237.03°, Pitch Range 207.69° 관측.
- 2026-08-09 재시험: 최신 Parser에서 Too-long 1회, 512byte Burst 후 System/IMU 정상, Overflow 0으로 Command/Burst PASS.
- 2026-08-09 1차 Recovery 시험: SDA 단절 시 `ERR,SENSOR_OFFLINE` 1회와 IMU 중지는 확인했으나 재연결 후 Recovery가 발생하지 않음.
- 원인 대응: STM32F1 I2C Peripheral의 BUSY/START 상태가 오류 뒤 잔류할 가능성에 대응해 Offline Probe 전에 `HAL_I2C_DeInit/Init` Software Reset을 추가함.
- 2026-08-09 최종 Recovery 시험: Seq 18 뒤 `ERR,SENSOR_OFFLINE`, SDA 재연결 뒤 `OK,SENSOR_RECOVERED`, 이어서 Seq 19~37 IMU 19건이 정상 발행되어 SENSOR-01~03 PASS.
- Firmware 실물 기능 검증과 공개용 전체 배선/Recovery Capture 정리를 완료했으며, 다음 산출물은 자세 변화 Capture다.
