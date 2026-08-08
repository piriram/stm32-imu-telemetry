# STM32F103 IMU 자세 추정 노드

STM32F103C8(Blue Pill)과 MPU6050 IMU 센서를 이용한 자세 추정 펌웨어.  
상보 필터(Complementary Filter)를 직접 구현하여 Roll/Pitch 각도를 실시간으로 추정하고, UART 텔레메트리로 전송합니다.

---

## 개요

| 항목 | 내용 |
|------|------|
| MCU | STM32F103C8T6 (Blue Pill) |
| 센서 | MPU6050 (가속도계 + 자이로스코프) |
| 통신 인터페이스 | I2C1 (센서), USART1 (텔레메트리/콘솔) |
| 필터 알고리즘 | 상보 필터 (α = 0.96) |
| 센서 샘플링 | 20 Hz |
| 텔레메트리 출력 | 10 Hz |
| Baud Rate | 115200 bps |
| 개발 환경 | STM32CubeIDE, STM32 HAL |

---

## 핵심 구현: 상보 필터

가속도 센서와 자이로 센서는 각각 치명적인 단점이 있습니다.

- **가속도 센서**: 시간이 지나도 절대 기준(중력 방향)을 잃지 않지만, 진동/이동 시 노이즈가 심하게 튑니다.
- **자이로 센서**: 단기적으로는 매우 부드럽지만, 시간이 지날수록 오차가 누적되는 드리프트(drift)가 발생합니다.

상보 필터는 두 센서의 장점만 취합니다.

```c
// mpu6050.c
// 96%는 자이로 적분값(빠른 응답, 드리프트 있음)을 신뢰하고,
// 4%는 가속도 절대 각도(느리지만 정확한 기준)로 보정합니다.
Filtered_Roll  = 0.96 * (Filtered_Roll  + gx_rate * dt) + 0.04 * Accel_Roll;
Filtered_Pitch = 0.96 * (Filtered_Pitch + gy_rate * dt) + 0.04 * Accel_Pitch;
```

---

## 소프트웨어 구조

```
Core/
├── Inc/
│   ├── mpu6050.h       # MPU6050 드라이버 인터페이스 & 데이터 구조체
│   ├── telemetry.h     # IMU_Sample_t 구조체, 텔레메트리 API
│   └── uart_console.h  # 링 버퍼 기반 UART 콘솔 인터페이스
└── Src/
    ├── main.c          # 메인 루프 (non-blocking 태스크 스케줄링)
    ├── mpu6050.c       # I2C 드라이버 + 상보 필터 구현
    ├── telemetry.c     # CSV 포맷 텔레메트리 직렬화 & 전송
    └── uart_console.c  # 링 버퍼 RX, 인터럽트 기반 커맨드 처리
```

### 메인 루프 태스크 구조

`while(1)` 내부는 `HAL_GetTick()`을 이용한 non-blocking 방식으로 태스크를 분리합니다.

| 태스크 | 주기 | 설명 |
|--------|------|------|
| UART 커맨드 처리 | 매 루프 | 링 버퍼에서 수신 바이트 소비 |
| 센서 샘플링 | 50ms (20Hz) | MPU6050 Burst Read + 상보 필터 |
| 센서 재연결 프로브 | 1000ms (1Hz) | 센서 오프라인 감지 시 재초기화 시도 |
| 텔레메트리 전송 | 100ms (10Hz) | CSV 포맷 데이터 UART 출력 |
| Heartbeat LED | 100ms (10Hz) | PC13 토글 (정상 동작 확인) |

---

## 텔레메트리 프로토콜

UART 출력은 CSV 포맷입니다. 터미널(PuTTY, CoolTerm 등)에서 바로 확인할 수 있습니다.

**부팅 메시지**
```
BOOT,OK,MPU6050_FOUND
```

**IMU 데이터 스트림** (`stream on` 커맨드로 활성화)
```
IMU,<seq>,<t_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<roll_cdeg>,<pitch_cdeg>,<status>
```

| 필드 | 설명 |
|------|------|
| `seq` | 프레임 순번 (손실 감지용) |
| `t_ms` | MCU 부팅 후 경과 시간 (ms) |
| `ax/ay/az` | 가속도 Raw 값 (16-bit) |
| `gx/gy/gz` | 자이로 Raw 값 (16-bit) |
| `roll_cdeg` | Roll 각도 × 100 (정수 전송) |
| `pitch_cdeg` | Pitch 각도 × 100 (정수 전송) |
| `status` | `OK` / `SENSOR_OFFLINE` |

**센서 오류 메시지**
```
ERR,SENSOR_OFFLINE
OK,SENSOR_RECOVERED
```

---

## UART 콘솔 커맨드

115200bps 터미널에서 아래 커맨드를 입력할 수 있습니다.

| 커맨드 | 응답 | 설명 |
|--------|------|------|
| `stream on` | `ACK,STREAM,ON` | 텔레메트리 스트림 시작 |
| `stream off` | `ACK,STREAM,OFF` | 텔레메트리 스트림 중지 |
| `status` | `STATUS,sensor=OK,stream=ON,...` | 현재 노드 상태 확인 |

---

## 하드웨어 연결

| MPU6050 핀 | STM32 핀 | 설명 |
|-----------|----------|------|
| VCC | 3.3V | 전원 |
| GND | GND | 공통 접지 |
| SDA | PB7 (I2C1_SDA) | I2C 데이터 |
| SCL | PB6 (I2C1_SCL) | I2C 클럭 |
| AD0 | GND | I2C 주소 0x68 선택 |

| UART | STM32 핀 | 설명 |
|------|----------|------|
| TX | PA9 (USART1_TX) | 텔레메트리 출력 |
| RX | PA10 (USART1_RX) | 커맨드 수신 |

### Host USB 연결

Multi-port USB Hub를 사용해 ST-Link V2와 USB-to-TTL을 Mac에 동시에 연결합니다.

| USB 장비 | 역할 |
|----------|------|
| ST-Link V2 | Firmware Flash, SWD Debug |
| USB-to-TTL | 115200 8N1 Telemetry 수신, UART Command 입력 |

두 장비를 물리적으로 동시에 연결해 둔 상태에서 Flash와 UART 검증을 이어서 수행할 수 있습니다. 단, STM32CubeIDE와 STM32CubeMonitor는 하나의 ST-Link를 동시에 점유할 수 없으므로 CubeMonitor 사용 전 IDE Debug Session을 종료해야 합니다.

---

## 구현 포인트 요약

1. **MPU6050 드라이버 직접 구현** — HAL 라이브러리 없이 I2C 레지스터 맵 직접 조작 (WHO_AM_I 확인, Sleep 해제, Burst Read)
2. **상보 필터** — 자이로 드리프트와 가속도 노이즈를 상호 보완하는 1차 상보 필터 직접 구현
3. **링 버퍼 기반 UART RX** — 인터럽트 콜백에서 링 버퍼에 수신 바이트를 쌓고, 메인 루프에서 비동기적으로 소비
4. **Non-blocking 멀티태스킹** — `HAL_GetTick()` 기반 주기별 태스크 분리 (RTOS 없이 협력적 스케줄링)
5. **센서 장애 복구** — 센서 오프라인 감지 후 1Hz 주기로 재연결 시도하는 상태 머신

---

## 빌드 방법

STM32CubeIDE에서 프로젝트를 Import 후 빌드합니다.

```
File → Import → Existing Projects into Workspace → 이 폴더 선택
```

또는 CMakeLists.txt를 이용한 CLI 빌드도 가능합니다.
