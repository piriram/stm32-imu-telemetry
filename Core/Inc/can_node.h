#ifndef __CAN_NODE_H
#define __CAN_NODE_H

#include "main.h"
#include "telemetry.h"
#include <stdbool.h>
#include <stdint.h>

#define CAN_IMU_POSE_ID       0x100U
#define CAN_COMMAND_ID        0x200U
#define CAN_RESPONSE_ID       0x201U
#define CAN_PROTOCOL_VERSION  0x01U

typedef enum {
    CAN_CMD_STREAM_ON = 0x01,
    CAN_CMD_STREAM_OFF = 0x02,
    CAN_CMD_STATUS_REQUEST = 0x03
} CAN_Command_t;

typedef enum {
    CAN_RESULT_OK = 0x00,
    CAN_RESULT_BAD_DLC = 0x01,
    CAN_RESULT_BAD_COMMAND = 0x02
} CAN_Result_t;

typedef struct {
    bool initialized;
    bool stream_enabled;
    uint32_t tx_enqueued;
    uint32_t tx_busy;
    uint32_t rx_isr_received;
    uint32_t rx_processed;
    uint32_t rx_overflow;
    uint32_t invalid_commands;
    uint32_t esr;
} CAN_Node_Stats_t;

bool CAN_Node_Init(UART_HandleTypeDef *diagnostic_uart);
void CAN_Node_Process(void);
bool CAN_Node_PublishIMU(const IMU_Sample_t *sample);
void CAN_Node_Rx0IRQHandler(void);

bool CAN_Node_IsStreamEnabled(void);
void CAN_Node_GetStats(CAN_Node_Stats_t *stats);

#endif /* __CAN_NODE_H */
