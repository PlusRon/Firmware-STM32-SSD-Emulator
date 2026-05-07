# 模擬 NVMe 通訊協定規範 (Protocol Specification)

- #### High-Efficiency Data Flow
  - Implemented **DMA Circular Buffers** and a **Producer-Consumer model** to minimize CPU overhead during 115200 bps UART transmission
- #### Cross-Platform Data Integrity
  - **Checksum** verification and handled **Endianness** challenges between PC and STM32
- #### System Robustness
  - **UART ORE (Overrun Error) self-healing mechanism** and applied **Defensive Programming** performs input validity checks and limits access length through **QoS** policies to prevent system hanging from malicious inputs
- #### Automation
  - Python-based Host Driver(**pySerial**) for **Negative Testing**, **Error Injection** and **automated validation** of the **NVMe read/write** command set



## 一、環境建置
### 查看 python 版本
```
python3 --version
```
### 下載 Python 的套件管理工具
```
sudo apt install python3-pip -y
pip --version
```

### 安裝 pyserial
  - **Linux 系統的安全性與穩定性 安裝**
    ```
    # 針對個人使用者安裝（建議）
    pip install pyserial --break-system-packages
    
    # 或者使用 apt 安裝系統層級的套件（某些 Linux 發行版推薦）
    sudo apt install python3-serial
    ```
  - **虛擬環境 (Virtual Environment, venv) 安裝**
    
    ```
    # 1. 在 host 資料夾下建立一個虛擬空間 (.venv)
    python3 -m venv .venv
    
    # 2. 啟動這個空間 (啟動後你的終端機前面會出現 (.venv) 字樣)
    source .venv/bin/activate
    
    # 3. 這時候你就可以「直接」安裝，不用加任何危險參數！
    pip install -r requirements.txt
    ```
    - pyserial 只會存在於 .venv 資料夾裡，完全不會碰到 Linux 系統
### 列出清單, 搜尋已安裝的套件
```
pip list | grep pyserial
```

### 檢查 pyserial 是否可執行
```
python3 -c 'import serial; print("Serial version:", serial.__version__)'
python3 -c 'import serial; print("SUCCESS: Pyserial is ready to use!")'
```
```
$ python3
>>> import serial
>>> print(serial.__version__)
3.5
>>> exit() 
```
### 查看套件詳細資訊
認它安裝在哪個路徑（是否裝在正在用的 Python 版本下）
```
pip show pyserial
:
Name: pyserial
Version: 3.5
Summary: Python Serial Port Extension
Home-page: https://github.com/pyserial/pyserial
Author: Chris Liechti
Author-email: cliechti@gmx.net
License: BSD
Location: /usr/lib/python3/dist-packages
Requires: 
Required-by: 
```

## 二、開發板權限設定 
### 查看 使用者端 的 身份權限設定
- #### 確認目前帳號
  
  ```
  whoami
  ```
  - 確認目前這個終端機視窗是以哪個使用者身分在執行, 確保沒有誤用 `root`
- #### 確認帳號擁有控制哪些群組的權限
  ```
  groups
  :
  dino adm cdrom sudo dip plugdev lpadmin lxd dialout
  ```
- #### 查看詳細身分資訊
  
  ```
  dino$ id
  :
  uid=1000(dino) gid=1000(dino) groups=1000(dino),4(adm),24(cdrom),27(sudo),30(dip),46(plugdev),100(users),114(lpadmin)
  ```
  ```
  root$ id
  :
  uid=0(root) gid=0(root) groups=0(root)
  ```
   - 一次看完 **使用者 ID (uid)**、**主要群組 (gid)** 以及 **所有附加群組**

### 確認串口權限
Linux 執行 `pyserial` 存取 `/dev/ttyUSB0` (STM32) 時，最常卡住的是 **Permission Denied**
- #### 查看串口設備檔案 `/dev/ttyUSB0` 的詳細屬性，被歸類為哪個群組
  ```
  ls -l /dev/ttyUSB0
  :
  crw-rw---- 1 root dialout ...
  ```
  - `/dev/ttyUSB0` 是 Linux 幫 STM32 建立的 **虛擬檔案 (字元設備檔)**
  - 只要目前使用者帳號的 groups 中有 dialout，就能讀寫它
