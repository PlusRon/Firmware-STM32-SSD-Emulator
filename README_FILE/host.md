# HOST NVMe Protocol
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

#### `protocol.h`：通訊協定的規格定義，定義了 Host (電腦) 與 Device (STM32) 溝通的語言格式
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
- **為何要用 `__attribute__((packed))`？**
  - 在 32 位元系統中，編譯器為了存取效率，通常會將 `uint8_t` 後面填充 **1 byte** 空間來對齊 **2 bytes** 的 `uint16_t`，但在 **通訊協定中，資料是緊密排列** 的。如果不加這個屬性，struct 的大小會變成 8 bytes 而非 7 bytes，導致解析位址完全錯亂
- **LBA vs. Length**
  |**欄位名稱**|**技術定義**|
  |:---|:---|
  |**LBA (Logical Block Address)**|指指令要從磁碟的 「哪一個位置」 開始執行|
  |**Length**|指從該位置開始，「連續操作多少個區塊」|
- **大端序 (Big-Endian) vs. 小端序 (Little-Endian)**
  - 大端序 (Big-Endian)：資料的高位元組 (Most Significant Byte, MSB) 放在低位址。在通訊協定（如 NVMe, TCP/IP）中，這被稱為「網路位元組序」。
  - 小端序 (Little-Endian)：資料的低位元組 (LSB) 放在低位址。是 STM32 (ARM) 和 x86 電腦在記憶體裡存取的方式
  - 多數的通訊協定為了標準化，規定使用大端序（網路位元組序）傳輸，而處理器（STM32）是小端序架構，為了正確解析 Host 傳來的 LBA 和 Length 數值，必須在軟體層進行 Byte Swap 操作，否則高低位元組顛倒會導致定址與長度計算完全錯誤
  - Endianness 轉換只發生在 **Multi-byte Data Types（多位元組資料型別）** 上，NVMe 指令結構中，start_byte 和 opcode 屬於 uint8_t，它們在記憶體中只佔用單一地址，因此不存在 bytes 順序問題，而 lba 和 length 屬於 uint16_t，跨越了兩個字節。由於通訊協定規定以 Big-Endian 傳輸，而 STM32 硬體是以 Little-Endian 方式讀取 16-bit 暫存器，因此只有這兩個欄位需要進行 `__builtin_bswap16` 轉換，以確保數值的正確性。

#### `protocol.c`：指令執行與校驗邏輯，模擬 SSD 控制器的核心邏輯層
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
- **CheckSum**
  - 設計 Checksum 驗證機制時，不僅單純回傳錯誤訊息，還將 STM32 計算出的預期校驗值回傳給 Host，才能快速定位 傳輸完整性 (Signal Integrity) 問題。Diagnostic (診斷型) 回應 能大幅縮短硬體除錯的時間
    - 預期值與 Host 端一致：校驗碼欄位受損
    - 預期值不符：指令數據 (Payload) 在傳輸路徑中產生了雜訊干擾，
- **`__builtin_bswap16` 的必要性**
  - x86 或 Python 在處理 struct.pack 時通常預設大端序（高位元組在前低位址）
  - 而 ARM Cortex-M 是小端序，例如地址 100 十六進位是 0x0064，若不經轉換，STM32 會把它讀成 0x6400 (25600)
- **RX_BUF_SIZE (1024) 比 virtual_disk (512) 大**
  - UART 是一個持續流入的資料流。當你的 CPU 正在處理 Protocol_Parse（例如正在算 Checksum 或印 Debug 訊息）時，DMA 仍會不停地把新資料塞進來
  - 如果 Ring Buffer 太小（例如也只有 512 或更小），一旦 CPU 稍微忙不過來，新進來的封包就會立刻覆蓋掉還沒處理的封包，導致 **ORE (Overrun Error)**
  - virtual_disk (512) 是為了模擬虛擬 SSD 只有 512 Byte 的容量
  - STM32F072 的 SRAM 有限（僅 16KB）。在實驗階段，不需要開一個幾千 Byte 的陣列。512 Byte 剛好對應一個標準的**磁碟磁區（Sector）大小**，非常具有代表性
