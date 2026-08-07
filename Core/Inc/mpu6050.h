#ifndef MPU6050_H
#define MPU6050_H

#include "main.h"

/* STM32 HAL expects the 7-bit device address shifted left by one bit. */
#define MPU6050_ADDR                 (0x68U << 1)

#define MPU6050_REG_SMPLRT_DIV       0x19U
#define MPU6050_REG_CONFIG           0x1AU
#define MPU6050_REG_GYRO_CONFIG      0x1BU
#define MPU6050_REG_ACCEL_CONFIG     0x1CU
#define MPU6050_REG_ACCEL_XOUT_H     0x3BU
#define MPU6050_REG_PWR_MGMT_1       0x6BU
#define MPU6050_REG_WHO_AM_I         0x75U

typedef struct
{
    int16_t Accel_X_Raw;
    int16_t Accel_Y_Raw;
    int16_t Accel_Z_Raw;
    int16_t Gyro_X_Raw;
    int16_t Gyro_Y_Raw;
    int16_t Gyro_Z_Raw;

    double Accel_Roll;
    double Accel_Pitch;

    uint32_t last_tick;

    double Filtered_Roll;
    double Filtered_Pitch;
} MPU6050_t;

void MPU6050_Init(I2C_HandleTypeDef *hi2c, MPU6050_t *pData);
void MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *pData);

#endif /* MPU6050_H */
