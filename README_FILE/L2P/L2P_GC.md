# L2P , Out-of-place update, and GC (Over-Provisioning)

### storage.h
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* SSD 規格：4 Blocks, 16 Pages per Block, 32 Bytes per Page */
/* 總物理容量 = 4 * 16 * 32 = 2048 Bytes (2KB) */
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     16    
#define PAGE_SIZE           32    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) // 64 Pages

/* Over-Provisioning: 50% 空間留給 GC，僅開放 50% 給使用者 */
#define USER_PAGES          (TOTAL_PAGES / 2) // 32 Pages

#define INVALID_ADDR        0xFF

/* 新增頁面狀態 */
#define STATE_FREE          0  // 乾淨且可用
#define STATE_VALID         1  // 存有最新資料 (L2P 映射中)
#define STATE_DIRTY         2  // 舊資料，可被回收 (Invalid)

/* 物理頁面節點 */
typedef struct PageNode {
    uint8_t id;                
    struct PageNode* next;     
} __attribute__((aligned(4))) PageNode_t;

/* FTL 核心 API */
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);
void Storage_GC(void); // 暴露 GC 接口供手動或自動觸發

#endif
```



### storage.c
```
#include "storage.h"
#include "usart.h"
#include <string.h>

/* 物理空間與映射表 */
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
/* L2P 表僅需對應使用者可見的 LBA 數量 */
__attribute__((aligned(4))) static uint8_t l2p_table[USER_PAGES];
/* 物理頁面池包含所有頁面 (含預留空間) */
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

