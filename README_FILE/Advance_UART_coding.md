# 非阻斷式非同步處理
建構一個 **非阻斷式(Non-blocking) 處理架構**，以解決 **阻塞式等待 (Polling)** 與 **頻繁中斷(Interrupt per Byte)** 兩個問題。透過 **DMA** 與 **Ring Buffer (循環緩衝區)** 的概念，實現 **通訊層** 與 **應用層** 的 **解耦**，讓 CPU 能在接收資料的同時，並行處理如 LED 閃爍、感測器運算等背景任務，挑戰平衡 通訊即時性 與 任務並行處理 兩任務。

## 一、理論、技術實作
### [1. UART 模組：理論(含通訊工具)、硬體設置、韌體實作、除錯驗證](UART_introduce.md)
### 2. DMA (Direct Memory Access)：零 CPU 介入的資料搬運
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

### 3. Ring Buffer (循環緩衝區) — 非同步生產者/消費者模型
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
### 4. IDLE Line Detection (空閒線路偵測)
- 理論：當 UART 線路維持一個 Byte 以上的高電平（無傳輸）時觸發。
- 關聯：這是「非固定長度封包」的最佳處理方案。它能主動通知 CPU：「一波資料傳輸已結束，可以開始解析了。」

### 5. 雙重流控機制 (Flow Control)
- 為了確保在高負載下的資料完整性，系統實施了兩層保護：
  - 硬體層 (RTS/CTS)：透過 PA12 (RTS) 腳位，由硬體自動控制發送端的節奏。當 Buffer 快滿時，硬體會物理性地讓對方停止傳送。
  - 軟體層 (ORE Detection & NACK)：若硬體流控失敗導致 Overrun Error (ORE)，系統會透過 USART1_IRQHandler 捕捉異常，並發送 0x15 (NAK) 訊號請求重傳，同時強制同步指標（Disaster Recovery）。
### 6. SysTick 時基系統
- 理論：內建於 Cortex-M 核心的遞減計數器。
- 關聯：提供精確的 ms 等級時間戳，用於實現非阻塞式的定時任務（如本專案中的 LED 定時翻轉）


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
|RCC|APB2ENR|UART周邊供電開關|Bit 14 (**USART1EN**)|`1`: 啟動 USART1 時脈，才能控制 UART1 周邊<br>(通訊電路開始運作)|
|GPIOA|MODER|引腳模式切換|Bits [19:18] (**PA9**)<br>Bits [21:20] (**PA10**)|`10`: 設定為 Alternate Function (複用模式)|
|GPIOA|AFRH|複用功能選擇|Bits [7:4] (**PA9**)<br>Bits [11:8] (**PA10**)|`0001`: 指定為 **AF0~AF7** 中的 **AF1** 模式 (硬體連通至 USART1)|
|USART1|BRR|波特率分頻器|Bits [15:0]|填入計算後的分頻值 : <br>`69`(適用HSI 8M Hz)<br>`417`(適用PLL 48M Hz)|
|USART1|CR1|UART1模組總控制|Bit 0 (**UE**)<br>Bit 3 (**TE**)<br>Bit 2 (**RE**)|Bit-0 = `1`: 啟動 UART1 模組總開關<br>Bit-3 = `1`: 啟動發送器 (Transmitter)<br>Bit-2 = `1`: 啟動接收器 (Receiver)|
|USART1|ISR|狀態**監控**(**唯讀**)|Bit 7 (**TXE**)<br>Bit 5 (**RXNE**)|Bit-7 = `1`: 發送區空 (**可寫資料**)<br>Bit-5 = `1`: 接收區有資料 (**可讀資料**)|
|USART1|TDR|**發送資料**暫存器|Bits [8:0]|寫入此處 ： 資料由 TX 腳位發出去|
|USART1|RDR|**接收資料**暫存器|Bits [8:0]|讀取此處 ： 由 RX 腳位接收資料|


