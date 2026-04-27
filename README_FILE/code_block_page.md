### storage.h (FTL 儲存層定義)
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// SSD 規格模擬：4 個物理區塊，每個區塊 4 個 Page，每個 Page 32 Byte
// 總物理頁面數 = 16，與原版 16 Blocks 記憶體用量一致
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     4    
#define PAGE_SIZE           32    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) 

#define INVALID_ADDR        0xFF  

// 物理頁面節點：管理空閒頁面 (Free Pool)
typedef struct PageNode {
    uint8_t id;                // 物理頁面 ID (0~15)
    struct PageNode* next;     
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

// [物理層模擬]：4x4x32 = 512 Bytes
static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];

// [邏輯層映射]：LBA 映射到 物理頁面 ID
static uint8_t l2p_table[TOTAL_PAGES]; 

// [空間管理]：Linked List 結構
static PageNode_t page_pool[TOTAL_PAGES];
static PageNode_t* free_list_head = 0; 

void Storage_Init(void) {
    // 1. 初始化 L2P 表
    for (int i = 0; i < TOTAL_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 2. 初始化空閒鏈表
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
    }
    free_list_head = &page_pool[0];
    
    // 3. 清空 Flash 內容
    memset(flash_memory, 0xFF, sizeof(flash_memory));
    
    UART_Send(USART1, "[FTL] Page-Based SSD Ready.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_PAGES) {
        UART_Send(USART1, "[ERR] LBA Range\r\n");
        return;
    }

    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_page();
        if (pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL\r\n");
            return;
        }
        l2p_table[lba] = pba;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK; // 計算 Block ID
    uint8_t p = pba_id % PAGES_PER_BLOCK; // 計算 Page ID

    memcpy(flash_memory[b][p], data, PAGE_SIZE);
    UART_Send(USART1, "[FTL] Write Done.\r\n");
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_PAGES || l2p_table[lba] == INVALID_ADDR) {
        memset(out_buf, 0, PAGE_SIZE);
        return;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;
    
    memcpy(out_buf, flash_memory[b][p], PAGE_SIZE);
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

// 關鍵修正：將緩衝區移出函式內部，防止 Stack Overflow
static uint8_t g_data_buf[PAGE_SIZE];

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] CS Mismatch\r\n");
        return;
    }

    // 手動處理 Big-Endian 轉換，比內建函式更穩定
    uint16_t lba = (uint16_t)((packet_buf[2] << 8) | packet_buf[3]);
    uint16_t len = (uint16_t)((packet_buf[4] << 8) | packet_buf[5]);

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

    UART_Send(USART1, "[ACK] DATA:");
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
}```


### main.c (主程式與 DMA 管理)
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

void System_Init(void){
    RCC->APB2ENR |= (1UL << 14);   
    RCC->AHBENR  |= (1UL << 0);    
    GPIO_Init_Output(GPIOC, 6);    
    GPIO_Init_AF(GPIOA, 9, 1);     
    GPIO_Init_AF(GPIOA, 10, 1);    

    USART1->ICR |= 0xFFFFFFFF;

    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69);     
    *NVIC_ISER = (1UL << 27);  
    SysTick_Init(8000);        

    Storage_Init();
}

int main(void) {
    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe diagnostics Mode ---\r\n");

    while (1) {
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_Send(USART1, "[SYS] ORE_ERROR\r\n");
            DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
            rd_ptr = 0;
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

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"-> {name:20} (LBA={lba:<3})", end=': ')
    actual_op = 0x99 if force_bad_op else opcode
    pkt = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    if force_bad_cs:
        checksum = (sum(pkt) + 1) & 0xFF 
    else:
        checksum = sum(pkt) & 0xFF        
        
    full_pkt = pkt + struct.pack('B', checksum)  
    
    try:
        ser.write(full_pkt)
        time.sleep(0.3) 
        
        if ser.in_waiting > 0:
            raw_data = ser.read_all()
            text_part = raw_data.decode('ascii', errors='ignore').strip()
            
            if "DATA:" in text_part:
                header_len = raw_data.find(b"DATA:") + 5
                payload = raw_data[header_len:]
                print(f"Result: [ACK] DATA: {payload.hex(' ').upper()}")
            else:
                clean_text = "".join(ch for ch in text_part if ch.isprintable() or ch in "\r\n")
                print(f"Result: {clean_text.strip()}")
        else:
            print("Result: [Timeout]")
    except Exception as e:
        print(f"Result: [Exception] {e}")

try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1) 
    time.sleep(1)
    ser.reset_input_buffer()

    print("="*50)
    print("  SSD Simulator: Page-Based FTL Test")
    print("="*50)

    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 5, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 5, 8)

    print("\n--- Filling SSD ---")
    for i in range(16):
        test_nvme(f"Fill-Test-{i}", ser, 0x02, i, 8)

    ser.close()
    print("\n" + "="*50)
    print("           Tests Completed")
    print("="*50)
except Exception as e:
    print(f"\n[FATAL ERROR]: {e}")
```







