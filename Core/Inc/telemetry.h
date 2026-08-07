#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#include "main.h"

// Telemetry stream status
typedef enum {
    STREAM_OFF = 0,
    STREAM_ON
} Stream_Status_t;

// Sensor node status for telemetry
typedef enum {
    NODE_OK = 0,
    NODE_ERR_I2C,
    NODE_SENSOR_OFFLINE,
    NODE_RX_OVERFLOW
} Node_Status_t;

// IMU Sample structure (transport-independent)
typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    int16_t roll_cdeg;  // Roll * 100
    int16_t pitch_cdeg; // Pitch * 100
    uint32_t t_ms;
    Node_Status_t status;
} IMU_Sample_t;

// Functions
void Telemetry_Init(void);
void Telemetry_Set_Stream(Stream_Status_t state);
Stream_Status_t Telemetry_Get_Stream(void);
void Telemetry_Publish(UART_HandleTypeDef *huart, IMU_Sample_t *sample);
void Telemetry_Publish_String(UART_HandleTypeDef *huart, const char *str);
uint32_t Telemetry_Get_Sequence(void);

#endif /* __TELEMETRY_H */
