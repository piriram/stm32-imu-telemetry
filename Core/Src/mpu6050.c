#include "mpu6050.h"
#include <math.h>

/**
 * @brief MPU6050 센서 초기화 함수
 * @param hi2c I2C 핸들 포인터 (어떤 I2C 채널을 쓸 건지)
 * @param pData MPU6050 데이터 구조체 포인터 (결과를 어디에 저장할 건지)
 * @return MPU6050_Status_t (초기화 성공 또는 실패 원인)
 */
MPU6050_Status_t MPU6050_Init(I2C_HandleTypeDef *hi2c, MPU6050_t *pData)
{
    uint8_t temp_data;
    uint8_t check;
    
    // [공부 포인트 - WHO_AM_I 확인]
    // 센서가 정상적으로 연결되어 있는지 신분증 레지스터(0x75)를 읽어옵니다.
    if (HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, 1, &check, 1, 100) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    
    if (check != 0x68) {
        return MPU6050_DEVICE_NOT_FOUND;
    }
    
    // [공부 포인트 1] 쓰레기값 방지
    // C언어에서 변수(구조체)를 선언하면 메모리에 이전 쓰레기값이 남아있을 수 있으므로 0으로 깔끔하게 밀어줍니다.
    pData->Accel_X_Raw = 0;
    pData->Accel_Y_Raw = 0;
    pData->Accel_Z_Raw = 0;
    pData->Gyro_X_Raw = 0;
    pData->Gyro_Y_Raw = 0;
    pData->Gyro_Z_Raw = 0;
    pData->Accel_Roll = 0.0;
    pData->Accel_Pitch = 0.0;
    pData->Filtered_Roll = 0.0;
    pData->Filtered_Pitch = 0.0;
    
    // [공부 포인트 2] 센서 깨우기 (Wake up)
    // MPU6050은 전원이 켜지면 기본적으로 'Sleep(절전) 모드' 상태입니다.
    // 전원 관리 레지스터(0x6B)의 Sleep 비트(6번 비트)를 0으로 만들어주기 위해 0x00을 전송합니다.
    temp_data = 0x00;
    if (HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, MPU6050_REG_PWR_MGMT_1, 1, &temp_data, 1, 100) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    
    // [공부 포인트 3] 첫 시간 기록
    // HAL_GetTick()은 MCU가 켜진 후 1ms마다 1씩 증가하는 카운터입니다. 
    // 나중에 '얼마나 시간이 지났는지(dt)' 계산하기 위한 출발점(기준)을 잡습니다.
    pData->last_tick = HAL_GetTick();
    
    return MPU6050_OK;
}

/**
 * @brief MPU6050 모든 센서값 읽기 및 상보 필터 적용 함수
 */
