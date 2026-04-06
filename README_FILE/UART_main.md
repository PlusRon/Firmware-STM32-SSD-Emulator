# UART 應用邏輯 主程式

## 一、UART 理論技術
### 1. UART (Universal Asynchronous Receiver/Transmitter) 通訊協定(介面)
- **非同步通訊 (UART)** : 雙方 **無共同的時脈**，因此必須事先約定好 **每秒讀取電位的次數 Baud Rate (波特率)**
    ```
    同步通訊 (SPI/I2C) : 由一根專門的 Clock 線告訴接收端「現在該讀取資料了」
    ```
- **工作原理** : 雙方使用**各自**內部計時器，約定**每秒鐘取樣幾次（例如 115200 次）**，若頻率不一致，資料就會因偏移而解析錯誤、讀成亂碼
- **硬體線路** : 三根線即可實現 **全雙工（Full-duplex）** 通訊
  |接腳|功能|
  |:---|:---|
  |**TX (Transmit)**|資料發送端|
  |**RX (Receive)**|資料接收端|
  |**GND (Ground**)|共同接地，作為電位參考基準 (否則 $0/1$ 電壓判斷會漂移)|
- **與 I2C 或 SPI 之差異** : 最大的不同在於 **有時脈線 (Clock Signal)**
- **與 USB 差異** : USB 具備 **自動握手(Handshake)協定**，UART 需由工程師手動定義通訊速度
### 2. UART 封包結構 (Data Frame)
- **起始位 (Start Bit)** :
  - **預設** 狀態下，UART 線路保持 **高電位（Logic 1）**
  - 當 **TX 將電壓拉低（Logic 0）** 持續一個Byte的時間，代表**資料要開始傳送**，這會喚醒接收端的計時器開始倒數採樣
- **資料位 (Data Frame)** :
  - 長度通常為 5 到 9 位元（最常用 **8-bit，即1 Byte**）
  - 預設由 **LSB (最低有效位) 開始傳送**
- **校驗位 (Parity Bit)** - 選配:
  - 錯誤偵測
  - **奇校驗 (Odd)**、**偶校驗 (Even)**，若資料位中 **1 的個數不符合約定**，代表**傳輸過程出錯**
- **停止位 (Stop Bit)** :
  - 將線路 **拉回高電位（Logic 1）**，代表**一個 Byte 的通訊結束**
  - 可設定為 1 Byte、1.5 Byte 或 2 Byte
### 3. 常見限制與誤區
- **距離限制** ： UART 是 **電壓訊號（TTL 準位）**，容易受雜訊干擾，通常建議傳輸距離在幾公尺內
- **電平不相容**
  - **TTL 準位** ： $0V$ 代表 $0$， $3.3V/5V$ 代表 $1$（**用於 STM32**）
  - **RS-232 準位** ： $+3V$ ~ $+15V$ 代表 $0$， $-3V$ ~ $-15V$ 代表 $1$（**用於 老式電腦 COM 埠**）
    ```
    直接連接會燒毀 STM32，必須經過電平轉換晶片 (如 MAX232)
    ```
- **無定址功能** ： **UART** 是 **點對點（Point-to-Point）** 通訊，不像 **I2C** 可以 **一對多**

## 二、Minicom 終端機模擬軟體 - 序列通訊工具
**Unix-like 系統 (Linux、macOS)** 中經典且輕量化的 **文字界面(CLI) 序列通訊程式**，是與硬體 (STM32、樹莓派、路由器) 溝通的核心工具。STM32 晶片本身**沒有螢幕**，Minicom 就像是 翻譯官/顯示器，會 **監聽** 電腦的 **USB 序列埠(`/dev/ttyUSB0`)的 UART 之 Rx** 所接收的訊息，將接收到的 **原始電壓訊號 (Hex)** 轉譯為 **人看得懂的ASCII** 文字； 當在 **鍵盤輸入字元**，Minicom 會透過 **USB 序列埠的 UART 之 Tx** 將字元傳送給 **受控端 (STM32)**

