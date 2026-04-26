### FTL 儲存層 storage.h
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// --- 物理層定義 (根據 STM32F072 2KB Page 特性優化) ---
#define PAGE_SIZE           64    // 每頁 64 Byte (讀寫單位)
#define PAGES_PER_BLOCK     32    // 每塊 32 頁 (32*64 = 2048B = 2KB，對齊硬體抹除單位)
#define PHYSICAL_BLOCKS     4     // 總共 4 個物理塊 (總計 8KB)
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) // 128 頁

// --- 邏輯層定義 ---
#define TOTAL_LBA           64    // 使用者邏輯地址範圍 (0~63)
#define INVALID_ADDR        0xFFFF 

// Phase 2.5: 模擬 Flash 物理狀態
typedef enum {
    PAGE_FREE = 0,    // 已抹除，可寫入
    PAGE_VALID,       // 存有當前最新有效數據
    PAGE_DIRTY        // 舊數據，需等待 Block 抹除才能再用
} PageState_t;

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_LBA_RANGE = -1,
    STORAGE_ERR_FULL = -2,      // 空間已滿 (無 FREE 頁面)
    STORAGE_ERR_NOT_ERASED = -3 // 物理寫入錯誤：目標非 FREE
} StorageStatus_t;

// API 宣告
void Storage_Init(void);
StorageStatus_t Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);
void Storage_Erase_Block(uint8_t block_id); // 以 Block 為抹除單位

#endif
```
### FTL 儲存層 storage.c
```
#include "storage.h"
#include "usart.h"
#include <string.h>

// 模擬實體儲存空間 (8KB)
static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];

// 映射表：LBA -> PBA (PBA 組成: bit 6-5 為 Block ID, bit 4-0 為 Page ID)
static uint16_t l2p_table[TOTAL_LBA];

// 頁面狀態表
static PageState_t page_states[PHYSICAL_BLOCKS][PAGES_PER_BLOCK];

void Storage_Init(void) {
    for (int i = 0; i < TOTAL_LBA; i++) {
        l2p_table[i] = INVALID_ADDR;
    }
    
    for (int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for (int p = 0; p < PAGES_PER_BLOCK; p++) {
            page_states[b][p] = PAGE_FREE;
        }
    }
    UART_Send(USART1, "[FTL] System Init: 64B Page / 2KB Block aligned.\r\n");
}

// 內部輔助：尋找一個 FREE 的物理頁面
static uint16_t allocate_page(void) {
    for (int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for (int p = 0; p < PAGES_PER_BLOCK; p++) {
            if (page_states[b][p] == PAGE_FREE) {
                return (uint16_t)((b << 5) | p); // 組合 PBA
            }
        }
    }
    return INVALID_ADDR;
}

// 以 Block (2KB) 為單位的抹除函數
void Storage_Erase_Block(uint8_t block_id) {
    if (block_id >= PHYSICAL_BLOCKS) return;
    
    // 模擬物理抹除：整塊置為 0xFF
    memset(flash_memory[block_id], 0xFF, PAGES_PER_BLOCK * PAGE_SIZE);
    
    // 將該塊內所有頁面恢復為 FREE
    for (int p = 0; p < PAGES_PER_BLOCK; p++) {
        page_states[block_id][p] = PAGE_FREE;
    }
    UART_Send(USART1, "[FTL] Physical Block Erased.\r\n");
}

StorageStatus_t Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_LBA) return STORAGE_ERR_LBA_RANGE;

    // 1. 異地更新：若有舊資料，將對應舊 PBA 標記為 DIRTY
    if (l2p_table[lba] != INVALID_ADDR) {
        uint16_t old_pba = l2p_table[lba];
        page_states[old_pba >> 5][old_pba & 0x1F] = PAGE_DIRTY;
    }

    // 2. 分配新頁面
    uint16_t new_pba = allocate_page();
    if (new_pba == INVALID_ADDR) return STORAGE_ERR_FULL;

    uint8_t b = new_pba >> 5;
    uint8_t p = new_pba & 0x1F;

    // 3. 物理限制檢查
    if (page_states[b][p] != PAGE_FREE) return STORAGE_ERR_NOT_ERASED;

    // 4. 寫入資料並更新狀態
    memcpy(flash_memory[b][p], data, PAGE_SIZE);
    page_states[b][p] = PAGE_VALID;
    l2p_table[lba] = new_pba;
    
    return STORAGE_OK;
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_LBA || l2p_table[lba] == INVALID_ADDR) {
        memset(out_buf, 0, PAGE_SIZE);
        return;
    }
    uint16_t pba = l2p_table[lba];
    memcpy(out_buf, flash_memory[pba >> 5][pba & 0x1F], PAGE_SIZE);
}
```
### 通訊協定層 protocol.h：定義 Payload 狀態
```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7
#define PAYLOAD_SIZE    64  // 與 PAGE_SIZE 對齊
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
    uint8_t read_data[PAGE_SIZE]; // 使用正確的 PAGE_SIZE
    Storage_Read(lba, read_data);
    UART_Send(USART1, "[ACK] DATA:");
    for (int i = 0; i < PAGE_SIZE; i++) UART_SendChar(USART1, read_data[i]);
    UART_Send(USART1, "\r\n");
}
```



### main.c 狀態機修正
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
                // --- 數據模式：等待電腦傳送 64 Byte 的真實數據 ---
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


### host_sender.py 測試指令修改
```
import serial
import struct
import time

def test_write_with_data(ser, lba, text_data):
    print(f"-> Writing to LBA {lba}: '{text_data}'")
    
    # 1. 指令階段 (7 bytes)
    pkt = struct.pack('>BBHH', 0xA5, 0x02, lba, 64)
    checksum = sum(pkt) & 0xFF
    ser.write(pkt + struct.pack('B', checksum))
    time.sleep(0.1) # 給 STM32 準備時間
    
    # 2. 數據階段 (32 bytes)
    payload = text_data.encode('ascii').ljust(64, b'\x00')
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