### 程式碼 : UART + DMA(Ring Buffer) + ORE（Overrun Error）Detection + 硬體流控 (RTS/CTS) + 加大 Buffer (1024 bytes) + ACK/NACK 機制
```
#include "stm32f072xb.h"

/* --- 巨集定義 --- */
#define RX_BUF_SIZE 1024  // (2) 增加緩衝區至 1024，大幅降低 ORE 機率

/* ASCII 通訊字元定義 */
#define ASCII_NAK 0x15    // Negative Acknowledge (資料錯誤/重傳請求)

/* --- 全域變數 --- */
volatile uint32_t msTicks = 0;
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0;
volatile uint8_t rx_event = 0;
volatile uint8_t uart_overrun_occurred = 0; // 軟體錯誤標記

/* --- 系統計時 --- */
void SysTick_Handler(void) {
    msTicks++;
}

uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED 模組 --- */
void LED_Init(void) {
    RCC->AHBENR |= (1UL << 19);
    GPIOC->MODER &= ~(3UL << 12);
    GPIOC->MODER |=  (1UL << 12);
}

void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        GPIOC->BSRR = (1UL << 6);
        *state = 1;
    } else {
        GPIOC->BSRR = (1UL << 22);
        *state = 0;
    }
}

/* --- UART 與 DMA --- */
void USART1_IRQHandler(void) {
    // 檢查 IDLE
    if (USART1->ISR & (1UL << 4)) {
        USART1->ICR = (1UL << 4);
        rx_event = 1;
    }
    // 檢查 ORE (Overrun)
    if (USART1->ISR & (1UL << 3)) {
        USART1->ICR = (1UL << 3);
        // 此處不處理邏輯，交由 main 處理 NACK 流程
        uart_overrun_occurred = 1;      // (標記) 告訴 main 剛剛出事了
    }
}

void UART_Send(char *s) {
    while (*s) {
        while (!(USART1->ISR & (1UL << 7)));
        USART1->TDR = *s++;
    }
}

void UART_SendChar(uint8_t c) {
    while (!(USART1->ISR & (1UL << 7)));
    USART1->TDR = c;
}

void My_Delay_ms(uint32_t ms) {
    uint32_t start = get_tick();
    while ((get_tick() - start) < ms);
}

/* --- 系統初始化 --- */
void System_Init(void) {
    // 1. 時鐘開啟
    RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    RCC->APB2ENR |= (1UL << 14);

    LED_Init();

    // 2. GPIO 設定 (PA9=TX, PA10=RX, PA12=RTS)
    // (3) 硬體流控：加入 PA12 作為 RTS 腳位
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20) | (3UL << 24));
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20) | (2UL << 24)); // 皆設為 AF 模式
    
    // PA9, PA10, PA12 的 AF 都是 AF1 (USART1)
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8) | (0xFUL << 16));
    GPIOA->AFRH |=  ((1UL << 4) | (1UL << 8) | (1UL << 16));

    // 3. DMA 設定
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    // 4. UART 設定
    USART1->BRR = 69; // 115200 @ 8MHz
    
    // CR3 修改：加入 RTSE (Bit 8) 啟動硬體流控
    USART1->CR3 = (1UL << 6) | (1UL << 8); 
    
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);

    // 5. NVIC 與 SysTick
    *((volatile uint32_t *)0xE000E100) = (1UL << 27);
    SysTick->LOAD = 8000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 7;
}

/* --- 主程式 --- */
int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send("Industrial UART System Initialized (RTS/NACK Enabled)\r\n");

    while (1) {
        // --- (1) NACK 重傳機制偵測 之 核心保險機制：檢查軟體錯誤標記 ------
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;   // 清除標記
            
            // 發送 NAK 字元，讓對端知道發生錯誤需要重傳 (需對端軟體支援)
            UART_SendChar(ASCII_NAK); 
            UART_Send("\r\n[NACK] Overflow Detected! Please resend last packet.\r\n");
            
            // 拋棄損壞片段，重置指標
            rd_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
        }

        // --- 任務 1：Ring Buffer 高效處理 ---
        uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;


        // 完美整合版：外層用雙重判斷門檻，內層用動態追趕邏輯
        if (rx_event || (rd_ptr != wr_ptr)) {
            
            // 只有在真的有資料（指標不相等）時，才進去讀取 Buffer
            if (rd_ptr != wr_ptr) {
                // [理論提示：這裡可以放一個開頭標籤，例如 UART_Send("Recv: "); ]
                
                while (rd_ptr != wr_ptr) {
                    uint8_t data = rx_buffer[rd_ptr];
        
                    // 處理資料 (Echo 或存入協議解析器)
                    while (!(USART1->ISR & (1UL << 7)));
                    USART1->TDR = data;
        
                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE) rd_ptr = 0;
                    
                    // 核心優點：在迴圈內動態抓取最新寫入指標，確保一次清空
                    wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
                }
                UART_Send("\r\n");
            }
            
            // 無論是因為指標不相等還是因為 IDLE 事件進來的，處理完後都清空事件
            rx_event = 0; 
        }

        // --- 任務 2：背景任務 ---
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(&led_current_state);
            last_blink = get_tick();
        }

        // --- 模擬模擬區：在這裡加入延遲 ---
        // 建議先試試看 Delay 2000ms (2秒)
        // 然後從電腦端一次貼上一段非常長的文字 (例如 2000 個字元)
        My_Delay_ms(2000);
    }
}
```



