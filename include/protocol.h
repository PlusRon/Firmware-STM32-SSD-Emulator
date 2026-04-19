#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* --- 協定常數定義 --- */
#define CMD_START_BYTE 0xA5
#define PKT_SIZE       7    // Start(1)+Op(1)+LBA(2)+Len(2)+CS(1)

/* --- NVMe Opcode 模擬 --- */
#define NVME_OP_READ     0x01
#define NVME_OP_WRITE    0x02
#define NVME_OP_IDENTIFY 0x03

/* --- 封包結構體 --- */
// 使用 packed 確保結構體不被填充，大小精確為 7 Bytes
typedef struct __attribute__((packed)) {
    uint8_t  start_byte; // 0xA5
    uint8_t  opcode;     
    uint16_t lba;        
    uint16_t length;     
    uint8_t  checksum;   
} NVMe_Command_t;

/* --- 函式宣告 --- */
void Protocol_Parse(uint8_t *packet_buf);
void handle_nvme_read(uint16_t lba);
void handle_nvme_write(uint16_t lba);

#endif
