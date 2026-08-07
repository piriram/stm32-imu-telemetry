# STM32F103 × MPU6050 IMU 자세 추정 펌웨어

STM32F103C8T6에서 MPU6050의 6축 IMU 데이터를 I2C로 수집하고, 상보 필터로 Roll/Pitch 자세각을 계산하는 펌웨어입니다. 센서 드라이버를 메인 루프와 분리했으며, 필터 적용 전·후 값을 UART 또는 SWD 기반 디버깅으로 비교할 수 있습니다.

## 핵심 기능

- MPU6050 Sleep 모드 해제 및 I2C 레지스터 접근
- 가속도계·자이로스코프 14바이트 Burst Read
- 16비트 Raw Data 복원 및 Roll/Pitch 계산
- 가속도 Noise와 자이로 Drift를 보완하는 상보 필터
- UART 시리얼 출력 및 STM32CubeIDE Live Expressions 디버깅
- CMake 기반 ARM Cross Compile와 GitHub Actions 자동 빌드

## 시스템 구성

```text
MPU6050 ──I2C 100 kHz──▶ STM32F103C8T6 ──UART 115200 bps──▶ PC
  IMU                      자세각 계산                 시리얼 모니터링
                                │
                                └──SWD/ST-Link──▶ Live Expressions
```

## 구현 현황

| 단계 | 내용 | 상태 |
|---|---|---|
| Phase 1 | I2C 초기화 및 MPU6050 Sleep 해제 | ✅ 완료 |
| Phase 2 | 가속도·자이로 14바이트 Burst Read 및 각도 변환 | ✅ 완료 |
| Phase 3 | 상보 필터 적용 및 출력 값 비교 | ✅ 완료 |

## 하드웨어

| 부품 | 사양 | 역할 |
|---|---|---|
| MCU | STM32F103C8T6 (BluePill) | 센서 데이터 수집·자세 추정 |
| IMU | MPU6050 (GY-521) | 3축 가속도·3축 자이로 측정 |
| Debugger | ST-Link V2 | Firmware Flash·SWD 디버깅 |
| Serial | USB-to-TTL | UART 데이터 모니터링 |

### 핀 매핑

| 신호 | STM32 핀 | 연결 대상 |
|---|---|---|
| I2C1_SCL | PB6 | MPU6050 SCL |
| I2C1_SDA | PB7 | MPU6050 SDA |
| USART1_TX | PA9 | USB-to-TTL RX |
| USART1_RX | PA10 | USB-to-TTL TX |
| Heartbeat LED | PC13 | BluePill Onboard LED |

## 소프트웨어 구조

```text
main.c
  ├── HAL·Clock·GPIO·I2C·UART 초기화
  ├── MPU6050 Driver 호출
  └── 20 Hz 주기 UART 출력

mpu6050.c
  ├── PWR_MGMT_1 설정
  ├── 14-byte Burst Read
  ├── Raw Data 복원·자세각 계산
  └── Complementary Filter
```

## 핵심 구현

### 1. MPU6050 초기화

MPU6050은 전원 인가 후 Sleep 상태이므로 `PWR_MGMT_1(0x6B)` 레지스터에 `0x00`을 기록합니다.

```c
uint8_t wake_command = 0x00;

HAL_I2C_Mem_Write(
    hi2c,
    MPU6050_ADDR,
    MPU6050_REG_PWR_MGMT_1,
    I2C_MEMADD_SIZE_8BIT,
    &wake_command,
    1,
    100
);
```

### 2. 14바이트 Burst Read

`ACCEL_XOUT_H(0x3B)`부터 가속도 6바이트, 온도 2바이트, 자이로 6바이트를 한 번에 읽습니다. I2C 통신에 실패하면 해당 주기의 자세각 갱신을 중단합니다.

```c
uint8_t received_data[14] = {0};

if (HAL_I2C_Mem_Read(
        hi2c,
        MPU6050_ADDR,
        MPU6050_REG_ACCEL_XOUT_H,
        I2C_MEMADD_SIZE_8BIT,
        received_data,
        sizeof(received_data),
        100
    ) != HAL_OK)
{
    return;
}
```

### 3. 상보 필터

가속도 기반 각도는 장기적으로 안정적이지만 진동에 민감하고, 자이로 적분값은 반응이 빠르지만 Drift가 누적됩니다. 두 값을 96:4 비율로 결합해 단기 응답성과 장기 안정성을 보완했습니다.

```c
filtered_angle =
    0.96 * (previous_angle + gyro_rate * delta_time)
    + 0.04 * accel_angle;
```

## 트러블슈팅

### I2C Bus Stuck Low

- **증상:** MPU6050 ACK 미수신, I2C 주소 탐색에서 Device 미검출
- **분리:** 전체 7-bit 주소에서 응답이 없음을 확인해 소프트웨어 레지스터 설정과 물리 계층 문제를 분리
- **원인:** Breadboard 접촉 불량으로 SDA Line이 GND에 단락
- **해결:** Pin Soldering 완료 Board로 교체한 후 I2C 통신 복구

### UART 없이 센서값 확인

초기 개발 단계에서는 USB-to-TTL 장비 없이 ST-Link와 STM32CubeIDE Live Expressions를 사용해 Raw Data와 필터 출력을 실시간으로 관찰했습니다. 이후에는 USART1 `printf` Redirection을 추가해 필터 적용 전·후 값을 시리얼 로그로 비교하도록 확장했습니다.

## Build

ARM GNU Toolchain과 CMake가 설치된 환경에서 다음과 같이 빌드합니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

빌드가 완료되면 `build/` 디렉터리에 ELF, HEX, BIN 파일이 생성됩니다.

## 개발 환경

| 항목 | 내용 |
|---|---|
| MCU | STM32F103C8T6 |
| IDE | STM32CubeIDE 2.x |
| Configuration | STM32CubeMX `.ioc` |
| Firmware Library | STM32CubeF1 HAL |
| Build | CMake, ARM GNU Toolchain |
| Language | C11 |

## 프로젝트 구조

```text
.
├── .github/workflows/build.yml      # CI Cross Build
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   └── mpu6050.h              # Register Map·Driver Interface
│   ├── Src/
│   │   ├── main.c                 # Peripheral Initialization·Main Loop
│   │   └── mpu6050.c              # Sensor Read·Attitude Estimation
│   └── Startup/                    # Cortex-M3 Startup
├── Drivers/                        # CMSIS·STM32F1 HAL
├── CMakeLists.txt
├── STM32F103C8TX_FLASH.ld
├── stm32_imu_attitude.ioc
└── README.md
```
