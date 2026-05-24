#include "protocol.h"
#include "usart.h"
#include "storage.h"
#include <string.h>

__attribute__((aligned(4))) static uint8_t g_data_buf[PAGE_SIZE];

void Protocol_Parse(uint8_t *packet_buf) {
    // 將輸入緩衝區強制轉換為結構體指針，方便直接透過名稱存取欄位
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;
    
    // 1. 計算 Checksum：將封包前 6 個 byte 累加
    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    
     // 2. 驗證 Checksum：若計算結果與封包內的 checksum 不符，判定為雜訊或傳輸錯誤
    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] CS Mismatch\r\n");
        UART_Send(USART1, "  Received: 0x");
        UART_SendChar(USART1, cmd->checksum); // 顯示封包帶來的 CS
        UART_Send(USART1, "\r\n  Expected: 0x");
        UART_SendChar(USART1, calculated_cs); // 顯示 STM32 算出的 CS
        UART_Send(USART1, "\r\n");
        return; // 放棄該封包，不執行指令
    }

    // 3. 處理位元組序 (Endianness)：使用內建指令將大端序(Host)轉為小端序(STM32)
    uint16_t lba = (uint16_t)((packet_buf[2] << 8) | packet_buf[3]);
    uint16_t len = (uint16_t)((packet_buf[4] << 8) | packet_buf[5]);

    // uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    // uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

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

    UART_Send(USART1, "[ACK] READ_OK. DATA:");
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