### 程式碼 : UART + DMA(Ring Buffer) + ORE（Overrun Error）Detection
- 一般 Ring Buffer判斷，rd_ptr 和 wr_ptr 再次相等時，是如何分辨緩衝區是「空的」還是「被塞滿一圈」?
  - 常用的做法 : 緩衝區永遠不能真正裝滿，必須保留一個位置
    - 空的定義：rd_ptr == wr_ptr
    - 滿的定義：(wr_ptr + 1) % RX_BUF_SIZE == rd_ptr
    - 如果你宣告了 128 bytes，你實際上只能存 127 bytes
    - 這種「犧牲一個位元組」的設計，是針對 「**軟體主導寫入**」（例如 CPU 寫資料進 Buffer）時為了區分滿空而設計的規範。
  - 我的專案是 **「硬體主導寫入」（DMA 循環搬運）**，硬體是不會遵守這個規範的
    - 不需要去「實作」犧牲一個位元組
    - 只需要確保：**讀的速度 > 寫的速度**，並把 rd_ptr == wr_ptr 當作「空」即可
      - **寫入速度（硬體世界）** ：假設波特率是 115200。傳輸 1 個 Byte（含起始/停止位共 10 bits）大約需要 86 微秒 ($\mu s$)。也就是說，DMA 每隔 $86 \mu s$ 才會把 wr_ptr 往後移動一格
      - **讀取速度（軟體世界）**： 你的 STM32F0 跑在 48MHz（或 8MHz）。CPU 執行一行簡單的判斷（如 if(rd_ptr != wr_ptr)）只需要幾個時脈週期，大約是 0.1 微秒 左右
      - 當 CPU 檢查一次指標時，硬體連 1 個 bit 都還沒傳完。所以對 CPU 來說，它有充裕的時間「蹲點」在 wr_ptr 旁邊，只要硬體一寫入，軟體在不到 $1 \mu s$ 內就能發現並讀走。
      - **讀太快，會不會讀到一半的資料？**  不會，因為 DMA 的搬運是 「原子性」 的。DMA 只有在接收完一個完整的 Byte 並校驗無誤後，才會觸發一次寫入記憶體的動作，並同時更新 CNDTR（即更新 wr_ptr）。在 wr_ptr 增加之前，資料根本還沒進 Buffer。一旦 wr_ptr 增加了，資料就已經完整躺在記憶體裡了。
      - **爆發狀態（大量資料接收）** :
        - 如果你一口氣貼上 500 個字
        - DMA 瘋狂寫入。
        - 如果你的 main 迴圈被某個 Delay 卡住了 10ms
        - 當 main 回過神來，wr_ptr 可能已經跑到 115 了，而 rd_ptr 還在 0
        - 這時 while(rd_ptr != wr_ptr) 就會發揮作用，CPU 會用極快的速度連衝 115 次，把落後的進度一口氣補回來
      - **專業開發者的心態：預期最壞的情況**
        - 萬一 CPU 去處理更高級的中斷（例如馬達控制、感測器讀取）卡住了呢？
        - 萬一波特率提高到 1Mbps 呢？
        - **這就是為什麼我們要設計 Ring Buffer、ORE 保險 和 RTS 流控。**
          - Ring Buffer 是為了給 CPU 「彈性時間」去處理別的事。
          - ORE/RTS 是為了萬一 CPU 真的「彈性疲乏」時的最後防線
    - 真正的保險： 檢查 UART 的 **ORE (Overrun Error) 旗標**。如果 rd_ptr == wr_ptr 且 ORE 亮了，代表硬體已經跑太快，發生覆蓋且出錯了

