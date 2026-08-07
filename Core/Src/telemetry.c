#include "telemetry.h"
#include <stdio.h>
#include <string.h>

static uint32_t telemetry_seq = 0;
static Stream_Status_t stream_state = STREAM_OFF;

void Telemetry_Init(void) {
    telemetry_seq = 0;
    stream_state = STREAM_OFF;
}

void Telemetry_Set_Stream(Stream_Status_t state) {
    stream_state = state;
}

Stream_Status_t Telemetry_Get_Stream(void) {
    return stream_state;
}

uint32_t Telemetry_Get_Sequence(void) {
    return telemetry_seq;
}

static const char* NodeStatusToString(Node_Status_t status) {
    switch (status) {
        case NODE_OK: return "OK";
        case NODE_ERR_I2C: return "ERR_I2C";
        case NODE_SENSOR_OFFLINE: return "SENSOR_OFFLINE";
        case NODE_RX_OVERFLOW: return "RX_OVERFLOW";
        default: return "UNKNOWN";
    }
}

void Telemetry_Publish(UART_HandleTypeDef *huart, IMU_Sample_t *sample) {
    if (stream_state == STREAM_OFF) {
        return; // Do not publish if stream is off
    }

    char tx_buf[160];
    
    // Format: IMU,<seq>,<t_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<roll_cdeg>,<pitch_cdeg>,<status>
    int len = snprintf(tx_buf, sizeof(tx_buf),
        "IMU,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%s\r\n",
        telemetry_seq,
        sample->t_ms,
        sample->ax, sample->ay, sample->az,
        sample->gx, sample->gy, sample->gz,
        sample->roll_cdeg, sample->pitch_cdeg,
        NodeStatusToString(sample->status)
    );

    // Prevent buffer overflow in snprintf truncation
    if (len > 0 && len < sizeof(tx_buf)) {
        // Calculate a reasonable timeout based on baud rate (115200) and length
        // e.g., 100 bytes / (115200 / 10 bits) = 8.6ms. Give it 20ms.
        HAL_UART_Transmit(huart, (uint8_t*)tx_buf, len, 20);
        telemetry_seq++;
    }
}

void Telemetry_Publish_String(UART_HandleTypeDef *huart, const char *str) {
    uint16_t len = strlen(str);
    HAL_UART_Transmit(huart, (uint8_t*)str, len, 20);
}
