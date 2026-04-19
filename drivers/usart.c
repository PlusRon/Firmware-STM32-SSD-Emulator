#include "usart.h"
#include <stdio.h>


/* 消滅 nosys 警告的 Stubs */
#include <sys/stat.h>

int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) { st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { return 1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { return -1; }


// 必須在這裡「定義」變數實體，不能只有 .h 的 extern
volatile uint8_t rx_idle_event = 0;
volatile uint8_t uart_overrun_occurred = 0;

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
    USARTx->TDR = c;
}

/* 實作 _write 函式，這是 printf 的底層出口 */
int _write(int file, char *ptr, int len) {
    for (int i = 0; i < len; i++) {
      // 呼叫你原本寫好的發送函式
        UART_SendChar(USART1, (uint8_t)ptr[i]);
    }
    return len;
}