```
#include "stm32f072xb.h"

/* --- 巨集定義 --- */
#define RX_BUF_SIZE 128

/* --- 全域變數 --- */
volatile uint32_t msTicks = 0;           // SysTick 計數
uint8_t rx_buffer[RX_BUF_SIZE];          // DMA Ring Buffer
uint16_t rd_ptr = 0;                     // 軟體讀取指標
volatile uint8_t rx_event = 0;           // 接收事件旗標

/* --- 系統與計時相關 --- */
void SysTick_Handler(void) {
    msTicks++;
}

uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED 模組化區段 --- */
void LED_Init(void) {
    RCC->AHBENR |= (1UL << 19);
    GPIOC->MODER &= ~(3UL << 12);
    GPIOC->MODER |=  (1UL << 12);
}

void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        GPIOC->BSRR = (1UL << 6);
        *state = 1;
    } else {
        GPIOC->BSRR = (1UL << 22);
        *state = 0;
    }
}

/* --- UART 與 DMA 處理區段 --- */

void USART1_IRQHandler(void) {
    // 檢查是否為 IDLE 中斷
    if (USART1->ISR & (1UL << 4)) {
        USART1->ICR = (1UL << 4);       // 清除 IDLE 旗標
        rx_event = 1;                   // 通知主迴圈
    }
    
    // 檢查是否有 Overrun Error
    if (USART1->ISR & (1UL << 3)) {
        USART1->ICR = (1UL << 3);       // 清除 ORE 旗標
        // 這裡可以選擇性地發送一個錯誤訊息
    }
}

void UART_Send(char *s) {
    while (*s) {
        while (!(USART1->ISR & (1UL << 7)));
        USART1->TDR = *s++;
    }
}

/* --- 系統初始化 --- */
void System_Init(void) {
    // 1. 時鐘
    RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    RCC->APB2ENR |= (1UL << 14);

    LED_Init();

    // 2. GPIO (PA9, PA10)
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20));
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFRH |=  ((1UL << 4) | (1UL << 8));

    // 3. DMA 設定 (CIRC 模式)
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    // 4. UART 設定
    USART1->BRR = 69; 
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);
    USART1->CR3 = (1UL << 6); 

    // 5. NVIC (USART1_IRQn = 27)
    *((volatile uint32_t *)0xE000E100) = (1UL << 27);

    // 6. SysTick
    SysTick->LOAD = 8000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 7;
}

/* --- 主程式執行 --- */
int main(void) {
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send("System Ready with ORE Insurance\r\n");

    while (1) {
        // --- 核心保險機制：檢查 ORE ---
        if (USART1->ISR & (1UL << 3)) {
            USART1->ICR = (1UL << 3); // 清除錯誤
            UART_Send("\r\n!! OVERRUN ERROR !!\r\n");
            // 災難復原 (Disaster Recovery) : 發生 ORE 時，通常建議強制將 rd_ptr 對齊 wr_ptr，拋棄舊資料重新同步
            rd_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
        }

        // --- 任務 1：Ring Buffer 高效處理 ---
        uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;

        if (rd_ptr != wr_ptr) {
            UART_Send("Receive: ");
            
            // 只要指標不相等，就拚命讀取（讀的速度 > 寫的速度）
            while (rd_ptr != wr_ptr) {
                uint8_t data = rx_buffer[rd_ptr];

                // Echo 輸出
                while (!(USART1->ISR & (1UL << 7)));
                USART1->TDR = data;

                // 指標遞增與繞回
                rd_ptr++;
                if (rd_ptr >= RX_BUF_SIZE) {
                    rd_ptr = 0;
                }
                
                // 讀取過程中重新抓取 wr_ptr，確保讀到最新位置
                wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
            }
            UART_Send("\r\n");
            rx_event = 0; // 清除 IDLE 事件
        }

        // --- 任務 2：其他背景任務 (LED 閃爍) ---
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(&led_current_state);
            last_blink = get_tick();
        }
    }
}
```


