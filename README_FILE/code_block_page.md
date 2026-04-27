### storage.h (FTL 儲存層定義)
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// SSD 規格模擬：總物理空間 = 4 Blocks * 16 Pages * 32 Bytes = 2048 Bytes (2KB)
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     16    
#define PAGE_SIZE           32    // 物理寫入最小單位
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) 

// 邏輯層定義
#define TOTAL_LBA           32    // 宣告 32 個邏輯區塊位址 (LBA 0~31)
#define INVALID_ADDR        0xFFFF 

// 物理頁面節點：用於管理空閒空間 (Free Pool)
typedef struct PageNode {
    uint16_t pba;              // 物理頁面 ID (Physical Block Address)
    struct PageNode* next;     // 指向鏈結串列下一個空閒頁
} PageNode_t;

// FTL 核心 API
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);

#endif
```

### storage.c (FTL 儲存層實作)
```
#include "storage.h"
#include "usart.h"
#include <string.h>

// [物理層模擬]：佔用 2048 Bytes SRAM
// 增加 volatile 確保編譯器不會將此大塊記憶體優化掉或誤判位址
static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];

// [邏輯層映射]：LBA -> PBA (Physical Address)
static uint16_t l2p_table[TOTAL_LBA];

// [空間管理]：使用靜態陣列建立 Linked List 節點
static PageNode_t page_pool[TOTAL_PAGES];
static PageNode_t* free_list_head = 0;

void Storage_Init(void) {
    // 1. 初始化 L2P 表，全部設為無效地址 (0xFFFF)
    for (int i = 0; i < TOTAL_LBA; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 2. 初始化空閒鏈表：將所有物理頁面串接起來 (0~63)
    // 顯式設定最後一個節點為 0 (NULL)，避免鏈表無限迴圈
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].pba = (uint16_t)i;
        if (i < (TOTAL_PAGES - 1)) {
            page_pool[i].next = &page_pool[i + 1];
        } else {
            page_pool[i].next = 0; 
        }
    }
    free_list_head = &page_pool[0];
    
    // 3. 初始物理內容清為 0xFF (模擬 Flash 擦除)
    // 修正點：將 memset 拆解為 Block 級別執行，避免在某些編譯器環境下一次性大動作造成 Stack 指標錯誤
    for (int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for (int p = 0; p < PAGES_PER_BLOCK; p++) {
            memset(flash_memory[b][p], 0xFF, PAGE_SIZE);
        }
    }
    
    UART_Send(USART1, "[FTL] 2KB Storage Ready. SRAM usage safe.\r\n");
}

// 私有函數：從 Free List 取出一個空閒頁
static uint16_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint16_t id = free_list_head->pba;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_LBA) {
        UART_Send(USART1, "[ERR] LBA Range Error\r\n");
        return;
    }

    // 異地更新邏輯
    if (l2p_table[lba] == INVALID_ADDR) {
        uint16_t pba = allocate_page();
        if (pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] SSD FULL\r\n");
            return;
        }
        l2p_table[lba] = pba;
    }

    // 計算物理位置
    uint16_t target_pba = l2p_table[lba];
    uint8_t b = (uint8_t)(target_pba / PAGES_PER_BLOCK);
    uint8_t p = (uint8_t)(target_pba % PAGES_PER_BLOCK);

    // 增加索引邊界保護，確保不會寫入到非法記憶體
    if (b < PHYSICAL_BLOCKS && p < PAGES_PER_BLOCK) {
        memcpy(flash_memory[b][p], data, PAGE_SIZE);
    }
    
    UART_Send(USART1, "[FTL] Physical Write Complete.\r\n");
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    // 邊界與無效位址檢查
    if (lba >= TOTAL_LBA || l2p_table[lba] == INVALID_ADDR) {
        memset(out_buf, 0, PAGE_SIZE);
        return;
    }

    uint16_t pba = l2p_table[lba];
    uint8_t b = (uint8_t)(pba / PAGES_PER_BLOCK);
    uint8_t p = (uint8_t)(pba % PAGES_PER_BLOCK);
    
    if (b < PHYSICAL_BLOCKS && p < PAGES_PER_BLOCK) {
        memcpy(flash_memory[b][p], out_buf, PAGE_SIZE); // 修正：應為從 flash 讀到 out_buf
        // 修正上行備註：正確順序應為 memcpy(目標, 來源, 長度)
        memcpy(out_buf, flash_memory[b][p], PAGE_SIZE);
    }
}

