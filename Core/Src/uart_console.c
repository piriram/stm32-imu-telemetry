#include "uart_console.h"
#include "telemetry.h"
#include <string.h>
#include <stdio.h>

extern Node_Status_t g_sensor_status;

static UART_HandleTypeDef *console_uart;
static RingBuffer_t rx_ring_buffer;
static uint8_t rx_byte;

static char cmd_line[CMD_LINE_BUFFER_SIZE];
static uint16_t cmd_idx = 0;

void UART_Console_Init(UART_HandleTypeDef *huart) {
    console_uart = huart;
    rx_ring_buffer.head = 0;
    rx_ring_buffer.tail = 0;
    rx_ring_buffer.overflow_count = 0;
    cmd_idx = 0;
    
    // Start RX interrupt for 1 byte
    HAL_UART_Receive_IT(console_uart, &rx_byte, 1);
}

void UART_Console_RxCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == console_uart->Instance) {
        // Calculate next head position
        uint16_t next_head = (rx_ring_buffer.head + 1) % RX_RING_BUFFER_SIZE;
        
        if (next_head != rx_ring_buffer.tail) {
            rx_ring_buffer.buffer[rx_ring_buffer.head] = rx_byte;
            rx_ring_buffer.head = next_head;
        } else {
            rx_ring_buffer.overflow_count++;
        }
        
        // Re-arm interrupt
        HAL_UART_Receive_IT(console_uart, &rx_byte, 1);
    }
}

static void ProcessCommand(const char *cmd) {
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "status") == 0) {
        char buf[80];
        const char *sensor_str = (g_sensor_status == NODE_OK) ? "OK" : "OFFLINE";
        snprintf(buf, sizeof(buf), "STATUS,sensor=%s,stream=%s,rate=10,overflow=%lu\r\n", 
                 sensor_str,
                 Telemetry_Get_Stream() == STREAM_ON ? "ON" : "OFF",
                 rx_ring_buffer.overflow_count);
        Telemetry_Publish_String(console_uart, buf);
    } 
    else if (strcmp(cmd, "stream on") == 0) {
        Telemetry_Set_Stream(STREAM_ON);
        Telemetry_Publish_String(console_uart, "ACK,STREAM,ON\r\n");
    } 
    else if (strcmp(cmd, "stream off") == 0) {
        Telemetry_Set_Stream(STREAM_OFF);
        Telemetry_Publish_String(console_uart, "ACK,STREAM,OFF\r\n");
    } 
    else {
        Telemetry_Publish_String(console_uart, "ERR,INVALID_COMMAND\r\n");
    }
}

void UART_Console_Process(void) {
    while (rx_ring_buffer.tail != rx_ring_buffer.head) {
        uint8_t c = rx_ring_buffer.buffer[rx_ring_buffer.tail];
        rx_ring_buffer.tail = (rx_ring_buffer.tail + 1) % RX_RING_BUFFER_SIZE;
        
        if (c == '\r' || c == '\n') {
            if (cmd_idx > 0) {
                cmd_line[cmd_idx] = '\0';
                ProcessCommand(cmd_line);
                cmd_idx = 0;
            }
        } else {
            if (cmd_idx < CMD_LINE_BUFFER_SIZE - 1) {
                cmd_line[cmd_idx++] = c;
            } else {
                // Command too long
                Telemetry_Publish_String(console_uart, "ERR,COMMAND_TOO_LONG\r\n");
                cmd_idx = 0; // Reset
            }
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    UART_Console_RxCallback(huart);
}