### 程式碼 : UART + DMA(Ring Buffer)
```
#include "stm32f072xb.h"

/* --- 巨集定義 --- */
#define RX_BUF_SIZE 128

/* --- 全域變數 --- */
volatile uint32_t msTicks = 0;           // SysTick 計數
uint8_t rx_buffer[RX_BUF_SIZE];          // DMA Ring Buffer
uint16_t rd_ptr = 0;                     // 軟體讀取指標 (Read Pointer)
volatile uint8_t rx_event = 0;           // 接收事件旗標 (由 IDLE 觸發)

/* --- 系統與計時相關 --- */

void SysTick_Handler(void) {
    msTicks++;
}

/**
 * @brief 取得系統啟動後的毫秒數
 */
uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED 模組化區段 --- */

void LED_Init(void) {
    /* 開啟 GPIOC 時鐘 */
    RCC->AHBENR |= (1UL << 19);
    
    /* 設定 PC6 為輸出模式 */
    GPIOC->MODER &= ~(3UL << 12);
    GPIOC->MODER |=  (1UL << 12);
}

void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        GPIOC->BSRR = (1UL << 6);
        *state = 1;
    } else {
        GPIOC->BSRR = (1UL << 22);
        *state = 0;
    }
}

/* --- UART 與 DMA 處理區段 --- */

/**
 * @brief USART1 中斷處理：捕捉 IDLE Line
 * 在 Ring Buffer 模式下，我們不關閉 DMA，只標記有資料進來。
 */
void USART1_IRQHandler(void) {
    if (USART1->ISR & (1UL << 4)) {
        USART1->ICR = (1UL << 4);       // 清除 IDLE 旗標
        rx_event = 1;                   // 通知主迴圈處理資料
    }
}

void UART_Send(char *s) {
    while (*s) {
        while (!(USART1->ISR & (1UL << 7)));
        USART1->TDR = *s++;
    }
}

/* --- 系統初始化整合 --- */

void System_Init(void) {
    /* 1. 時鐘開啟 */
    RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    RCC->APB2ENR |= (1UL << 14);

    /* 2. LED 初始化 */
    LED_Init();

    /* 3. UART GPIO 設定 (PA9, PA10) */
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20));
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA->AFRH |=  ((1UL << 4) | (1UL << 8));

    /* 4. DMA 設定 (關鍵修改：開啟 CIRC 循環模式) */
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    /* Bit 7: MINC (記憶體遞增)
       Bit 5: CIRC (循環模式 - Ring Buffer 核心)
       Bit 0: EN (開啟)
    */
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    /* 5. UART 參數設定 */
    USART1->BRR = 69; 
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4); // UE, TE, RE, IDLEIE
    USART1->CR3 = (1UL << 6); // DMAR (使能 DMA 請求)

    /* 6. NVIC 開啟 USART1 中斷 */
    *NVIC_ISER = (1UL << 27);

    /* 7. SysTick 設定 (1ms) */
    SysTick->LOAD = 8000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = 7;
}

/* --- 主程式執行 --- */

int main(void) {
    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send("Advanced DMA Ring-Buffer System Initialized\r\n");

    while (1) {
        /* 任務 1：非阻塞 LED 閃爍 */
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(&led_current_state);
            last_blink = get_tick();
        }

        /* 任務 2：Ring Buffer 接收處理 */
        // 當偵測到 IDLE (一串資料傳完) 或 緩衝區有新資料時進入
        uint16_t wr_ptr = RX_BUF_SIZE - DMA1->CH[2].CNDTR; // 獲取硬體當前寫入位置

        if (rx_event || (rd_ptr != wr_ptr)) {
            if (rd_ptr != wr_ptr) {
                UART_Send("Recv: ");
                
                // 從讀取指標 (rd_ptr) 一直讀到 寫入指標 (wr_ptr)
                while (rd_ptr != wr_ptr) {
                    // 等待 UART 傳送暫存器空
                    while (!(USART1->ISR & (1UL << 7)));
                    USART1->TDR = rx_buffer[rd_ptr];

                    // 移動讀取指標，並在到達緩衝區末尾時自動繞回
                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE) {
                        rd_ptr = 0;
                    }
                }
                UART_Send("\r\n");
            }
            rx_event = 0; // 重置事件標記
        }
    }
}
```

