#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7      // 定義每個指令封包固定長度為 7 Byte
#define CMD_START_BYTE  0xA5   // 定義同步字頭，用於在資料流中定位封包起點

/* NVMe 簡化指令集：模擬標準 NVMe 的 Opcode */
#define NVME_OP_READ    0x01    // 讀取指令編碼
#define NVME_OP_WRITE   0x02    // 寫入指令編碼

/* 封包格式：使用 __attribute__((packed)) 確保編譯器不進行位元組對齊填充 */
typedef struct {
    uint8_t  start_byte;   // 偏移 0: 標頭 (1 byte), 0xA5  
    uint8_t  opcode;       // 偏移 1: 操作碼 (1 byte), 0x01: Read, 0x02: Write
    uint16_t lba;          // 偏移 2: 邏輯區塊位址 (2 bytes, 大端序 Big-endian)
    uint16_t length;       // 偏移 4: 資料長度 (2 bytes, Big-endian)
    uint8_t  checksum;     // 偏移 6: 校驗碼 (1 byte)，前 6 bytes 的總和
} __attribute__((packed)) NVMe_Command_t;

// 函式原型宣告
void Protocol_Parse(uint8_t *packet_buf);           // 解析封包主程序
void handle_nvme_read(uint16_t lba, uint16_t len);  // 處理讀取邏輯
void handle_nvme_write(uint16_t lba, uint16_t len); // 處理寫入邏輯

#endif
