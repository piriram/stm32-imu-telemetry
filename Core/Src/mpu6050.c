#include "mpu6050.h"

#include <math.h>

#define MPU6050_GYRO_SENSITIVITY 131.0
#define RAD_TO_DEG               57.29577951308232
#define COMPLEMENTARY_GYRO_GAIN  0.96
#define COMPLEMENTARY_ACCEL_GAIN (1.0 - COMPLEMENTARY_GYRO_GAIN)

/**
 * @brief Initialize MPU6050 state and leave the sensor's default sleep mode.
 * @param hi2c I2C handle used to communicate with the sensor.
 * @param pData Sensor state and measurement storage.
 */
void MPU6050_Init(I2C_HandleTypeDef *hi2c, MPU6050_t *pData)
{
    uint8_t wake_command = 0x00;

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

    HAL_I2C_Mem_Write(
        hi2c,
        MPU6050_ADDR,
        MPU6050_REG_PWR_MGMT_1,
        I2C_MEMADD_SIZE_8BIT,
        &wake_command,
        1,
        100
    );

    pData->last_tick = HAL_GetTick();
}

/**
 * @brief Read accelerometer and gyroscope data and update the attitude estimate.
 * @param hi2c I2C handle used to communicate with the sensor.
 * @param pData Sensor state and measurement storage.
 */
void MPU6050_Read_All(I2C_HandleTypeDef *hi2c, MPU6050_t *pData)
{
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

    pData->Accel_X_Raw = (int16_t)((received_data[0] << 8) | received_data[1]);
    pData->Accel_Y_Raw = (int16_t)((received_data[2] << 8) | received_data[3]);
    pData->Accel_Z_Raw = (int16_t)((received_data[4] << 8) | received_data[5]);

    /* Bytes 6 and 7 contain temperature data and are not used here. */
    pData->Gyro_X_Raw = (int16_t)((received_data[8] << 8) | received_data[9]);
    pData->Gyro_Y_Raw = (int16_t)((received_data[10] << 8) | received_data[11]);
    pData->Gyro_Z_Raw = (int16_t)((received_data[12] << 8) | received_data[13]);

    const double accel_x = pData->Accel_X_Raw;
    const double accel_y = pData->Accel_Y_Raw;
    const double accel_z = pData->Accel_Z_Raw;

    pData->Accel_Roll = atan2(accel_y, accel_z) * RAD_TO_DEG;
    pData->Accel_Pitch = atan2(-accel_x, sqrt(accel_y * accel_y + accel_z * accel_z)) * RAD_TO_DEG;

    const double gyro_x_rate = pData->Gyro_X_Raw / MPU6050_GYRO_SENSITIVITY;
    const double gyro_y_rate = pData->Gyro_Y_Raw / MPU6050_GYRO_SENSITIVITY;

    const uint32_t current_tick = HAL_GetTick();
    const double delta_time = (current_tick - pData->last_tick) / 1000.0;
    pData->last_tick = current_tick;

    pData->Filtered_Roll =
        COMPLEMENTARY_GYRO_GAIN * (pData->Filtered_Roll + gyro_x_rate * delta_time)
        + COMPLEMENTARY_ACCEL_GAIN * pData->Accel_Roll;
    pData->Filtered_Pitch =
        COMPLEMENTARY_GYRO_GAIN * (pData->Filtered_Pitch + gyro_y_rate * delta_time)
        + COMPLEMENTARY_ACCEL_GAIN * pData->Accel_Pitch;
}
