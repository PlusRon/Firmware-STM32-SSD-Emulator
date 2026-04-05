# UART 應用邏輯 主程式

## 一、理論、技術討論
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

## 二、Minicom 終端機模擬軟體
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

---
```
開 minicom的目的是作為電腦與 STM32之間的一個作為通訊的界面，因為電腦本身撰寫程式可以直接顯示在螢幕上是因為電腦作業系統與螢幕是一整體，所以可以直接顯示，但因為 STM32晶片本身是沒有系統且沒有顯示器螢幕可以顯示，必須透過電腦系統上的虛擬界面 minicom來作為電腦與 STM32晶片的溝通橋樑。當電腦在這個虛擬螢幕界面上輸入訊號 會將電腦訊號透過 UART模組 的 Tx端將資料傳給 STM32內建UART的Rx進行接收，而我事前燒入到STM32晶片中的程式執行時 晶片的傳輸訊號程式會透過 STM32的內建 UART的Tx端將資料傳送出去 經過連接在電腦端的UART模組的Rx端進行接收，所以若沒有 UART就電腦與 STM32就無法進行溝通。

至於為何一定要設定 Baud Rate，他們之間不是都直接連接電線了嗎 不就跟一般我們用USB連接手機和電腦一定可以直接溝通才對 為何要設定 Baud Rate，電腦與STM32之間一定要利用UART才能溝通媽 不能用其方法嗎 
```

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

```
#include "stm32f072xb.h"

// 函式原型宣告
void uart_init(uint32_t baudrate);
void uart_send_char(char c);
char uart_receive_char(void);

/**
 * @brief 延時函式 (防止編譯器優化)
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
    /* 1. 開啟時脈 (Clock Enable) */
    // 給 GPIOA 送電 (第 17 位元)
    RCC->AHBENR |= (1UL << 17); 
    // 給 USART1 送電 (第 14 位元)
    RCC->APB2ENR |= (1UL << 14);

    /* 2. 設定 GPIOA 模式 (MODER) */
    // PA9, PA10 設為 Alternate Function (10)
    // 清零 [19:18] (PA9) 與 [21:20] (PA10) 位元
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20)); 
    // 設定為 10 (AF 模式)
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20));

    /* 3. 設定複用功能選擇 (AFRH) */
    /* 因為 PA9 和 PA10 屬於高位組 (Pin 8-15)，所以操作 AFRH */
    // PA9 對應 AFRH 的 [7:4] 位元，設為 AF1 (USART1)
    // PA10 對應 AFRH 的 [11:8] 位元，設為 AF1 (USART1)
    // 註：在結構體中我們定義的名字是 AFRH，不是 AFR[1]
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFRH |=  ((1UL << 4)  | (1UL << 8));

    /* 4. 設定波特率 (Baud Rate) */
    // 假設系統時脈為 48MHz (STM32F0 預設通常靠內部 HSI 或 PLL)
    // BRR = 48,000,000 / 115200 = 416.66... 填入 417 (0x1A1)
    USART1->BRR = 48000000 / baudrate;

    /* 5. 啟動 USART1 (Control Register) */
    // UE: 總使能 (bit 0), TE: 發送使能 (bit 3), RE: 接收使能 (bit 2)
    USART1->CR1 |= (1UL << 0) | (1UL << 3) | (1UL << 2);
}

/**
 * @brief 傳送一個字元到電腦
 */
void uart_send_char(char c) {
    // 等待 TXE (Transmit data register empty) 變為 1 (bit 7)
    // 表示 TDR 暫存器已經空了，可以塞入下一個字元
    while (!(USART1->ISR & (1UL << 7)));
    USART1->TDR = c;
}

/**
 * @brief 從電腦接收一個字元
 */
char uart_receive_char(void) {
    // 等待 RXNE (Read data register not empty) 變為 1 (bit 5)
    // 表示硬體已經收到資料並搬移到 RDR 暫存器
    while (!(USART1->ISR & (1UL << 5)));
    return (char)(USART1->RDR);
}

int main(void) {
    // 初始化 UART，波特率設定為 115200
    uart_init(115200);

    // 開機歡迎訊息 (測試 TX 功能)
    uart_send_char('S');
    uart_send_char('S');
    uart_send_char('D');
    uart_send_char(' ');
    uart_send_char('R');
    uart_send_char('D');
    uart_send_char('Y');
    uart_send_char('\n');

    while (1) {
        // 迴圈測試：Echo Test (接收什麼就傳回什麼)
        char received = uart_receive_char();
        
        // 傳回剛收到的字元
        uart_send_char(received); 
    }

    return 0;
}

```
