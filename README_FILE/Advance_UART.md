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
    USART1->BRR = 833; 
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
