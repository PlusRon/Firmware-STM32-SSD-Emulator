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
