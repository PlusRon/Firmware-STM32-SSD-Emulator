#ifndef GPIO_H_
#define GPIO_H_

#include "stm32f072xb.h"

void GPIO_Init_Output(GPIO_TypeDef *GPIOx, uint8_t pin);
void GPIO_Init_AF(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t af_num);
void LED_Toggle(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t *state);

#endif // GPIO_H_