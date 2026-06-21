#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7      
#define CMD_START_BYTE  0xA5   

#define NVME_OP_READ    0x01
#define NVME_OP_WRITE   0x02

typedef struct {
    uint8_t  start_byte;   
    uint8_t  opcode;       
    uint16_t lba;          
    uint16_t length;       
    uint8_t  checksum;     
} __attribute__((packed)) NVMe_Command_t;

void Protocol_Parse(uint8_t *packet_buf);
void handle_nvme_read(uint16_t lba, uint16_t len);
void handle_nvme_write(uint16_t lba, uint16_t len);

#endif