### A. 運作原理
- 運作於 Linux 的 **User Space**，透過 **調用 Kernel 中的序列驅動程式** 來操作硬體
- 開啟裝置檔案 `/dev/ttyUSB0`
- **常見配置** (**115200 8N1**)
  |參數|配置|
  |:---|:---:|
  |Baud Rate|115200|
  |Data Bits|8|
  |Parity|N|
  |Stop Bit|1|
  |Hardware Flow Control|N|
  |Software Flow Control|N|
  |Local Echo|N|
  - **`115200`** ： Baud Rate (每秒傳輸的位元數)
  - **`8`** ： 8 個 Data Bits
  - **`N`** ： None Parity (不使用校驗位)
  - **`1`** ： 1 個 Stop Bit
  - **Hardware Flow Control** (硬體流控)
    - 底層開發中通常設為 `NO`
    - 若設 `Yes` : Minicom 會 **等待 CTS/RTS** 訊號，導致鍵盤輸入被鎖死，無法送出指令
  - **Software Flow Control** : 設為 `NO`，避免特殊字元衝突
  - **Local Echo** (本地回顯)
    - 預設關閉 : 必須依賴 **受控端(STM32)將收到的字 再傳回來** 才能看見
    - 開啟 : 在鍵盤打的字會直接顯現在螢幕上，通常會顯示 **本地回顯 + STM32回傳 (例如. AA)**
- 非同步 I/O 運作
  - **Read** ： Minicom 持續 **監測 Rx 緩衝區**，有字就印在螢幕
  - **Write** ： Minicom **偵測鍵盤事件**，並立刻透過序列埠 從 **Tx 發出** 字元給受控端 (STM32)
```
@ 採樣定理與誤差 (Over-sampling)
為了提高準確度，接收端通常會以比 Baud Rate 快 16 倍的速度進行採樣
會尋找起始位的下降邊緣，在位元的中間點進行取樣，避開電壓變換時的雜訊與不穩定區間
```
### B. 操作 Minicom 軟體
- **初始化設定** : 初次使用、切換硬體 時
  ```
  sudo minicom -s
  ```
  - 進入 **Serial port setup**
    - 按 `A` ： 修改路徑 為 `/dev/ttyUSB0`
    - 按 `E` ： 修改速率 為 `115200 8N1`
    - 按 `F` ： 將 Hardware Flow Control 改為 `No`
  - 選擇 **Save setup as dfl** ： 將此設定儲存為 **全域預設值**，下次使用同裝置只需輸入 `sudo minicom` 即可
- **常用快捷鍵** 選單 (`Ctrl + A` 為前導鍵)
  - `Ctrl + A` ➔ `Z` ： 顯示 指令集總覽
  - `Ctrl + A` ➔ `O` ： 開啟設定視窗
  - `Ctrl + A` ➔ `L` ： 開啟捕捉功能（把輸出的 Log 存成純文字檔，方便分析）
  - `Ctrl + A` ➔ `X` ： 離開並關閉（離開程式）
  - `Ctrl + A` ➔ `Q` ： 離開但不關閉（保留序列埠狀態）
- **解決文字往右斜下跳 (階梯狀)**
  - **階梯現象 (Staircase Effect)** ： 若只傳送 `\n`，游標僅**垂直下移**，**水平** 位置 **停留在前一行結尾**
  - **標準順序** :
    - `\r` **(CR, Carriage Return, 0x0D)** ： 回車，將游標推回行首（左側）
    - `\n` **(LF, Line Feed, 0x0A)** ： 換行。將游標垂直下跳一行
  - **方法一 (修改程式碼)** : 改用 先 `\r` ，才 `\n`
      ```
      // 傳送開機訊息
      uart_send_char('S');
      uart_send_char('S');
      uart_send_char('D');
      uart_send_char(' ');
      uart_send_char('R');
      uart_send_char('D');
      uart_send_char('Y');
      uart_send_char('\r'); // Carriage Return: 移回行首
      uart_send_char('\n'); // Line Feed: 跳到下一行
      ```
      ```
      if (received == '\r' || received == '\n') {
          uart_send_char('\r');
          uart_send_char('\n');
      } else {
          uart_send_char(received); 
      }
      ```
  - **方法二 (Minicom 設定)**
    - 按 `Ctrl + A` ➔ `Z` ➔ `U` ，開啟 **Add Carriage Return**
## 三、GNU Screen 終端機虛擬視窗 - 序列連接工具
是一個 **全螢幕視窗管理器**，將單一的 **終端機界面（Terminal）** 分割成 **多個** 橫向或縱向的 **虛擬視窗**。韌體開發領域，主要利用它的 **序列連接（Serial Connection）** 功能，可直接 **掛載設備檔案** 並建立與硬體的高速通訊

