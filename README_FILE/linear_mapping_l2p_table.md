## 三、程式碼
### FTL 核心層 `storage.h` 
```
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// SSD 規格模擬：16 個物理區塊，每個區塊 32 Byte
#define TOTAL_BLOCKS   16       
#define BLOCK_SIZE     32       
#define INVALID_ADDR   0xFF     // 標記未映射狀態

// 物理區塊節點：用於 Linked List 管理空閒空間 (Free Pool)
typedef struct BlockNode {
    uint8_t id;                // 物理區塊 ID (PBA)
    struct BlockNode* next;    // 指向鏈結串列下一個空閒塊
} BlockNode_t;

// FTL 核心 API
void Storage_Init(void);                         // 初始化地圖與空閒鏈表
void Storage_Write(uint16_t lba, uint8_t* data); // 寫入：包含地圖分配
void Storage_Read(uint16_t lba, uint8_t* out_buf); // 讀取：包含地圖查找

#endif
```
### FTL 核心層 `storage.c`
```
#include "storage.h"
#include "usart.h"

// [物理層模擬]：真正的資料儲存區
static uint8_t flash_memory[TOTAL_BLOCKS][BLOCK_SIZE];

// [邏輯層映射]：索引是 LBA，值是 PBA (Physical Block Address)
static uint8_t l2p_table[TOTAL_BLOCKS];

// [空間管理]：Linked List 結構
static BlockNode_t block_pool[TOTAL_BLOCKS];
static BlockNode_t* free_list_head = 0;

void Storage_Init(void) {
    // 1. 初始化 L2P 表，全部設為無效地址
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 2. 初始化空閒鏈表：將所有區塊串接起來
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        block_pool[i].id = i;
        block_pool[i].next = (i < TOTAL_BLOCKS - 1) ? &block_pool[i+1] : 0;
    }
    free_list_head = &block_pool[0];
    
    UART_Send(USART1, "[FTL] L2P & Linked List Ready.\r\n");
}

// 私有函數：從 Linked List 取出一個空閒塊 (Pop)
static uint8_t allocate_block(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    // 邊界檢查：目前僅支援 16 個 LBA
    if (lba >= TOTAL_BLOCKS) {
        UART_Send(USART1, "[ERR] LBA out of range!\r\n");
        return;
    }

    // 若該 LBA 尚未映射，則分配一個實體塊
    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_block();
        if (pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL (No Free PBA)\r\n");
            return;
        }
        l2p_table[lba] = pba;
    }

    // 寫入資料到物理位置
    uint8_t target_pba = l2p_table[lba];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        flash_memory[target_pba][i] = data[i];
    }
    UART_Send(USART1, "[FTL] Write Done.\r\n");
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_BLOCKS || l2p_table[lba] == INVALID_ADDR) {
        UART_Send(USART1, "[ERR] Read Invalid/Unmapped LBA\r\n");
        // 若未映射，回傳全 0
        for(int i=0; i<BLOCK_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba = l2p_table[lba];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        out_buf[i] = flash_memory[pba][i];
    }
}
```


### 通訊協定層 `protocol.h`：
```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7      // 定義每個指令封包固定長度為 7 Byte
#define CMD_START_BYTE  0xA5   // 定義同步字頭，用於在資料流中定位封包起點

/* NVMe 簡化指令集：模擬標準 NVMe 的 Opcode */
#define NVME_OP_READ    0x01    // 讀取指令編碼
#define NVME_OP_WRITE   0x02    // 寫入指令編碼

/* 封包格式：使用 __attribute__((packed)) 確保編譯器不進行位元組對齊填充 */
typedef struct {
    uint8_t  start_byte;   // 偏移 0: 標頭 (1 byte), 0xA5  
    uint8_t  opcode;       // 偏移 1: 操作碼 (1 byte), 0x01: Read, 0x02: Write
    uint16_t lba;          // 偏移 2: 邏輯區塊位址 (2 bytes, 大端序 Big-endian)
    uint16_t length;       // 偏移 4: 資料長度 (2 bytes, Big-endian)
    uint8_t  checksum;     // 偏移 6: 校驗碼 (1 byte)，前 6 bytes 的總和
} __attribute__((packed)) NVMe_Command_t;

// 函式原型宣告
void Protocol_Parse(uint8_t *packet_buf);           // 解析封包主程序
void handle_nvme_read(uint16_t lba, uint16_t len);  // 處理讀取邏輯
void handle_nvme_write(uint16_t lba, uint16_t len); // 處理寫入邏輯

#endif
```

