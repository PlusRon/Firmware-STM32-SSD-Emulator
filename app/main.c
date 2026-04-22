#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"

/* --- MACRO define --- */
#define RX_BUF_SIZE 1024 // Increasing the buffer size to 1024, significantly reduces the probability of ORE

/* --- global variable --- */

uint8_t rx_buffer[RX_BUF_SIZE]; // buffer assign from RAM
uint16_t rd_ptr = 0;            // CPU(consumer) read pointer (software)

/* --- System initialization --- */
void System_Init(void)
{
    // 1. clock on
    // RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    // RCC->APB2ENR |= (1UL << 14);

    // 1. 核心時鐘與 UART/DMA 初始化
    RCC->APB2ENR |= (1UL << 14); // USART1 Clock
    RCC->AHBENR |= (1UL << 0);   // DMA1 Clock

    // LED_Init();
    GPIO_Init_Output(GPIOC, 6);

    // 2. 彈性配置 UART 引腳 (PA9, PA10, PA12 使用 AF1)
    GPIO_Init_AF(GPIOA, 9, 1);
    GPIO_Init_AF(GPIOA, 10, 1);
    GPIO_Init_AF(GPIOA, 12, 1);

    /* 3. DMA 配置 (採用你想要的 DMAx->CH[i] 架構)
       USART1_RX 固定在 DMA1 的 Channel 3，對應索引為 2 */
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);

    /* 4. UART 彈性初始化 */
    UART_Init(USART1, 69); // 115200 Baud Rate @ 8MHz

    // 8. NVIC and SysTick
    *NVIC_ISER = (1UL << 27);

    SysTick_Init(8000);
}

/* --- Main --- */
int main(void)
{
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send(USART1, "Industrial UART System Initialized (RTS/NACK Enabled)\r\n");

    while (1)
    {
        /* --- Core safeguard mechanism for NACK retransmission detection: checking software error flags ------ */
        if (uart_overrun_occurred)
        {
            uart_overrun_occurred = 0; // clear flag

            // Send the NAK character to let the other end know that an error has occurred and a retransmission is needed
            // requires software !support! on the other end
            UART_SendChar(USART1, ASCII_NAK);
            UART_Send(USART1, "\r\n[NACK] Overflow Detected! Please resend last packet.\r\n");

            // 同步讀取指標到最新的寫入點，捨棄損壞資料
            // Discard corrupted segments and reset pointer.
            rd_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);
            // rx_idle_event = 0; // 同步清除 IDLE 標記
            // continue;          // 重新開始下一輪循環
        }

        // 使用封裝函式獲取寫入指標
        // --- Task 1: Efficient processing of Ring Buffer ---
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        // The outer layer uses a double-judgment threshold, and the inner layer uses dynamic catch-up logic.
        if (rx_idle_event || (rd_ptr != wr_ptr))
        {

            // Only when there is actual data (pointer are not equal) will the buffer be read.
            if (rd_ptr != wr_ptr)
            {
                // [ hint : can put a header tag here, for example, UART_Send("Recv: ");]

                while (rd_ptr != wr_ptr)
                {
                    uint8_t data = rx_buffer[rd_ptr];

                    // Process data (Echo or store into the protocol parser)
                    // while (!(USART1->ISR & (1UL << 7)))
                    //     ;
                    // USART1->TDR = data;
                    UART_SendChar(USART1, data);

                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE)
                        rd_ptr = 0;

                    // 內圈動態追蹤最新的 wr_ptr
                    // Dynamically captures the latest written pointer within the loop, ensuring a single clear.
                    wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);
                }
                UART_Send(USART1, "\r\n");
            }

            // 無論是因為指標不相等還是因為 IDLE 事件進來的，處理完後都清空事件
            // All events should be cleared after processing, whether the issue from unequal pointer or from an IDLE event
            rx_idle_event = 0;
        }

        // --- Task 2: Background Task ---
        if ((get_tick() - last_blink) >= 500)
        {
            // LED_Toggle(&led_current_state);
            LED_Toggle(GPIOC, 6, &led_current_state);
            last_blink = get_tick();
        }

        // --- Simulation area: Add delay here ---
        // Trying Delay 2000ms (2 seconds) first.
        // Then paste a very long text (e.g., 2000 characters) from the computer at once
        // My_Delay_ms(2000);
    }
}
