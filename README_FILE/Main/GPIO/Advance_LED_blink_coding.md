# GPIO 原子性操作 (BSRR) 與 SysTick 非阻塞時基系統
採用 **BSRR 暫存器** 實現 GPIO 的 **原子性操作**，藉由硬體 **RS 鎖存機制 消除 讀取-修改-寫回** 造成的 **Race Condition** 風險，確保高頻中斷環境下的位準正確性。同時結合 **SysTick 核心計時器** 建立毫秒級全域時基，透過 **無符號整數減法規避溢位問題**，實現精確且 **非阻塞** 的 **虛擬多工任務調度**

<img src="/images/GPIO_BSRR_SysTick.png" alt="GPIO-BSRR with SysToick" style="width:100%">

## 一、 BSRR (Bit Set Reset Register) - 確保硬體操作的原子性
#### 原子操作 (Atomic Operation)
- 高頻中斷或多工環境下，改用 BSRR (Bit Set Reset Register)
#### **寫入即觸發 (Write-only to trigger)**
- 低 16 位為 SET 操作 ： **寫 1** 到 低 16 位，對應 **Pin 變高** (RS-Latch 的 S = 1)
- 低 16 位為 RESET 操作 ： **寫 1** 到 高 16 位，對應 **Pin 變低** (RS-Latch 的 R = 1)
- 寫 0 則完全沒影響 (RS-Latch 的 S=0 且 R = 0)

  ```
  void LED_Toggle(uint8_t *state) {
      if (*state == 0) {
          /* Set PC6 (High) -> 寫入 BSRR 第 6 位元 */
          GPIOC->BSRR = (1UL << 6);  // 只有第 6 位元被改動，其他腳位保持原樣
          *state = 1;
      } else {
          /* Reset PC6 (Low) -> 寫入 BSRR 第 22 位元 (6 + 16) */
          GPIOC->BSRR = (1UL << 22); // BSRR 的高 16 位元負責 Reset 功能
          *state = 0;
      }
  }
  ```
#### **RS 鎖存器 (RS Latch)** 的概念
- 當 Set 線 收到脈衝 (寫入 1)，輸出鎖定在 1
- 當 Reset 線 收到脈衝 (寫入 1)，輸出鎖定在 0
- 如果兩條線都沒訊號 (寫入 0)，輸出就保持上一次的狀態
#### BSRR 內部連接了兩條線到 PC6 的控制電路
- 一條叫 **Set 線** (連接到 BSRR 的 Bit 6)
- 一條叫 **Reset 線** (連接到 BSRR 的 Bit 22)
#### 無內建的 **翻轉 (Toggle)** 位元
- 需要配合一個軟體變數來記錄狀態
- 或是讀取 ODR 的當前值來判斷
#### 優先權機制
- 根據手冊，如果同時在 **BS6** 與 **BR6** 寫入 `1`，**BS6 (Set) 具有優先權**（視型號而定），進一步定義了硬體在極端衝突下的預期行為
#### ODR 非原子性 (Non-atomic)
- `|=` 或 `&=`  ： 在處理器內部會分解成 **讀取-修改-寫回** 三個步驟
- 如果在 **讀取後、寫回前** 發生了中斷，且中斷裡也修改了同一個連接埠的其他腳位，那麼中斷所做的修改會被後續主程式寫回的舊值蓋掉，導致狀態錯誤


## 二、 SysTick 時基系統 — 從 Handler 到 Task 調度
SysTick (System Tick Timer) 能在 **不使用即時作業系統(RTOS)** 的情況下，依然能精確 **管理多個併行任務**。`SysTick` 是內建於 ARM Cortex-M 核心內的一個 **24 位元遞減計數器**，是為了提供一個穩定的 **心跳 (Heartbeat)**，讓系統擁有統一的 ms 級 時間戳記，實現非阻塞式的定時任務


#### 硬體初始化與頻率換算

  ```
  SysTick->LOAD = 8000 - 1; // 設定自動重載值
  SysTick->VAL = 0;        // 清空當前計數值
  SysTick->CTRL = 7;       // 啟動：內部時鐘(4), 中斷開啟(2), 致能(1)
  ```
  - 由於 STM32F0 預設運行於 **8MHz**，設定 `LOAD = 8000 - 1` 代表計數器**每數 8000 次(Tick) 會觸發一次中斷**
  - $8000 \div 8,000,000 \text{ Hz} = 0.001 \text{ 秒} = 1 \text{ ms}$，確保系統心跳頻率精確維持在 1000Hz (1000 個 Tick)
  - 當 `VAL = 0` 會重新載入 `8000 - 1` 繼續下數
  - 當 從 1 數到 0 的負緣 會觸發中斷自增
#### 中斷自增系統心跳 (SysTick_Handler) 與 原子性考量

  ```
  volatile uint32_t msTicks = 0; // 必須使用 volatile 防止編譯器優化

  void SysTick_Handler(void) {
      msTicks++; // 每 1ms 遞增一次
  }
  ```
  - `msTicks` 記錄了自系統啟動以來經過的總毫秒數
  - 使用 `volatile` 關鍵字是為了確保 main 迴圈每次讀取該變數時，都是直接從記憶體抓取最新的數值，而不會將該變數優化到 CPU 暫存器中
  - 而不是讀取 CPU 快取暫存器中的舊值。
#### 抽象化介面 (get_tick)

  ```
  uint32_t get_tick(void) {
      return msTicks;
  }
  ```
  - 不直接存取 `msTicks` 變數，而是透過 `get_tick()` 函式獲取
  - 封裝 便於未來將計時器更換為 `Timer 2` 或其他時鐘源，而不需要改動主程式邏輯

#### 非阻塞式任務調度設計 (Non-blocking Task)

  ```
  // 任務 2：背景任務 (LED 閃爍)
  if ((get_tick() - last_blink) >= 500) {
      LED_Toggle(&led_current_state);
      last_blink = get_tick(); // 更新上次執行的時間點
  }
  ```
  - **時間差判斷法** ： 不會像 `delay()` 一樣卡死 CPU，如果時間還沒到(不到 `500ms`)，CPU 會直接跳過這段程式碼去執行 UART 接收邏輯
  - 是實現 **虛擬併行處理** 的關鍵
  - **溢位處理 (Overflow Resilience)** ： 使用 `(現在時間 - 上次執行時間) >= 間隔`
    - 即使在 `msTicks` 達到 `0xFFFFFFFF` 並翻轉回 0 時
    - 利用 **無符號整數（unsigned int）的減法特性**，運算結果依然會是正確的正值時間差
    - 確保系統能連續運行好幾天而不會死機

## 三、程式碼
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



