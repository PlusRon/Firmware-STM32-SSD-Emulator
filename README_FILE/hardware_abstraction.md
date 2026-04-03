# 標頭檔 `stm32f072xb.h` 之 硬體抽象層設計 (Hardware Abstraction Layer, HAL)
在嵌入式開發中，不直接操作雜亂的記憶體位址，而是透過 **結構體映射 (Struct Mapping)** 技術建立一套可讀性高、且符合工業標準（如 CMSIS）的抽象層
```
#ifndef STM32F072XB_H
#define STM32F072XB_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 1. Define Register Structures
 * ------------------------------------------------------------------------- */

/* GPIO Register Structures (4-bytes Implicit-Offset for each member of struct) */
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

/* RCC Register Structures (Reset and Clock Gating Control) */
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
#define RCC         ((RCC_TypeDef *)  RCC_BASE)          /* AHB1-Bus-Base(0x4002 0000) + RCC-Peripheral-offset(0x4002 1000) */
#define GPIOC       ((GPIO_TypeDef *) GPIOC_BASE)        /* AHB2-Bus-Base(0x4800 0000) + GPIOC-Peripheral-offest(0x4800 0800) */

#endif /* STM32F072XB_H */
```
## 一、程式碼實作
利用 C 語言 結構體成員 **連續配置** 的特性，實現了 **隱形偏移量 (Implicit Offset)**
### 解構基底與偏移 (Base & Offset)
在 STM32 中，周邊設備存取都是透過 **記憶體映射 I/O (MMIO)** 實現，採分層管理架構 (STM32F072 參考手冊)
#### **總線基位址 (Bus Base Address)**：
- AHB1 總線的起始位址為 **0x4002 0000**，其中 RCC 掛載於此 BUS
- AHB2 總線的起始位址為 **0x48000000**，其中 GPIO 掛載於此 BUS
#### **周邊偏移量 (Peripheral Offset)**：
- RCC 位於 AHB2 的偏移處，基位址變為 **0x4002 1000**
- GPIOC 位於 AHB2 的偏移處，基位址變為 **0x48000800**
#### **暫存器偏移量 (Register Offset)**：
- **RCC (周邊基底)** 內部 : 不同的功能之暫存器有各自的偏移，例如 AHBENR (AHB Peripheral Clock Enable Register) 的偏移為 **0x14**
  - **計算公式** : $Address_{AHBENR} = Base_{RCC} + Offset_{AHBENR} = 0x4002 1000 + 0x14 = 0x40021014$
    -  $Offset_{AHBENR}$ 以C 語言 **struct 成員連續配置** 的特性，實作出偏移
    - **RCC->AHBENR** 的最終地址: **0x40021014**
    - 當對該地址的 **第 19 位元 (IOPCEN, I/O Peripheral Clock Enable)** 寫入 1 時，硬體電路會把時鐘訊號送到 GPIOC，該周邊才能開始工作
    - **低功耗設計（Low Power Design）** : 數位電路中，電晶體翻轉在訊號 0 和 1 切換時最耗電，若時鐘訊號一直跑，即便沒用該腳位，內部百萬電晶體仍會持續翻轉並浪費電力，透過 **RCC（Reset and Clock Control）**，可以只開啟目前需要的硬體模組，省電
    - 周邊設備（GPIO, UART, SPI…）初始狀態下，其內部的 **時鐘訊號線（Clock Line）都是斷開的**
      - Bit 17: IOPAEN (GPIOA Enable)
      - Bit 18: IOPBEN (GPIOB Enable)
      - Bit 19: IOPCEN (GPIOC Enable)
      - `RCC_AHBENR |= (1 << 19);` 的二進位的變化如下
          ```
          RCC_AHBENR 原值 : 0000 0000 0000 0000 0000 0000 0000 0000
             (1 << 19)    : 0000 0000 0000 1000 0000 0000 0000 0000
          -------------------------------------------------------
             OR 運算後結果 : 0000 0000 0000 1000 0000 0000 0000 0000
                                           ^ 第 19 位變成 1
          ```
      - 處理 **共用控制暫存器** 的標準做法，利用 **Read-Modify-Write (RMW)** 操作 (**|=、&=、^|**)，確保只針對第 19 位元進行 Set，不干擾暫存器中其他的配置狀態
        - RCC_AHBENR 暫存器中同時控制了多個關鍵外設（如 GPIOA, GPIOB, DMA 等）。如果直接使用 **= 賦值** 會意外地將其他已經開啟的外設時鐘全部關閉
- **GPIOC (周邊基底)** 內部 : 不同的功能之暫存器有各自的偏移，例如 ODR (Output Data Register) 的偏移為 **0x14**
  - **計算公式** : $Address_{ODR} = Base_{GPIOC} + Offset_{ODR} = 0x4800 0800 + 0x14 = 0x48000814$

 