- #### 將帳號加入串口群組中，就可不需要 sudo 也可操控串口裝置
  ```
  sudo usermod -a -G dialout $USER
  ```
  - Linux 預設不允許一般使用者亂動硬體，將使用者帳號加入 `dialout` 群組後，就擁有對 `/dev/ttyUSB0` 的讀寫權限
  - 必須 **重新登入** 或是 **重新啟動 Linux** 才會生效
  - Linux 的權限清單(Token) 只會在登入時載入一次, 即便執行 `usermod`，舊視窗依然拿著 **沒權限的舊身分證**, 不重登，指令就不會生效
  - 當 dino 執行的 Python 腳本, 想去動 `/dev/ttyUSB0` 時，系統會檢查身分，發現在 `dialout` 名單內，就允許通過
  - 指令拆解 : 請管理員將 **可操作串口的群組** 授權給 **目前帳號**
    - `sudo` ： 使用管理員權限執行
    - `usermod` ： User Modify，修改使用者的設定
    - `-a` ：Append，代表 **追加**
    - `-G` ：Group，代表 **群組**
    - `dialout` ：這是 Linux 系統中專門 **管理通訊設備 (UART, Modem)** 的群組
    - `$USER` ：是一個環境變數，會自動帶入現在登入的帳號名稱 (例如 dino)
  - **方法二** ： 不想重開機，直接將串口的鎖撬開
    ```
    sudo chmod 666 /dev/ttyUSB0
    ```
    - `666` : 代表讓 **所有人** 都可以讀寫這個設備,為暫時性
    - 當把 STM32 拔掉重插，或者是電腦重開機，這個設定就會消失

## 三、程式碼

### `protocol.h`：通訊協定的規格定義
定義了 Host (電腦) 與 Device (STM32) 溝通的語言格式
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
- #### 記憶體對齊與 Packed Structure
  - 在 32 位元系統中，編譯器預設會進行 Alignment (將 `uint8_t` 後面填充 **1 byte** 空間來對齊 2 bytes 的 uint16_t)
  - 通訊協定中的資料必須緊密排列的，因此使用 `__attribute__((packed))` 強制緊湊排列，確保軟體解析位址與物理傳輸流完全一致
  - 避免結構體大小會從 7 bytes 變成 8 bytes，導致 LBA 的解析偏移出錯

- #### 邏輯定址與長度 (LBA vs. Length)
  |**欄位名稱**|**技術定義**|
  |:---|:---|
  |**LBA (Logical Block Address)**|指令在虛擬磁碟中操作的起始物理位置|
  |**Length**|從 LBA 位址開始，連續操作的區塊數量|

- #### 位元組序挑戰 (Endianness)
  - **Big-Endian (大端序)**：通訊協定(NVMe, TCP/IP...網路位元組序)標準，高位元組(Most Significant Byte, MSB)放在低位址
  - **Little-Endian (小端序)**：STM32 (ARM) 和 x86 電腦在記憶體內部處理資料的方式，資料的低位元組 (LSB) 放在低位址
  - **通訊協定為了標準化，規定使用 Big-Endian(網路位元組序)傳輸**，而處理器(STM32)是 Little-Endian 架構，必須在 STM32 軟體層進行 Byte Swap 操作，以正確解析 Host 傳來的 LBA 和 Length 數值
  - **Multi-byte Data Types (多位元組資料型別)** 需進行 Endianness 轉換
    - `start_byte` 與 `opcode` 屬於 `uint8_t` (1 byte)，在記憶體中不存在順序問題
    - `lba` 與 `length` 為 `uint16_t` (2 bytes)，因為 Host 以 Big-Endian 送出，STM32 讀取 16-bit 暫存器時會產生高低位元倒置，因此必須調用 `__builtin_bswap16` 進行手動翻轉

