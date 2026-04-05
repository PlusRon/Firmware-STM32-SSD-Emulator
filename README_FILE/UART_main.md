# UART_main() 主程式邏輯





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