### A. 運作原理
- 透過底層 **系統調用(System Calls)**，直接與 Linux 的 **tty(Teletype)** 驅動程式對話
- 下達 **設備連接** 指令時，Screen 會建立一個 **獨立的 Session**
- **劫持 I/O** ： 獨立的 Session 會接管當前終端機的 **鍵盤輸入(Standard Input)** 並 **導向至序列埠的 TX**，同時將 **RX 收到的資料即時渲染到該 Session 螢幕**
- 脫離 (**Detach**) 特性 ： Screen 強大之處，即使關閉電腦視窗，只要 **不手動 Kill Session**，**通訊行程** 依然會在 **後端持續執行**

### B. 操作 Screen 工具
- #### 安裝工具
  ```
  sudo apt update
  sudo apt install screen
  screen -v
  ```
- #### 啟動連線
  - 若沒給 Baud Rate，Screen **預設為 9600**，會導致 STM32 傳來的 115200 資料變成亂碼
    ```
    # 連接到 STM32
    sudo screen /dev/ttyUSB0 115200
    ```
  - `/dev/ttyUSB0` 為 Device
    - 當 screen 的第一個參數是存在於 `/dev/` 下的設備檔案時，該 session 就會自動切換到 **序列連線模式 (Serial Mode)** 開啟 **硬體串口**
    - 若寫 `screen -S /dev/ttyUSB0`，screen 會以為要建立一個名稱為 `/dev/ttyUSB0` 的普通虛擬終端，而不會去開啟真正的硬體串口
- #### 指令操作
  |指令|功能說明|
  |:---|:---|
  |`sudo screen /dev/ttyUSB0 115200`|序列硬體串扣(簡模式) : 直接連線，Baud Rate 必填|
  |`sudo screen -S My_session`|開普通終端(具名模式)|
  |`sudo screen -S [自訂名稱] [設備路徑] [Baud Rate]`|序列硬體串扣(具名模式) ： 名為 MyUART，方便後續恢復連線|
  |`screen -ls`|**列出清單** ： 查看目前背景有多少連線在跑|
  |`screen -r [PID or NAME]`|**恢復連線** ： **接回** 之前 **Detach** 的連線|
  |`screen -wipe`|**清除殘留** ： 清理已經斷開但仍留在清單中的死連線|

- #### 核心快捷鍵 (以 `Ctrl + A` 為前導鍵)
  |動作|快捷鍵組合|說明|
  |:---|:---|:---|
  |退出並關閉|`Ctrl` + `A` ➔ `K` (Kill)|徹底終止通訊並釋放 `/dev/ttyUSB0`|
  |脫離 session|`Ctrl` + `A` ➔ `D` (Detach)|暫時退出，但連線在後端繼續跑(Log 繼續收)|
  |恢復 session|`screen -r`|重新接回剛才脫離的連線|
  |清除螢幕|`Ctrl` + `A` ➔ `C`|建立新視窗 (多工模式下使用)|
- #### 進階設定與自動化 (`.screenrc`)
  - Screen 不像 Minicom 有選單可以存檔，其設定主要靠 **HOME 目錄下的配置文件 `.screenrc`**
    ```
    # 建立/編輯設定檔
    vim ~/.screenrc
    ```
  - 建議加入的設定
    ```
    # 讓底部的狀態列顯示時間與會話名稱
    caption always "%{= kw}%-w%{= BW}%n %t%{-}%+w %=%H %Y-%m-%d %c"
    
    # 設定序列埠預設參數 (8 bit, No parity, 1 stop bit)
    defshell -bash
    ```
- #### 比較 Screen vs. Minicom
  |特性|Minicom|GNU Screen|
  |:---|:---|:---|
  |**介面**|複雜選單、狀態列明顯|極簡、完全空白|
  |**設定方式**|互動式 UI (-s)|啟動參數、設定檔|
  |**最強優勢**|功能齊 (支援腳本、傳檔)|**Detach/Attach** (中途斷開不丟失數據)|
  |**流控設定**|容易設定|較難在指令行直接改 (通常需進設定檔)|
  - **一行指令** `sudo screen /dev/ttyUSB0 115200`
    - 優 : 即可進入除錯狀態
    - 優 : Detach 功能能在 **長時間壓力測試** 中，隨時切換視窗而不中斷接收 Log
    - 缺 : 遇到鍵盤無法輸入的情況，通常是因為 **硬體流控(Flow Control)** 在背景開啟
      - 建議回到 `minicom -s` 關閉預設流控，或在 **啟動 screen 時增加特定的參數**