```


### protocol.h (通訊協定定義)
```
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
```


### protocol.c (指令解析與派發)
```
#include "protocol.h"
#include "usart.h"
#include "storage.h"
#include <string.h>

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] Checksum Mismatch!\r\n");
        return;
    }

    // 處理位元組序：Host (Big) -> STM32 (Little)
    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        UART_Send(USART1, "[ERR] Unknown Opcode\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    uint8_t read_data[PAGE_SIZE];
    Storage_Read(lba, read_data);

    UART_Send(USART1, "[ACK] DATA:");
    // 回傳 PAGE_SIZE 長度的二進位數據
    for (int i = 0; i < PAGE_SIZE; i++) {
        UART_SendChar(USART1, read_data[i]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    uint8_t dummy_data[PAGE_SIZE];
    // 生成測試數據：每個 byte 為 (LBA + 索引)
    for (int i = 0; i < PAGE_SIZE; i++) {
        dummy_data[i] = (uint8_t)(lba + i);
    }
    
    Storage_Write(lba, dummy_data);
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
```


### main.c (主程式與 DMA 管理)
```
#include "stm32f072xb.h"
#include "usart.h"
#include "dma.h"
#include "protocol.h"
#include "storage.h"
#include "systick.h"
#include "gpio.h"

#define RX_BUF_SIZE 512  // 縮小 RX Buffer 以節省 SRAM
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0;

void System_Init(void){
    RCC->APB2ENR |= (1UL << 14);   
    RCC->AHBENR  |= (1UL << 0);    
    GPIO_Init_Output(GPIOC, 6);    
    GPIO_Init_AF(GPIOA, 9, 1);     
    GPIO_Init_AF(GPIOA, 10, 1);    

    USART1->ICR |= 0xFFFFFFFF;
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69);
    *NVIC_ISER = (1UL << 27);  // 開啟中斷向量表中的 UART1 中斷
    SysTick_Init(8000);        
    Storage_Init();
}

int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_state = 0;
    UART_Send(USART1, "--- NVMe Sim Stage 2.1 (SRAM Optimized) ---\r\n");

    while (1) {
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
            rd_ptr = 0;
            UART_Send(USART1, "[SYS] ORE_RESET\r\n");
        }

        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        if (rd_ptr != wr_ptr) {
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            while (available >= PKT_SIZE) {
                if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                    Protocol_Parse(&rx_buffer[rd_ptr]);
                    rd_ptr = (rd_ptr + PKT_SIZE) % RX_BUF_SIZE;
                    available -= PKT_SIZE;
                } else {
                    rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE;
                    available--;
                }
            }
        }


        // 背景心跳燈：確認系統沒有死機 (Non-blocking Blink)
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(GPIOC, 6, &led_state);
            last_blink = get_tick();
        }
    }
}
```


### host_sender.py (自動化測試)
```
import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length):
    print(f"-> Sending: {name:20} (LBA={lba})", end=': ')
    pkt = struct.pack('>BBHH', 0xA5, opcode, lba, length)
    checksum = sum(pkt) & 0xFF
    full_pkt = pkt + struct.pack('B', checksum)
    
    ser.write(full_pkt)
    time.sleep(0.2) # 給 STM32 處理時間
    
    if ser.in_waiting > 0:
        raw = ser.read_all()
        text = raw.decode('ascii', errors='ignore').strip()
        
        if "DATA:" in text:
            # 尋找 DATA: 標頭並列印後面的二進位內容 (Hex)
            idx = raw.find(b"DATA:") + 5
            payload = raw[idx:idx+32] # 讀取 32 bytes 數據
            print(f"[ACK] Data Received: {payload.hex(' ').upper()}")
        else:
            print(f"Response: {text}")
    else:
        print("No Response")

try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)
    
    print("Starting Optimized SRAM Test...")
    # 1. 測試寫入 LBA 5 (會分配一個物理 PBA)
    test_nvme("WRITE TEST", ser, 0x02, 5, 32)
    # 2. 測試讀回 LBA 5
    test_nvme("READ TEST",  ser, 0x01, 5, 32)
    # 3. 測試讀取未映射位址 (應回傳全 0)
    test_nvme("UNMAPPED READ", ser, 0x01, 20, 32)
    
    ser.close()
except Exception as e:
    print(f"Error: {e}")
```