### `protocol.c`：負責模擬 SSD 控制器的 運算層 (Execution Layer)
包含指令解析與執行、數據校驗及空間映射
```
#include "protocol.h"
#include "usart.h"

// 模擬快閃記憶體 (NAND Flash) 的儲存空間，大小為 512 Bytes
static uint8_t virtual_disk[512];

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
        UART_Send(USART1, ")\r\n");
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
    // 將長度限制在「虛擬磁碟的大小」以內，防止非法存取
    // uint16_t safe_len = (len > 512) ? 512 : len;

    UART_Send(USART1, "[ACK] READ_OK:");
    // 限制讀取長度，避免非法存取，並循環模擬磁碟空間
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        UART_SendChar(USART1, virtual_disk[(lba + i) % 512]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    // 模擬寫入邏輯：將 LBA 地址轉換為資料存入，用於後續讀取驗證
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        virtual_disk[(lba + i) % 512] = (uint8_t)(lba + i);
    }
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
```
- #### 數據校驗機制 (Checksum Verification)
  - 確保指令在傳輸路徑中的完整性 (Signal Integrity)，實作 8-bit 累加校驗
  - **診斷型回應 (Diagnostic Response)**：當 Checksum 失敗時，系統不僅回傳錯誤代碼，也回傳 STM32 計算出的預期值 (Expected) 與 實際接收值 (Received)
    |預期值與 Host 一致|預期值與 Host 不符|
    |:---:|:---:|
    |代表數據段正確，僅校驗碼位元受損|代表指令數據 (Payload) 遭受雜訊干擾，導致 Bit Flip|
- #### 位元組序翻轉 (Endianness Conversion)
  - 通訊協定（如 Python `struct.pack`）預設使用 大端序 (Big-Endian) 傳輸，而 ARM Cortex-M (STM32) 為 小端序 (Little-Endian) 架構，系統必須進行顯式轉換
  - 使用 **編譯器內建** 指令 `__builtin_bswap16` 進行硬體級翻轉，避免位址解析錯誤（例如將 LBA 100 解析為 25600），確保地址映射的精確性
- #### 多層級緩衝區架構 (Buffer Management)
  |RX_BUF_SIZE (1024 Bytes)|Virtual Disk (512 Bytes)|
  |:---|:---|
  |通訊層緩衝區|模擬 SSD 儲存空間|
  |提供充足的**流量控制 (Flow Control)** 空間。當 CPU 正在處理校驗或中斷時，DMA 仍可持續搬運資料。較大的 Buffer 可有效**避免 Overrun Error (ORE)** 導致的封包遺失|對應標準 **磁碟磁區 (Sector Size)**，在有限的 SRAM (16KB) 中實現具備代表性的儲存模擬|

- #### 指令合法性檢查 (Payload Sanity Check)
  - 協定理論支援 64KB 傳輸，但在韌體實作中採取 **防禦性編程 (Defensive Programming)**，不信任任何外部輸入
  - 讀取長度限制 (Quality of Service, QoS 策略)：將單次讀取強制限制為 16 Bytes
  - **防止總線霸佔 (Bus Hogging)**：避免長達數秒(讀取長資料)的 UART 傳輸導致系統任務 **飢餓 (Starvation)**，確保馬達控制或溫度監控等高優先權任務的即時性
  - **系統自保 (Self-Protection)**：防止 Host 端因惡意或錯誤的大長度請求，導致系統卡死
  - **可觀測性 (Observability)**：確保 Debug 終端機能輸出整齊、易讀的驗證字串，提升除錯效率
- #### 循環定址模擬
  - 透過 `(lba + i) % 512` 實作循環邊界處理
  - 確保在模擬環境下，即使 LBA 超出範圍，系統仍能安全運行，防止 **記憶體非法存取 (Out-of-bounds Access)**


### `main.c`：負責管理底層硬體資源
透過 DMA 環形緩衝區 (Circular Buffer) 技術，在高傳輸負載下確保數據處理的完整性與系統穩定性
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"

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
- #### 生產者-消費者模型 (Producer-Consumer Model)
  - **生產者 (Producer)**：由 DMA1 Channel 2 擔任，負責將 UART 接收到的原始數據自動搬運至 rx_buffer，無需 CPU 介入
  - **消費者 (Consumer)**：於 `while(1)` 主迴圈執行，負責根據協定邏輯解析緩衝區中的數據
  - 避免了頻繁進出中斷服務程式 (ISR) 導致的**上下文切換 (Context Switch)** 開銷，是處理 NVMe 高速指令流的工業級標準做法
