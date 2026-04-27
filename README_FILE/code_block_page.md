


## 
### storage.h (FTL 儲存層定義)
```

```



### storage.c (FTL 儲存層實作)
```
```

### protocol.h
```
```


### protocol.c
```
```



### main.c
```
```


### host_sender.py
```

```

## SSD physical BLOCK/PAGE
### storage.h
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* SSD 規格修改：4 Blocks, 32 Pages per Block, 64 Bytes per Page */
/* 總容量 = 4 * 32 * 64 = 8192 Bytes (8KB) */
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     32    
#define PAGE_SIZE           64    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) // 128 Pages

#define INVALID_ADDR        0xFF  

/* 物理頁面節點：加上對齊確保指針操作安全 */
typedef struct PageNode {
    uint8_t id;                
    struct PageNode* next;     
} __attribute__((aligned(4))) PageNode_t;

/* FTL 核心 API */
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);

#endif
```



### storage.c
```
#include "storage.h"
#include "usart.h"
#include <string.h>

/* 靜態分配 8KB 空間，使用 aligned(4) 防止非對齊存取造成的 HardFault */
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
__attribute__((aligned(4))) static uint8_t l2p_table[TOTAL_PAGES];
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

void Storage_Init(void) {
    /* 1. 初始化 L2P 表 (映射 128 個 LBA) */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    /* 2. 初始化空閒頁面鏈表 */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
    }
    free_list_head = &page_pool[0];
    
    /* 3. 手動清除 Flash 內容 (8KB)，避免使用 memset 在某些編譯器下的不穩定 */
    for(int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for(int p = 0; p < PAGES_PER_BLOCK; p++) {
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
        }
    }
    
    UART_Send(USART1, "[FTL] 8KB SSD Initialized (4B/32P/64S).\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    /* 邊界檢查：LBA 現在最高可支援到 127 */
    if (lba >= TOTAL_PAGES) {
        UART_Send(USART1, "[ERR] LBA Out of Range\r\n");
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
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;

    /* 寫入 64 Bytes 資料 */
    for(int i = 0; i < PAGE_SIZE; i++) {
        flash_memory[b][p][i] = data[i];
    }
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_PAGES || l2p_table[lba] == INVALID_ADDR) {
        for(int i = 0; i < PAGE_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;
    
    for(int i = 0; i < PAGE_SIZE; i++) {
        out_buf[i] = flash_memory[b][p][i];
    }
}
```

### protocol.h
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


### protocol.c
```
#include "protocol.h"
#include "usart.h"
#include "storage.h"
#include <string.h>

/* 靜態全域緩衝區：大小隨 PAGE_SIZE 自動變為 64 */
__attribute__((aligned(4))) static uint8_t g_data_buf[PAGE_SIZE];

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

    /* 手動處理 Big-Endian 轉換 */
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
    /* 回傳前 8 Byte 作為驗證，其餘 56 Byte 儲存在 Flash 中 */
    for (int i = 0; i < 8; i++) {
        UART_SendChar(USART1, g_data_buf[i]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    /* 填充 64 Bytes 的模擬資料 */
    for (int i = 0; i < PAGE_SIZE; i++) {
        g_data_buf[i] = (uint8_t)(lba + i);
    }
    
    Storage_Write(lba, g_data_buf);
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
```



### main.c
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"
#include "storage.h"

#define RX_BUF_SIZE 1024
/* 接收緩衝區也加上對齊 */
__attribute__((aligned(4))) uint8_t rx_buffer[RX_BUF_SIZE];  
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

    /* 核心邏輯初始化 */
    Storage_Init();
}

int main(void) {
    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe 8KB Page-Mode Online ---\r\n");

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





### host_sender.py
```
import serial
import struct
import time
import random

# 配置區域
PORT = '/dev/ttyUSB0' # Windows 請改為 'COMx'
BAUD = 115200
PAGE_SIZE = 64
TOTAL_CAPACITY = 128  # 128 Pages

def send_pkt(ser, opcode, lba, length, force_bad_cs=False):
    """建構並發送協定封包"""
    pkt = struct.pack('>BBHH', 0xA5, opcode, lba, length)
    if force_bad_cs:
        checksum = (sum(pkt) + 1) & 0xFF
    else:
        checksum = sum(pkt) & 0xFF
    
    ser.write(pkt + struct.pack('B', checksum))
    time.sleep(0.05) # 縮短等待時間提高測試速度

def read_response(ser):
    """讀取並解析回應"""
    time.sleep(0.1)
    if ser.in_waiting > 0:
        raw = ser.read_all()
        text = raw.decode('ascii', errors='ignore').strip()
        # 提取十六進位資料部分
        if "DATA:" in text:
            idx = raw.find(b"DATA:") + 5
            return f"[ACK] DATA: {raw[idx:idx+8].hex(' ').upper()}..."
        return text
    return "[Timeout]"

def run_test_case(name, action_func):
    print(f"\n>>> Running: {name}")
    print("-" * 60)
    action_func()
    print("-" * 60)

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(1)
    ser.reset_input_buffer()

    # --- 測試 1：基礎讀寫與覆蓋 ---
    def test_basic():
        print("Writing to LBA 10...")
        send_pkt(ser, 0x02, 10, PAGE_SIZE)
        print(f"Result: {read_response(ser)}")
        
        print("Reading LBA 10 (Verify)...")
        send_pkt(ser, 0x01, 10, PAGE_SIZE)
        print(f"Result: {read_response(ser)}")

    # --- 測試 2：錯誤注入測試 ---
    def test_errors():
        print("Sending Bad Checksum (Should return CS Mismatch)...")
        send_pkt(ser, 0x02, 5, PAGE_SIZE, force_bad_cs=True)
        print(f"Result: {read_response(ser)}")
        
        print("Sending Invalid Opcode 0x99...")
        send_pkt(ser, 0x99, 5, PAGE_SIZE)
        print(f"Result: {read_response(ser)}")

    # --- 測試 3：全盤寫滿壓力測試 (直到 DISK FULL) ---
    def test_fill_disk():
        print(f"Filling all {TOTAL_CAPACITY} pages...")
        full_flag = False
        for i in range(TOTAL_CAPACITY + 5): # 故意寫超過
            send_pkt(ser, 0x02, i, PAGE_SIZE)
            resp = read_response(ser)
            if "DISK FULL" in resp:
                print(f"Successfully detected [DISK FULL] at LBA {i}")
                full_flag = True
                break
            if i % 20 == 0: print(f"Progress: {i}/{TOTAL_CAPACITY}...")
        
        if not full_flag:
            print("Warning: Disk was not reported full!")

    # --- 測試 4：隨機讀取測試 ---
    def test_random_read():
        print("Randomly reading 5 existing LBAs...")
        for _ in range(5):
            target = random.randint(0, TOTAL_CAPACITY - 1)
            print(f"Reading LBA {target}:", end=" ")
            send_pkt(ser, 0x01, target, PAGE_SIZE)
            print(read_response(ser))

    # 執行所有測試
    print("="*60)
    print("      STM32 FTL Advanced Stress Test Tool")
    print("="*60)
    
    run_test_case("Basic Write/Read & Over-write", test_basic)
    run_test_case("Error Injection Test", test_errors)
    run_test_case("Full Disk Pressure Test", test_fill_disk)
    run_test_case("Random Access Test", test_random_read)

    ser.close()
    print("\n[FINISH] All advanced tests completed.")

except Exception as e:
    print(f"\n[ERROR]: {e}")
```

## BASIC
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

// 1. 加入 aligned(4) 確保硬體存取安全
// 2. 避免使用 memset(sizeof)，改用明確的迴圈初始化，這是最保險的做法
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
__attribute__((aligned(4))) static uint8_t l2p_table[TOTAL_PAGES];
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

void Storage_Init(void) {
    // 分段初始化，避免 memset 造成的大範圍記憶體踩踏
    for (int i = 0; i < TOTAL_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 初始化鏈表
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
    }
    free_list_head = &page_pool[0];
    
    // 手動清除 Flash 內容，不使用 memset(sizeof)
    for(int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for(int p = 0; p < PAGES_PER_BLOCK; p++) {
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
        }
    }
    
    UART_Send(USART1, "[FTL] SSD Logic Initialized.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_PAGES) return;

    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_page();
        if (pba == INVALID_ADDR) return;
        l2p_table[lba] = pba;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;

    for(int i=0; i<PAGE_SIZE; i++) {
        flash_memory[b][p][i] = data[i];
    }
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_PAGES || l2p_table[lba] == INVALID_ADDR) {
        for(int i=0; i<PAGE_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;
    
    for(int i=0; i<PAGE_SIZE; i++) {
        out_buf[i] = flash_memory[b][p][i];
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
}
```


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







