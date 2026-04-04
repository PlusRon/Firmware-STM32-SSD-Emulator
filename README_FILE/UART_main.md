# UART_main() 主程式邏輯
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
