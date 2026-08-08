# UART Interface Specification

## Physical and Serial Configuration

| Item | Value |
|---|---|
| Peripheral | USART1 |
| STM32 TX | PA9 |
| STM32 RX | PA10 |
| Baud Rate | 115200 bps |
| Frame | 8N1 |
| Flow Control | None |
| Command Terminator | CR, LF 또는 CRLF |
| RX Method | 1byte Interrupt + 64byte Software Ring Buffer |
| Command Line Buffer | 32byte |

ST-Link V2는 Flash/SWD Debug, USB-to-TTL은 UART Log/Command에 사용하며 Multi-port USB Hub를 통해 동시에 물리적으로 연결할 수 있다.

## Boot and State Records

```text
BOOT,INFO,SCANNING_I2C_BUS...
BOOT,INFO,FOUND_I2C_AT_0x68_WHOAMI_0x72
BOOT,OK,MPU6050_FOUND
BOOT,ERR,NO_I2C_DEVICES_FOUND
BOOT,ERR,DEVICE_NOT_FOUND
ERR,SENSOR_OFFLINE
OK,SENSOR_RECOVERED
```

## Telemetry Record

```text
IMU,<seq>,<t_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<roll_cdeg>,<pitch_cdeg>,<status>
```

| Field | Type | Meaning |
|---|---|---|
| `seq` | uint32 | 실제 발행한 IMU Record마다 1 증가 |
| `t_ms` | uint32 | MCU Boot 후 경과시간, ms |
| `ax`, `ay`, `az` | int16 | 가속도 Raw 3축 |
| `gx`, `gy`, `gz` | int16 | 자이로 Raw 3축 |
| `roll_cdeg` | int16 | Roll × 100, 0.01° 단위 |
| `pitch_cdeg` | int16 | Pitch × 100, 0.01° 단위 |
| `status` | string | 현재 정상 발행에서는 `OK` |

Sensor는 20Hz로 읽고 Telemetry는 10Hz로 발행한다. `stream off` 상태에서는 IMU Record를 발행하지 않으며 Sequence도 증가하지 않는다.

## Command and Response

| Command | Response | State Change |
|---|---|---|
| `status` | `STATUS,sensor=<OK/OFFLINE>,stream=<ON/OFF>,rate=10,overflow=<n>` | 없음 |
| `stream on` | `ACK,STREAM,ON` | Telemetry 활성화 |
| `stream off` | `ACK,STREAM,OFF` | Telemetry 비활성화 |
| 정의되지 않은 명령 | `ERR,INVALID_COMMAND` | 없음 |
| 31자를 초과하는 명령 | `ERR,COMMAND_TOO_LONG` | 현재 Line을 개행까지 폐기 |

Command는 소문자 정확 일치다. ISR은 Byte Enqueue와 다음 RX 재무장전만 담당하고 문자열 비교, 응답 전송, System State 변경은 main context에서 수행한다.

## RX Overflow Policy

Ring Buffer가 가득 차면 새 Byte를 Drop하고 `overflow_count`를 증가시킨다. `status` 응답에서 Counter를 확인할 수 있다. Overflow 발생 여부는 시험 부하에 따라 결정되며, Counter가 증가하지 않았다면 발생했다고 주장하지 않는다.