### 通訊協定層 `protocol.c`：負責模擬 SSD 控制器的 運算層 (Execution Layer)
包含指令解析與執行、數據校驗及空間映射
```
#include "protocol.h"
#include "usart.h"
#include "storage.h"  // 對接新的儲存層

// 模擬快閃記憶體 (NAND Flash) 的儲存空間，大小為 512 Bytes
// static uint8_t virtual_disk[512];

void Protocol_Parse(uint8_t *packet_buf) {
    // 將輸入緩衝區強制轉換為結構體指針，方便直接透過名稱存取欄位
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    // 1. 計算 Checksum：將封包前 6 個 byte 累加
    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    // 2. 驗證 Checksum：若計算結果與封包內的 checksum 不符，判定為雜訊或傳輸錯誤
    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] Checksum Mismatch!\r\n");
        UART_Send(USART1, "  Received: 0x");
        UART_SendChar(USART1, cmd->checksum); // 顯示封包帶來的 CS
        UART_Send(USART1, "\r\n  Expected: 0x");
        UART_SendChar(USART1, calculated_cs); // 顯示 STM32 算出的 CS
        UART_Send(USART1, "\r\n");
        return;　// 放棄該封包，不執行指令
    }

    // 3. 處理位元組序 (Endianness)：使用內建指令將大端序(Host)轉為小端序(STM32)
    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

    // 4. 指令派發 (Command Dispatching)
    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        // 若 Opcode 不在定義內，回傳無效指令錯誤
        UART_Send(USART1, "[ERR] INVALID_OP\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    uint8_t read_data[BLOCK_SIZE];
    Storage_Read(lba, read_data); // 透過 FTL 讀取

    UART_Send(USART1, "[ACK] DATA:");
    // 只回傳前 8 Byte 示意，避免 UART 傳輸過久
    for (int i = 0; i < 8; i++) {
        UART_SendChar(USART1, read_data[i]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    uint8_t dummy_data[BLOCK_SIZE];
    // 產生模擬資料：資料內容與 LBA 相關以便驗證
    for (int i = 0; i < BLOCK_SIZE; i++) {
        dummy_data[i] = (uint8_t)(lba + i);
    }
    
    Storage_Write(lba, dummy_data); // 透過 FTL 寫入
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
```

### `main.c`：負責管理底層硬體資源
透過 DMA 環形緩衝區 (Circular Buffer) 技術，在高傳輸負載下確保數據處理的完整性與系統穩定性
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"
#include "storage.h"

#define RX_BUF_SIZE 1024
uint8_t rx_buffer[RX_BUF_SIZE];  // 定義 1KB 的接收緩衝區
uint16_t rd_ptr = 0;             // 軟體讀取指標 (Software Read Pointer)

void System_Init(void){
    /* 硬體底層初始化節點 */
    RCC->APB2ENR |= (1UL << 14);   // 開啟 USART1 時鐘
    RCC->AHBENR  |= (1UL << 0);    // 開啟 DMA1 時鐘
    GPIO_Init_Output(GPIOC, 6);    // 初始化 LED
    GPIO_Init_AF(GPIOA, 9, 1);     // UART1 TX 腳位設定
    GPIO_Init_AF(GPIOA, 10, 1);    // UART1 RX 腳位設定

    // 清除 USART1 所有狀態旗標，防止啟動時的垃圾雜訊導致錯誤
    USART1->ICR |= 0xFFFFFFFF;

    // 初始化 DMA：將 UART->RDR 直接映射到 rx_buffer，循環搬運
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69);     // 設定波特率 115200
    *NVIC_ISER = (1UL << 27);  // 開啟中斷向量表中的 UART1 中斷
    SysTick_Init(8000);        // 1ms 系統滴答

    // 初始化 FTL 核心邏輯
    Storage_Init();
}