- #### 雙指標環形緩衝區管理 (Dual-Pointer Management)
  - 在 **非阻塞 (Non-blocking)** 環境下精確管理數據，系統維護了兩個關鍵指標
  - **硬體寫入指標 (wr_ptr)**：透過監控 DMA 的 CNDTR 暫存器即時計算獲得
    - `wr_ptr = Total_Size - CNDTR` (由於 CNDTR 是倒數計數器, 紀錄剩餘空間)
  - **軟體讀取指標 (rd_ptr)**：由軟體維護，標記**目前處理到的位置**
  - **可用數據量 (available)**：
    - `(wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr)
    - CNDTR 僅反映硬體填充進度，唯有結合 `rd_ptr` 才能得知**真正尚未處理的數據量**，確保指令不遺漏、不重複(避免重複處理已讀取的舊資料)、精確判斷何時緩衝區內已積累足夠的完整封包(7 Bytes)
- #### 硬體層級的安全保障機制
  - **W1C (Write 1 to Clear) 機制**： `USART1->ICR |= 0xFFFFFFFF;`，硬體旗標（如溢位錯誤 ORE）由電路自動置位。採用 **寫 1 清除** 邏輯可**避免 讀取-修改-寫入** 過程中產生的 **競態條件 (Race Condition)**，確保狀態清除的原子性
  - **NVIC 分層授權管理**：`*NVIC_ISER = (1UL << 27);` (開啟 **IRQ 27：USART1**)，即便外設配置正確，若無 NVIC 的全域授權，CPU 仍不會響應中斷。這提供了精確的硬體過濾功能，讓開發者能嚴格控制 CPU 的執行時機
- #### ORE (Overrun Error) 偵測與自癒
  - 當系統處理速度無法跟上資料流入速度時，硬體會觸發 ORE 旗標
  - 系統主動偵測此異常，並執行診斷回應（發送 `[SYS] ORE_ERROR`），隨後立即重置 DMA 鏈路
  - 防止傳輸鏈路永久卡死，確保系統在遭遇極端高壓後能自動恢復正常運作
- #### 指令同步機制 (Command Synchronization)
  - UART 為流式傳輸，封包邊界可能位移
  - 解析器會掃描 rx_buffer 尋找同步字頭 0xA5
  - 若首字節不符，系統會逐位元偏移讀取指標 (rd_ptr++) 重新搜尋，具備強大的數據流自校正能力

### `host_sender.py`：驗證與自動化測試
 Host Driver (主機驅動程式) 為驅動模擬腳本，生成各種邊界測試案例，驗證 Device 端的穩定性。負責將抽象的指令封裝為符合規格的二進位流 (Binary Stream)，並透過 Python 實作自動化測試與 **錯誤注入 (Error Injection)** 機制，並透過串口送給 STM32 驗證

```
import serial  # 負責串口通訊 (pySerial 庫)
import struct  # 負責將 Python 資料型別轉換為 C 語言結構體二進位格式 (最關鍵)
import time    # 負責延時控制

# 定義測試主函式，參數包含：測試名稱、串口物件、指令碼、位址、長度、以及兩個錯誤注入旗標
def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"\n--- Running: {name} ---")
    
    # 模擬錯誤 Opcode 測試 (三元運算)
    actual_op = 0x99 if force_bad_op else opcode

    # 使用 Big-Endian (>) 封裝資料：Header(B), Opcode(B), LBA(H), Len(H)
    # struct.pack ：
    # '>' : 代表使用 Big-Endian (大端序) 編碼
    # 'B' : 1 Byte (unsigned char)，對應 start_byte
    # 'B' : 1 Byte (unsigned char)，對應 opcode
    # 'H' : 2 Bytes (unsigned short)，對應 lba
    # 'H' : 2 Bytes (unsigned short)，對應 length
    raw = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # # 模擬校驗錯誤測試，計算 Checksum
    if force_bad_cs:
        # 故意將正確的總和 + 1，STM32 收到後算出來會對不起來
        checksum = (sum(raw) + 1) & 0xFF # 故意讓校驗碼錯誤
    else:
        # sum(raw) 會累加前面 6 個 Byte 的數值，& 0xFF 是為了確保它只佔 1 Byte (0-255)
        checksum = sum(raw) & 0xFF       # 標準計算
        
    pkt = raw + struct.pack('B', checksum)  # 組合最終 7-byte 封包
    ser.write(pkt)                          # 透過實體串口送出 # 呼叫底層驅動，將二進位資料經由 USB 傳送到 STM32
    
    time.sleep(0.2)  # 等待 Device 處理回應 # 給 STM32 一點時間運算並回傳訊息
    if ser.in_waiting > 0:
        # read_all() 讀取所有回傳資料
        # decode('ascii') 將二進位轉回文字，errors='ignore' 防止因為亂碼導致程式崩潰
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")

