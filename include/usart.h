#ifndef UART_H_
#define UART_H_

#include "stm32f072xb.h"

/* ASCII communication character definitions */
#define ASCII_NAK 0x15 // Negative Acknowledge (Data Error / Re-transmission Request)

extern volatile uint8_t rx_idle_event;         // IDLE event
extern volatile uint8_t uart_overrun_occurred; // software ORE flag

void UART_Init(USART_TypeDef *USARTx, uint32_t baudrate_divider);
void UART_Send(USART_TypeDef *USARTx, char *s);
void UART_SendChar(USART_TypeDef *USARTx, uint8_t c);

#endif /* UART_H_ */