## 四、硬體環境設置
#### Rx 與 Tx **交叉原則**、**共地原則**
| STM32F072 | USB-to-TTL 模組 | 說明 |
|:---:|:---:|:---|
|**GND**|**GND**|提供基準電位，未接會導致訊號判斷失效|
|**PA9 (TX)**|**RXD**|晶片資料出口連至電腦入口|
|**PA10 (RX)**|**TXD**|電腦資料出口連至晶片入口|

## 五、韌體實作細節 (Firmware)
### 核心開發文件
|查找目標|建議手冊|關鍵章節 (Keywords)|
|:---:|:---|:---|
|各周邊 **暫存器位元 (Bit)** 定義|Reference Manual (RM0091)|各周邊(Peripheral) 章節末尾的 **Register description**|
|**時脈樹 (Clock Tree)** 頻率|Reference Manual (RM0091)|**Reset and clock control (RCC)**/Clock tree、HSI clock|
|引腳 **複用功能 (AF)** 對照表|Datasheet (DS9826)|Pinouts and pin descriptions / **Alternate functions**|
|**系統控制** 相關的 **暫存器**|Programming Manual (PM0215)|**System control block (SCB)**/AIRCR|
|處理器異常與中斷架構|Programming Manual (PM0215)|Exception model / NVIC|

### 各周邊、暫存器、位元 (Bit) 控制
|周邊 (Peripheral)|暫存器 (Register)|控制功能|Bit(位元)位置|設定值與物理意義|
|:---:|:---:|:---:|:---|:---|
|RCC|AHBENR|GPIO周邊供電開關|Bit 17 (IOPAEN)|`1`: 啟動 GPIOA 時脈，GIPOA 周邊暫存器才能運作 (MODER、ODR、AFRH、AFRL 暫存器才能通電做控制)|
|RCC|APB2ENR|UART周邊供電開關|Bit 14 (USART1EN)|`1`: 啟動 USART1 時脈，才能控制 UART1 周邊(通訊電路開始運作)|
|GPIOA|MODER|引腳模式切換|Bits [18:19] (PA9)<br>Bits [20:21] (PA10)|`10`: 設定為 Alternate Function (複用模式)|
|GPIOA|AFRH|複用功能選擇|Bits [7:4] (PA9)<br>Bits [11:8] (PA10)|`0001`: 指定為 AF0~AF7 中的 AF1 模式 (硬體連通至 USART1)|
|USART1|BRR|波特率分頻器|Bits [15:0]|填入計算後的分頻值 : <br>`69`(適用HSI 8M Hz)<br>`417`(適用PLL 48M Hz)|
|USART1|CR1|UART1模組總控制|Bit 0 (UE)<br>Bit 3 (TE)<br>Bit 2 (RE)|Bit-0 = `1`: 啟動 UART1 模組總開關<br>Bit-3 = `1`: 啟動發送器 (Transmitter)<br>Bit-2 = `1`: 啟動接收器 (Receiver)|
|USART1|ISR|狀態監控(**唯讀**)|Bit 7 (TXE)<br>Bit 5 (RXNE)|Bit-7 = `1`: 發送區空 (**可寫資料**)<br>Bit-5 = `1`: 接收區有資料 (**可讀資料**)|
|USART1|TDR|發送資料暫存器|Bits [8:0]|寫入此處 ： 資料由 TX 腳位發出去|
|USART1|RDR|接收資料暫存器|Bits [8:0]|讀取此處 ： 由 RX 腳位接收資料|
  