try:
    # 初始化序列埠，115200, N, 8, 1
    # 初始化 /dev/ttyUSB0 (Linux 格式)，波特率 115200 必須與 STM32 一致
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)   # 硬體重置後通常需要 1 秒讓電位穩定
    ser.reset_input_buffer()  # 清除啟動時可能產生的雜訊資料

    # 1. 冒煙測試：基本讀寫功能，成功案例 (Write & Read)
    # 測試 A：正常寫入指令 (LBA 100, 長度 8)
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 100, 8)

    # 測試 B：正常讀取指令 (確認剛才寫入的資料)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 100, 8)

    # 2. 魯棒性測試：故意傳送 Checksum 錯誤的封包, Checksum 攻擊，測試 STM32 是否會被損壞的封包騙到
    test_nvme("CHECKSUM ERROR TEST", ser, 0x02, 200, 8, force_bad_cs=True)

    # 3. 語義測試：傳送未定義的指令、不支援的指令 (Invalid Opcode),非法指令攻擊，測試 STM32 的邊界檢查是否有效
    test_nvme("INVALID OPCODE TEST", ser, 0x99, 0, 0, force_bad_op=True)

    # 4. 硬體溢位 壓力與異常測試：模擬 Host 端產生過載 (Overrun)，模擬 ORE (一次噴大量垃圾數據塞爆 Buffer)
    print("\n--- Running: ORE OVERFLOW TEST ---")
    ser.write(b'X' * 2000) # 噴 2000 個字元，超過 rx_buffer 的 1024 緩衝區的數據量
    time.sleep(0.5)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore')
        print(f"Result: {res}")

    ser.close()  # 養成好習慣，結束後關閉資源
except Exception as e:
    print(f"Error: {e}")
