#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7
#define CMD_START_BYTE  0xA5

/* NVMe 簡化指令集 */
#define NVME_OP_READ    0x01
#define NVME_OP_WRITE   0x02

/* 封包格式 (需對齊硬體位元組序) */
typedef struct {
    uint8_t  start_byte; // 0xA5
    uint8_t  opcode;     // 0x01: Read, 0x02: Write
    uint16_t lba;        // 邏輯區塊位址 (Big-endian)
    uint16_t length;     // 資料長度 (Big-endian)
    uint8_t  checksum;   // 前 6 bytes 之和
} __attribute__((packed)) NVMe_Command_t;

void Protocol_Parse(uint8_t *packet_buf);
void handle_nvme_read(uint16_t lba, uint16_t len);
void handle_nvme_write(uint16_t lba, uint16_t len);

#endif
