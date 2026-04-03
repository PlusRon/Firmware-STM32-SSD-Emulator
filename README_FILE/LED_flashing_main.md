# LED main() 主程式邏輯
```
#include "stm32f072xb.h"

/**
 * @brief simple delay function
 * Use __asm("nop") to prevent the compiler from optimizing away null-loop
 */
void delay(int32_t count) {
    while (count--) {
        __asm("nop");
    }
}

/**
 * @brief Entry of main
 */
int main(void) {
    /* 1. Open the GPIOC's Clock (IOPC EN is the 19-th bit in AHBENR) */
    RCC->AHBENR |= (1UL << 19);

    /* 2. Setting PC6 be the Output-Mode ( MODER6's 12, 13-th bit can control PC6's mode ) 
     * 00: Input, 01: Output, 10: Alternate, 11: Analog */
    GPIOC->MODER &= ~(3UL << 12); // Firstly, Clear [13:12]-bits to 00
    GPIOC->MODER |= (1UL << 12);  // Secondly, Set [13:12]-bits to 01 can be output-pin 

    while (1) {
        /* 3. use XOR operation to invert PC6 Voltage */
        GPIOC->ODR ^= (1UL << 6); 

        /* Flashing by the delay() */
        delay(100000); 
    }

    return 0;
}
```
## 一、ODR (Output Data Register) 的 讀取-修改-寫入 (Read-Modify-Write, RMW)
- #### Read-Modify-Write (RMW) 操作
  - 能同時控制多個外設（如 GPIOA, DMA）， `|=` 能確保只置位第 19 位而不干擾其他配置，直接賦值（=）會意外關閉其他已開啟的時鐘
  - `GPIOC_ODR ^= (1 << 6)`，使用位元運算子（Bitwise Operators）能精確操作特定腳位，避免覆蓋掉該 Port 其他 15 個腳位的狀態
- #### `delay()` 函式內部的 `__asm("nop")`
  - NOP (No Operation) 叫 CPU 原地踏步一個週期
  - 若沒有這行，編譯器可能會發現 `while(count--)` 什麼都沒做，進而將整個迴圈優化刪除，導致延遲失效
 
## 二、 BSRR (Bit Set Reset Register) 的 寫入即觸發 (Write-only to trigger)
- **原子操作 (Atomic Operation)** : 高頻中斷或多工環境下，改用 BSRR (Bit Set Reset Register)，可達成 **寫入即觸發 (Write-only to trigger)**
    - 低 16 位為 SET 操作 : **寫 1** 到 低 16 位，對應 **Pin 變高**
    - 低 16 位為 RESET 操作 : **寫 1** 到 高 16 位，對應 **Pin 變低**
    - 寫 0 則完全沒影響
- 像是一個 RS 鎖存器 (RS Latch)
  - 當 Set 線 收到脈衝 (寫入 1)，輸出鎖定在 1
  - 當 Reset 線 收到脈衝 (寫入 1)，輸出鎖定在 0
  - 如果兩條線都沒訊號 (寫入 0)，輸出就保持上一次的狀態
- BSRR 內部連接了兩條線到 PC6 的控制電路
  - 一條叫 **Set 線** (連接到 Bit 6)
  - 一條叫 **Reset 線** (連接到 Bit 22)
- 無內建的 **翻轉 (Toggle)** 位元
  - 需要配合一個軟體變數來記錄狀態
  - 或是讀取 ODR 的當前值來判斷
- 優先權機制
  - 根據手冊，如果同時在 BS6 與 BR6 寫入 1，**BS6 (Set) 具有優先權**（視型號而定），進一步定義了硬體在極端衝突下的預期行為
```
int main(void) {
    /* 1. 開啟時鐘與設定 PC6 為輸出 (與之前相同) */
    RCC->AHBENR |= (1UL << 19);
    GPIOC->MODER &= ~(3UL << 12);
    GPIOC->MODER |= (1UL << 12);

    uint8_t led_state = 0; // 軟體追蹤狀態

    while (1) {
        if (led_state == 0) {
            /* 使用 BSRR 置位 (Set) PC6 : 寫入第 6 位元 */
            GPIOC->BSRR = (1UL << 6); 
            led_state = 1;
        } else {
            /* 使用 BSRR 復位 (Reset) PC6 : 寫入第 6 + 16 = 22 位元 */
            GPIOC->BSRR = (1UL << 22); 
            led_state = 0;
        }

        delay(100000);
    }
}
```

