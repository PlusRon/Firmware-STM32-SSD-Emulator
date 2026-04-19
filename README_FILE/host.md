# HOST
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

## 三、執行 Host Driver 與 GDB 驗證 (測試層)
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


#### protocol.h
```
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* --- 協定常數定義 --- */
#define CMD_START_BYTE 0xA5
#define PKT_SIZE       7    // Start(1)+Op(1)+LBA(2)+Len(2)+CS(1)

/* --- NVMe Opcode 模擬 --- */
#define NVME_OP_READ     0x01
#define NVME_OP_WRITE    0x02
#define NVME_OP_IDENTIFY 0x03

/* --- 封包結構體 --- */
// 使用 packed 確保結構體不被填充，大小精確為 7 Bytes
typedef struct __attribute__((packed)) {
    uint8_t  start_byte; // 0xA5
    uint8_t  opcode;     
    uint16_t lba;        
    uint16_t length;     
    uint8_t  checksum;   
} NVMe_Command_t;

/* --- 函式宣告 --- */
void Protocol_Parse(uint8_t *packet_buf);
void handle_nvme_read(uint16_t lba);
void handle_nvme_write(uint16_t lba);

#endif
```

#### protocol.c
```
#include "protocol.h"

/**
 * @brief 解析指令封包
 * @param packet_buf 指向接收緩衝區中 A5 開頭的位址
 */
void Protocol_Parse(uint8_t *packet_buf) {
    NVMe_Command_t *cmd = (NVMe_Command_t *)packet_buf;

    // 1. 再次確認起始位元 (雙重檢查)
    if (cmd->start_byte != CMD_START_BYTE) return;

    // 2. 實作 Checksum 驗證
    uint8_t calculated_cs = 0;
    for (int i = 0; i < PKT_SIZE - 1; i++) {
        calculated_cs += packet_buf[i];
    }

    // 只有校驗通過，才執行指令
    if (calculated_cs == cmd->checksum) {
        
        // 3. 位元組序轉換 (Big-endian from Python to Little-endian for STM32)
        uint16_t lba = (uint16_t)__builtin_bswap16(cmd->lba);
        
        switch (cmd->opcode) {
            case NVME_OP_READ:
                handle_nvme_read(lba);
                break;
            case NVME_OP_WRITE:
                handle_nvme_write(lba);
                break;
            default:
                break;
        }
    }
}

/* 這是 stub functions (樁函式)，專門給 GDB 下斷點觀察 lba 數值 */
void handle_nvme_read(uint16_t lba) {
    // GDB 斷點位置: b handle_nvme_read
    __asm("NOP"); 
}

void handle_nvme_write(uint16_t lba) {
    // GDB 斷點位置: b handle_nvme_write
    __asm("NOP");
}
```
#### main.c
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h" // 引入協定模組

#define RX_BUF_SIZE 1024

/* --- global variable --- */
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0; 

// 假設這兩個 flag 在 usart.c 的中斷服務程式中定義
extern volatile uint8_t rx_idle_event;
extern volatile uint8_t uart_overrun_occurred;

void System_Init(void) {
    RCC->APB2ENR |= (1UL << 14); // USART1 Clock
    RCC->AHBENR  |= (1UL << 0);  // DMA1 Clock

    GPIO_Init_Output(GPIOC, 6);
    GPIO_Init_AF(GPIOA, 9, 1);
    GPIO_Init_AF(GPIOA, 10, 1);
    GPIO_Init_AF(GPIOA, 12, 1);

    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69); // 115200 @ 8MHz

    *NVIC_ISER = (1UL << 27);
    SysTick_Init(8000);
}

int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send(USART1, "NVMe Emulator Interface Ready...\r\n");

    while (1) {
        // 1. 錯誤處理 (Overrun)
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_SendChar(USART1, 0x15); // ASCII NAK
            rd_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);
        }

        // 2. 指令解析與指標追蹤
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        if (rx_idle_event || (rd_ptr != wr_ptr)) {
            // 計算當前 Buffer 中有多少未讀資料
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            // 如果資料夠一個封包長度
            while (available >= PKT_SIZE) {
                if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                    // 執行解析 (呼叫 drivers/protocol.c 中的函式)
                    Protocol_Parse(&rx_buffer[rd_ptr]);
                    
                    // rd_ptr 往前推一個封包長度
                    for(int i=0; i<PKT_SIZE; i++) {
                        rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE;
                    }
                    available -= PKT_SIZE;
                } else {
                    // 若開頭不是 A5，跳過這格尋找下一個 A5
                    rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE;
                    available--;
                }
            }
            rx_idle_event = 0; // 處理完所有可讀封包後清除標誌
        }

        // 3. 背景閃爍任務
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(GPIOC, 6, &led_current_state);
            last_blink = get_tick();
        }
    }
}
```


#### host_sender.py
```
import serial
import struct

# 請根據實際狀況修改路徑，如 /dev/ttyACM0
DEV_PATH = '/dev/ttyACM0'
BAUD = 115200

def send_nvme_read_cmd(lba, length):
    try:
        with serial.Serial(DEV_PATH, BAUD, timeout=1) as ser:
            # >BBHH: 大端序, Start(A5), Op(1), LBA(H,L), Len(H,L)
            raw_pkt = struct.pack('>BBHH', 0xA5, 0x01, lba, length)
            # 計算 Checksum (前 6 bytes 累加)
            checksum = sum(raw_pkt) & 0xFF
            # 完整 7 bytes
            final_pkt = raw_pkt + struct.pack('B', checksum)
            
            ser.write(final_pkt)
            print(f"[*] 指令已送出: {final_pkt.hex().upper()}")
            print(f"[*] 目標 LBA: {lba}, 預計讀取長度: {length}")
    except Exception as e:
        print(f"[!] 錯誤: {e}")

if __name__ == "__main__":
    # 發送一個讀取 LBA 10 的指令
    send_nvme_read_cmd(lba=10, length=256)
```
