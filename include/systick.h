#ifndef SYSTICK_H_
#define SYSTICK_H_

#include "stm32f072xb.h"

void SysTick_Init(uint32_t tick);
uint32_t get_tick(void);
void My_Delay_ms(uint32_t ms);

#endif /* SYSTICK_H_ */