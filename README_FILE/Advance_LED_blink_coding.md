# GPIO 原子性操作 (BSRR) 與 SysTick 非阻塞時基系統

## 一、 BSRR (Bit Set Reset Register) - 確保硬體操作的原子性
**原子操作 (Atomic Operation)** ： 高頻中斷或多工環境下，改用 BSRR (Bit Set Reset Register)
  - **寫入即觸發 (Write-only to trigger)**
    - 低 16 位為 SET 操作 ： **寫 1** 到 低 16 位，對應 **Pin 變高** (RS-Latch 的 S = 1)
    - 低 16 位為 RESET 操作 ： **寫 1** 到 高 16 位，對應 **Pin 變低** (RS-Latch 的 R = 1)
    - 寫 0 則完全沒影響 (RS-Latch 的 S=0 且 R = 0)
- **RS 鎖存器 (RS Latch)** 的概念
  - 當 Set 線 收到脈衝 (寫入 1)，輸出鎖定在 1
  - 當 Reset 線 收到脈衝 (寫入 1)，輸出鎖定在 0
  - 如果兩條線都沒訊號 (寫入 0)，輸出就保持上一次的狀態
- BSRR 內部連接了兩條線到 PC6 的控制電路
  - 一條叫 **Set 線** (連接到 BSRR 的 Bit 6)
  - 一條叫 **Reset 線** (連接到 BSRR 的 Bit 22)
- 無內建的 **翻轉 (Toggle)** 位元
  - 需要配合一個軟體變數來記錄狀態
  - 或是讀取 ODR 的當前值來判斷
- 優先權機制
  - 根據手冊，如果同時在 **BS6** 與 **BR6** 寫入 `1`，**BS6 (Set) 具有優先權**（視型號而定），進一步定義了硬體在極端衝突下的預期行為
```
* 非原子性 (Non-atomic)
  - |= 或 &=  ： 在處理器內部會分解成 「讀取-修改-寫回」 三個步驟
  - 如果在「讀取」後、「寫回」前發生了中斷，且中斷裡也修改了同一個連接埠的其他腳位，那麼中斷所做的修改會被主程式寫回的舊值蓋掉，導致狀態錯誤
```
### 程式碼
```
#include "stm32f072xb.h"

/* --- 全域變數與計時器相關 --- */
volatile uint32_t msTicks = 0; 

void SysTick_Handler(void) {
    msTicks++;
}

void SysTick_Init(uint32_t ticks) {
    SysTick->LOAD = (uint32_t)(ticks - 1UL);
    SysTick->VAL = 0UL;
    SysTick->CTRL = (1UL << 2) | (1UL << 1) | (1UL << 0);
}

uint32_t get_tick(void) {
    return msTicks;
}

/* --- LED 模組化區段 --- */

/**
 * @brief 初始化 LED 相關硬體 (PC6)
 */
void LED_Init(void) {
    /* 1. 開啟 GPIOC 時鐘 (Bit 19) */
    RCC->AHBENR |= (1UL << 19);
    
    /* 2. 設定 PC6 為輸出模式 (MODER6[1:0] = 01) */
    GPIOC->MODER &= ~(3UL << 12); // 先清零第 12, 13 位元
    GPIOC->MODER |=  (1UL << 12); // 設定為 01 (General purpose output mode)
}

/**
 * @brief 翻轉 LED 狀態 (使用 BSRR 確保原子性操作)
 * @param state 指向目前 LED 狀態變數的指標
 */
void LED_Toggle(uint8_t *state) {
    if (*state == 0) {
        /* Set PC6 (High) -> 寫入 BSRR 第 6 位元 */
        GPIOC->BSRR = (1UL << 6);
        *state = 1;
    } else {
        /* Reset PC6 (Low) -> 寫入 BSRR 第 22 位元 (6 + 16) */
        GPIOC->BSRR = (1UL << 22);
        *state = 0;
    }
}

/* --- 主程式 --- */

int main(void) {
    /* 初始化階段 */
    LED_Init();
    SysTick_Init(8000); // 1ms @ 8MHz

    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    while (1) {
        /* 非阻塞判斷：每 500ms 執行一次 */
        if ((get_tick() - last_blink) >= 500) {
            
            // 使用模組化函式翻轉 LED
            LED_Toggle(&led_current_state);
            
            last_blink = get_tick();
        }

        /* 這裡可以放其他的非阻塞任務，例如：
           UART_Check_Status(); 
           DMA_Transfer_Monitor();
        */
    }
}
```