MPU6050_Status_t MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *pData)
{
    uint8_t rec_data[14];
    
    // [공부 포인트 4] Burst Read (연속 읽기)
    // 0x3B(가속도X_H)부터 시작해서 총 14바이트를 한 번에 쭉 읽어옵니다.
    // 왜 14바이트일까요? 가속도(X,Y,Z 각 2바이트=6) + 온도(2바이트) + 자이로(X,Y,Z 각 2바이트=6) = 14바이트가 연속해 있기 때문입니다.
    // 1바이트씩 14번 통신하는 것보다 속도가 훨씬 빠릅니다.
    if (HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, MPU6050_REG_ACCEL_XOUT_H, 1, rec_data, 14, 100) != HAL_OK) {
        return MPU6050_ERR_I2C;
    }
    
    // [공부 포인트 5] 비트 결합 (Bit Shift)
    // 센서 데이터는 16비트(int16_t)인데, I2C 통신은 8비트(1바이트) 단위로만 전송됩니다.
    // 그래서 센서가 High byte(상위 8비트)와 Low byte(하위 8비트)로 쪼개서 보냅니다.
    // 이를 다시 합치기 위해 High byte를 왼쪽으로 8칸 밀고(<< 8), Low byte와 OR(|) 연산으로 이어 붙입니다.
    pData->Accel_X_Raw = (int16_t)(rec_data[0] << 8 | rec_data[1]);
    pData->Accel_Y_Raw = (int16_t)(rec_data[2] << 8 | rec_data[3]);
    pData->Accel_Z_Raw = (int16_t)(rec_data[4] << 8 | rec_data[5]);
    
    // 6번, 7번 방은 온도 데이터(Temp_H, Temp_L)이므로 패스합니다.
    pData->Gyro_X_Raw = (int16_t)(rec_data[8] << 8 | rec_data[9]);
    pData->Gyro_Y_Raw = (int16_t)(rec_data[10] << 8 | rec_data[11]);
    pData->Gyro_Z_Raw = (int16_t)(rec_data[12] << 8 | rec_data[13]);
    
    // [공부 포인트 6] 가속도 기반의 각도 계산 (Trigonometry)
    // 중력 가속도(Z축 방향)가 센서가 기울어짐에 따라 X축, Y축으로 분산되는 비율을 아크탄젠트(atan2)로 계산하여 각도를 구합니다.
    // 장점: 시간이 지나도 오차가 누적되지 않고 절대적인 각도를 유지합니다.
    // 단점: 센서가 이동하거나 덜컹거리면 그 가속도까지 중력으로 오해해서 값이 미친듯이 튑니다.
    pData->Accel_Roll  = atan2(pData->Accel_Y_Raw, pData->Accel_Z_Raw) * 180.0 / 3.14159265;
    pData->Accel_Pitch = atan2(-pData->Accel_X_Raw, sqrt(pData->Accel_Y_Raw * pData->Accel_Y_Raw + pData->Accel_Z_Raw * pData->Accel_Z_Raw)) * 180.0 / 3.14159265;
    
    // [공부 포인트 7] 자이로스코프 데이터 스케일링
    // 자이로 값은 '현재 얼마나 빠르게 회전하고 있는가(각속도)'를 나타냅니다.
    // 데이터시트에 따르면 기본 설정(±250deg/s)일 때, Raw 값 131 이 실제 1도/초(deg/s)를 의미합니다.
    double gx_rate = pData->Gyro_X_Raw / 131.0;
    double gy_rate = pData->Gyro_Y_Raw / 131.0;
    
    // [공부 포인트 8] 시간 변화량(dt) 적분
    // 속도 * 시간 = 이동 거리(각도) 입니다.
    // 지난번 계산 이후 '몇 초'가 흘렀는지(dt)를 구해서 자이로 속도에 곱해주면 회전한 각도를 얻을 수 있습니다.
    uint32_t current_tick = HAL_GetTick();
    double dt = (current_tick - pData->last_tick) / 1000.0; // ms를 s로 변환
    pData->last_tick = current_tick;
    
    // [공부 포인트 9] 대망의 상보 필터 (Complementary Filter) 🔥
    // 가속도 센서의 단점(노이즈, 진동에 약함)과 자이로 센서의 단점(시간이 지날수록 오차가 누적되는 드리프트 현상)을 서로 보완합니다!
    // (기존 각도 + 자이로 회전량)에는 96%의 높은 신뢰도를 주고, 가속도로 구한 절대 각도에는 4%의 낮은 신뢰도를 줍니다.
    // 이렇게 하면 진동에는 둔감하면서도(자이로 덕분), 절대 기울기를 잃어버리지 않는(가속도 덕분) 완벽한 각도가 탄생합니다.
    pData->Filtered_Roll  = 0.96 * (pData->Filtered_Roll + gx_rate * dt) + 0.04 * pData->Accel_Roll;
    pData->Filtered_Pitch = 0.96 * (pData->Filtered_Pitch + gy_rate * dt) + 0.04 * pData->Accel_Pitch;
    
    return MPU6050_OK;
}
