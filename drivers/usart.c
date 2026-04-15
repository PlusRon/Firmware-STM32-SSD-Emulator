#include "include/usart.h"

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

/* --- UART Module --- */

/**
 * @brief  初始化 UART 配置 (包含 Baudrate, DMA Enable, RTS Enable, Interrupt Enable)
 * @param  USARTx   : UART 指標 (如 USART1)
 * @param  baudrate : 鮑率 (如 115200)
 */
void UART_Init(USART_TypeDef *USARTx, uint32_t baudrate_divider)
{
    /* 1. 計算鮑率 (假設系統時鐘為 8MHz)
       公式: BRR (baudrate_divider) = f_CK(8M Hz) / baudrate */
    USARTx->BRR = baudrate_divider;

    /* 2. CR3 配置:
       Bit 6: DMAR (DMA Enable for receiver)
       Bit 8: RTSE (RTS Enable for hardware flow control) */
    // CR3 Modification: Added RTSE (Bit 8) to enable hardware flow control.
    USARTx->CR3 = (1UL << 6) | (1UL << 8);

    /* 3. CR1 配置:
       Bit 0: UE (UART Enable)
       Bit 3: TE (Transmitter Enable)
       Bit 2: RE (Receiver Enable)
       Bit 4: IDLEIE (IDLE Interrupt Enable) */
    // Bit-0 : UART EN , Bit-3 : Tx EN , Bit-2 : Rx EN  , Bit-4 : IDLE EN
    USARTx->CR1 = (1UL << 0) | (1UL << 3) | (1UL << 2) | (1UL << 4);
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