#include "protocol.h"
#include "usart.h"

/**
 * @brief 解析指令封包
 * @param packet_buf 指向接收緩衝區中 A5 開頭的位址
 */
void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;

    // 1. 再次確認起始位元 (雙重檢查)
    if (cmd->start_byte != CMD_START_BYTE) return;

    // 2. 實作 Checksum 驗證
    uint8_t calculated_cs = 0;
    for (int i = 0; i < PKT_SIZE - 1; i++) {
        calculated_cs += packet_buf[i];
    }

    // 只有校驗通過，才執行指令
    if (calculated_cs == cmd->checksum) {
        
        // 3. 位元組序轉換 (Big-endian from Python to Little-endian for STM32)
        uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
        
        switch (cmd->opcode) {
            case NVME_OP_READ:
                handle_nvme_read(lba);
                break;
            case NVME_OP_WRITE:
                handle_nvme_write(lba);
                break;
            default:
                break;
        }
    }
}

/* 這是 stub functions (樁函式)，專門給 GDB 下斷點觀察 lba 數值 */
void handle_nvme_read(uint16_t lba) {
    // GDB 斷點位置: b handle_nvme_read
    __asm("NOP"); 
    /* 2. 增加回應功能 */
    // 由於你目前沒有實作 sprintf (在無標準庫環境下)，
    // 我們先用你寫好的 UART_Send 發送固定字串
    UART_Send(USART1, "ACK: NVME READ LBA RECEIVED\n");

    /* 3. 如果你想確認 LBA 是否正確，可以送出一個簡單的字元
       例如：將 LBA 轉成 ASCII (僅限個位數測試用) */
    // UART_SendChar(USART1, (uint8_t)(lba + '0'));

}

void handle_nvme_write(uint16_t lba) {
    // GDB 斷點位置: b handle_nvme_write
    __asm("NOP");
}
