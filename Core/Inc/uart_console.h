#ifndef __UART_CONSOLE_H
#define __UART_CONSOLE_H

#include "main.h"

#define RX_RING_BUFFER_SIZE 64
#define CMD_LINE_BUFFER_SIZE 32

typedef struct {
    uint8_t buffer[RX_RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
} RingBuffer_t;

// Functions
void UART_Console_Init(UART_HandleTypeDef *huart);
void UART_Console_RxCallback(UART_HandleTypeDef *huart);
void UART_Console_Process(void);

#endif /* __UART_CONSOLE_H */
