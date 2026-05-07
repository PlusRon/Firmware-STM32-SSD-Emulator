# UART 非阻斷式、非同步收發資料處理
建構一個 **非阻斷式(Non-blocking) 處理架構**，以解決 **阻塞式等待 (Polling)** 與 **頻繁中斷(Interrupt per Byte)** 兩個問題。透過 **DMA** 與 **Ring Buffer (循環緩衝區)** 的概念，實現 **通訊層** 與 **應用層** 的 **解耦**，讓 CPU 能在接收資料的同時，並行處理如 LED 閃爍、感測器運算等背景任務，挑戰平衡 通訊即時性 與 任務並行處理 兩任務。


## Outline
#### [理論、技術實作](#一理論技術實作)
- [UART 硬體介紹(含通訊工具)、硬體設置、韌體實作、除錯驗證](#1-UART-硬體介紹含通訊工具硬體設置韌體實作除錯驗證)
- [DMA (Direct Memory Access)：零CPU介入的資料搬運](#2-DMA-Direct-Memory-Access零CPU介入的資料搬運)
- [Ring Buffer (循環緩衝區)：非同步生產者/消費者模型](#3-Ring-Buffer-循環緩衝區非同步生產者消費者模型)
- [IDLE Line Detection (空閒線路偵測)：處理 非固定長度封包](#4-IDLE-Line-Detection-空閒線路偵測處理-非固定長度封包)
- [Flow Control 雙重流控機制：高負載下的資料零遺失與自癒能力](#5-Flow-Control-雙重流控機制高負載下的資料零遺失與自癒能力)
- [SysTick 時基系統：非阻塞式任務調度](#6-SysTick-時基系統非阻塞式任務調度)
#### [硬體環境設置](#二硬體環境設置)
- [Rx 與 Tx 交叉原則、共地原則](#Rx-與-Tx-交叉原則共地原則)
#### [韌體實作細節 (Firmware)](#三韌體實作細節-Firmware)
- [核心開發文件](#核心開發文件)
- [周邊、暫存器、位元 (Bit) 控制](#周邊暫存器位元-Bit-控制)
- [程式碼](#程式碼)
#### [除錯與驗證 (Troubleshooting)](#四除錯與驗證-Troubleshooting)
- [模組迴圈測試 (Loopback Test)](#模組迴圈測試-Loopback-Test)
- [硬體物理排查](#硬體物理排查)
- [指令級強制測試，跳過 Minicom (Bypass Minicom)](#指令級強制測試跳過-Minicom-Bypass-Minicom)
- [交叉驗證邏輯](#交叉驗證邏輯)

## 一、理論、技術實作
### [(1) UART 硬體介紹(含通訊工具)、硬體設置、韌體實作、除錯驗證](UART_introduce.md)
### (2) DMA (Direct Memory Access)：零CPU介入的資料搬運
[`阻斷式 Polling`](Polling_to_ISR_DMA/polling.md) → [`中斷驅動`](Polling_to_ISR_DMA/ISR.md) → `DMA 硬體自動化`
- **DMA 硬體自動化**
  - DMA 是 **獨立**於 CPU 的 **硬體控制器**，擁有 **直接存取記憶體匯流排的權限**
  - DMA 允許 外設(UART) 直接與記憶體 (SRAM) 交換資料

    ```
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR); // 外設位址：指向 UART 接收暫存器
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;      // 記憶體位址：指向我們定義的 Ring Buffer
    ```
    - **CPAR (Channel Peripheral Address Register)** ： 固定指向 UART 的接收口，即 外設 (Peripheral)
    - **CMAR (Channel Memory Address Register)** ： 指向存放資料的 Ring Buffer 陣列的起始位址，即 記憶體 (Memory)
  - **DMA 監聽 UART 的接收請求(Request)**　： UART 的資料準備好會發送通知給 DMA，背景下 DMA 直接將資料從 `USART->RDR` 搬運到自定義的 **SRAM 緩衝區**

    ```
    // 4. UART 設定
    USART1->CR3 = (1UL << 6) | (1UL << 8);
    ```
    - **Bit 6 (DMAR, DMA Enable Receiver)**
    - 告訴 UART 硬體，當 **RXNE 變成 1 (接收暫存器非空)** 時，立刻發出一個 **DMA 請求 (DMA Request)** 給 DMA 控制器
    - DMA 硬體會直接偵測 RXNE 訊號，只要 RXNE 為 `1`，瞬間把資料從 `USART1->RDR` 搬到 `rx_buffer`，然後自動清空 RXNE 為 `0`
    - 沒這行，UART 就算收到資料，也只會等待 CPU 來讀 (即 所提及的中斷內搬移)，而不會通知 DMA
  - **控制暫存器配置 (CCR)**

    ```
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);
    ```
    - **MINC (Bit 7)** ： **記憶體位址增量** 模式，確保每搬一個 **字元 (Byte)**，目的地位址會自動 `+1` (硬體內部的 write **pointer**)
    - **CIRC (Bit 5)** ： **循環模式 (Circular Mode)**，實現不斷電、不停機接收的關鍵，讓 **緩衝區首尾相連**，形成 Ring Buffer
    - **EN (Bit 0)** ： 啟動 通道 (Channel)
  - **傳輸長度** 與 **循環模式**

    ```
    DMA1->CH[2].CNDTR = RX_BUF_SIZE; // 設定緩衝區總長度 (1024)
    ```
    - `CNDTR` 是 **DMA 的下數計數暫存器** (Buffer 剩餘的空間)
    - 每搬運完一個 Byte，該值會自動遞減 (RXNE 由 `1` 變 `0` 提供給 Flip-Flop 的負緣觸發驅動)
    - 當遞減到 0 時，因為已啟用 **循環模式（Circular Mode）**，會自動重新載入 `RX_BUF_SIZE` 並回到緩衝區開頭開始寫入 (利用 Flip-Flop 的 CLR 和 Reset)
    - 
  - **動態指標換算(主程式中)**

    ```
    uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
    ```
    - CNDTR 是 **遞減計數** 器
    - 用 **總長度 - 剩餘計數(CNDTR)**，精確換算出 `wr_ptr`（算出隱藏在硬體內部的 write pointer，即 `MINC = 1` 所創建的指標）
    - 使軟體中的 `rd_ptr` 能夠精確地追蹤硬體中 Buffer 內的存放狀態

### (3) Ring Buffer (循環緩衝區)：非同步生產者/消費者模型
透過 **兩個指標 (`rd_ptr` 與 `wr_ptr`)** 並 設定 **DMA 硬體循環模式(Circular Mode)**，達到 Ring Buffer
- **生產者 / 消費者模型**
  - **DMA** 作為 **生產者**(負責寫入)，**CPU** 作為 **消費者**(負責讀取)，只要 **消費者讀取速度快於生產者寫入速度**，緩衝區就能源源不絕的運作
- #### 空間配置與定義

  ```
  #define RX_BUF_SIZE 1024         // 深度決定了系統對延遲的忍受度
  uint8_t rx_buffer[RX_BUF_SIZE];  // 實際的物理儲存空間
  uint16_t rd_ptr = 0;             // 軟體維護的讀取指標
  ```
  - 設定 `RX_BUF_SIZE = 1024`，提供足夠深度來緩衝突發的大量數據，提高系統對延遲的忍受度，避免 CPU 因短暫忙碌而導致資料被覆蓋
  - 在 `115200` Baud Rate 下，填滿此緩衝區約需 `88ms`，即使 CPU 即使在 `My_Delay_ms(2000)` 的壓力測試下，只需靠 **RTS 流控** 稍微阻擋，醒來後依然有足夠的深度處理殘留資料
- #### 硬體級自動繞回 (Hardware Wrap-around)

  ```
  DMA1->CH[2].CCR |= (1UL << 5); // 啟動 CIRC (Circular Mode)
  ```
  - Ring Buffer 的動力來源，當 DMA 計數器 **CNDTR 歸零** 時，硬體會自動將寫入位址 **重新指向 `rx_buffer[0]`**，不需任何 CPU 指令介入，保證了高頻通訊下的絕對穩定
  - 當指標到達陣列末尾(`Size - 1`)時，下一個位置自動回到 `0`，為循環模式(Circular Mode)，首尾相連
- #### 讀寫指標的資訊提取

  ```
  uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
  ```
  - 因 DMA 是在背景工作的，必須額外設計讓軟體知道 Ring Buffer 當下寫到哪？
  - **CNDTR 暫存器** 是儲存 **剩餘可傳入量**
    - 初始 ： `CNDTR = 1024`, `wr_ptr = 0`
    - 接收 10 筆 ： `CNDTR = 1014`, `wr_ptr = 10`
  - 為何不用 `MINC`？
    - 雖然 **MINC** 負責 **位址遞增**，但它是內部機制，不提供直接讀取的位址暫存器，因此必須 **讀取 CNDTR 並計算** 來獲取當前 DMA 位置
- #### 軟體繞回邏輯 (Software Wrap-around)

  ```
  rd_ptr++;
  if (rd_ptr >= RX_BUF_SIZE) rd_ptr = 0;  // 軟體指標到達邊界，強制歸零
  ```
  - CPU 讀取資料時，也必須手動維護圓環邏輯
  - 確保 `rd_ptr` 永遠在 `1024` 的區間內跟隨 `wr_ptr` 循環
- #### 動態追趕 讀寫指標

  ```
  while (rd_ptr != wr_ptr) {
      // ... 處理資料 ...
      wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;  // 在迴圈中動態更新
  }
  ```
  - 高流量下，進入 `while-loop` 處理 50 個字元的期間，DMA 可能又寫入了 20 個字
  - 必須在迴圈末端動態更新 `wr_ptr`，而 `rd_ptr` 會像 追隨者 一樣不斷消耗 Ring Buffer 內的新進資料，直到緩衝區真正被清空為止，極大地降低了中斷觸發次數與封包延遲 
### (4) IDLE Line Detection (空閒線路偵測)：處理 非固定長度封包
- 當 UART 的 RX 線路接收完一個 Byte 後，其 **ISR (Interrupt State) 偵測到 IDLE 狀態** 維持在 **高電平超過一個 Byte 的時間(無傳輸)**，硬體就會自動將 IDLE 旗標置 `1`
- 能精確在 一波資料 **傳送結束的瞬間通知 CPU**，適合處理長度不一的封包 (EX. AT 指令、自定義協議)
- #### 硬體中斷配置

  ```
  USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);
  ```
  - Bit 4 ： 是 **IDLEIE (IDLE Interrupt Enable)**，UART 開啟空閒通知，只要線路進入空閒中斷 (由 ISR 的 bit-4 判斷)，UART 會立刻叫醒 CPU
- #### 中斷服務程式 (ISR) 的處理

  ```
  void USART1_IRQHandler(void) {
      // 檢查 IDLE 旗標位 (Bit 4)
      if (USART1->ISR & (1UL << 4)) {
          USART1->ICR = (1UL << 4); // 關鍵：寫入 ICR 暫存器手動清除旗標
          rx_idle_event = 1;             // 標記「收件結束」事件，通知主迴圈
      }
      // ... 其他旗標檢查 ...
  }
  ```
  - 中斷發生時，CPU 在 IRQ Handler 中僅需做兩件事 (**精簡** 中斷服務程式)
    - 清除 狀態旗標 `ISR` 為 `ICR` (防止重複進入中斷) 
    - 設定一個全域布林標記 `rx_idle_event`，這符合 ISR (Interrupt Sevice Routine) 應 **極簡且快速** 的原則
    - 將耗時的資料處理留給 main 迴圈，透過 事件布林標記判斷以執行資料處理
   
- #### 主迴圈的雙重門檻判斷

  ```
  // 完美整合版：外層用雙重判斷門檻
  if (rx_idle_event || (rd_ptr != wr_ptr)) {
      // ... 進入資料解析 ...
      rx_idle_event = 0; // 處理完畢後清空
  }
  ```
  - 使用 **事件驅動 (rx_idle_event)** 與 **狀態驅動 (`rd_ptr` 和 `wr_ptr` 不相等)** 的雙重保險
  - `rx_idle_event` ： 處理整串封包抵達後的立即解析
  - `rd_ptr` 和 `wr_ptr` 不相等 ： 處理在解析過程中又持續進來的散碎資料
  - 即使 `My_Delay_ms(2000)` 讓 CPU 錯過了 IDLE 觸發的瞬間，CPU 醒來後依然可以透過指標判斷將資料領走，而 `rx_idle_event` 則作為一種 **主動喚醒** 的高效機制


### (5) Flow Control 雙重流控機制：高負載下的資料零遺失與自癒能力
在非同步通訊中，當 **發送端速度 > 接收端處理速度** 時，會發生嚴重的 **資料溢位**，透過 硬體 與 軟體 的雙重守護，構建穩健的通訊鏈路
- **第一層防護 ： 硬體級流控 (Hardware Flow Control, RTS/CTS)** ： 直接在物理層運作，使用 **RTS (Request To Send)** 訊號
  - 當 STM32 負責接收的 FIFO 或 Buffer 接近滿載時，STM32 硬體會自動將其 **`PA12` (RTS)** 腳位拉高
  - 發送端（電腦）偵測到其 USB to TTL 模組的 **CTS (Clear To Send, 連接到 STM32 的 RTS)** 線變為高電位後，會立即停止物理性發送，直到 STM32 處理完資料並拉低 RTS 為止
  - **GPIO 配置**
    ```
    // (3) 硬體流控：加入 PA12 作為 RTS 腳位
    GPIOA->MODER |= (2UL << 24); // 設定為 Alternate Function 模式
    GPIOA->AFRH  |= (1UL << 16); // 指向 AF1 (USART1_RTS)
    ```
  - **外設控制**

    ```
    USART1->CR3 |= (1UL << 8); // 啟動 RTSE (RTS Enable) 位元
    ```
    - 設定完成後，**RTS 的電位切換** 完全 **由 UART 硬體控制器自動管理**，不佔用任何 CPU 週期
- **第二層防護：軟體級容錯與自癒 (ORE & NACK)** ： 災難偵測 並 重新同步
  - 即使有硬體流控，若通訊線路不穩 或 雜訊導致硬體無法及時反應，仍可能發生 **Overrun Error (ORE)**，因此系統必須具備 **自我修復(Self-healing)** 能力
  - **捕捉 ORE 異常** ： 當 **硬體流控失敗導致 Overrun Error (ORE)** 發生時，STM32 硬體會鎖死接收暫存器，必須在中斷中立即解鎖

    ```
    void USART1_IRQHandler(void) {
        if (USART1->ISR & (1UL << 3)) {   // 偵測到 ORE 旗標
            USART1->ICR = (1UL << 3);     // 手動清除旗標，重啟硬體接收
            uart_overrun_occurred = 1;    // 標記軟體狀態
        }
    }
    ```
    - 透過 **`USART1_IRQHandler` 捕捉異常**
  - **NACK 重傳機制與指標對齊 (Disaster Recovery)**
    ```
    if (uart_overrun_occurred) {
        uart_overrun_occurred = 0;
        UART_SendChar(ASCII_NAK); // 發送 0x15 (NAK) 告知對端：資料遺失，請重發
        UART_Send("\r\n[NACK] Overflow Detected!\r\n");
        
        // 強制指標同步：放棄受損的殘餘資料，將讀取指標對準當前 DMA 寫入點
        rd_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR; 
    }
    ```
    - 發送 `0x15 (NAK)` 訊號請求重傳，同時強制同步指標（Disaster Recovery）
    - 當資料流出錯時，最危險的是 **指標偏移** 導致 **後續所有封包解析錯位**
    - 透過強制將 `rd_ptr` 對齊硬體的 `wr_ptr`，實現秒級重啟通訊
 - **軟體與硬體流控比較**
    |特性|第一層 (RTS)|第二層 (NACK/ORE)|
    |:---|:---|:---|
    |性質|預防性 (Prevention)|補救性 (Recovery)|
    |層級|硬體層 (Physical Layer)|協議層 (Application Layer)|
    |目標|避免 Buffer 滿載|處理突發異常，確保指標對齊|
    |對手|高速資料流|系統當機、干擾、協議失效|
    
### (6) SysTick 時基系統：非阻塞式任務調度
SysTick (System Tick Timer) 能在 **不使用即時作業系統(RTOS)** 的情況下，依然能精確 **管理多個併行任務**。`SysTick` 是內建於 ARM Cortex-M 核心內的一個 **24 位元遞減計數器**，是為了提供一個穩定的 **心跳 (Heartbeat)**，讓系統擁有統一的 ms 級 時間戳記，實現非阻塞式的定時任務
- #### 硬體初始化與頻率換算

  ```
  SysTick->LOAD = 8000 - 1; // 設定自動重載值
  SysTick->VAL = 0;        // 清空當前計數值
  SysTick->CTRL = 7;       // 啟動：內部時鐘(4), 中斷開啟(2), 致能(1)
  ```
  - 由於 STM32F0 預設運行於 **8MHz**，設定 `LOAD = 8000 - 1` 代表計數器**每數 8000 次(Tick) 會觸發一次中斷**
  - $8000 \div 8,000,000 \text{ Hz} = 0.001 \text{ 秒} = 1 \text{ ms}$，確保系統心跳頻率精確維持在 1000Hz (1000 個 Tick)
  - 當 `VAL = 0` 會重新載入 `8000 - 1` 繼續下數
  - 當 從 1 數到 0 的負緣 會觸發中斷自增
- #### 中斷自增與原子性考量

  ```
  volatile uint32_t msTicks = 0; // 必須使用 volatile 防止編譯器優化

  void SysTick_Handler(void) {
      msTicks++; // 每 1ms 遞增一次
  }
  ```
  - `msTicks` 記錄了自系統啟動以來經過的總毫秒數
  - 使用 `volatile` 關鍵字是為了確保 main 迴圈每次讀取該變數時，都是直接從記憶體抓取最新的數值，而不會將該變數優化到 CPU 暫存器中
  - 而不是讀取 CPU 快取暫存器中的舊值。

- #### 非阻塞式任務設計 (Non-blocking Task)

  ```
  // 任務 2：背景任務 (LED 閃爍)
  if ((get_tick() - last_blink) >= 500) {
      LED_Toggle(&led_current_state);
      last_blink = get_tick(); // 更新上次執行的時間點
  }
  ```
  - **時間差判斷法** ： 不會像 `delay()` 一樣卡死 CPU，如果時間還沒到(不到 `500ms`)，CPU 會直接跳過這段程式碼去執行 UART 接收邏輯
  - 是實現 **虛擬併行處理** 的關鍵


## 二、硬體環境設置
#### Rx 與 Tx **交叉原則**、**共地原則**
| STM32F072 | USB-to-TTL 模組 | 說明 |
|:---:|:---:|:---|
|**GND**|**GND**|提供基準電位，未接會導致訊號判斷失效|
|**PA9 (TX)**|**RXD**|晶片資料出口連至電腦入口|
|**PA10 (RX)**|**TXD**|電腦資料出口連至晶片入口|

## 三、韌體實作細節 (Firmware)
### 核心開發文件
|查找目標|建議手冊|關鍵章節 (Keywords)|
|:---:|:---|:---|
|各周邊 **暫存器位元 (Bit)** 定義|[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)|各周邊(Peripheral) 章節末尾的 **Register description**|
|**時脈樹 (Clock Tree)** 頻率|[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)|**Reset and clock control (RCC)**/Clock tree、HSI clock|
|引腳 **複用功能 (AF)** 對照表|[Datasheet (DS9826)](https://www.st.com/resource/en/datasheet/stm32f072c8.pdf)|Pinouts and pin descriptions / **Alternate functions**|
|**系統控制** 相關的 **暫存器**|[Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)|**System control block (SCB)**/AIRCR|
|處理器異常與中斷架構|[Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)|Exception model / NVIC|

### 周邊、暫存器、位元 (Bit) 控制
|周邊|暫存器|控制功能|位元 (Bit) 控制|設定值與物理意義|
|:---:|:---:|:---:|:---|:---|
|RCC|AHBENR|GPIO周邊供電開關|Bit 17 (**IOPAEN**)|`1`: 啟動 GPIOA 時脈，GIPOA 周邊暫存器才能運作 <br>(MODER、ODR、AFRH、AFRL 暫存器才能通電做控制)|
|RCC|AHBENR|GPIO周邊供電開關|Bit 19 (**IOPCEN**)|`1`: 啟動 GPIOC 時脈，控制 LED 引腳與 BSRR 運作|
|RCC|APB2ENR|UART周邊供電開關|Bit 14 (**USART1EN**)|`1`: 啟動 USART1 時脈，才能控制 UART1 周邊<br>(通訊電路開始運作)|
|GPIOA|MODER|引腳模式切換|Bits [19:18] (**PA9**)<br>Bits [21:20] (**PA10**)|`10`: 設定為 Alternate Function (複用模式)|
|GPIOA|AFRH|複用功能選擇|Bits [7:4] (**PA9**)<br>Bits [11:8] (**PA10**)|`0001`: 指定為 **AF0~AF7** 中的 **AF1** 模式 (硬體連通至 USART1)|
|GPIOA|MODER|引腳模式切換|Bits[25:24] (**PA12**)|`10` : 設定為 Alternate Function (複用模式) 以支援 **RTS**|
|GPIOA|AFRH|複用功能選擇|Bits [19:16] (**PA12**)|`0001` : 指定為 AF1，將引腳硬體連通至 **USART1_RTS**|
|GPIOC|MODER|引腳模式切換|Bits [13:12] (**PC6**)|`01`: 設定為 General Purpose Output (一般輸出模式)|
|GPIOC|BSRR|位元設定/清除|Bit 6 (**BS6**)<br>Bit 22 (**BR6**)|BS6=`1`: Set (輸出高電位)<br>BR6=`1`: Reset (輸出低電位)<br>具備原子性操作，不受中斷干擾|
|USART1|BRR|波特率分頻器|Bits [15:0]|填入計算後的分頻值 : <br>`69`(適用HSI 8M Hz)<br>`417`(適用PLL 48M Hz)|
|USART1|CR1|UART1模組總控制|Bit 0 (**UE**)<br>Bit 3 (**TE**)<br>Bit 2 (**RE**)|Bit-0 = `1`: 啟動 UART1 模組總開關<br>Bit-3 = `1`: 啟動發送器 (Transmitter)<br>Bit-2 = `1`: 啟動接收器 (Receiver)|
|USART1|TDR|**發送資料**暫存器|Bits [8:0]|寫入此處 ： 資料由 TX 腳位發出去|
|USART1|RDR|**接收資料**暫存器|Bits [8:0]|讀取此處 ： 由 RX 腳位接收資料|
|USART1|CR1|IDLE中斷致能|Bit 4 (IDLEIE)|`1`: 當偵測到線路空閒超過 1 Byte 時間，觸發 CPU 中斷|
|USART1|ISR|狀態**監控**(**唯讀**)|Bit 7 (**TXE**)<br>Bit 5 (**RXNE**)<br>Bit 4 (**IDLE**)|Bit-7 = `1`: 發送區空 (**可寫資料**)<br>Bit-5 = `1`: 接收區有資料 (**可讀資料**)<br>Bit-4 = `1`: IDLE 中斷發生|
|USART1|ICR|中斷旗標清除|Bit 4 (IDLECF)<br>Bit 3 (ORECF)|寫入 `1`: 清除 IDLE 或 Overrun 旗標，使 CPU 能再次進入中斷|
|USART1|CR3|DMA 與流控配置|Bit 6 (DMAR)<br>Bit 8 (RTSE)|Bit-6 = `1`: 允許 UART 接收資料後觸發 DMA 請求<br>Bit-8 = `1`: RTS 硬體流控啟動，當 Buffer 滿載時自動拉高引腳阻止發送|
|DMA1|CPAR2|DMA 通道外設地址|Bits [31:0]|寫入 `(uint32_t)&USART1->RDR`：指定資料來源為 UART 接收暫存器|
|DMA1|CMAR2|DMA 通道記憶體地址|Bits [31:0]|寫入 `rx_buffer` 地址：指定搬運目的地為軟體 Ring Buffer|
|DMA1|CNDTR2|DMA 剩餘計數器|Bits [15:0]|初始值 `1024`: 每收一個 Byte 減 `1`，可用來換算當前寫入指標|
|DMA1|CCR2|DMA 通道配置|Bit 7 (MINC)<br>Bit 5 (CIRC)<br>Bit 0 (EN)|Bit-7 = `1`: 記憶體位址自動增量<br>Bit-5 = `1`: 循環模式 (自動回繞至緩衝區開頭)<br>Bit-0 = 1: 啟動 DMA 通道搬運|
|SysTick|LOAD|自動重載暫存器|Bits [23:0]|`8000-1`: 設定計數器週期。在 8MHz 下對應 1ms|
|SysTick|VAL|當前數值暫存器|Bits [23:0]|寫入任何值將其歸零，並清除 `COUNTFLAG`|
|SysTick|CTRL|SysTick 總控制|Bit 2 (CLKSOURCE)<br>Bit 1 (TICKINT)<br>Bit 0 (ENABLE)|`7` (二進制 `111`): 時鐘源選擇、開啟異常中斷、啟動計時器<br>Bit-2 : 時鐘源選擇，`1` 表示使用處理器核心時鐘 (HCLK)<br>Bit-1 : 開啟異常中斷，倒數到 `0` 時，觸發 `SysTick_Handler()`<br>Bit-0 : 啟動定時器，開始從 LOAD 暫存器的值倒數|

### RCC Peripheral
- #### 位於 AHB1 BUS (RM0091, P.48)
  <img width="853" height="604" alt="image" src="https://github.com/user-attachments/assets/30dc49c7-5551-4c6f-838d-136f55ae89d3" />
- #### 所擁有的 Register (RM0091)
  <img width="796" height="796" alt="image" src="https://github.com/user-attachments/assets/48e96b6f-ad72-4527-b17d-a3facdc93b27" />

### GIPOA Peripheral
- #### 位於 AHB2 BUS (RM0091, P.48)
  <img width="800" height="218" alt="image" src="https://github.com/user-attachments/assets/b4f3cbbf-f675-4284-916b-8ea127c2f157" />
- #### 所擁有的 Register (RM0091)
  <img width="795" height="956" alt="image" src="https://github.com/user-attachments/assets/5b951c7e-f639-4b99-aa3a-5716f81cf6e5" />
  <img width="796" height="283" alt="image" src="https://github.com/user-attachments/assets/cc2d0705-70ac-4cdb-a73e-856490023967" />
- #### AF0~AF7 的八種複用功能，由 GPIOA 的 AFR registers 之 bit 控制 (DS9826, P.44)
  <img width="1322" height="618" alt="image" src="https://github.com/user-attachments/assets/55ec2ed4-5380-43e9-9bbe-6b5bf3acc672" />


### UART1 Peripheral
- #### 位於 APB BUS (RM0091, P.49)
  <img width="802" height="685" alt="image" src="https://github.com/user-attachments/assets/c65e5042-4a15-454e-a4e1-40c11997b0bd" />
- #### 所擁有的 Register (RM0091)
  <img width="797" height="741" alt="image" src="https://github.com/user-attachments/assets/9aab3c5c-1c9a-4fff-bf42-0e86a6b031aa" />
  <img width="796" height="450" alt="image" src="https://github.com/user-attachments/assets/cba2bcf5-8990-4d22-96ac-35f6eccef2be" />




### 程式碼
UART + DMA(Ring Buffer) + ORE（Overrun Error）Detection + 硬體流控 (RTS/CTS) + 加大 Buffer (1024 bytes) + ACK/NACK 機制
```
#include "stm32f072xb.h"

/* --- MACRO define --- */
#define RX_BUF_SIZE 1024  // Increasing the buffer size to 1024, significantly reduces the probability of ORE

/* ASCII communication character definitions */
#define ASCII_NAK 0x15    // Negative Acknowledge (Data Error / Re-transmission Request)

/* --- global variable --- */
volatile uint32_t msTicks = 0;               // 8000 clock as a 1ms(1 Tick)
uint8_t rx_buffer[RX_BUF_SIZE];              // buffer assign from RAM
uint16_t rd_ptr = 0;                         // CPU(consumer) read pointer (software)
volatile uint8_t rx_idle_event = 0;          // IDLE event
volatile uint8_t uart_overrun_occurred = 0;  // software ORE flag

/* --- system timing ISR--- */
void SysTick_Handler(void) {
    msTicks++;
}

uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED module --- */
void LED_Init(void) {
    RCC->AHBENR |= (1UL << 19);     // power on GIPOC
    GPIOC->MODER &= ~(3UL << 12);   // PC6 clear Mode
    GPIOC->MODER |=  (1UL << 12);   // PC6 set Mode as GPO
}

/* Bit Set Reset Register*/
void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        GPIOC->BSRR = (1UL << 6);     // SET PC6
        *state = 1;
    } else {
        GPIOC->BSRR = (1UL << 22);    // RESET PC6
        *state = 0;
    }
}

/* --- UART and DMA --- */
void USART1_IRQHandler(void) {
    // check IDLE
    if (USART1->ISR & (1UL << 4)) { // Bit-4 : IDLE occur when IDLE-state is true
        USART1->ICR = (1UL << 4);   // Bit-4 : IDLE-state be cleared
        rx_idle_event = 1;          // IDLE event will be trigger
    }

    // check ORE (Overrun)
    if (USART1->ISR & (1UL << 3)) { // Bit-3 : ORE occur when ORE-state is true
        USART1->ICR = (1UL << 3);   // Bit-3 : ORE-state be cleared

        // No logic is handled here; the NACK process is delegated to the main function.
        uart_overrun_occurred = 1;      // (Mark) to tell main() that something just happened (ORE)
    }
}

void UART_Send(char *s) {
    while (*s) {
        while (!(USART1->ISR & (1UL << 7)));  // Bit-7 : stuck here when TxE-state is false (previous data hasn't been fully transmitted yet)
        USART1->TDR = *s++;                   // Bit-7 : transmitted the next data when TxE-state is true
    }
}

void UART_SendChar(uint8_t c) {
    while (!(USART1->ISR & (1UL << 7)));
    USART1->TDR = c;
}

void My_Delay_ms(uint32_t ms) {
    uint32_t start = get_tick();         // now time
    while ((get_tick() - start) < ms);   // stuck ms
}

/* --- System initialization --- */
void System_Init(void) {
    // 1. clock on
    RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    RCC->APB2ENR |= (1UL << 14);

    LED_Init();

    // 2. clear setting
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20) | (3UL << 24));

    // 2. GPIO settings (PA9=TX, PA10=RX, PA12=RTS)
    // 3. Hardware flow control : Add PA12 as an RTS pin
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20) | (2UL << 24));  // All set to AF mode
    
    // 4. The AF values ​​for PA9, PA10, and PA12 are all AF1 (USART1)
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8) | (0xFUL << 16));
    GPIOA->AFRH |=  ((1UL << 4) | (1UL << 8) | (1UL << 16));

    // 5. DMA setting
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    // 6. UART setting
    USART1->BRR = 69;  // 115200 Baud Rate @ 8MHz
    
    // 7. CR3 Modification: Added RTSE (Bit 8) to enable hardware flow control.
    USART1->CR3 = (1UL << 6) | (1UL << 8); 

    // Bit-0 : UART EN , Bit-3 : Tx EN , Bit-2 : Rx EN  , Bit-4 : IDLE EN
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);

    // 8. NVIC and SysTick
    *((volatile uint32_t *)0xE000E100) = (1UL << 27);
    SysTick->LOAD = 8000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 7;
}

/* --- Main --- */
int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send("Industrial UART System Initialized (RTS/NACK Enabled)\r\n");

    while (1) {
        /* --- Core safeguard mechanism for NACK retransmission detection: checking software error flags ------ */
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;   // clear flag
            
            // Send the NAK character to let the other end know that an error has occurred and a retransmission is needed
            // requires software !support! on the other end
            UART_SendChar(ASCII_NAK); 
            UART_Send("\r\n[NACK] Overflow Detected! Please resend last packet.\r\n");
            
            // Discard corrupted segments and reset pointer.
            rd_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
        }

        // --- Task 1: Efficient processing of Ring Buffer ---
        uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;


        // The outer layer uses a double-judgment threshold, and the inner layer uses dynamic catch-up logic.
        if (rx_idle_event || (rd_ptr != wr_ptr)) {
            
            // Only when there is actual data (pointer are not equal) will the buffer be read.
            if (rd_ptr != wr_ptr) {
                // [ hint : can put a header tag here, for example, UART_Send("Recv: ");]
                
                while (rd_ptr != wr_ptr) {
                    uint8_t data = rx_buffer[rd_ptr];
        
                    // Process data (Echo or store into the protocol parser)
                    while (!(USART1->ISR & (1UL << 7)));
                    USART1->TDR = data;
        
                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE) rd_ptr = 0;
                    
                    // Dynamically captures the latest written pointer within the loop, ensuring a single clear.
                    wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
                }
                UART_Send("\r\n");
            }
            
            // 無論是因為指標不相等還是因為 IDLE 事件進來的，處理完後都清空事件
            // All events should be cleared after processing, whether the issue from unequal pointer or from an IDLE event 
            rx_idle_event = 0; 
        }

        // --- Task 2: Background Task ---
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(&led_current_state);
            last_blink = get_tick();
        }

        // --- Simulation area: Add delay here ---
        // Trying Delay 2000ms (2 seconds) first.
        // Then paste a very long text (e.g., 2000 characters) from the computer at once
        My_Delay_ms(2000);
    }
}
```
## 四、除錯與驗證 (Troubleshooting)
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


