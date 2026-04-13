#ifndef STM32F072XB_H
#define STM32F072XB_H

#include <stdint.h>

/* 定義存取權限 */
#define __I volatile const
#define __O volatile
#define __IO volatile

/* -------------------------------------------------------------------------
 * 1. Define Register Structures
 * ------------------------------------------------------------------------- */

/* RCC Register Structures (Reset and Clock Gating Control) */
typedef struct
{
    __IO uint32_t CR;       /* 0x00: Clock Control */
    __IO uint32_t CFGR;     /* 0x04: Clock Configuration */
    __IO uint32_t CIR;      /* 0x08: Interrupt */
    __IO uint32_t APB2RSTR; /* 0x0C: APB2 Reset */
    __IO uint32_t APB1RSTR; /* 0x10: APB1 Reset */
    __IO uint32_t AHBENR;   /* 0x14: AHB Peripheral Clock Enable */
    __IO uint32_t APB2ENR;  /* 0x18: APB2 Peripheral Clock Enable */
    __IO uint32_t APB1ENR;  /* 0x1C: APB1 Peripheral Clock Enable */
    __IO uint32_t BDCR;
    __IO uint32_t CSR;
    __IO uint32_t AHBRSTR;
    __IO uint32_t CFGR2;
    __IO uint32_t CFGR3;
} RCC_TypeDef;

/* GPIO Register Structures (4-bytes Implicit-Offset for each member of struct) */
typedef struct
{
    __IO uint32_t MODER;   /* 0x00: Mode */
    __IO uint32_t OTYPER;  /* 0x04: Output Type */
    __IO uint32_t OSPEEDR; /* 0x08: Output Speed */
    __IO uint32_t PUPDR;   /* 0x0C: Pull-up/Pull-down */
    __IO uint32_t IDR;     /* 0x10: Input Date */
    __IO uint32_t ODR;     /* 0x14: Output Data */
    __IO uint32_t BSRR;    /* 0x18: bit set/reset */
    __IO uint32_t LCKR;    /* 0x1C: Configuration Lock */
    __IO uint32_t AFRL;    /* 0x20: Function reuse Low-bit */
    __IO uint32_t AFRH;    /* 0x24: Function reuse High-bit */
} GPIO_TypeDef;

/* USART Register Structures */
typedef struct
{
    __IO uint32_t CR1;  /* 0x00: Control register 1 */
    __IO uint32_t CR2;  /* 0x04: Control register 2 */
    __IO uint32_t CR3;  /* 0x08: Control register 3 */
    __IO uint32_t BRR;  /* 0x0C: Baud rate register */
    __IO uint32_t GTPR; /* 0x10: Guard time and prescaler */
    __IO uint32_t RTOR; /* 0x14: Receiver timeout */
    __IO uint32_t RQR;  /* 0x18: Request register */
    __IO uint32_t ISR;  /* 0x1C: Interrupt and status */
    __IO uint32_t ICR;  /* 0x20: Interrupt flag clear */
    __IO uint32_t RDR;  /* 0x24: Receive data */
    __IO uint32_t TDR;  /* 0x28: Transmit data */
} USART_TypeDef;

typedef struct
{
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __I uint32_t CALIB;
} SysTick_TypeDef;

/* DMA Channel Structure */
typedef struct
{
    __IO uint32_t CCR;   /* 0x00: Configuration register */
    __IO uint32_t CNDTR; /* 0x04: Number of data register */
    __IO uint32_t CPAR;  /* 0x08: Peripheral address register */
    __IO uint32_t CMAR;  /* 0x0C: Memory address register */
    uint32_t RESERVED;   /* 0x10: 每個 Channel 之間有 4-byte 的間隔 */
} DMA_Channel_TypeDef;

typedef struct
{
    __IO uint32_t ISR;         /* 0x00: Interrupt status register */
    __IO uint32_t IFCR;        /* 0x04: Interrupt flag clear register */
    DMA_Channel_TypeDef CH[7]; /* 從 0x08 開始對應各個通道 */
} DMA_TypeDef;

/* -------------------------------------------------------------------------
 * 2. Peripheral Base Addresses
 * ------------------------------------------------------------------------- */
#define RCC_BASE (0x40021000UL)
#define GPIOA_BASE (0x48000000UL) // start of AHB2
#define GPIOC_BASE (0x48000800UL)
#define USART1_BASE (0x40013800UL) // from APB2
#define NVIC_BASE (0xE000E100UL)
#define DMA1_BASE (0x40020000UL)

/* Convert the address to struct pointer (Mapping) */
#define RCC ((RCC_TypeDef *)RCC_BASE) /* AHB1-Bus-Base(0x4002 0000) + RCC-Peripheral-offset(0x4002 1000) */
#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE) /* AHB2-Bus-Base(0x4800 0000) + GPIOC-Peripheral-offest(0x4800 0800) */
#define USART1 ((USART_TypeDef *)USART1_BASE)
#define SysTick ((SysTick_TypeDef *)0xE000E010UL) /* PM0215 : STM32 core peripheral register regions*/
#define DMA1 ((DMA_TypeDef *)DMA1_BASE)
#define NVIC_ISER ((__IO uint32_t *)0xE000E100UL)

#endif /* STM32F072XB_H */