# UART Validation Test Matrix

> Status: 2026-08-09 실물 검증 완료. 60초 Telemetry, Command/Burst, Sensor 단절 및 자동 복구가 모두 PASS했다.

| ID | Scenario | Procedure | Expected Result | Actual Result | Status | Evidence |
|---|---|---|---|---|---|---|
| UART-TX-01 | 60초 정상 Telemetry | `stream on`, 60초 수집 | 약 10Hz, Sequence 연속, Status OK | 600건, Seq 8~607, Gap 0, Min/Avg/Max 100ms, 10.000Hz, OK 600 | PASS | `uart_60s_session.txt`, `uart_60s_summary.md` |
| UART-TX-02 | Stream 중지 | `stream off`, 1초 관찰 | ACK 후 IMU 발행 중지 | `ACK,STREAM,OFF`, 관찰 구간 IMU 0건 | PASS | `uart_command_session.txt` |
| UART-TX-03 | Stream 재개 | `stream on`, 1초 관찰 | ACK 후 IMU 발행 재개 | ACK 후 Seq 617부터 IMU 재개 | PASS | `uart_command_session.txt` |
| UART-RX-01 | 상태 조회 | `status` | Sensor, Stream, Rate, Overflow 응답 | `sensor=OK,stream=ON,rate=10,overflow=0` | PASS | `uart_command_session.txt` |
| UART-RX-02 | Invalid Command | `hello` | `ERR,INVALID_COMMAND` | 기대 응답 1회 | PASS | `uart_command_session.txt` |
| UART-RX-03 | Too-long Command | 31자 초과 Line 전송 | `ERR,COMMAND_TOO_LONG` 1회, Line 폐기 | 최신 Source Flash 후 Smoke Test에서 `ERR,COMMAND_TOO_LONG` 1회만 출력 | PASS | 2026-08-09 Flash 직후 Smoke Test |
| UART-RX-04 | Burst Input | 512byte 연속 전송 후 `status` | System 정상, Overflow Counter 관측 가능 | Too-long 1회, Invalid 추가 없음, System과 10Hz IMU 정상 유지, Counter 0 | PASS | `uart_command_session.txt` |
| SENSOR-01 | 실행 중 Sensor 단절 | Streaming 중 SDA Signal 분리 | `ERR,SENSOR_OFFLINE` 1회 | Seq 18 이후 `ERR,SENSOR_OFFLINE` 1회, 이후 IMU 발행 중지 | PASS | `uart_error_recovery.txt`, `evidence/uart_sensor_recovery_pass.png` |
| SENSOR-02 | Sensor 자동 복구 | SDA 재연결 후 대기 | 1Hz Probe 후 `OK,SENSOR_RECOVERED` | SDA 재연결 후 `OK,SENSOR_RECOVERED` 1회 확인 | PASS | `uart_error_recovery.txt`, `evidence/uart_sensor_recovery_pass.png` |
| SENSOR-03 | 복구 후 회귀 | Recovered 이후 IMU 관찰 | 정상 IMU 발행 재개, 오래된 값 정상값 오인 없음 | Recovered 직후 Seq 19~37 IMU 19건 정상 발행, 단절 전 Seq 18에서 연속 | PASS | `uart_error_recovery.txt`, `evidence/uart_sensor_recovery_pass.png` |
| REG-01 | 기존 자세각 회귀 | 보드 자세 변경 | Roll/Pitch 방향과 값 변화 정상 | Roll Range 237.03°, Pitch Range 207.69°로 자세 변화 관측 | PASS | `uart_60s_session.txt` |

## 판정 원칙

- 목표 10Hz는 약 100ms 간격이라는 설계값이다. 최종 PASS는 `uart_60s_summary.md`의 실측 분포를 확인한 뒤 결정한다.
- Sequence Gap, 중복 또는 역순, Parsing 실패가 있으면 원인을 분석하기 전 PASS로 바꾸지 않는다.
- Burst Input에서 Overflow를 재현하지 못해도 System이 정상 유지됐다는 결과와 시험 부하는 기록할 수 있다. Counter 증가를 관찰하지 않았다면 Overflow 발생으로 쓰지 않는다.
- Sensor 단절 시험에서는 VCC와 GND가 아니라 SDA 같은 Signal Jumper만 다룬다.
