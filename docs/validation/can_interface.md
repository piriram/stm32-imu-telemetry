# CAN 인터페이스 및 실물 검증 절차

## 구현 범위

- STM32F103 bxCAN, 500 kbit/s, Standard ID, Data Frame
- 기본 Pin: PA11(CAN_RX), PA12(CAN_TX)
- CAN Rx FIFO 0 ISR에서는 Frame을 Software Queue에 저장
- 명령 해석, 상태 변경, UART 진단 출력은 main loop에서 처리
- Bus-Off 발생 시 bxCAN의 Automatic Bus-Off Management 사용

APB1 Clock은 현재 프로젝트 설정인 8MHz를 전제로 한다. 다른 Clock으로 변경되면 CAN 초기화를 실패 처리해 잘못된 Bitrate로 송신하지 않는다. Bit Timing은 Prescaler 1, BS1 12tq, BS2 3tq, SJW 1tq이며 Sample Point는 81.25%다.

현재 `SystemClock_Config()`는 HSI 8MHz를 사용한다. Bench 실물 시험에서 CAN 통신 안정성을 먼저 측정하고, 최종 구성에서는 Board의 HSE Crystal 상태를 확인한 뒤 HSE 전환과 UART/I2C 회귀 시험을 별도 변경으로 다룬다.

## Frame 정의

모든 다중 Byte 값은 Little Endian이다.

### `0x100 IMU_POSE` — STM32 → CANable, 100ms

| Byte | Type | 내용 |
|---|---|---|
| 0~1 | `int16_t` | Roll × 100 |
| 2~3 | `int16_t` | Pitch × 100 |
| 4~5 | `uint16_t` | CAN Sequence |
| 6 | `uint8_t` | Sensor Status |
| 7 | `uint8_t` | Protocol Version (`1`) |

Sensor Status는 `0=OK`, `1=ERR_I2C`, `2=SENSOR_OFFLINE`, `3=RX_OVERFLOW`다.

### `0x200 COMMAND` — CANable → STM32

| Byte 0 | 명령 |
|---|---|
| `0x01` | CAN IMU Stream On |
| `0x02` | CAN IMU Stream Off |
| `0x03` | Status Response 요청 |

STM32 Acceptance Filter는 Standard Data Frame `0x200`만 FIFO 0으로 전달한다.

### `0x201 RESPONSE` — STM32 → CANable

| Byte | 내용 |
|---|---|
| 0 | 수신한 Command |
| 1 | Result: `0=OK`, `1=BAD_DLC`, `2=BAD_COMMAND` |
| 2 | CAN Stream: `0=OFF`, `1=ON` |
| 3 | Sensor Status |
| 4~5 | TX Mailbox Busy 횟수 |
| 6~7 | RX Queue/FIFO Overflow 횟수 |

## 배선 전 확인

사용 중인 Transceiver Module의 회로와 전압 조건을 먼저 확인한다. TJA1050 계열 Module은 제품별로 전원, Logic Level 변환, 종단저항 구성에 차이가 있을 수 있으므로 Module 판매 페이지 또는 회로도 확인 없이 STM32와 직접 연결하지 않는다.

- STM32 PA12 → Transceiver TXD
- STM32 PA11 ← Transceiver RXD
- STM32와 Transceiver GND 공통
- Transceiver CANH/CANL ↔ CANable CANH/CANL
- 전원을 끈 상태에서 CANH-CANL 합성 종단저항을 측정하고, 양 끝 120Ω 두 개가 있는 구성에서는 약 60Ω인지 확인

## Mac CANable 도구

CANable이 SLCAN Firmware로 동작한다는 전제다. Python 도구는 `python-can`의 SLCAN Backend를 사용한다.

```bash
python3 -m pip install -r requirements-can.txt
ls /dev/cu.*
python3 tools/can_node_tool.py /dev/cu.usbmodem101 monitor --duration 60
python3 tools/can_node_tool.py /dev/cu.usbmodem101 off
python3 tools/can_node_tool.py /dev/cu.usbmodem101 on
python3 tools/can_node_tool.py /dev/cu.usbmodem101 status
```

설치 및 Port 이름은 현재 Mac 환경에 맞춰 조정한다. `python-can` SLCAN Backend는 Serial Channel, CAN Bitrate, Serial Baudrate를 별도로 받는다.

## 실물 완료 기준

- [ ] 부팅 UART에 `BOOT,OK,CAN_500K_READY` 출력
- [ ] CANable에서 `0x100`, DLC 8, 100ms 주기 확인
- [ ] 60초 600건 수신 및 `sequence_gaps=0`
- [ ] Roll/Pitch UART 값과 CAN Decode 값 일치
- [ ] `off` 전송 후 `0x201 OK` 응답 및 `0x100` 정지
- [ ] `on` 전송 후 `0x201 OK` 응답 및 `0x100` 재개
- [ ] `status` 전송 후 Sensor/Stream/오류 Counter 확인
- [ ] 미정의 명령에 `BAD_COMMAND` 응답
- [ ] UART `status`에서 CAN ESR, TX Busy, RX Overflow 기록

코드 빌드 성공은 위 실물 완료 기준을 대신하지 않는다. 포트폴리오에는 CANable Trace와 UART ACK가 확보된 뒤에만 CAN을 완료 기술로 표기한다.
