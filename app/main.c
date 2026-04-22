#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"
#include "protocol.h"

#define RX_BUF_SIZE 1024
uint8_t rx_buffer[RX_BUF_SIZE];
uint16_t rd_ptr = 0;

int main(void) {
    // 系統初始化 (維持原有邏輯)
    RCC->APB2ENR |= (1UL << 14); 
    RCC->AHBENR  |= (1UL << 0);  
    GPIO_Init_Output(GPIOC, 6);
    GPIO_Init_AF(GPIOA, 9, 1);
    GPIO_Init_AF(GPIOA, 10, 1);
    USART1->ICR |= 0xFFFFFFFF;
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
    UART_Init(USART1, 69); 
    *NVIC_ISER = (1UL << 27);
    SysTick_Init(8000);

    uint32_t last_blink = 0;
    uint8_t led_state = 0;

    UART_Send(USART1, "\r\n--- Diagnostics Mode Active ---\r\n");

    while (1) {
        // --- 錯誤處理：硬體溢位 ---
        if (uart_overrun_occurred) {
            uart_overrun_occurred = 0;
            UART_Send(USART1, "[SYS] ORE_ERROR (Hardware Buffer Full)\r\n");
            // 重置傳輸鏈
            DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);
            rd_ptr = 0;
        }

        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        if (rd_ptr != wr_ptr) {
            uint16_t available = (wr_ptr >= rd_ptr) ? (wr_ptr - rd_ptr) : (RX_BUF_SIZE - rd_ptr + wr_ptr);

            while (available >= PKT_SIZE) {
                if (rx_buffer[rd_ptr] == CMD_START_BYTE) {
                    Protocol_Parse(&rx_buffer[rd_ptr]);
                    rd_ptr = (rd_ptr + PKT_SIZE) % RX_BUF_SIZE;
                    available -= PKT_SIZE;
                } else {
                    // 如果發現標頭不是 0xA5，視為無效數據並跳過
                    rd_ptr = (rd_ptr + 1) % RX_BUF_SIZE;
                    available--;
                }
            }
        }

        if ((get_tick() - last_blink) >= 500) {
            LED_Toggle(GPIOC, 6, &led_state);
            last_blink = get_tick();
        }
    }
}
