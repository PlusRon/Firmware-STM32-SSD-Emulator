/* include/registers.h */
#ifndef REGISTERS_H
#define REGISTERS_H

#define RCC_BASE      0x40021000
#define GPIOC_BASE    0x48000800

#define RCC_AHBENR    (*(volatile unsigned int*)(RCC_BASE + 0x14))
#define GPIOC_MODER   (*(volatile unsigned int*)(GPIOC_BASE + 0x00))
#define GPIOC_ODR     (*(volatile unsigned int*)(GPIOC_BASE + 0x14))

#endif