- **限制只能讀取 16 個長度的 LBA**
  - length 欄位有 2 Bytes，理論上可以要求讀取 65535 Bytes，但在韌體開發中，我們嚴禁完全信任 Host 端傳來的數值
  - 如果 Host 惡意或不小心傳了一個 length = 60000
    - 以 115200 波特率傳送 60000 Bytes 需要約 5.2 秒。在這 5.2 秒內，STM32 的 CPU 會卡在 for 迴圈裡不斷送資料，完全無法處理新的指令。
    - 在真實系統中，這可能導致其他高優先權的任務（如馬達控制、溫度監控）被餓死（Starvation）。
  - virtual_disk 只有 512 塊。如果讀取長度超過 512，資料就會開始重複
  - 限制在 16，是為了讓你在 Minicom 或 Python 終端機 上能清楚看到一整行易讀的輸出。如果你一次噴 1000 個字元，畫面會亂掉，難以觀察 `defghijk...` 這種驗證字串
- 協定支援 64KB 的傳輸，但在韌體實作中，加入了 **Payload Sanity Check（合法性檢查）**。將單次讀取限制在 16 Bytes，是為了**防止 Host 端的非法大長度請求佔用系統總線（Bus）** 過長時間，確保系統具備基本的 **Quality of Service (QoS) 與自保能力**。
#### `main.c`：硬體驅動與 DMA Ring Buffer 管理，是系統穩定性的靈魂，負責在高負載下確保資料不遺失
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

int main(void) {
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

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- Diagnostics Mode Active ---\r\n");

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
- DMA Ring Buffer 的優勢
  - 展示了 **生產者-消費者模型 (Producer-Consumer Model)**
  - DMA 是生產者，負責搬資料；while(1) 是消費者，負責解析資料
  - 這種設計讓 UART 接收不需要頻繁進出中斷服務程式（ISR），極大地降低了 CPU 負荷，這也是處理 NVMe 高速指令流的必備技術

#### `host_sender.py`：Host 端驅動模擬腳本，生成各種邊界測試案例，驗證 Device 端的穩定性
```
import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"\n--- Running: {name} ---")
    
    # 模擬錯誤 Opcode 測試
    actual_op = 0x99 if force_bad_op else opcode

    # 使用 Big-Endian (>) 封裝資料：Header(B), Opcode(B), LBA(H), Len(H)
    raw = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # # 模擬校驗錯誤測試，計算 Checksum
    if force_bad_cs:
        checksum = (sum(raw) + 1) & 0xFF # 故意讓校驗碼錯誤
    else:
        checksum = sum(raw) & 0xFF       # 標準計算
        
    pkt = raw + struct.pack('B', checksum)  # 組合最終 7-byte 封包
    ser.write(pkt)                          # 透過實體串口送出
    
    time.sleep(0.2)  # 等待 Device 處理回應
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")

try:
    # 初始化序列埠，115200, N, 8, 1
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)   # 等待硬體穩定
    ser.reset_input_buffer()

    # 1. 冒煙測試：基本讀寫功能，成功案例 (Write & Read)
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 100, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 100, 8)

    # 2. 魯棒性測試：故意傳送 Checksum 錯誤的封包
    test_nvme("CHECKSUM ERROR TEST", ser, 0x02, 200, 8, force_bad_cs=True)

    # 3. 語義測試：傳送未定義的指令、不支援的指令 (Invalid Opcode)
    test_nvme("INVALID OPCODE TEST", ser, 0x99, 0, 0, force_bad_op=True)

    # 4. 壓力與異常測試：模擬 Host 端產生過載 (Overrun)，模擬 ORE (一次噴大量垃圾數據塞爆 Buffer)
    print("\n--- Running: ORE OVERFLOW TEST ---")
    ser.write(b'X' * 2000) # 噴 2000 個字元，超過 rx_buffer 的 1024 緩衝區的數據量
    time.sleep(0.5)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore')
        print(f"Result: {res}")

    ser.close()
except Exception as e:
    print(f"Error: {e}")
```


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

## 整體測試的邏輯流向（意義）
- **Python 腳本** ：把人類看得懂的 `要讀 LBA 10` 打包成二進位封包
- **USB-to-UART** ：封包透過電線傳給 STM32
- **STM32 DMA** ：完全不驚動 CPU，靜悄悄地把資料存進 `rx_buffer`
- **Main Loop** ：發現 `rd_ptr != wr_ptr`，STM32 開始掃描讀取 RING BUFFER
- **Protocol Parser** ：看到 `0xA5`，檢查 **Checksum**
- **觸發斷點** ：如果封包正確，CPU 會停在 `handle_nvme_read`
- **總結**
  - 封包同步機制：處理流式資料中的封包邊界問題。
  - 數據完整性 (Data Integrity)：透過 Checksum 保護指令。
  - 異構系統通訊：解決 PC 與 MCU 之間的 Endianness 問題。
  - 硬體資源優化：利用 DMA 實作高效率 Ring Buffer。
  - 異常處理機制：主動偵測並從硬體溢位 (ORE) 中自癒。
