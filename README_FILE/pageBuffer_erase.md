### FTL 儲存層 storage.h：新增物理狀態管理
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

#define TOTAL_BLOCKS   16       
#define BLOCK_SIZE     32       
#define INVALID_ADDR   0xFF     

// Phase 2.5: 模擬 Flash 物理狀態
typedef enum {
    BLOCK_FREE = 0,    // 已抹除，可寫入
    BLOCK_VALID,       // 存有有效數據
    BLOCK_DIRTY        // 舊數據，需抹除才能再用
} BlockState_t;

typedef struct BlockNode {
    uint8_t id;
    struct BlockNode* next;
} BlockNode_t;

typedef enum {
    STORAGE_OK = 0,             // 成功
    STORAGE_ERR_LBA_RANGE = -1, // LBA 超出範圍
    STORAGE_ERR_FULL = -2,      // 磁碟已滿 (Free List 用完)
    STORAGE_ERR_NOT_ERASED = -3 // 物理塊未抹除
} StorageStatus_t;

void Storage_Init(void);
StorageStatus_t Storage_Write(uint16_t lba, uint8_t* data); // 修改為回傳 int 以回報錯誤
void Storage_Read(uint16_t lba, uint8_t* out_buf);
void Storage_Erase(uint8_t pba);                 // 新增抹除 API

#endif
```

### FTL 儲存層 storage.c：實作「寫入前抹除」限制

```
#include "storage.h"
#include "usart.h"

static uint8_t flash_memory[TOTAL_BLOCKS][BLOCK_SIZE];
static uint8_t l2p_table[TOTAL_BLOCKS];
static BlockState_t block_states[TOTAL_BLOCKS]; // 追蹤物理塊狀態

static BlockNode_t block_pool[TOTAL_BLOCKS];
static BlockNode_t* free_list_head = 0;

void Storage_Init(void) {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        l2p_table[i] = INVALID_ADDR;
        block_states[i] = BLOCK_FREE; 
    }
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        block_pool[i].id = i;
        block_pool[i].next = (i < TOTAL_BLOCKS - 1) ? &block_pool[i+1] : 0;
    }
    free_list_head = &block_pool[0];
    UART_Send(USART1, "[FTL] Phase 2.5: P/E Cycle & Block State Ready.\r\n");
}

static uint8_t allocate_block(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Erase(uint8_t pba) {
    if (pba >= TOTAL_BLOCKS) return;
    for(int i=0; i<BLOCK_SIZE; i++) flash_memory[pba][i] = 0xFF; // 模擬 Flash 特性
    block_states[pba] = BLOCK_FREE;
    UART_Send(USART1, "[FTL] Block Erased.\r\n");
}

StorageStatus_t Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_BLOCKS) return STORAGE_ERR_LBA_RANGE;

    // 如果該 LBA 已經有舊資料，標記舊塊為 DIRTY (這就是為什麼需要 GC)
    if (l2p_table[lba] != INVALID_ADDR) {
        block_states[l2p_table[lba]] = BLOCK_DIRTY;
        l2p_table[lba] = INVALID_ADDR; 
    }

    uint8_t pba = allocate_block();
    if (pba == INVALID_ADDR) return STORAGE_ERR_FULL;

    // 物理限制檢查：未抹除不能寫入
    if (block_states[pba] != BLOCK_FREE) return STORAGE_ERR_NOT_ERASED;

    l2p_table[lba] = pba;
    for (int i = 0; i < BLOCK_SIZE; i++) flash_memory[pba][i] = data[i];
    block_states[pba] = BLOCK_VALID;
    
    return STORAGE_OK;
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_BLOCKS || l2p_table[lba] == INVALID_ADDR) {
        for(int i=0; i<BLOCK_SIZE; i++) out_buf[i] = 0;
        return;
    }
    uint8_t pba = l2p_table[lba];
    for (int i = 0; i < BLOCK_SIZE; i++) out_buf[i] = flash_memory[pba][i];
}
```

### 通訊協定層 protocol.h：定義 Payload 狀態
```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7
#define PAYLOAD_SIZE    32 
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

// 狀態機旗標
extern uint8_t is_waiting_for_payload;

void Protocol_Parse(uint8_t *packet_buf);
void Process_Payload(uint8_t *payload_buf);
void handle_nvme_read(uint16_t lba);

