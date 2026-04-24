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

#### protocol.h
```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PKT_SIZE        7
#define CMD_START_BYTE  0xA5

/* NVMe 簡化指令集 */
#define NVME_OP_READ    0x01
#define NVME_OP_WRITE   0x02

/* 封包格式 (需對齊硬體位元組序) */
typedef struct {
    uint8_t  start_byte; // 0xA5
    uint8_t  opcode;     // 0x01: Read, 0x02: Write
    uint16_t lba;        // 邏輯區塊位址 (Big-endian)
    uint16_t length;     // 資料長度 (Big-endian)
    uint8_t  checksum;   // 前 6 bytes 之和
} __attribute__((packed)) NVMe_Command_t;

void Protocol_Parse(uint8_t *packet_buf);
void handle_nvme_read(uint16_t lba, uint16_t len);
void handle_nvme_write(uint16_t lba, uint16_t len);

#endif
```

#### protocol.c
```
#include "protocol.h"
#include "usart.h"

static uint8_t virtual_disk[512];

void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;
    uint8_t calculated_cs = 0;

    // 1. 計算 Checksum
    for (int i = 0; i < 6; i++) {
        calculated_cs += packet_buf[i];
    }

    // 2. 驗證 Checksum
    if (calculated_cs != cmd->checksum) {
        UART_Send(USART1, "[ERR] CS_FAIL (Expected: 0x");
        UART_SendChar(USART1, calculated_cs); // 這裡輸出原始值作為調試
        UART_Send(USART1, ")\r\n");
        return;
    }

    // 3. 驗證 Opcode
    uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
    uint16_t len = (uint16_t)__builtin_bswap16(cmd->length);

    if (cmd->opcode == NVME_OP_READ) {
        handle_nvme_read(lba, len);
    } else if (cmd->opcode == NVME_OP_WRITE) {
        handle_nvme_write(lba, len);
    } else {
        UART_Send(USART1, "[ERR] INVALID_OP\r\n");
    }
}

void handle_nvme_read(uint16_t lba, uint16_t len) {
    UART_Send(USART1, "[ACK] READ_OK:");
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        UART_SendChar(USART1, virtual_disk[(lba + i) % 512]);
    }
    UART_Send(USART1, "\r\n");
}

void handle_nvme_write(uint16_t lba, uint16_t len) {
    for (int i = 0; i < (len > 16 ? 16 : len); i++) {
        virtual_disk[(lba + i) % 512] = (uint8_t)(lba + i);
    }
    UART_Send(USART1, "[ACK] WRITE_OK\r\n");
}
```
#### main.c
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"

#define RX_BUF_SIZE 1024
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0;

int main(void) {
    // 系統初始化 (維持原有邏輯)
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

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- Diagnostics Mode Active ---\r\n");

    while (1) {
        // --- 錯誤處理：硬體溢位 ---
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_Send(USART1, "[SYS] ORE_ERROR (Hardware Buffer Full)\r\n");
            // 重置傳輸鏈
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
                    // 如果發現標頭不是 0xA5，視為無效數據並跳過
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


#### host_sender.py
```
import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"\n--- Running: {name} ---")
    
    # 打包封包
    actual_op = 0x99 if force_bad_op else opcode
    raw = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # 計算 Checksum
    if force_bad_cs:
        checksum = (sum(raw) + 1) & 0xFF # 故意加 1 破壞校驗
    else:
        checksum = sum(raw) & 0xFF
        
    pkt = raw + struct.pack('B', checksum)
    ser.write(pkt)
    
    time.sleep(0.2)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")

try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)
    ser.reset_input_buffer()

    # 1. 成功案例 (Write & Read)
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 100, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 100, 8)

    # 2. 錯誤案例：Checksum 錯誤
    test_nvme("CHECKSUM ERROR TEST", ser, 0x02, 200, 8, force_bad_cs=True)

    # 3. 錯誤案例：不支援的指令 (Invalid Opcode)
    test_nvme("INVALID OPCODE TEST", ser, 0x99, 0, 0, force_bad_op=True)

    # 4. 錯誤案例：模擬 ORE (一次噴大量垃圾數據塞爆 Buffer)
    print("\n--- Running: ORE OVERFLOW TEST ---")
    ser.write(b'X' * 2000) # 噴 2000 個字元，超過 rx_buffer 的 1024
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


