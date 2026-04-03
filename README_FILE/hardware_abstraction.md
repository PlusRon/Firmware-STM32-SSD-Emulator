# 標頭檔 `stm32f072xb.h` 之硬體定義（Hardware Abstraction）
```
#ifndef STM32F072XB_H
#define STM32F072XB_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 1. Define Register Structures
 * ------------------------------------------------------------------------- */

/* GPIO Register Structures (4-bytes offset for each member) */
typedef struct {
    volatile uint32_t MODER;    /* 0x00: Mode */
    volatile uint32_t OTYPER;   /* 0x04: Output Type */
    volatile uint32_t OSPEEDR;  /* 0x08: Output Speed */
    volatile uint32_t PUPDR;    /* 0x0C: Pull-up/Pull-down */
    volatile uint32_t IDR;      /* 0x10: Input Date */
    volatile uint32_t ODR;      /* 0x14: Output Data */
    volatile uint32_t BSRR;     /* 0x18: bit set/reset */
    volatile uint32_t LCKR;     /* 0x1C: Configuration Lock */
    volatile uint32_t AFRL;     /* 0x20: Function reuse Low-bit */
    volatile uint32_t AFRH;     /* 0x24: Function reuse High-bit */
} GPIO_TypeDef;

/* RCC 暫存器結構體 (Reset and Clock Control) */
typedef struct {
    volatile uint32_t CR;        /* 0x00: Clock Control */
    volatile uint32_t CFGR;      /* 0x04: Clock Configuration */
    volatile uint32_t CIR;       /* 0x08: Interrupt */
    volatile uint32_t APB2RSTR;  /* 0x0C: APB2 Reset */
    volatile uint32_t APB1RSTR;  /* 0x10: APB1 Reset */
    volatile uint32_t AHBENR;    /* 0x14: AHB Peripheral Clock Enable */
    volatile uint32_t APB2ENR;   /* 0x18: APB2 Peripheral Clock Enable */
    volatile uint32_t APB1ENR;   /* 0x1C: APB1 Peripheral Clock Enable */
} RCC_TypeDef;

/* -------------------------------------------------------------------------
 * 2. Peripheral Base Addresses
 * ------------------------------------------------------------------------- */
#define RCC_BASE    (0x40021000UL)
#define GPIOC_BASE  (0x48000800UL)

/* Convert the address to struct pointer (Mapping) */
#define RCC         ((RCC_TypeDef *)  RCC_BASE)
#define GPIOC       ((GPIO_TypeDef *) GPIOC_BASE)

#endif /* STM32F072XB_H */
```
