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
## 一、程式碼實作
- #### Read-Modify-Write (RMW) 操作
  - 能同時控制多個外設（如 GPIOA, DMA）， `|=` 能確保只置位第 19 位而不干擾其他配置，直接賦值（=）會意外關閉其他已開啟的時鐘
  - `GPIOC_ODR ^= (1 << 6)`，使用位元運算子（Bitwise Operators）能精確操作特定腳位，避免覆蓋掉該 Port 其他 15 個腳位的狀態
- #### `delay()` 函式內部的 `__asm("nop")`
  - NOP (No Operation) 叫 CPU 原地踏步一個週期
  - 若沒有這行，編譯器可能會發現 `while(count--)` 什麼都沒做，進而將整個迴圈優化刪除，導致延遲失效
