
## 一、程式碼實作
利用 C 語言 結構體成員 **連續配置** 的特性，實現了 **隱形偏移量 (Implicit Offset)**
### 語法結構解析
- #### struct 結構 定義 Peripheral Resgister 記憶體映射 I/O (MMIO)
  - 建立 Peripheral 的 struct
  - 結構成員配置為該周邊的所有暫存器，照位址 offset 向下排列，因 struct 為連續配置產生隱形 offset
    - 每個暫存器修飾為 `volatile` 防止編譯器優化，強制 CPU 每次都要執行 **LDR/STR 指令讀取實體記憶體** 的值
  - 定義 Peripheral 的基底位址
    - **`#define RCC_BASE    (0x40021000UL)`**
    - **`#define RCC_BASE    (0x40021000UL)`**
  - 定義 Peripheral 的結構指標 並 賦值 為 對應的基底位址
    - **`#define RCC         ((RCC_TypeDef *)  RCC_BASE)`**
    - **`#define GPIOC       ((GPIO_TypeDef *) GPIOC_BASE)`**
    - 可以透過 **`RCC->AHBENR`** 的方式操作暫存器的內容
- #### 非 struct 結構 定義 Peripheral Resgister 記憶體映射 I/O (MMIO)
  ```
  #define RCC_AHBENR   (*(volatile uint32_t*)(0x40021014))
  ```
  - **`volatile`** : 韌體開發的核心，強制 CPU 每次都要執行 **LDR/STR 指令讀取實體記憶體** 的值，防止編譯器優化（因為硬體狀態 像是按鈕輸入是會隨時改變的）
  - **`(volatile uint32_t*)`** : 告訴編譯器這是一個指向 **32-bit 硬體空間的指標**
  - **`*`** : **解引用 (Dereference)**，代表要 **操作該地址裡的內容**

### 解構基底與偏移 (Base & Offset)
在 STM32 中，周邊設備存取都是透過 **記憶體映射 I/O (MMIO)** 實現，採分層管理架構 (STM32F072 參考手冊)
#### 1. 總線基位址 (Bus Base Address)：
- AHB1 總線的起始位址為 **0x4002 0000**，其中 RCC 掛載於此 BUS
- AHB2 總線的起始位址為 **0x48000000**，其中 GPIO 掛載於此 BUS
#### 2. 周邊偏移量 (Peripheral Offset)：
- RCC 位於 AHB2 的偏移處，基位址變為 **0x4002 1000**
- GPIOC 位於 AHB2 的偏移處，基位址變為 **0x48000800**
#### 3.暫存器偏移量 (Register Offset)：
- **RCC (周邊基底) 內部** : 不同的功能之暫存器有各自的偏移，例如 **AHBENR (AHB Peripheral Clock Enable Register)** 的偏移為 **0x14**
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
      - **原子操作 (Atomic Operations)** : 處理 **共用控制暫存器** 的標準做法，利用 **Read-Modify-Write (RMW)** 操作 (**|=、&=、^|**)，確保只針對第 19 位元進行 Set，不干擾暫存器中其他的配置狀態
        - RCC_AHBENR 暫存器中同時控制了多個關鍵外設（如 GPIOA, GPIOB, DMA 等）。如果直接使用 **= 賦值** 會意外地將其他已經開啟的外設時鐘全部關閉
- **GPIOC (周邊基底)內部**  : 不同的功能之暫存器有各自的偏移，例如 **ODR (Output Data Register)** 的偏移為 **0x14**
  - **計算公式** : $Address_{MODER} = Base_{GPIOC} + Offset_{MODER} = 0x4800 0800 + 0x00 = 0x48000800$
    - **GPIOC->MODER** 的最終地址: **0x48000800**
      - 四種模式，輸入、輸出、複用、類比
      - 硬體設計上規定，每個 Pin 腳 的 Mode 佔用 **2 個 Bits 去控制**
      - MODER 的 **Bit 12, 13** ： 是一個 **多工器 (MUX)** 電路，負責切換 **Pin 腳 6** 的電路路徑為哪種模式 (輸入、輸出、複用、類比)
  - **計算公式** : $Address_{ODR} = Base_{GPIOC} + Offset_{ODR} = 0x4800 0800 + 0x14 = 0x48000814$
    - **GPIOC->ODR** 的最終地址: **0x48000814**
      - ODR 的 Bit 6 會連接到一個 **輸出驅動器 (Output Driver)** 電路，負責把電壓送到 Pin 6
  
## 二、系統架構：AHB 與 APB 資料高速公路
STM32 內部透過不同的匯流排（Bus）平衡效能與功耗
- AHB (Advanced High-performance Bus)
  - 特性 ： 高頻寬、低延遲，掛在系統時鐘下
  - 對象 ： **Flash、RAM、DMA、GPIO**（為了最快開關）
- APB (Advanced Peripheral Bus)
  - 特性 ： 速度較 **慢** 但 **省電**，透過預分頻器運作
  - 對象 ： UART、SPI、I2C、Timers、ADC
- 為什麼要區分？
  - 為了功耗與電磁干擾 (EMI) 的權衡
  - **高速訊號線翻轉非常耗電**，將低速設備（如 UART）放在 APB 上可大幅 **延長電力壽命**




