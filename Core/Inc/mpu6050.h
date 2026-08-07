#ifndef __MPU6050_H
#define __MPU6050_H

#include "main.h"

/* 
 * [공부 포인트 1] I2C 주소(Address)
 * MPU6050은 하드웨어 핀(AD0)의 상태에 따라 주소가 바뀝니다.
 * AD0 핀이 GND(0V)에 연결되어 있으면 기본 주소는 0x68 입니다.
 * STM32의 HAL 라이브러리는 8비트 주소 체계를 사용하므로, 
 * 7비트 주소인 0x68을 왼쪽으로 1칸 시프트(<< 1)한 0xD0를 사용해야 합니다.
 */
#define MPU6050_ADDR          0xD0 // (0x68 << 1)

/* 
 * [공부 포인트 2] 레지스터(Register) 맵
 * 센서 내부에 있는 작은 메모리 방들의 주소입니다. 
 * 데이터시트(Datasheet)의 "Register Map" 섹션을 보면 각 방의 주소와 역할을 알 수 있습니다.
 */
#define MPU6050_REG_SMPLRT_DIV      0x19  // 샘플링 속도 조절
#define MPU6050_REG_CONFIG          0x1A  // 기본 설정 (Low Pass Filter 등)
#define MPU6050_REG_GYRO_CONFIG     0x1B  // 자이로스코프 측정 범위 설정 (±250, 500, 1000, 2000 deg/s)
#define MPU6050_REG_ACCEL_CONFIG    0x1C  // 가속도계 측정 범위 설정 (±2g, 4g, 8g, 16g)
#define MPU6050_REG_ACCEL_XOUT_H    0x3B  // 센서 데이터가 시작되는 첫 번째 방 (가속도 X축 High byte)
#define MPU6050_REG_PWR_MGMT_1      0x6B  // 전원 관리 (Sleep 모드 해제에 사용)
#define MPU6050_REG_WHO_AM_I        0x75  // 센서가 정상 연결되었는지 확인하는 신분증 같은 방 (기본값 0x68)

/*
 * MPU6050 동작 상태
 */
typedef enum {
    MPU6050_OK = 0,
    MPU6050_ERR_I2C,
    MPU6050_DEVICE_NOT_FOUND
} MPU6050_Status_t;

/* 
 * [공부 포인트 3] 객체지향적 구조체 설계
 * 센서의 모든 '상태(State)'와 '데이터'를 하나의 바구니에 담습니다.
 * 이렇게 하면 나중에 MPU6050 센서를 2개, 3개 연결해도 변수 이름이 꼬이지 않습니다.
 * (예: MPU6050_t sensor_left, sensor_right;)
 */
typedef struct {
    /* 1. 원시 데이터 (Raw Data) - 센서가 보내주는 가공되지 않은 16비트 정수값 */
    int16_t Accel_X_Raw;
    int16_t Accel_Y_Raw;
    int16_t Accel_Z_Raw;
    int16_t Gyro_X_Raw;
    int16_t Gyro_Y_Raw;
    int16_t Gyro_Z_Raw;

    /* 2. 중간 계산값 - 가속도로만 구한 흔들리는 각도 */
    double Accel_Roll;
    double Accel_Pitch;
    
    /* 3. 시간 측정 - 자이로스코프는 '초당 회전량'이므로, 이전 측정 후 '얼마의 시간(dt)'이 흘렀는지 알아야 각도를 적분할 수 있습니다. */
    uint32_t last_tick;

    /* 4. 최종 결과 - 상보 필터를 거친 깔끔하고 부드러운 각도 */
    double Filtered_Roll;
    double Filtered_Pitch;
} MPU6050_t;

/* 
 * [공부 포인트 4] API 함수 선언 (인터페이스)
 * 외부(main.c)에서는 이 함수들만 호출해서 센서를 조종합니다.
 */
MPU6050_Status_t MPU6050_Init(I2C_HandleTypeDef *hi2c, MPU6050_t *pData);
MPU6050_Status_t MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *pData);

#endif /* __MPU6050_H */