### 程式碼 : UART + DMA(Fixed Buffer)
```
#include "stm32f072xb.h"

/* --- 巨集定義 --- */
#define RX_BUF_SIZE 128

/* --- 全域變數 --- */
volatile uint32_t msTicks = 0;           // SysTick 計數
uint8_t rx_buffer[RX_BUF_SIZE];          // DMA 接收緩衝區
volatile uint16_t rx_data_len = 0;       // 接收到的資料長度
volatile uint8_t rx_ready = 0;           // 接收完成旗標

/* --- 系統與計時相關 --- */

void SysTick_Handler(void) {
    msTicks++;
}

/**
 * @brief 取得系統啟動後的毫秒數
 */
uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED 模組化區段 --- */

void LED_Init(void) {
    /* 開啟 GPIOC 時鐘 */
    RCC->AHBENR |= (1UL << 19);
    
    /* 設定 PC6 為輸出模式 (MODER6[1:0] = 01) */
    GPIOC->MODER &= ~(3UL << 12);
    GPIOC->MODER |=  (1UL << 12);
}

/**
 * @brief 翻轉 LED 狀態 (非阻塞切換)
 */
void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        GPIOC->BSRR = (1UL << 6);  // Set
        *state = 1;
    } else {
        GPIOC->BSRR = (1UL << 22); // Reset
        *state = 0;
    }
}

/* --- UART 與 DMA 處理區段 --- */

/**
 * @brief USART1 中斷處理：捕捉 IDLE Line (空閒中斷)
 */
void USART1_IRQHandler(void) {
    /* 檢查是否為 IDLE 中斷觸發 */
    if (USART1->ISR & (1UL << 4)) {
        USART1->ICR = (1UL << 4);       // 1. 清除中斷旗標
        
        // 2. 暫時關閉 DMA 通道以計算長度
        DMA1->CH[2].CCR &= ~(1UL << 0); 
        
        // 3. 計算實際收到的 Byte 數 (總長 - 剩餘未搬運量)
        rx_data_len = RX_BUF_SIZE - DMA1->CH[2].CNDTR;
        rx_ready = 1;                   // 標記非阻塞任務可處理
        
        // 4. 重置 DMA 計數器並重新啟動，準備接收下一包
        DMA1->CH[2].CNDTR = RX_BUF_SIZE;
        DMA1->CH[2].CCR |= (1UL << 0);
    }
}

/**
 * @brief UART 字串發送函式
 */
void UART_Send(char *s) {
    while (*s) {
        while (!(USART1->ISR & (1UL << 7))); // 等待傳送暫存器空 (TXE)
        USART1->TDR = *s++;
    }
}

/* --- 系統初始化整合 --- */

void System_Init(void) {
    /* 1. 開啟所有必要時鐘 (GPIOA, GPIOC, DMA1, USART1) */
    RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    RCC->APB2ENR |= (1UL << 14);

    /* 2. LED 腳位初始化 (PC6) */
    LED_Init();

    /* 3. UART GPIO 設定 (PA9=TX, PA10=RX, 使用 AF1) */
    GPIOA->MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA->MODER |=  ((2UL << 18) | (2UL << 20));
    /* --- 修正處 --- */
    // 因為 PA9 與 PA10 屬於 Pin 8~15，所以對應 AFRH (Alternate Function High register)
    // 每個 Pin 佔用 4 bits，PA9 是第 4~7 位，PA10 是第 8~11 位
    // 寫入 1UL (AF1) 代表選擇 USART1 功能
    GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8)); // 先清空對應位元
    GPIOA->AFRH |=  ((1UL << 4) | (1UL << 8));    // 設定為 AF1 (USART1)

    /* 4. DMA 設定 (USART1_RX 固定對應 DMA1 Channel 3) */
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR); // 來源位址
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;      // 目標位址
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;             // 設定搬運總量
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 0);   // MINC=1, EN=1

    /* 5. UART 參數設定 (9600 Baud @ 8MHz) */
    USART1->BRR = 69; 
    // CR1: 開啟 UE, TE, RE 並啟動 IDLEIE (空閒中斷)
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);
    // CR3: 開啟 DMAR (使能 UART 請求 DMA)
    USART1->CR3 = (1UL << 6);

    /* 6. NVIC 設定：開啟 USART1 中斷 (IRQ 27) */
    // 注意：*NVIC_ISER 指向的是中斷使能暫存器位址
    // *((volatile uint32_t *)0xE000E100UL) = (1UL << 27);
    *NVIC_ISER = (1UL << 27);

    /* 7. SysTick 設定 (1ms @ 8MHz) */
    SysTick->LOAD = 8000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = (1UL << 2) | (1UL << 1) | (1UL << 0);
}

/* --- 主程式執行 --- */

int main(void) {
    /* 初始化系統硬體與中斷 */
    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send("Bare-Metal DMA+IDLE System Ready!\r\n");

    while (1) {
        /* --- 任務 1：非阻塞 LED 閃爍 (500ms) --- */
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(&led_current_state);
            last_blink = get_tick();
        }

        /* --- 任務 2：非阻塞 UART 接收處理 --- */
        if (rx_ready) {
            UART_Send("Data Received: ");
            // 迴圈將 DMA 緩衝區中的資料送出
            for (uint16_t i = 0; i < rx_data_len; i++) {
                while (!(USART1->ISR & (1UL << 7)));
                USART1->TDR = rx_buffer[i];
            }
            UART_Send("\r\n");
            
            rx_ready = 0; // 處理完畢，恢復旗標
        }

        /* 這裡可以繼續添加其他非阻塞任務，例如偵測按鈕狀態 */
    }
}
```
