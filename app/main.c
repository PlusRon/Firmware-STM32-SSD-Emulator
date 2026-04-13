#include "stm32f072xb.h"
#include "include/gpio.h"

/* --- MACRO define --- */
#define RX_BUF_SIZE 1024 // Increasing the buffer size to 1024, significantly reduces the probability of ORE

/* ASCII communication character definitions */
#define ASCII_NAK 0x15 // Negative Acknowledge (Data Error / Re-transmission Request)

/* --- global variable --- */
volatile uint32_t msTicks = 0;              // 8000 clock as a 1ms(1 Tick)
uint8_t rx_buffer[RX_BUF_SIZE];             // buffer assign from RAM
uint16_t rd_ptr = 0;                        // CPU(consumer) read pointer (software)
volatile uint8_t rx_idle_event = 0;         // IDLE event
volatile uint8_t uart_overrun_occurred = 0; // software ORE flag

/* --- system timing ISR--- */
void SysTick_Handler(void)
{
    msTicks++;
}

uint32_t get_tick(void)
{
    return msTicks;
}

/* --- LED module --- */
// void LED_Init(void)
// {
//     RCC->AHBENR |= (1UL << 19);   // power on GIPOC
//     GPIOC->MODER &= ~(3UL << 12); // PC6 clear Mode
//     GPIOC->MODER |= (1UL << 12);  // PC6 set Mode as GPO
// }

/* Bit Set Reset Register*/
// void LED_Toggle(uint8_t *state)
// {
//     if (*state == 0)
//     {
//         GPIOC->BSRR = (1UL << 6); // SET PC6
//         *state = 1;
//     }
//     else
//     {
//         GPIOC->BSRR = (1UL << 22); // RESET PC6
//         *state = 0;
//     }
// }

// void SysTick_Init(void)
// {
//     SysTick->LOAD = 8000 - 1;
//     SysTick->VAL = 0;
//     SysTick->CTRL = 7;
// }

/**
 * @brief  初始化 SysTick 計時器
 * @param  ticks: 觸發中斷所需的計數次數 (例如 8MHz 下 1ms 需要 8000 ticks)
 * @retval None
 */
void SysTick_Init(uint32_t ticks)
{
    /* 1. 設定重載值 (LOAD)
       由於計數器數到 0 才會觸發，所以實際次數要減 1 */
    SysTick->LOAD = (uint32_t)(ticks - 1UL);

    /* 2. 清空當前計數值 (VAL)
       寫入任何值都會將其歸零，並清除計數標誌 */
    SysTick->VAL = 0UL;

    /* 3. 設定控制暫存器 (CTRL)
       Bit 2: CLKSOURCE = 1 (使用處理器核心時鐘)
       Bit 1: TICKINT   = 1 (開啟 SysTick 中斷)
       Bit 0: ENABLE    = 1 (啟動計時器)
       7 的二進制是 111，剛好開啟上述三個功能 */
    SysTick->CTRL = (1UL << 2) | (1UL << 1) | (1UL << 0);
}

/* --- UART and DMA --- */
void USART1_IRQHandler(void)
{
    // check IDLE
    if (USART1->ISR & (1UL << 4))
    {                             // Bit-4 : IDLE occur when IDLE-state is true
        USART1->ICR = (1UL << 4); // Bit-4 : IDLE-state be cleared
        rx_idle_event = 1;        // IDLE event will be trigger
    }

    // check ORE (Overrun)
    if (USART1->ISR & (1UL << 3))
    {                             // Bit-3 : ORE occur when ORE-state is true
        USART1->ICR = (1UL << 3); // Bit-3 : ORE-state be cleared

        // No logic is handled here; the NACK process is delegated to the main function.
        uart_overrun_occurred = 1; // (Mark) to tell main() that something just happened (ORE)
    }
}

void UART_Send(USART_TypeDef *USARTx, char *s)
{
    while (*s)
    {
        // while (!(USART1->ISR & (1UL << 7)))
        //     ;               // Bit-7 : stuck here when TxE-state is false (previous data hasn't been fully transmitted yet)
        // USART1->TDR = *s++; // Bit-7 : transmitted the next data when TxE-state is true
        UART_SendChar(USARTx, (uint8_t)(*s++));
    }
}

void UART_SendChar(USART_TypeDef *USARTx, uint8_t c)
{
    /* 檢查 ISR 暫存器的第 7 位元 (TXE: Transmit data register empty)
       當 TXE 為 1 時，代表 TDR 已經空了，可以寫入下一個資料 */
    while (!(USARTx->ISR & (1UL << 7)))
        ;
    /* 寫入資料到 TDR (Transmit Data Register) */
    USART1->TDR = c;
}

void My_Delay_ms(uint32_t ms)
{
    uint32_t start = get_tick(); // now time
    while ((get_tick() - start) < ms)
        ; // stuck ms
}

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

    // // 2. clear setting
    // GPIOA->MODER &= ~((3UL << 18) | (3UL << 20) | (3UL << 24));

    // // 2. GPIO settings (PA9=TX, PA10=RX, PA12=RTS)
    // // 3. Hardware flow control : Add PA12 as an RTS pin
    // GPIOA->MODER |= ((2UL << 18) | (2UL << 20) | (2UL << 24)); // All set to AF mode

    // // 4. The AF values ​​for PA9, PA10, and PA12 are all AF1 (USART1)
    // GPIOA->AFRH &= ~((0xFUL << 4) | (0xFUL << 8) | (0xFUL << 16));
    // GPIOA->AFRH |= ((1UL << 4) | (1UL << 8) | (1UL << 16));

    // 2. 彈性配置 UART 引腳 (PA9, PA10, PA12 使用 AF1)
    GPIO_Init_AF(GPIOA, 9, 1);
    GPIO_Init_AF(GPIOA, 10, 1);
    GPIO_Init_AF(GPIOA, 12, 1);

    // 5. DMA setting
    DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    // 6. UART setting
    USART1->BRR = 69; // 115200 Baud Rate @ 8MHz

    // 7. CR3 Modification: Added RTSE (Bit 8) to enable hardware flow control.
    USART1->CR3 = (1UL << 6) | (1UL << 8);

    // Bit-0 : UART EN , Bit-3 : Tx EN , Bit-2 : Rx EN  , Bit-4 : IDLE EN
    USART1->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);

    // 8. NVIC and SysTick
    *((volatile uint32_t *)0xE000E100) = (1UL << 27);

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

            // Discard corrupted segments and reset pointer.
            rd_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
        }

        // --- Task 1: Efficient processing of Ring Buffer ---
        uint16_t wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;

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
                    while (!(USART1->ISR & (1UL << 7)))
                        ;
                    USART1->TDR = data;

                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE)
                        rd_ptr = 0;

                    // Dynamically captures the latest written pointer within the loop, ensuring a single clear.
                    wr_ptr = RX_BUF_SIZE - (uint16_t)DMA1->CH[2].CNDTR;
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