/* 新增：追蹤每個物理頁面 (PBA) 的狀態 */
__attribute__((aligned(4))) static uint8_t page_status[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

/* 輔助函數：將頁面丟回 Free List 前端 */
static void release_to_free_list(uint8_t pba) {
    page_status[pba] = STATE_FREE;
    page_pool[pba].next = free_list_head;
    free_list_head = &page_pool[pba];
}

void Storage_Init(void) {
    /* 1. 初始化 L2P 表 (映射 64 個 LBA) */
    for (int i = 0; i < USER_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    /* 2. 初始化空閒頁面鏈表 (128 個物理頁面全部進入 Free List) */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
        page_status[i] = STATE_FREE; // 初始化全部為 Free
    }
    free_list_head = &page_pool[0];
    
    /* 3. 清除 Flash 內容 */
    for(int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for(int p = 0; p < PAGES_PER_BLOCK; p++) {
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
        }
    }
    
    UART_Send(USART1, "[FTL] 2KB SSD Initialized. User: 50%, Reserved: 50%. Out-of-place Write Enabled.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

/* 簡易垃圾回收：抹除所有 DIRTY 頁面並還給 Free List */
void Storage_GC(void) {
    UART_Send(USART1, "[GC] Starting Garbage Collection...\r\n");
    int reclaimed = 0;
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (page_status[i] == STATE_DIRTY) {
            // 模擬抹除動作：將資料填回 0xFF
            uint8_t b = i / PAGES_PER_BLOCK;
            uint8_t p = i % PAGES_PER_BLOCK;
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
            
            // 還給空閒鏈表
            release_to_free_list(i);
            reclaimed++;
        }
    }
    UART_Send(USART1, "[GC] Finished. Reclaimed pages.\r\n");
}



void Storage_Write(uint16_t lba, uint8_t* data) {
    /* 邊界檢查：僅允許存取 0 ~ USER_PAGES-1 */
    if (lba >= USER_PAGES) {
        UART_Send(USART1, "[ERR] LBA Out of User Range\r\n");
        return;
    }

    /* 1. 每次寫入都先嘗試分配新頁面 */
    uint8_t new_pba = allocate_page();

    /* 2. 如果沒有空閒頁面，觸發 GC */
    if (new_pba == INVALID_ADDR) {
        Storage_GC();
        new_pba = allocate_page();
        if (new_pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL ! Out of Physical Space!\r\n");
            return;
        }
    }

    /* 3. 處理舊資料：如果 LBA 之前有對應的 PBA，標記舊 PBA 為 DIRTY */
    if (l2p_table[lba] != INVALID_ADDR) {
        uint8_t old_pba = l2p_table[lba];
        page_status[old_pba] = STATE_DIRTY;
        // 注意：這裡不馬上釋放回 Free List，因為 Flash 需要 Block Erase 才能再寫
    }

    /* 4. 更新 L2P 與狀態 */
    l2p_table[lba] = new_pba;
    page_status[new_pba] = STATE_VALID;

    /* 5. 執行物理寫入 */
    uint8_t b = new_pba / PAGES_PER_BLOCK;
    uint8_t p = new_pba % PAGES_PER_BLOCK;

    for(int i = 0; i < PAGE_SIZE; i++) {
        flash_memory[b][p][i] = data[i];
    }
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    /* 邊界檢查 */
    if (lba >= USER_PAGES || l2p_table[lba] == INVALID_ADDR) {
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

    Storage_Init();
}

int main(void) {
    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe 8KB Mode (50%% User Space) ---\r\n");

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
####
```
import serial
import struct
import time
import random
from datetime import datetime

# --- ANSI 顏色配置 ---
class Color:
    PURPLE = '\033[95m'
    BLUE   = '\033[94m'
    CYAN   = '\033[96m'
    GREEN  = '\033[92m'
    YELLOW = '\033[93m'
    RED    = '\033[91m'
    END    = '\033[0m'
    BOLD   = '\033[1m'
    GRAY   = '\033[90m'

# --- 測試配置 ---
PORT = '/dev/ttyUSB0' 
BAUD = 115200
USER_CAPACITY = 32
PAGE_SIZE = 32

class FTLUnitTester:
    def __init__(self):
        try:
            self.ser = serial.Serial(PORT, BAUD, timeout=0.1)
            time.sleep(1)
            self.ser.reset_input_buffer()
            self.stats = {"pass": 0, "fail": 0, "total": 0}
        except Exception as e:
            print(f"{Color.RED}[FATAL]{Color.END} Serial Error: {e}")
            exit(1)

    def _format_hex(self, data):
        return data.hex(' ').upper() if data else "NONE"

    def _send_cmd(self, op, lba, length, bad_cs=False):
        pkt = struct.pack('>BBHH', 0xA5, op, lba, length)
        cs = (sum(pkt) + (1 if bad_cs else 0)) & 0xFF
        full_pkt = pkt + struct.pack('B', cs)
        self.ser.write(full_pkt)
        return full_pkt

    def _receive_resp(self, expected_data=False):
        raw = b""
        start_time = time.time()
        while (time.time() - start_time) < 0.6: 
            if self.ser.in_waiting > 0:
                raw += self.ser.read(self.ser.in_waiting)
                if expected_data and b"DATA:" in raw:
                    if len(raw.split(b"DATA:")[1]) >= PAGE_SIZE: break
                elif not expected_data and len(raw) > 5: break
            time.sleep(0.01)

        text = raw.decode('ascii', errors='ignore').strip()
        data_payload = None
        if b"DATA:" in raw:
            idx = raw.find(b"DATA:") + 5
            data_payload = raw[idx : idx + PAGE_SIZE]
        return raw, text, data_payload

    def run_unit(self, tag, name, op, lba, length=PAGE_SIZE, expect="ACK", check_content=False, bad_cs=False, bad_op=False):
        self.stats["total"] += 1
        opcode = 0x99 if bad_op else op
        
        tx_raw = self._send_cmd(opcode, lba, length, bad_cs)
        rx_raw, rx_text, rx_data = self._receive_resp(expected_data=check_content)
        
        is_pass = False
        if expect in rx_text:
            is_pass = True
        elif check_content and rx_data and rx_data[0] == lba:
            is_pass = True
        
        status_str = f"{Color.GREEN}PASS{Color.END}" if is_pass else f"{Color.RED}FAIL{Color.END}"
        print(f"[{Color.BLUE}{tag:6}{Color.END}] {name:32} | LBA:{lba:<3} | [{status_str}]")

        if rx_data and is_pass:
            print(f"       {Color.GRAY}└─ DATA: {rx_data[:16].hex(' ').upper()} ... (32B){Color.END}")
        
        if tag in ["EDGE", "PROT", "RECO"] or not is_pass:
            if rx_text: print(f"       {Color.CYAN}└─ MSG: \"{rx_text}\"{Color.END}")
            print(f"       {Color.GRAY}└─ RAW: [{self._format_hex(rx_raw)}]{Color.END}")

        if is_pass: self.stats["pass"] += 1
        else: self.stats["fail"] += 1

    def header(self, title):
        print(f"\n{Color.BOLD}{Color.PURPLE}== {title} =={Color.END}")
        print(f"{Color.GRAY}{'-'*75}{Color.END}")

if __name__ == "__main__":
    t = FTLUnitTester()
    print(f"\n{Color.YELLOW}{Color.BOLD}STM32 FTL Logic & Protocol Unit Testing Suite v3.0{Color.END}")
    print(f"Config: {PORT} @ {BAUD} | Page Size: {PAGE_SIZE} Bytes")

    t.header("STEP 1: Basic Functionality (CRUD)")
    t.run_unit("IO", "Single Page Write", 0x02, 10)
    t.run_unit("IO", "Single Page Read & Verify", 0x01, 10, check_content=True)

    t.header("STEP 2: Address Space Pressure Fill")
    print(f" {Color.GRAY}-> Filling Range 0-31...{Color.END}")
    for i in range(USER_CAPACITY):
        t._send_cmd(0x02, i, PAGE_SIZE)
        if i % 10 == 0: print(f"    Progress: {i}/{USER_CAPACITY}")
        time.sleep(0.01)
    t.run_unit("FILL", "Full Range Integrity Check", 0x01, 31, check_content=True)

    t.header("STEP 3: Boundary & Error Injection")
    t.run_unit("EDGE", "LBA Out of Range Test", 0x02, 32, expect="ERR")
    t.run_unit("PROT", "Protocol Checksum Attack", 0x02, 5, bad_cs=True, expect="CS")
    t.run_unit("PROT", "Invalid Opcode Handling", 0x02, 0, bad_op=True, expect="OP")

    t.header("STEP 4: Random Access Verification")
    for s in sorted(random.sample(range(USER_CAPACITY), 4)):
        t.run_unit("RAND", f"Random Consistency Check", 0x01, s, check_content=True)

    t.header("STEP 5: System Resilience (Hardware ORE)")
    print(f" {Color.GRAY}-> Triggering 2000-byte UART Overrun...{Color.END}")
    t.ser.write(b'\xFF' * 2000)
    time.sleep(1.2)
    t.run_unit("RECO", "Post-Overrun Auto-Recovery", 0x01, 10, expect="RECOVER")

    print(f"\n{Color.BOLD}{'='*75}{Color.END}")
    print(f"  {Color.BOLD}UNIT TEST RESULT - {datetime.now().strftime('%H:%M:%S')}{Color.END}")
    print(f"  Total: {t.stats['total']} | Passed: {Color.GREEN}{t.stats['pass']}{Color.END} | Failed: {Color.RED}{t.stats['fail']}{Color.END}")
    print(f"{Color.BOLD}{'='*75}{Color.END}\n")
    t.ser.close()
```


#### SSD
```
import serial
import struct
import time
import random
from datetime import datetime

# --- ANSI 顏色配置 ---
class Color:
    PURPLE = '\033[95m'
    BLUE   = '\033[94m'
    CYAN   = '\033[96m'
    GREEN  = '\033[92m'
    YELLOW = '\033[93m'
    RED    = '\033[91m'
    END    = '\033[0m'
    BOLD   = '\033[1m'
    GRAY   = '\033[90m'

# --- 測試配置 ---
PORT = '/dev/ttyUSB0' 
BAUD = 115200
USER_CAPACITY = 32
PAGE_SIZE = 32

class FTLUnitTester:
    def __init__(self):
        try:
            self.ser = serial.Serial(PORT, BAUD, timeout=0.1)
            time.sleep(1)
            self.ser.reset_input_buffer()
            self.stats = {"pass": 0, "fail": 0, "total": 0}
        except Exception as e:
            print(f"{Color.RED}[FATAL]{Color.END} Serial Error: {e}")
            exit(1)

    def _format_hex(self, data):
        return data.hex(' ').upper() if data else "NONE"

    def _send_cmd(self, op, lba, length, bad_cs=False):
        pkt = struct.pack('>BBHH', 0xA5, op, lba, length)
        cs = (sum(pkt) + (1 if bad_cs else 0)) & 0xFF
        full_pkt = pkt + struct.pack('B', cs)
        self.ser.write(full_pkt)
        return full_pkt

    def _receive_resp(self, expected_data=False):
        raw = b""
        start_time = time.time()
        while (time.time() - start_time) < 0.6: # 增加到 600ms 確保長資料完整
            if self.ser.in_waiting > 0:
                raw += self.ser.read(self.ser.in_waiting)
                if expected_data and b"DATA:" in raw:
                    if len(raw.split(b"DATA:")[1]) >= PAGE_SIZE: break
                elif not expected_data and len(raw) > 5: break
            time.sleep(0.01)

        text = raw.decode('ascii', errors='ignore').strip()
        data_payload = None
        if b"DATA:" in raw:
            idx = raw.find(b"DATA:") + 5
            data_payload = raw[idx : idx + PAGE_SIZE]
        return raw, text, data_payload

    def run_unit(self, tag, name, op, lba, length=PAGE_SIZE, expect="ACK", check_content=False, bad_cs=False, bad_op=False):
        self.stats["total"] += 1
        opcode = 0x99 if bad_op else op
        
        tx_raw = self._send_cmd(opcode, lba, length, bad_cs)
        rx_raw, rx_text, rx_data = self._receive_resp(expected_data=check_content)
        
        # 邏輯判定
        is_pass = False
        if expect in rx_text:
            is_pass = True
        elif check_content and rx_data and rx_data[0] == lba:
            is_pass = True
        
        # 視覺化輸出
        status_str = f"{Color.GREEN}PASS{Color.END}" if is_pass else f"{Color.RED}FAIL{Color.END}"
        # 嚴格對齊排版：Tag(8), Name(32), LBA(8), Status
        print(f"[{Color.BLUE}{tag:6}{Color.END}] {name:32} | LBA:{lba:<3} | [{status_str}]")

        # 輸出詳細 Data (僅讀取測試)
        if rx_data and is_pass:
            print(f"       {Color.GRAY}└─ DATA: {rx_data[:16].hex(' ').upper()} ... (32B){Color.END}")
        
        # 輸出錯誤訊息或異常 Raw (僅 EDGE/PROT/RECO 或 FAIL 時)
        if tag in ["EDGE", "PROT", "RECO"] or not is_pass:
            if rx_text: print(f"       {Color.CYAN}└─ MSG: \"{rx_text}\"{Color.END}")
            print(f"       {Color.GRAY}└─ RAW: [{self._format_hex(rx_raw)}]{Color.END}")

        if is_pass: self.stats["pass"] += 1
        else: self.stats["fail"] += 1

    def header(self, title):
        print(f"\n{Color.BOLD}{Color.PURPLE}== {title} =={Color.END}")
        print(f"{Color.GRAY}{'-'*75}{Color.END}")

# --- 測試執行流 ---
if __name__ == "__main__":
    t = FTLUnitTester()
    print(f"\n{Color.YELLOW}{Color.BOLD}STM32 FTL Logic & Protocol Unit Testing Suite v3.0{Color.END}")
    print(f"Config: {PORT} @ {BAUD} | Target: 11th Grade IT Vocational Lab")

    # STEP 1: 基本功能
    t.header("STEP 1: Basic Functionality (CRUD)")
    t.run_unit("IO", "Single Page Write", 0x02, 10)
    t.run_unit("IO", "Single Page Read & Verify", 0x01, 10, check_content=True)

    # STEP 2: 壓力測試 (自動填充)
    t.header("STEP 2: Address Space Pressure Fill")
    print(f" {Color.GRAY}-> Filling Range 0-63...{Color.END}")
    for i in range(USER_CAPACITY):
        t._send_cmd(0x02, i, PAGE_SIZE)
        if i % 20 == 0: print(f"    Progress: {i}/{USER_CAPACITY}")
        time.sleep(0.01)
    t.run_unit("FILL", "Full Range Integrity Check", 0x01, 63, check_content=True)

    # STEP 3: 邊界與異常 (補齊單元測試項目)
    t.header("STEP 3: Boundary & Error Injection")
    t.run_unit("EDGE", "LBA Out of Range Test", 0x02, 64, expect="ERR")
    t.run_unit("IO",   "Unmapped LBA Read Test", 0x01, 50, check_content=True) # 補齊此項
    t.run_unit("PROT", "Protocol Checksum Attack", 0x02, 5, bad_cs=True, expect="CS")
    t.run_unit("PROT", "Invalid Opcode Handling", 0x02, 0, bad_op=True, expect="OP")

    # STEP 4: 隨機讀寫驗證
    t.header("STEP 4: Random Access Verification")
    for s in sorted(random.sample(range(USER_CAPACITY), 4)):
        t.run_unit("RAND", f"Random Consistency Check", 0x01, s, check_content=True)

    # STEP 5: 系統韌性
    t.header("STEP 5: System Resilience (Hardware ORE)")
    print(f" {Color.GRAY}-> Triggering 2000-byte UART Overrun...{Color.END}")
    t.ser.write(b'\xFF' * 2000)
    time.sleep(1.2)
    t.run_unit("RECO", "Post-Overrun Auto-Recovery", 0x01, 10, expect="ORE")

    # 總結
    print(f"\n{Color.BOLD}{'='*75}{Color.END}")
    print(f"  {Color.BOLD}UNIT TEST RESULT - {datetime.now().strftime('%H:%M:%S')}{Color.END}")
    print(f"  Total: {t.stats['total']} | Passed: {Color.GREEN}{t.stats['pass']}{Color.END} | Failed: {Color.RED}{t.stats['fail']}{Color.END}")
    print(f"  Score: {(t.stats['pass']/t.stats['total'])*100:.1f}%")
    print(f"{Color.BOLD}{'='*75}{Color.END}\n")
    t.ser.close()
```