```
#include "stm32f072xb.h"

// 函式原型宣告
void uart_init(uint32_t baudrate);
void uart_send_char(char c);
char uart_receive_char(void);
void delay(int32_t count);

/**
 * @brief 延時函式
 */
void delay(int32_t count) {
    while (count--) {
        __asm("nop");
    }
}

/**
 * @brief 初始化 USART1 (PA9=TX, PA10=RX)
 */
void uart_init(uint32_t baudrate) {
    /* 1. 開啟時脈 */
    RCC->AHBENR |= (1UL << 17);  // GPIOA Clock
    RCC->APB2ENR |= (1UL << 14); // USART1 Clock

    /* 2. 設定 GPIOA 模式 (PA9, PA10) */
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20)); 
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20)); // AF Mode (10)

    /* 3. 設定複用功能 (AF1) */
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFRH |=  ((1UL << 4)  | (1UL << 8));   // AF1 為 USART1

    /* 4. 設定波特率 (關鍵修改) */
    // 假設系統跑 HSI 8MHz。 8,000,000 / 115200 ≈ 69
    // 直接賦值避免編譯器呼叫不存在的除法函式
    USART1->BRR = 69; 

    /* 5. 啟動 USART1 */
    USART1->CR1 |= (1UL << 0) | (1UL << 3) | (1UL << 2);
}

void uart_send_char(char c) {
    while (!(USART1->ISR & (1UL << 7)));
    USART1->TDR = c;
}

char uart_receive_char(void) {
    while (!(USART1->ISR & (1UL << 5)));
    return (char)(USART1->RDR);
}

int main(void) {
    /* --- 診斷代碼：點亮 PC9 綠色 LED --- */
    RCC->AHBENR |= (1UL << 19);     // 開啟 GPIOC 時脈
    GPIOC->MODER &= ~(3UL << 18);   // 清除 PC9 設定
    GPIOC->MODER |=  (1UL << 18);   // 設定 PC9 為輸出 (01)
    GPIOC->ODR |= (1UL << 9);       // 先點亮 LED，證明程式已進入 main

    // 初始化 UART
    uart_init(115200);

    // 傳送開機訊息
    uart_send_char('S');
    uart_send_char('S');
    uart_send_char('D');
    uart_send_char(' ');
    uart_send_char('R');
    uart_send_char('D');
    uart_send_char('Y');
    uart_send_char('\r'); // Carriage Return: 移回行首
    uart_send_char('\n'); // Line Feed: 跳到下一行

    while (1) {
        /* --- 診斷代碼：讓 LED 翻轉，證明程式沒當掉 --- */
        // 每收到一個字元就翻轉一次 LED 狀態
        char received = uart_receive_char();
        
        // 翻轉 PC9 LED
        GPIOC->ODR ^= (1UL << 9);


        // Echo 回傳：如果你希望打字換行也正常，可以判斷收到的字
        if (received == '\r' || received == '\n') {
            uart_send_char('\r');
            uart_send_char('\n');
        } else {
            uart_send_char(received); 
        }

        // Echo 回傳
        // uart_send_char(received); 
    }

    return 0;
}
```
## 六、除錯與驗證 (Troubleshooting)
### 模組迴圈測試 (Loopback Test)
- 將 USB 模組 **TXD 與 RXD 短路**
- 開啟 Minicom **Local Echo**
- 打字
- 判定
  - 若出現雙重字元（如 **AA**） : 代表 **電腦 ➔ Minicom 驅動 ➔ 硬體線路 ➔ 物理晶片** 全線暢通
  - 若出現單字元，代表可能只有電腦的 回顯

### 硬體物理排查
- GND 漂移 ： **未共地** 會導致 **訊號無基準**，螢幕會噴亂碼或完全不顯示
- TX/RX 對調 ： 最常見錯誤，若 診斷代碼 中的 PC9 LED 已亮（代表進 Main），但無輸出，請立刻 **交換 PA9 / PA10**

### 指令級強制測試，跳過 Minicom (Bypass Minicom)
Minicom 軟體介面無反應時，使用 **底層指令排除軟體設定錯誤**
- 視窗 A (本地 **監聽 RX**) ： `sudo cat /dev/ttyUSB0`
  - 強行讀取 **驅動程式 RX 緩衝區** 內容並顯示
- 視窗 B (本地 **發送 TX**) ： `echo "TEST" | sudo tee /dev/ttyUSB0`
  - `tee` 作為三通管，同時將 `"TEST"` **輸出至螢幕** 並 **寫入裝置檔案 (觸發本地物理  TX)**
- #### Linux 裝置檔案系統 (Everything is a File)
  - 在 Linux 中，`/dev/ttyUSB0` 被視為一個檔案
  - **寫入 (Write) = TX 發送** ： 從電腦噴出訊號
  - **讀取 (Read) = RX 接收** ： 監聽外部進來的訊號
### 交叉驗證邏輯
- 若 **`cat` 內有字，但 minicom 沒字**
  - 代表 USB-to-TTL 晶片沒壞、接線正確、電腦正常有抓到裝置 `/dev/ttyUSB0`
  - **100% 為 Minicom 軟體 或 設定問題**，檢查波特率、流控
- 若 **兩者皆無字**
  - 檢查 杜邦線
  - 檢查 USB 晶片是否損壞
  - 檢查 Linux `sudo dmesg` 是否抓到裝置

