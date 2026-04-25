#include "protocol.h"
#include "usart.h"

// 模擬快閃記憶體 (NAND Flash) 的儲存空間，大小為 512 Bytes
static uint8_t virtual_disk[512];

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
        UART_Send(USART1, "[ERR] Checksum Mismatch!\r\n");
        UART_Send(USART1, "  Received: 0x");
        UART_SendChar(USART1, cmd->checksum); // 顯示封包帶來的 CS
        UART_Send(USART1, "\r\n  Expected: 0x");
        UART_SendChar(USART1, calculated_cs); // 顯示 STM32 算出的 CS
        UART_Send(USART1, "\r\n");
        return; // 放棄該封包，不執行指令
    }

    // 3. 處理位元組序 (Endianness)：使用內建指令將大端序(Host)轉為小端序(STM32)
    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

    // 4. 指令派發 (Command Dispatching)
    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        // 若 Opcode 不在定義內，回傳無效指令錯誤
        UART_Send(USART1, "[ERR] INVALID_OP\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    // 將長度限制在「虛擬磁碟的大小」以內，防止非法存取
    // uint16_t safe_len = (len > 512) ? 512 : len;

    UART_Send(USART1, "[ACK] READ_OK:");
    // 限制讀取長度，避免非法存取，並循環模擬磁碟空間
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        UART_SendChar(USART1, virtual_disk[(lba + i) % 512]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    // 模擬寫入邏輯：將 LBA 地址轉換為資料存入，用於後續讀取驗證
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        virtual_disk[(lba + i) % 512] = (uint8_t)(lba + i);
    }
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