```
- #### 跨平台封包封裝 (Data Serialization)
  - 利用 Python 的 `struct` 庫解決高級語言與 C 語言結構體之間的數據轉換問題
  - 使用 `struct.pack('>BBHH', ...)` 顯式指定 **Big-Endian (大端序)** 編碼
  - 確保 Host 端產生的位元組流符合 NVMe 與網路傳輸協定的標準，模擬真實的異質系統通訊
- #### 錯誤注入與負向測試 (Negative Testing)
  - **校驗碼攻擊 (Checksum Injection)**
    - 故意計算錯誤的 Checksum 並發送
    - 驗證 Device 端是否能精確攔截受損封包，防止無效數據進入邏輯層
  - **非法指令攻擊 (Opcode Fuzzing)**
    - 發送未定義的操作碼（如 `0x99`）
    - 驗證韌體的 **邊界檢查 (Boundary Check)** 能力，確保系統不會因未知指令而產生未定義行為 (Undefined Behavior) 或當機
  - **硬體溢位壓力測試 (Overrun Stress Test)**
    - 一次性爆發式傳送 2000 Bytes 的數據（遠超 Device 端 1024 Bytes 的接收緩衝區）
    - 模擬 Host 端產生過載，驗證 Device 端處理 **ORE (Overrun Error)** 硬體旗標與**自我復原 (Self-Recovery)** 的魯棒性
- #### 診斷與監控 (Diagnostics)
  - **ASCII 解碼與錯誤容忍**：使用 `.decode('ascii', errors='ignore')` 讀取回傳值
  - 即便 Device 端因為故障回傳了亂碼，測試腳本仍能保持運行而不崩潰，並記錄下 Device 端最後的遺言作為除錯依據
- #### 測試案例清單 (Test Cases)
  |測試類型|案例名稱|驗證重點|
  |:---|:---|:---|
  |**Smoke Test**|SUCCESSFUL WRITE/READ|驗證基本通訊與 LBA 寫入讀取的邏輯正確性|
  |**Robustness**|CHECKSUM ERROR TEST|驗證數據校驗攔截機制，模擬信號干擾情境|
  |**Semantic**|INVALID OPCODE TEST|驗證指令集解析的完備性與非法指令防禦|
  |Hardware|ORE OVERFLOW TEST|驗證 DMA Ring Buffer 耗盡後的硬體旗標自癒與重置|
- #### System-Level Thinking
  - **自動化測試(Unit Testing)**：取代手動輸入，透過腳本實現一鍵式回歸測試，注入錯誤來驗證系統的健壯性（Robustness）
  - **端到端**：從 **物理層 (UART/DMA)** 到 **協定層 (Protocol)**，最後到 **驅動層 (Python Test Script)**，完整掌握數據在系統中的生命週期


## 四、執行 Host Driver 與 GDB 驗證 (測試層)
驗證 **通訊協定解析器 (Protocol Parser)** 是否真的能正確拆解來自電腦的指令
### STM32
  ```
  b handle_nvme_read
  continue
  ```
  - `b` (break)：在處理 NVMe 讀取指令的函式入口設下紅綠燈
  - `c` (continue)：讓 CPU 開始跑，直到撞到紅綠燈為止
  - 在晶片內部設好陷阱，等待 Python 發過來的封包是否能正確觸發這個邏輯
### Linux
  ```
  python3 host/host_sender.py
  :
  [!] 錯誤: [Errno 13] could not open port /dev/ttyUSB0: [Errno 13] Permission denied: '/dev/ttyUSB0' -> 未加入 dialout 群組
  ```
  - 執行主機端腳本, 模擬真實的 NVMe 主機(Host)
  - 會把 **`0xA5` (起始位元)、`0x01` (Read Opcode)、LBA** 等資料包裝成一個 **7-byte 封包** 送出去

## 五、結論
### 系統運作流程與測試邏輯 (Logic Flow)
- #### 資料封裝 (Python Host)
  - 將人類可讀的指令（如：讀取 LBA 10）透過 `struct.pack` 序列化為二進位封包
- #### 實體傳輸 (Physical Layer)
  - 封包經由 USB-to-UART 轉換器，透過串列通訊傳送至 STM32
- #### 零負載接收 (Hardware STM32 DMA)
  - STM32 的 DMA 控制器在不驚動 CPU 的情況下，靜悄悄地將資料即時存入 rx_buffer
- #### 指標監控 (Main Loop)
  - 系統持續偵測 `rd_ptr != wr_ptr`，一旦硬體寫入指標變動，立即啟動 Ring Buffer 掃描
- #### 協定解析與驗證 (Protocol Parser)
  - **同步 (Sync)**：定位 0xA5 標頭以對齊封包邊界
  - **校驗 (Integrity)**：計算並驗證 Checksum，確保指令未在傳輸中變質
  - **轉換 (Endianness)**：處理異構系統通訊， PC (Big-Endian) 與 MCU (Little-Endian) 的位元組序差異
- #### 指令執行 (Execution)
  - 封包驗證通過後，觸發斷點或跳轉至 `handle_nvme_read/write` 執行模擬閃存操作

### 核心價值 (Core Engineering Values)
- #### 數據完整性 (Data Integrity)
  - 透過 Checksum 演算法保護指令流，模擬真實工業環境中的雜訊處理
  - 實作 Diagnostic (診斷型) 錯誤回應，主動告知 Host 預期與實際校驗值的差異，極大化除錯效率
- #### 異構系統通訊與位元組序
  - 理解跨平台開發中的 Endianness (位元組序) 問題，並在韌體層實作無損轉換
- #### 硬體資源與效能優化 (Efficiency)
  - **DMA Ring Buffer**：展示了**生產者-消費者模型**，最大限度減少中斷頻率，釋放 CPU 運算資源
  - **非阻塞設計**：確保系統在等待資料傳輸時，背景心跳燈仍能正常運作，不產生死機 (Hanging)
- #### 系統級異常自癒機制 (Hardware Robustness)
  - **ORE (Overrun Error) 處理**：主動偵測硬體溢位並實作**自動復位 (Reset-and-Sync)**，模擬 SSD 控制器在高負載下的穩定性挑戰

### 測試思維 (Testing Methodology)
- #### 自動化單元測試 (Unit Testing)
  - 捨棄手動調試，撰寫 Python 腳本實作一鍵式回歸測試，確保每次修改碼後功能依然正確
- #### 負向測試 (Negative Testing)
  - 具備攻擊者思維，透過**錯誤注入**驗證系統在極端情況（非法指令、校驗錯誤、硬體過載）下的行為
- #### 魯棒性驗證 (Robustness)
  - 模擬 Host 端產生 Overrun Error 的場景，驗證系統在極度壓力下的恢復能力，這是開發高品質儲存產品的關鍵指標