int main(void) {

    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe diagnostics Mode ---\r\n");

    while (1) {
        // --- 錯誤處理：硬體溢位 (ORE) ---
        // 當 CPU 處理太慢導致 UART 接收器被塞爆時，觸發此邏輯
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_Send(USART1, "[SYS] ORE_ERROR (Hardware Buffer Full)\r\n");
            // 重置 DMA 傳輸鏈，清空緩衝區重新同步
            DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
            rd_ptr = 0;
        }

        // 獲取目前的 DMA 寫入位置 (Hardware Write Pointer)
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        // 如果讀取與寫入指標不相等，代表有新資料進入
        if (rd_ptr != wr_ptr) {
            // 計算目前緩衝區內有多少剩餘資料
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            // 只有當資料量大於等於一個完整封包 (7 bytes) 才開始解析
            while (available >= PKT_SIZE) {
                // 同步機制：確認第一個 Byte 必須是 0xA5
                if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                    Protocol_Parse(&rx_buffer[rd_ptr]);         // 進入協定解析
                    rd_ptr = (rd_ptr + PKT_SIZE) % RX_BUF_SIZE; // 指標後移
                    available -= PKT_SIZE;
                } else {
                    // 如果不是 0xA5，視為無效數據並跳過，代表資料流位移，逐位元尋找標頭
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


### 驗證與自動化測試 `host_sender.py`
 Host Driver (主機驅動程式) 為驅動模擬腳本，生成各種邊界測試案例，驗證 Device 端的穩定性。負責將抽象的指令封裝為符合規格的二進位流 (Binary Stream)，並透過 Python 實作自動化測試與 **錯誤注入 (Error Injection)** 機制，並透過串口送給 STM32 驗證

```
import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    """
    精確測試函式：支援二進位資料與文字混合解析
    """
    print(f"-> {name:20} (LBA={lba:<3})", end=': ')
    
    # 建立封包
    actual_op = 0x99 if force_bad_op else opcode
    pkt = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # 計算 Checksum
    if force_bad_cs:
        checksum = (sum(pkt) + 1) & 0xFF
    else:
        checksum = sum(pkt) & 0xFF
        
    full_pkt = pkt + struct.pack('B', checksum)
    
    try:
        ser.write(full_pkt)
        
        # 增加延時確保 STM32 完成 FTL 操作與 UART 傳輸
        time.sleep(0.3)
        
        if ser.in_waiting > 0:
            raw_data = ser.read_all()
            
            # --- 混合解析邏輯 ---
            # 嘗試將原始資料轉為文字觀察
            text_part = raw_data.decode('ascii', errors='ignore').strip()
            
            # 狀況 A: 針對 [ACK] DATA: 的特殊處理 (解決資料看不見的問題)
            if "DATA:" in text_part:
                # 找出 DATA: 字串的位置，取出後面的二進位部分
                header_len = raw_data.find(b"DATA:") + 5
                payload = raw_data[header_len:]
                print(f"Result: [ACK] DATA: {payload.hex(' ').upper()}")
            
            # 狀況 B: 針對 Checksum Mismatch 的數值處理
            elif "Received: 0x" in text_part:
                # 重新格式化輸出，將不可見的數值轉為 Hex
                # 找出 Received: 0x 後面那個 Byte
                rx_idx = raw_data.find(b"Received: 0x") + 12
                ex_idx = raw_data.find(b"Expected: 0x") + 12
                # 確保不越界
                rx_val = raw_data[rx_idx] if rx_idx < len(raw_data) else 0
                ex_val = raw_data[ex_idx] if ex_idx < len(raw_data) else 0
                print(f"Result: [ERR] Checksum Mismatch! Received: 0x{rx_val:02X}, Expected: 0x{ex_val:02X}")

            # 狀況 C: 一般文字訊息
            else:
                # 過濾掉可能干擾顯示的控制字元
                clean_text = "".join(ch for ch in text_part if ch.isprintable() or ch in "\r\n")
                print(f"Result: {clean_text.strip()}")
        else:
            print("Result: [Timeout] No response from STM32")
            
    except Exception as e:
        print(f"Result: [Exception] {e}")

# ========================================
# 主測試流程
# ========================================
try:
    # 串口設定 (請確保 Minicom 已關閉)
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1) 
    ser.reset_input_buffer()

    print("="*50)
    print("  SSD Simulator Stage 2: FTL & Protocol Integrity Test")
    print("="*50)

    # 1. 基本功能測試
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 5, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 5, 8)

    # 2. FTL 空間管理測試 (PBA 分配與 LBA 限制)
    print("\n--- Filling SSD (PBA Allocation) ---")
    for i in range(17):
        # 注意：正確的參數順序是 (name, ser, opcode, lba, length)
        test_nvme(f"Fill-Test-{i}", ser, 0x02, i, 8)

    # 3. 邊界與異常測試
    print("\n--- Edge Case Test ---")
    test_nvme("Read Out of Range", ser, 0x01, 99, 8)
    test_nvme("Read Unmapped",    ser, 0x01, 30, 8)

    # 4. 通訊魯棒性測試
    print("\n--- Protocol Robustness Test ---")
    test_nvme("CHECKSUM ATTACK",  ser, 0x02, 6, 8, force_bad_cs=True)
    test_nvme("INVALID OPCODE",   ser, 0x99, 0, 0, force_bad_op=True)

    # 5. ORE 硬體溢位壓力測試
    print("\n--- ORE OVERFLOW TEST ---")
    print("-> Sending 2000 bytes garbage to trigger Overrun...")
    ser.write(b'X' * 2000)
    # 給予較長延時，因為 STM32 需要偵測錯誤、進入主迴圈、執行重置
    time.sleep(1.0) 
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")
    else:
        print("Result: No response (Check if LED stopped blinking - HardFault?)")

    ser.close()
    print("\n" + "="*50)
    print("           All Tests Completed")
    print("="*50)

except Exception as e:
    print(f"\n[FATAL ERROR]: {e}")
```
