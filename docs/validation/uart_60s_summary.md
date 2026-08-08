# UART 60초 Telemetry 분석 요약

- Source: `/Users/piri/code/portfolio_piri/11_Portfolio/01_gpio_uart_blink/docs/validation/uart_60s_session.txt`
- Valid IMU Records: **600**
- First Sequence: **8**
- Last Sequence: **607**
- Missing Records: **0**
- Sequence Continuity: **PASS**
- Malformed IMU Lines: **0**
- Parsing: **PASS**
- Interval Minimum: **100 ms**
- Interval Average: **100.00 ms**
- Interval Maximum: **100 ms**
- Observed Publish Rate: **10.000 Hz**

## Status 분포

- `OK`: 600

## 제어 Record 분포

- None

## 판정 주의

- 10Hz와 60초는 목표 조건이다. 최종 Portfolio에는 이 분석에서 나온 실측값만 사용한다.
- `BOOT`, `ACK`, `ERR`, `STATUS`, `OK`는 제어 Record이며 IMU Parsing 실패로 계산하지 않는다.
