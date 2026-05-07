# FTL with NVMe-like protocol
實作基於 STM32F072 的輕量級 SSD 模擬器，技術包含 L2P (Logical to Physical) 映射表、Out-of-place Update (異地更新) 以及具備 50% Over-Provisioning (OP) 空間的 Garbage Collection (垃圾回收) 機制。

## L2P Table、Garbage Collection
### (1) Page-Level FTL (Flash Translation Layer)
模擬真實 NAND Flash 的物理限制 (Program after Erase)，實作地址轉換邏輯
- **L2P Mapping**：透過映射表將主機端的邏輯位址 (LBA) 轉換為 Flash 內部的物理位址 (PBA)
- **物理空間管理**：使用 `PageNode_t` 鏈結串列維護 Free List，實現 $O(1)$ 時間複雜度的頁面分配

### (2) Out-of-place Update (異地更新)
符合 Flash 寫入前必須抹除的特性，不可執行原地覆寫（In-place Update）
- 每次寫入請求都會從 Free List 分配一個全新的 PBA
- 舊的資料頁面被標記為 `STATE_DIRTY`，不再被映射表指向，等待後續回收

### (3) 50% Over-Provisioning (OP) 與垃圾回收
- **空間配置**：總容量 64 Pages，使用者僅可見 32 Pages，其餘保留給 GC 機制
- **GC 機制**：當物理空閒頁面耗盡時，自動觸發垃圾回收程序。系統會掃描所有 `STATE_DIRTY` 頁面，執行抹除並還原至 Free List 中，確保系統持續運行

## 通訊協定 (NVMe-like Protocol)
|偏移量|欄位名稱|長度|說明|
|:---|:---|:---|:---|
|0|Start Byte|1B|固定值 0xA5|
|1|Opcode|1B|`0x01: Read`, `0x02: Write`|
|2-3|LBA|2B|邏輯區塊位址 (Big-Endian)|
|4-5|Length|2B|資料長度 (Big-Endian)|
|6|Checksum|1B|前 6 Bytes 之累積和 (Mod 256)|

## 自動化測試驗證
Python 測試腳本 `host_sender.py`，支援以下測試階段
- STEP 1 **(CRUD)**：驗證單一頁面的讀寫一致性。
- STEP 2 **(Pressure Fill)**：填滿所有使用者 LBA 位址，測試邊界處理。
- STEP 3 **(Error Injection)**：注入錯誤的 Checksum 或無效 Opcode，驗證協定韌性。
- STEP 5 **(Hardware Resilience)**：模擬高流量導致的 UART Overrun，驗證 DMA 自動重置功能。
- STEP 6 **(GC Stress Test)**：連續複寫相同 LBA，迫使系統耗盡物理頁面並觀察 GC 觸發與資料完整性。

## 程式碼
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


    t.header("STEP 6: Garbage Collection & Out-of-Place Test")
    print(f" {Color.GRAY}-> Target: Force GC by overwriting the same LBA repeatedly.{Color.END}")
    print(f" {Color.GRAY}-> Logic: Physical pages (64) will be exhausted soon.{Color.END}")
    
    # 重複寫入同一個位址，迫使 FTL 不斷分配新頁面直到消耗完所有空閒頁面 (Free List)
    # 物理總量 64, User 已佔用部分, 寫入 50 次足以觸發至少一次 GC
    OVERWRITE_COUNT = 50 
    gc_triggered = False
    
    for i in range(OVERWRITE_COUNT):
        t._send_cmd(0x02, 7, PAGE_SIZE) # 狂寫 LBA 7
        raw, text, _ = t._receive_resp()
        if "GC" in text:
            gc_triggered = True
            print(f" [{Color.GREEN}INFO{Color.END}] GC Triggered at write index: {i}")
        time.sleep(0.005)

    # 最後檢查資料是否依然正確（GC 不應破壞最新資料）
    t.run_unit("GC", "Post-GC Data Consistency", 0x01, 7, check_content=True)
    
    if gc_triggered:
        print(f" [{Color.GREEN}PASS{Color.END}] Garbage Collection was successfully exercised.")
    else:
        print(f" [{Color.YELLOW}WARN{Color.END}] GC was not detected. Try increasing OVERWRITE_COUNT.")


    print(f"\n{Color.BOLD}{'='*75}{Color.END}")
    print(f"  {Color.BOLD}UNIT TEST RESULT - {datetime.now().strftime('%H:%M:%S')}{Color.END}")
    print(f"  Total: {t.stats['total']} | Passed: {Color.GREEN}{t.stats['pass']}{Color.END} | Failed: {Color.RED}{t.stats['fail']}{Color.END}")
    print(f"{Color.BOLD}{'='*75}{Color.END}\n")
    t.ser.close()
```