## 標頭檔 `stm32f072xb.h` 之 Hardware Abstraction Layer (HAL)
在嵌入式開發中，不直接操作雜亂的記憶體位址，而是透過 **結構體映射 (Struct Mapping)** 技術建立一套可讀性高、且符合工業標準（如 CMSIS）的抽象層
```
#ifndef STM32F072XB_H
#define STM32F072XB_H

#include <stdint.h>

/* 定義存取權限 */
#define __I  volatile const
#define __O  volatile
#define __IO volatile


/* -------------------------------------------------------------------------
 * 1. Define Register Structures
 * ------------------------------------------------------------------------- */

/* RCC Register Structures (Reset and Clock Gating Control) */
typedef struct {
    __IO uint32_t CR;        /* 0x00: Clock Control */
    __IO uint32_t CFGR;      /* 0x04: Clock Configuration */
    __IO uint32_t CIR;       /* 0x08: Interrupt */
    __IO uint32_t APB2RSTR;  /* 0x0C: APB2 Reset */
    __IO uint32_t APB1RSTR;  /* 0x10: APB1 Reset */
    __IO uint32_t AHBENR;    /* 0x14: AHB Peripheral Clock Enable */
    __IO uint32_t APB2ENR;   /* 0x18: APB2 Peripheral Clock Enable */
    __IO uint32_t APB1ENR;   /* 0x1C: APB1 Peripheral Clock Enable */
    __IO uint32_t BDCR;
    __IO uint32_t CSR;
    __IO uint32_t AHBRSTR;
    __IO uint32_t CFGR2;
    __IO uint32_t CFGR3;
} RCC_TypeDef;


/* GPIO Register Structures (4-bytes Implicit-Offset for each member of struct) */
typedef struct {
    __IO uint32_t MODER;    /* 0x00: Mode */
    __IO uint32_t OTYPER;   /* 0x04: Output Type */
    __IO uint32_t OSPEEDR;  /* 0x08: Output Speed */
    __IO uint32_t PUPDR;    /* 0x0C: Pull-up/Pull-down */
    __IO uint32_t IDR;      /* 0x10: Input Date */
    __IO uint32_t ODR;      /* 0x14: Output Data */
    __IO uint32_t BSRR;     /* 0x18: bit set/reset */
    __IO uint32_t LCKR;     /* 0x1C: Configuration Lock */
    __IO uint32_t AFRL;     /* 0x20: Function reuse Low-bit */
    __IO uint32_t AFRH;     /* 0x24: Function reuse High-bit */
} GPIO_TypeDef;


/* USART Register Structures */
typedef struct {
    __IO uint32_t CR1;      /* 0x00: Control register 1 */
    __IO uint32_t CR2;      /* 0x04: Control register 2 */
    __IO uint32_t CR3;      /* 0x08: Control register 3 */
    __IO uint32_t BRR;      /* 0x0C: Baud rate register */
    __IO uint32_t GTPR;     /* 0x10: Guard time and prescaler */
    __IO uint32_t RTOR;     /* 0x14: Receiver timeout */
    __IO uint32_t RQR;      /* 0x18: Request register */
    __IO uint32_t ISR;      /* 0x1C: Interrupt and status */
    __IO uint32_t ICR;      /* 0x20: Interrupt flag clear */
    __IO uint32_t RDR;      /* 0x24: Receive data */
    __IO uint32_t TDR;      /* 0x28: Transmit data */
} USART_TypeDef;

typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __I  uint32_t CALIB;
} SysTick_TypeDef;


/* DMA Channel Structure */
typedef struct {
    __IO uint32_t CCR;    /* 0x00: Configuration register */
    __IO uint32_t CNDTR;  /* 0x04: Number of data register */
    __IO uint32_t CPAR;   /* 0x08: Peripheral address register */
    __IO uint32_t CMAR;   /* 0x0C: Memory address register */
    uint32_t RESERVED;    /* 0x10: 每個 Channel 之間有 4-byte 的間隔 */
} DMA_Channel_TypeDef;

typedef struct {
    __IO uint32_t ISR;    /* 0x00: Interrupt status register */
    __IO uint32_t IFCR;   /* 0x04: Interrupt flag clear register */
    DMA_Channel_TypeDef CH[7]; /* 從 0x08 開始對應各個通道 */
} DMA_TypeDef;

/* -------------------------------------------------------------------------
 * 2. Peripheral Base Addresses
 * ------------------------------------------------------------------------- */
#define RCC_BASE      (0x40021000UL)
#define GPIOA_BASE    (0x48000000UL)  // start of AHB2 
#define GPIOC_BASE    (0x48000800UL)
#define USART1_BASE   (0x40013800UL)  // from APB2
#define NVIC_BASE     (0xE000E100UL)
#define DMA1_BASE     (0x40020000UL)


/* Convert the address to struct pointer (Mapping) */
#define RCC         ((RCC_TypeDef *) RCC_BASE)           /* AHB1-Bus-Base(0x4002 0000) + RCC-Peripheral-offset(0x4002 1000) */
#define GPIOA       ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOC       ((GPIO_TypeDef *) GPIOC_BASE)        /* AHB2-Bus-Base(0x4800 0000) + GPIOC-Peripheral-offest(0x4800 0800) */
#define USART1      ((USART_TypeDef *) USART1_BASE)
#define SysTick     ((SysTick_TypeDef *) 0xE000E010UL)     /* PM0215 : STM32 core peripheral register regions*/
#define DMA1        ((DMA_TypeDef *) DMA1_BASE)
#define NVIC_ISER   ((__IO uint32_t *) 0xE000E100UL)

#endif /* STM32F072XB_H */
```








