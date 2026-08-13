#include "can_node.h"
#include <stdio.h>

#define CAN_RX_QUEUE_SIZE          8U
#define CAN_INIT_TIMEOUT_MS       10U
#define CAN_REQUIRED_PCLK1_HZ 8000000U

/*
 * APB1 = 8 MHz, 500 kbit/s, 16 time quanta per bit:
 * 8 MHz / (prescaler 1 * (1 + BS1 12 + BS2 3)) = 500 kbit/s.
 * Register fields store each configured value minus one.
 */
#define CAN_BTR_500K_8MHZ \
    ((0U << CAN_BTR_SJW_Pos) | (2U << CAN_BTR_TS2_Pos) | \
     (11U << CAN_BTR_TS1_Pos) | (0U << CAN_BTR_BRP_Pos))

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} CAN_Frame_t;

typedef struct {
    CAN_Frame_t frames[CAN_RX_QUEUE_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} CAN_RxQueue_t;

extern Node_Status_t g_sensor_status;

static UART_HandleTypeDef *diag_uart;
static CAN_RxQueue_t rx_queue;
static volatile CAN_Node_Stats_t node_stats;
static volatile bool can_stream_enabled = true;
static uint16_t can_sequence;
static uint32_t reported_overflow_count;

static void DiagnosticPrint(const char *message)
{
    if (diag_uart != NULL) {
        Telemetry_Publish_String(diag_uart, message);
    }
}

static bool WaitForRegisterState(volatile uint32_t *reg,
                                 uint32_t mask,
                                 bool set)
{
    uint32_t start = HAL_GetTick();

    while (((*reg & mask) != 0U) != set) {
        if ((HAL_GetTick() - start) >= CAN_INIT_TIMEOUT_MS) {
            return false;
        }
    }

    return true;
}

static void ConfigurePins(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* Keep CAN1 on its default pins: PA11 RX, PA12 TX. */
    AFIO->MAPR &= ~AFIO_MAPR_CAN_REMAP;

    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void ConfigureCommandFilter(void)
{
    const uint32_t filter_id = CAN_COMMAND_ID << CAN_RI0R_STID_Pos;
    const uint32_t filter_mask = CAN_RI0R_STID_Msk |
                                 CAN_RI0R_IDE_Msk |
                                 CAN_RI0R_RTR_Msk;

    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~CAN_FA1R_FACT0;
    CAN1->FM1R &= ~CAN_FM1R_FBM0;   /* Identifier-mask mode. */
    CAN1->FS1R |= CAN_FS1R_FSC0;    /* One 32-bit filter. */
    CAN1->FFA1R &= ~CAN_FFA1R_FFA0; /* Route matches to FIFO 0. */
    CAN1->sFilterRegister[0].FR1 = filter_id;
    CAN1->sFilterRegister[0].FR2 = filter_mask;
    CAN1->FA1R |= CAN_FA1R_FACT0;
    CAN1->FMR &= ~CAN_FMR_FINIT;
}

static bool WriteFrame(uint16_t id, const uint8_t *data, uint8_t dlc)
{
    uint32_t mailbox_index;
    CAN_TxMailBox_TypeDef *mailbox;
    uint32_t low = 0U;
    uint32_t high = 0U;

    if (!node_stats.initialized || data == NULL || id > 0x7FFU || dlc > 8U) {
        return false;
    }

    if ((CAN1->TSR & CAN_TSR_TME0) != 0U) {
        mailbox_index = 0U;
    } else if ((CAN1->TSR & CAN_TSR_TME1) != 0U) {
        mailbox_index = 1U;
    } else if ((CAN1->TSR & CAN_TSR_TME2) != 0U) {
        mailbox_index = 2U;
    } else {
        node_stats.tx_busy++;
        return false;
    }

    for (uint8_t i = 0U; i < 4U && i < dlc; ++i) {
        low |= (uint32_t)data[i] << (8U * i);
    }
    for (uint8_t i = 4U; i < 8U && i < dlc; ++i) {
        high |= (uint32_t)data[i] << (8U * (i - 4U));
    }

    mailbox = &CAN1->sTxMailBox[mailbox_index];
    mailbox->TIR = (uint32_t)id << CAN_TI0R_STID_Pos;
    mailbox->TDTR = dlc;
    mailbox->TDLR = low;
    mailbox->TDHR = high;
    mailbox->TIR |= CAN_TI0R_TXRQ;

    node_stats.tx_enqueued++;
    return true;
}

static bool PopFrame(CAN_Frame_t *frame)
{
    uint8_t tail;

    if (frame == NULL || rx_queue.tail == rx_queue.head) {
        return false;
    }

    tail = rx_queue.tail;
    *frame = rx_queue.frames[tail];
    rx_queue.tail = (uint8_t)((tail + 1U) % CAN_RX_QUEUE_SIZE);
    return true;
}

static void PublishResponse(uint8_t command, CAN_Result_t result)
{
    CAN_Node_Stats_t stats;
    uint8_t payload[8];

    CAN_Node_GetStats(&stats);
    payload[0] = command;
    payload[1] = (uint8_t)result;
    payload[2] = can_stream_enabled ? 1U : 0U;
    payload[3] = (uint8_t)g_sensor_status;
    payload[4] = (uint8_t)(stats.tx_busy & 0xFFU);
    payload[5] = (uint8_t)((stats.tx_busy >> 8) & 0xFFU);
    payload[6] = (uint8_t)(stats.rx_overflow & 0xFFU);
    payload[7] = (uint8_t)((stats.rx_overflow >> 8) & 0xFFU);
    (void)WriteFrame(CAN_RESPONSE_ID, payload, sizeof(payload));
}

static void ProcessCommandFrame(const CAN_Frame_t *frame)
{
    char message[64];
    uint8_t command = 0U;
    CAN_Result_t result = CAN_RESULT_OK;

    if (frame->dlc < 1U) {
        result = CAN_RESULT_BAD_DLC;
    } else {
        command = frame->data[0];
        switch (command) {
        case CAN_CMD_STREAM_ON:
            can_stream_enabled = true;
            DiagnosticPrint("CAN,ACK,STREAM,ON\r\n");
            break;

        case CAN_CMD_STREAM_OFF:
            can_stream_enabled = false;
            DiagnosticPrint("CAN,ACK,STREAM,OFF\r\n");
            break;

        case CAN_CMD_STATUS_REQUEST:
            DiagnosticPrint("CAN,ACK,STATUS\r\n");
            break;

        default:
            result = CAN_RESULT_BAD_COMMAND;
            node_stats.invalid_commands++;
            (void)snprintf(message, sizeof(message),
                           "CAN,ERR,BAD_COMMAND,0x%02X\r\n", command);
            DiagnosticPrint(message);
            break;
        }
    }

    if (result == CAN_RESULT_BAD_DLC) {
        node_stats.invalid_commands++;
        DiagnosticPrint("CAN,ERR,BAD_DLC\r\n");
    }

    PublishResponse(command, result);
}

bool CAN_Node_Init(UART_HandleTypeDef *diagnostic_uart)
{
    diag_uart = diagnostic_uart;
    rx_queue.head = 0U;
    rx_queue.tail = 0U;
    node_stats = (CAN_Node_Stats_t){0};
    can_stream_enabled = true;
    can_sequence = 0U;
    reported_overflow_count = 0U;

    /* Bit timing below is valid only for the project's current APB1 clock. */
    if (HAL_RCC_GetPCLK1Freq() != CAN_REQUIRED_PCLK1_HZ) {
        return false;
    }

    ConfigurePins();
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_CAN1_FORCE_RESET();
    __HAL_RCC_CAN1_RELEASE_RESET();

    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    if (!WaitForRegisterState(&CAN1->MSR, CAN_MSR_INAK, true)) {
        return false;
    }

    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_TXFP | CAN_MCR_ABOM;
    CAN1->BTR = CAN_BTR_500K_8MHZ;
    ConfigureCommandFilter();

    CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_FOVIE0;
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (!WaitForRegisterState(&CAN1->MSR, CAN_MSR_INAK, false)) {
        HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
        return false;
    }

    node_stats.initialized = true;
    return true;
}

bool CAN_Node_PublishIMU(const IMU_Sample_t *sample)
{
    uint8_t payload[8];
    bool enqueued;

    if (sample == NULL || !can_stream_enabled) {
        return false;
    }

    payload[0] = (uint8_t)((uint16_t)sample->roll_cdeg & 0xFFU);
    payload[1] = (uint8_t)(((uint16_t)sample->roll_cdeg >> 8) & 0xFFU);
    payload[2] = (uint8_t)((uint16_t)sample->pitch_cdeg & 0xFFU);
    payload[3] = (uint8_t)(((uint16_t)sample->pitch_cdeg >> 8) & 0xFFU);
    payload[4] = (uint8_t)(can_sequence & 0xFFU);
    payload[5] = (uint8_t)((can_sequence >> 8) & 0xFFU);
    payload[6] = (uint8_t)sample->status;
    payload[7] = CAN_PROTOCOL_VERSION;

    enqueued = WriteFrame(CAN_IMU_POSE_ID, payload, sizeof(payload));
    if (enqueued) {
        can_sequence++;
    }
    return enqueued;
}

void CAN_Node_Process(void)
{
    CAN_Frame_t frame;

    while (PopFrame(&frame)) {
        node_stats.rx_processed++;
        if (frame.id == CAN_COMMAND_ID) {
            ProcessCommandFrame(&frame);
        }
    }

    if (node_stats.rx_overflow != reported_overflow_count) {
        reported_overflow_count = node_stats.rx_overflow;
        DiagnosticPrint("CAN,ERR,RX_OVERFLOW\r\n");
    }
}

void CAN_Node_Rx0IRQHandler(void)
{
    if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
        node_stats.rx_overflow++;
        CAN1->RF0R = CAN_RF0R_FOVR0;
    }

    while ((CAN1->RF0R & CAN_RF0R_FMP0) != 0U) {
        CAN_Frame_t frame;
        uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
        uint32_t rdtr = CAN1->sFIFOMailBox[0].RDTR;
        uint32_t low = CAN1->sFIFOMailBox[0].RDLR;
        uint32_t high = CAN1->sFIFOMailBox[0].RDHR;
        uint8_t next_head;

        frame.id = (uint16_t)((rir & CAN_RI0R_STID_Msk) >> CAN_RI0R_STID_Pos);
        frame.dlc = (uint8_t)(rdtr & CAN_RDT0R_DLC_Msk);
        if (frame.dlc > 8U) {
            frame.dlc = 8U;
        }

        frame.data[0] = (uint8_t)(low & 0xFFU);
        frame.data[1] = (uint8_t)((low >> 8) & 0xFFU);
        frame.data[2] = (uint8_t)((low >> 16) & 0xFFU);
        frame.data[3] = (uint8_t)((low >> 24) & 0xFFU);
        frame.data[4] = (uint8_t)(high & 0xFFU);
        frame.data[5] = (uint8_t)((high >> 8) & 0xFFU);
        frame.data[6] = (uint8_t)((high >> 16) & 0xFFU);
        frame.data[7] = (uint8_t)((high >> 24) & 0xFFU);

        next_head = (uint8_t)((rx_queue.head + 1U) % CAN_RX_QUEUE_SIZE);
        if (next_head != rx_queue.tail) {
            rx_queue.frames[rx_queue.head] = frame;
            rx_queue.head = next_head;
            node_stats.rx_isr_received++;
        } else {
            node_stats.rx_overflow++;
        }

        CAN1->RF0R = CAN_RF0R_RFOM0;
    }
}

bool CAN_Node_IsStreamEnabled(void)
{
    return can_stream_enabled;
}

void CAN_Node_GetStats(CAN_Node_Stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->initialized = node_stats.initialized;
    stats->stream_enabled = can_stream_enabled;
    stats->tx_enqueued = node_stats.tx_enqueued;
    stats->tx_busy = node_stats.tx_busy;
    stats->rx_isr_received = node_stats.rx_isr_received;
    stats->rx_processed = node_stats.rx_processed;
    stats->rx_overflow = node_stats.rx_overflow;
    stats->invalid_commands = node_stats.invalid_commands;
    stats->esr = node_stats.initialized ? CAN1->ESR : 0U;
}
