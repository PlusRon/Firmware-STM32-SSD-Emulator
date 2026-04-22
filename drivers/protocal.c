#include "protocol.h"
#include "usart.h"

static uint8_t virtual_disk[512];

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    // 1. 計算 Checksum
    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    // 2. 驗證 Checksum
    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] CS_FAIL (Expected: 0x");
        UART_SendChar(USART1, calculated_cs); // 這裡輸出原始值作為調試
        UART_Send(USART1, ")\r\n");
        return;
    }

    // 3. 驗證 Opcode
    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        UART_Send(USART1, "[ERR] INVALID_OP\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    UART_Send(USART1, "[ACK] READ_OK:");
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        UART_SendChar(USART1, virtual_disk[(lba + i) % 512]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        virtual_disk[(lba + i) % 512] = (uint8_t)(lba + i);
    }
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
