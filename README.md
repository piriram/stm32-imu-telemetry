# STM32F103 × MPU6050 IMU 자세 추정 펌웨어

> STM32F103(BluePill) 보드에서 MPU6050 IMU 센서 데이터를 I2C로 읽고,  
> 가속도와 자이로 데이터를 융합해 안정적인 Roll/Pitch 자세각을 계산하는 펌웨어입니다.

## 📌 시스템 구성

```
MPU6050 ──I2C──▶ STM32F103 ──SWD/ST-Link──▶ PC
 (IMU)          (데이터 수집·자세 추정)        (모니터링)
```

## ✅ 구현 현황

| 단계 | 내용 | 상태 |
|------|------|------|
| Phase 1 | I2C 초기화 및 MPU6050 Sleep 해제 (`0x6B` ← `0x00`) | ✅ 완료 |
| Phase 2 | 가속도 + 자이로 14바이트 Burst Read, Raw → 각도 변환 | ✅ 완료 |
| Phase 3 | 상보 필터(Complementary Filter) 적용, Roll/Pitch 안정화 | ✅ 완료 |

---

## 🔩 하드웨어

| 부품 | 사양 | 역할 |
|------|------|------|
| MCU | STM32F103C8T6 (BluePill) | 센서 데이터 수집·자세 추정 |
| IMU | MPU6050 (GY-521) | 6축 가속도 + 자이로 |
| Debugger | ST-Link V2 | 플래시 / SWD 디버깅 |

**핀 매핑**

| 신호 | STM32 핀 | 연결 대상 |
|------|----------|-----------|
| I2C1_SCL | PB6 | MPU6050 SCL |
| I2C1_SDA | PB7 | MPU6050 SDA |

---

## 🧠 핵심 구현

### 1. MPU6050 초기화 — Sleep 모드 해제

```c
// 전원 관리 레지스터(0x6B)에 0x00 Write → Sleep 해제
uint8_t wake = 0x00;
HAL_I2C_Mem_Write(&hi2c1, 0xD0, 0x6B, 1, &wake, 1, 100);
```

> MPU6050은 전원 인가 시 기본적으로 Sleep 상태입니다.  
> 데이터시트 §4.28(PWR_MGMT_1) 참조.

---

### 2. 14바이트 Burst Read — 가속도 + 자이로 동시 취득

```c
// 0x3B: ACCEL_XOUT_H 레지스터부터 14바이트 연속 읽기
// accel(6) + temp(2) + gyro(6) = 14 bytes
HAL_I2C_Mem_Read(&hi2c1, 0xD0, 0x3B, 1, i2c_buf, 14, 100);

// 상위/하위 바이트 결합 (Big-endian)
accel_x = (int16_t)(i2c_buf[0]  << 8 | i2c_buf[1]);
accel_y = (int16_t)(i2c_buf[2]  << 8 | i2c_buf[3]);
accel_z = (int16_t)(i2c_buf[4]  << 8 | i2c_buf[5]);
gyro_x  = (int16_t)(i2c_buf[8]  << 8 | i2c_buf[9]);
gyro_y  = (int16_t)(i2c_buf[10] << 8 | i2c_buf[11]);

// 단위 변환
// 가속도: ÷ 16384 → g (±2g 기본 범위)
// 자이로: ÷ 131   → deg/s (±250°/s 기본 범위)
double gx_rate = gyro_x / 131.0;
double gy_rate = gyro_y / 131.0;
```

---

### 3. 상보 필터 (Complementary Filter)

**문제:** 가속도만으로 각도를 구하면(`atan2`) 충격 시 값이 크게 튐. 자이로는 장기 오차(Drift) 누적.

**해결:** 두 센서의 장점을 융합.

```
가속도 → 노이즈 크지만 장기 정확도 ✓ (4% 반영)
자이로  → 반응 빠르지만 Drift 누적  ✓ (96% 반영)
```

```c
#define ALPHA 0.96f  // 자이로 가중치

// dt: 이전 루프와의 시간 간격 (ms → s)
uint32_t current_tick = HAL_GetTick();
double dt = (current_tick - last_tick) / 1000.0;
last_tick = current_tick;

// 상보 필터 적용
filtered_roll  = ALPHA * (filtered_roll  + gx_rate * dt) + (1.0f - ALPHA) * accel_roll;
filtered_pitch = ALPHA * (filtered_pitch + gy_rate * dt) + (1.0f - ALPHA) * accel_pitch;
```

**검증 방법:** STM32CubeIDE Live Expressions에서 `accel_roll` vs `filtered_roll` 동시 모니터링 →  
충격 인가 시 `accel_roll`은 크게 튀는 반면 `filtered_roll`은 안정적으로 유지됨을 확인.

---

## 🐛 트러블슈팅

### I2C Bus Stuck Low (SDA 0V 고정)

- **증상:** MPU6050 ACK 미수신, I2C 스캐너로 디바이스 미검출
- **원인:** 브레드보드 접촉 불량으로 SDA 라인이 GND에 단락
- **증명:** 소프트웨어 I2C 스캐너로 0x00~0x7F 전 주소 순환 탐색 → 응답 없음으로 하드웨어 문제 확정
- **해결:** 공장 납땜 완제품 보드로 교체 후 즉시 통신 성공

### UART 부재 상황에서의 디버깅

- **상황:** USB-TTL(CH340) 모듈 없어 `printf` 출력 불가
- **해결:** STM32CubeIDE **Live Expressions** 기능 활용 — 칩 내부 전역 변수를 SWD(디버거)로 실시간 감시
- **교훈:** UART 없이도 SWD + Live Expressions만으로 변수 변화량 충분히 검증 가능

---

## 🛠 개발 환경

| 항목 | 내용 |
|------|------|
| MCU | STM32F103C8T6 |
| IDE | STM32CubeIDE 2.x (Mac) |
| HAL | STM32CubeF1 HAL (I2C 초기화) + 레지스터 직접 접근 |
| 디버거 | ST-Link V2 + st-link-server |
| 언어 | C |

---

## 📁 프로젝트 구조

```
project/
├── Core/
│   ├── Inc/
│   │   └── main.h
│   └── Src/
│       └── main.c          ← 센서 수집·자세각 계산·상보 필터
├── Drivers/                ← STM32 HAL 드라이버
└── README.md
```