#endif
```


### 通訊協定層 protocol.c：處理雙階段通訊
```
#include "protocol.h"
#include "usart.h"
#include "storage.h"

uint8_t is_waiting_for_payload = 0;
static uint16_t current_lba = 0;

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;
    for (int i = 0; i < 6; i++) calculated_cs += packet_buf[i];

    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] Checksum Mismatch!\r\n");
        return;
    }

    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);

    if (cmd->opcode == NVME_OP_WRITE) {
        current_lba = lba;
        is_waiting_for_payload = 1; // 啟動狀態機：下次讀取 32B 數據
        UART_Send(USART1, "[SYS] WRITE_CMD_ACK. Send 32B Data now.\r\n");
    } else if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba);
    }
}

void Process_Payload(uint8_t *payload_buf) {
    int res = Storage_Write(current_lba, payload_buf);
    if (res == 0) UART_Send(USART1, "[ACK] Flash Write Success.\r\n");
    else if (res == -2) UART_Send(USART1, "[ERR] Disk Full.\r\n");
    else if (res == -3) UART_Send(USART1, "[ERR] Flash Not Erased.\r\n");
    
    is_waiting_for_payload = 0; // 回到指令模式
}

void handle_nvme_read(uint16_t lba) {
    uint8_t read_data[BLOCK_SIZE];
    Storage_Read(lba, read_data);
    UART_Send(USART1, "[ACK] DATA:");
    for (int i = 0; i < BLOCK_SIZE; i++) UART_SendChar(USART1, read_data[i]);
    UART_Send(USART1, "\r\n");
}
```

### main.c：核心狀態機迴圈
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"
#include "storage.h"

#define RX_BUF_SIZE 1024
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0;

// ... System_Init 保持不變 ...

int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe Stage 2.5: Payload State Machine ---\r\n");

    while (1) {
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        if (rd_ptr != wr_ptr) {
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            if (is_waiting_for_payload) {
                // --- 數據模式：等待 32 Byte ---
                if (available >= PAYLOAD_SIZE) {
                    Process_Payload(&rx_buffer[rd_ptr]);
                    rd_ptr = (rd_ptr + PAYLOAD_SIZE) % RX_BUF_SIZE;
                }
            } else {
                // --- 指令模式：等待 7 Byte ---
                if (available >= PKT_SIZE) {
                    if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                        Protocol_Parse(&rx_buffer[rd_ptr]);
                        rd_ptr = (rd_ptr + PKT_SIZE) % RX_BUF_SIZE;
                    } else {
                        rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE; // 找標頭同步
                    }
                }
            }
        }

        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(GPIOC, 6, &led_state);
            last_blink = get_tick();
        }
    }
}
```

### host_sender.py：雙階段發送測試
```
import serial
import struct
import time

def test_write_with_data(ser, lba, text_data):
    print(f"-> Writing to LBA {lba}: '{text_data}'")
    
    # 1. 指令階段 (7 bytes)
    pkt = struct.pack('>BBHH', 0xA5, 0x02, lba, 32)
    checksum = sum(pkt) & 0xFF
    ser.write(pkt + struct.pack('B', checksum))
    time.sleep(0.1) # 給 STM32 準備時間
    
    # 2. 數據階段 (32 bytes)
    payload = text_data.encode('ascii').ljust(32, b'\x00')
    ser.write(payload)
    time.sleep(0.3)
    print(f"Result: {ser.read_all().decode('ascii', errors='ignore').strip()}")

def test_read(ser, lba):
    print(f"-> Reading LBA {lba}")
    pkt = struct.pack('>BBHH', 0xA5, 0x01, lba, 32)
    checksum = sum(pkt) & 0xFF
    ser.write(pkt + struct.pack('B', checksum))
    time.sleep(0.3)
    res = ser.read_all()
    # 解析 ACK DATA: 之後的二進位內容
    idx = res.find(b"DATA:")
    if idx != -1:
        data = res[idx+5:]
        print(f"Result: [ACK] {data.hex(' ').upper()}")
        print(f"Text: {data.decode('ascii', errors='ignore')}")

# 執行測試
try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)
    
    # 測試 1: 寫入真實資料
    test_write_with_data(ser, 7, "Hello NVMe 2.5")
    
    # 測試 2: 讀回驗證
    test_read(ser, 7)
    
    ser.close()
except Exception as e:
    print(f"Error: {e}")
```










