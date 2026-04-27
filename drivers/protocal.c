#include "protocol.h"
#include "usart.h"
#include "storage.h"
#include <string.h>

// 關鍵修正：將緩衝區移出函式內部，防止 Stack Overflow
static uint8_t g_data_buf[PAGE_SIZE];

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] CS Mismatch\r\n");
        return;
    }

    // 手動處理 Big-Endian 轉換，比內建函式更穩定
    uint16_t lba = (uint16_t)((packet_buf[2] << 8) | packet_buf[3]);
    uint16_t len = (uint16_t)((packet_buf[4] << 8) | packet_buf[5]);

    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        UART_Send(USART1, "[ERR] INVALID_OP\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    Storage_Read(lba, g_data_buf); 

    UART_Send(USART1, "[ACK] DATA:");
    for (int i = 0; i < 8; i++) {
        UART_SendChar(USART1, g_data_buf[i]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    for (int i = 0; i < PAGE_SIZE; i++) {
        g_data_buf[i] = (uint8_t)(lba + i);
    }
    
    Storage_Write(lba, g_data_buf);
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
