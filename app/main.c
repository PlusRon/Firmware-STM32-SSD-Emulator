#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"

#define RX_BUF_SIZE 1024
uint8_t rx_buffer[RX_BUF_SIZE];  // 定義 1KB 的接收緩衝區
uint16_t rd_ptr = 0;             // 軟體讀取指標 (Software Read Pointer)

void System_Init(void){
    /* 硬體底層初始化節點 */
    RCC->APB2ENR |= (1UL << 14);   // 開啟 USART1 時鐘
    RCC->AHBENR  |= (1UL << 0);    // 開啟 DMA1 時鐘
    GPIO_Init_Output(GPIOC, 6);    // 初始化 LED
    GPIO_Init_AF(GPIOA, 9, 1);     // UART1 TX 腳位設定
    GPIO_Init_AF(GPIOA, 10, 1);    // UART1 RX 腳位設定

    // 清除 USART1 所有狀態旗標，防止啟動時的垃圾雜訊導致錯誤
    USART1->ICR |= 0xFFFFFFFF;

    // 初始化 DMA：將 UART->RDR 直接映射到 rx_buffer，循環搬運
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69);     // 設定波特率 115200
    *NVIC_ISER = (1UL << 27);  // 開啟中斷向量表中的 UART1 中斷
    SysTick_Init(8000);        // 1ms 系統滴答
}


int main(void) {

    System_Init();

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- NVMe diagnostics Mode ---\r\n");

    while (1) {
        // --- 錯誤處理：硬體溢位 (ORE) ---
        // 當 CPU 處理太慢導致 UART 接收器被塞爆時，觸發此邏輯
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_Send(USART1, "[SYS] ORE_ERROR (Hardware Buffer Full)\r\n");
            // 重置 DMA 傳輸鏈，清空緩衝區重新同步
            DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
            rd_ptr = 0;
        }

        // 獲取目前的 DMA 寫入位置 (Hardware Write Pointer)
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        // 如果讀取與寫入指標不相等，代表有新資料進入
        if (rd_ptr != wr_ptr) {
            // 計算目前緩衝區內有多少剩餘資料
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            // 只有當資料量大於等於一個完整封包 (7 bytes) 才開始解析
            while (available >= PKT_SIZE) {
                // 同步機制：確認第一個 Byte 必須是 0xA5
                if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                    Protocol_Parse(&rx_buffer[rd_ptr]);         // 進入協定解析
                    rd_ptr = (rd_ptr + PKT_SIZE) % RX_BUF_SIZE; // 指標後移
                    available -= PKT_SIZE;
                } else {
                    // 如果不是 0xA5，視為無效數據並跳過，代表資料流位移，逐位元尋找標頭
                    rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE;
                    available--;
                }
            }
        }

        // 背景心跳燈：確認系統沒有死機 (Non-blocking Blink)
        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(GPIOC, 6, &led_state);
            last_blink = get_tick();
        }
    }
}
