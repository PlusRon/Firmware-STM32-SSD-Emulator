# CODE
## `linker/stm32.ld`
```
/* 1. Define Memory Block */
MEMORY
{
    /* 128KB Flash, start from 0x08000000 */
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    /* 16KB RAM, start from 0x20000000 */
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

/* 定義堆疊頂端 (RAM 結束位址)，供 startup.c 使用 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 2. Define Order of Code-Section */
SECTIONS
{
    /* .text-section have to start at FLASH head */
    .text : {
        . = ALIGN(4);         /* 4-bytes align be sure at the begin*/
        _stext = .;           /* record the start-point of .text-section */
        KEEP(*(.isr_vector))  /* put interrupt-vector-table at the FLASH's head will be sure */
        *(.text*)             /* put all c-code */
        *(.text.*)            /* 包含所有子段 */
        *(.rodata*)           /* put read-only-data(string, constant) */
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;           /* record the end-point of .text-section (will be the start-point of .data-section's LMA) */
    } > FLASH

    /* _la_data 是 .data 段在 FLASH 中的載入位址 (LMA) */
    /* 它會緊跟在 .text 之後 */
    _la_data = LOADADDR(.data);

    /* .data-section(be initialized global-variable), LMA stored in FLASH, and VMA be move into RAM while executing */
    .data : {
        . = ALIGN(4);         /* VMA's begin align be sure */
        _sdata = .;           /* record the start-point of .data-section in RAM */
        *(.data*)
        *(.data.*)
        . = ALIGN(4);         /* VMA's final align be sure */
        _edata = .;           /* record the end-point of .data-section in RAM */
    } > RAM AT > FLASH        /* AT represent the original position(LMA) at FLASH */

    /* .bss-section(be un-initialized global-variable), stored in RAM(LMA = VMA) directly */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(.bss.*)
        *(COMMON)             /* collect the global-variable of Tentative Definition */
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
```


## `hw/startup.c`
```
#include <stdint.h>

/* import the defined-symbol(external variable) from linker_script.ld */
/* Notice： they are the address-label, use their address in C-langue */
extern uint32_t _estack;  // 堆疊頂端位址
extern uint32_t _la_data; // .data 在 FLASH 中的載入位址 (LMA)
// extern uint32_t _etext;   // In Flash, be the start-point of .data for LMA
extern uint32_t _sdata; // In RAM, be the start-point of .data for VMA
extern uint32_t _edata; // In RAM, be the end-point of .data
extern uint32_t _sbss;  // In RAM, be the start-point of .bss
extern uint32_t _ebss;  // In RAM, be the end-point of .bss

/* declare the external main function */
extern int main(void);

/* 3. 定義預設中斷處理器 (保險絲) */
void Default_Handler(void)
{
    while (1)
        ;
}

/* 4. 使用 Weak Alias 宣告中斷，增加相容性 */
/* 這樣你在 main.c 寫了同名函式，這裡就會被自動取代 */
void Reset_Handler(void);
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

/* A function that force trigger software reset (using the kernel registers of ARM Cortex-M) */
void system_soft_reset(void)
{
    // Application Interrupt and Reset Control Register (AIRCR) from ARM Cortex-M's kernel
    // Key-0x05FA and SYSRESETREQ-bit will be writen in
    uint32_t *aircr = (uint32_t *)0xE000ED0C;
    *aircr = (0x05FA << 16) | (1 << 2);

    // Enter the infinite-loop to waiting for reset
    while (1)
        ;
}
/* Execute this first function while CPU-reset or power-on */
void Reset_Handler(void)
{

    // 1. move .data-section : copy initial-global-variable from Flash to RAM
    // Ex : int count = 100; this 100 have to copy from FLASH to RAM, then program to be read/write correctly
    // uint32_t *src = &_etext;   // LMA
    // A. 搬運 .data 段：從 FLASH (LMA) 搬到 RAM (VMA)
    uint32_t *src = &_la_data;
    uint32_t *dest = &_sdata; // VMA

    while (dest < &_edata)
    {
        *dest++ = *src++;
    }

    // 2. initialize the .bss-section : clear the uninitial-global-variable to be zero
    // Ex : int buffer[10]; all of them be zero when power on
    dest = &_sbss;
    while (dest < &_ebss)
    {
        *dest++ = 0;
    }

    // 3. Finish system initialization, jump to main() function of C-language-layer
    main();

    /* --- Strengthen the protection machanism --- */
    // if main() unexpected ending, represent the the major anomaly occured in the system logic
    // Before enter in the dead-loop, we can :

    // A. Trigger the software-reset (Let system restart immediately)
    system_soft_reset();

    // B. Alternatively remain stationary, waiting for Watchdog detect to trigger the hardware reset
    while (1)
        ;
}

/* Interrupt Vector Table */
/* Use the attribute ensure the data is placed in the .isr_vector-region that specified by Linker Script */
__attribute__((section(".isr_vector")))
uint32_t vector_table[] = {
    (uint32_t)&_estack,        // 0. initial Main Stack Point(MSP) : top of RAM (0x20000000 + 16KB)
    (uint32_t)Reset_Handler,   // 1. Reset Vector : CPU first jump to this location after power-on
    (uint32_t)Default_Handler, // 2. NMI
    (uint32_t)Default_Handler, // 3. HardFault
    0, 0, 0, 0, 0, 0, 0,       // 4-10. 保留
    (uint32_t)Default_Handler, // 11. SVCall
    0, 0,                      // 12-13. 保留
    (uint32_t)Default_Handler, // 14. PendSV
    (uint32_t)SysTick_Handler, // 15. SysTick (非阻塞計時核心)
    /* 外設中斷 (按硬體手冊 IRQ 順序排列) */
    (uint32_t)Default_Handler,  // 16. WWDG (IRQ 0)
    (uint32_t)Default_Handler,  // 17. PVD
    (uint32_t)Default_Handler,  // 18. RTC
    (uint32_t)Default_Handler,  // 19. FLASH
    (uint32_t)Default_Handler,  // 20. RCC
    (uint32_t)Default_Handler,  // 21. EXTI0_1
    (uint32_t)Default_Handler,  // 22. EXTI2_3
    (uint32_t)Default_Handler,  // 23. EXTI4_15
    (uint32_t)Default_Handler,  // 24. TSC
    (uint32_t)Default_Handler,  // 25. DMA_CH1
    (uint32_t)Default_Handler,  // 26. DMA_CH2_3
    (uint32_t)Default_Handler,  // 27. DMA_CH4_5
    (uint32_t)Default_Handler,  // 28. ADC1_COMP
    (uint32_t)Default_Handler,  // 29. TIM1_BRK_UP_TRG_COM
    (uint32_t)Default_Handler,  // 30. TIM1_CC
    (uint32_t)Default_Handler,  // 31. TIM2
    (uint32_t)Default_Handler,  // 32. TIM3
    (uint32_t)Default_Handler,  // 33. TIM6_DAC
    (uint32_t)Default_Handler,  // 34. TIM7
    (uint32_t)Default_Handler,  // 35. TIM14
    (uint32_t)Default_Handler,  // 36. TIM15
    (uint32_t)Default_Handler,  // 37. TIM16
    (uint32_t)Default_Handler,  // 38. TIM17
    (uint32_t)Default_Handler,  // 39. I2C1
    (uint32_t)Default_Handler,  // 40. I2C2
    (uint32_t)Default_Handler,  // 41. SPI1
    (uint32_t)Default_Handler,  // 42. SPI2
    (uint32_t)USART1_IRQHandler // 43. USART1 (STM32F072 的 IRQ 27)
};
```

## `include/stm32f072xb.h`
```
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
```





## `app/main.c`
```
#include "stm32f072xb.h"
#include "gpio.h"
#include "systick.h"
#include "dma.h"
#include "usart.h"

/* --- MACRO define --- */
#define RX_BUF_SIZE 1024 // Increasing the buffer size to 1024, significantly reduces the probability of ORE

/* --- global variable --- */

uint8_t rx_buffer[RX_BUF_SIZE]; // buffer assign from RAM
uint16_t rd_ptr = 0;            // CPU(consumer) read pointer (software)

/* --- System initialization --- */
void System_Init(void)
{
    // 1. clock on
    // RCC->AHBENR |= (1UL << 17) | (1UL << 19) | (1UL << 0);
    // RCC->APB2ENR |= (1UL << 14);

    // 1. 核心時鐘與 UART/DMA 初始化
    RCC->APB2ENR |= (1UL << 14); // USART1 Clock
    RCC->AHBENR |= (1UL << 0);   // DMA1 Clock

    // LED_Init();
    GPIO_Init_Output(GPIOC, 6);

    // 2. 彈性配置 UART 引腳 (PA9, PA10, PA12 使用 AF1)
    GPIO_Init_AF(GPIOA, 9, 1);
    GPIO_Init_AF(GPIOA, 10, 1);
    GPIO_Init_AF(GPIOA, 12, 1);

    /* 3. DMA 配置 (採用你想要的 DMAx->CH[i] 架構)
       USART1_RX 固定在 DMA1 的 Channel 3，對應索引為 2 */
    DMA_Init(DMA1, 2, (uint32_t)&(USART1->RDR), (uint32_t)rx_buffer, RX_BUF_SIZE);

    /* 4. UART 彈性初始化 */
    UART_Init(USART1, 69); // 115200 Baud Rate @ 8MHz

    // 8. NVIC and SysTick
    *NVIC_ISER = (1UL << 27);

    SysTick_Init(8000);
}

/* --- Main --- */
int main(void)
{
    System_Init();
    uint32_t last_blink = 0;
    uint8_t led_current_state = 0;

    UART_Send(USART1, "Industrial UART System Initialized (RTS/NACK Enabled)\r\n");

    while (1)
    {
        /* --- Core safeguard mechanism for NACK retransmission detection: checking software error flags ------ */
        if (uart_overrun_occurred)
        {
            uart_overrun_occurred = 0; // clear flag

            // Send the NAK character to let the other end know that an error has occurred and a retransmission is needed
            // requires software !support! on the other end
            UART_SendChar(USART1, ASCII_NAK);
            UART_Send(USART1, "\r\n[NACK] Overflow Detected! Please resend last packet.\r\n");

            // 同步讀取指標到最新的寫入點，捨棄損壞資料
            // Discard corrupted segments and reset pointer.
            rd_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);
            // rx_idle_event = 0; // 同步清除 IDLE 標記
            // continue;          // 重新開始下一輪循環
        }

        // 使用封裝函式獲取寫入指標
        // --- Task 1: Efficient processing of Ring Buffer ---
        uint16_t wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);

        // The outer layer uses a double-judgment threshold, and the inner layer uses dynamic catch-up logic.
        if (rx_idle_event || (rd_ptr != wr_ptr))
        {

            // Only when there is actual data (pointer are not equal) will the buffer be read.
            if (rd_ptr != wr_ptr)
            {
                // [ hint : can put a header tag here, for example, UART_Send("Recv: ");]

                while (rd_ptr != wr_ptr)
                {
                    uint8_t data = rx_buffer[rd_ptr];

                    // Process data (Echo or store into the protocol parser)
                    // while (!(USART1->ISR & (1UL << 7)))
                    //     ;
                    // USART1->TDR = data;
                    UART_SendChar(USART1, data);

                    rd_ptr++;
                    if (rd_ptr >= RX_BUF_SIZE)
                        rd_ptr = 0;

                    // 內圈動態追蹤最新的 wr_ptr
                    // Dynamically captures the latest written pointer within the loop, ensuring a single clear.
                    wr_ptr = DMA_Get_Write_Index(DMA1, 2, RX_BUF_SIZE);
                }
                UART_Send(USART1, "\r\n");
            }

            // 無論是因為指標不相等還是因為 IDLE 事件進來的，處理完後都清空事件
            // All events should be cleared after processing, whether the issue from unequal pointer or from an IDLE event
            rx_idle_event = 0;
        }

        // --- Task 2: Background Task ---
        if ((get_tick() - last_blink) >= 500)
        {
            // LED_Toggle(&led_current_state);
            LED_Toggle(GPIOC, 6, &led_current_state);
            last_blink = get_tick();
        }

        // --- Simulation area: Add delay here ---
        // Trying Delay 2000ms (2 seconds) first.
        // Then paste a very long text (e.g., 2000 characters) from the computer at once
        // My_Delay_ms(2000);
    }
}
```


## `include/gpio.h`
```
#ifndef GPIO_H_
#define GPIO_H_

#include "stm32f072xb.h"

void GPIO_Init_Output(GPIO_TypeDef *GPIOx, uint8_t pin);
void GPIO_Init_AF(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t af_num);
void LED_Toggle(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t *state);

#endif // GPIO_H_
```

## `drivers/gpio.c`
```
#include "gpio.h"

/**
 * @brief 彈性初始化指定 GPIO 上的引腳為輸出模式
 * @param GPIOx : 指定 Port (例如 GPIOA, GPIOB, GPIOC)
 * @param pin   : 指定引腳編號 (0 ~ 15)
 */
void GPIO_Init_Output(GPIO_TypeDef *GPIOx, uint8_t pin)
{
    /* 1. 自動判斷並啟動對應時鐘 (使用位移取代除法與乘法) */
    uint32_t port_bit_offset = ((uint32_t)GPIOx - GPIOA_BASE) >> 10; // GPIO 每個區塊間隔 0x400 (即 2^10)
    RCC->AHBENR |= (1UL << (17 + port_bit_offset));                  // power on GPIOx

    /* 2. 設定 MODER 為輸出模式 (01) */
    // 使用 (pin << 1) 代替 pin * 2
    GPIOx->MODER &= ~(3UL << (pin << 1)); // clear Mode
    GPIOx->MODER |= (1UL << (pin << 1));  // set Mode as GPO
}

/**
 * @brief 初始化引腳為複用功能 (Alternate Function)
 * @param GPIOx : 指定 Port
 * @param pin   : 引腳編號
 * @param af_num: AF 編號 (如 USART1 為 AF1)
 */
void GPIO_Init_AF(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t af_num)
{
    /* 1. 開啟時鐘 */
    uint32_t port_bit_offset = ((uint32_t)GPIOx - GPIOA_BASE) >> 10;
    RCC->AHBENR |= (1UL << (17 + port_bit_offset));

    /* 2. 設定 MODER 為 10 (Alternate Function) */
    GPIOx->MODER &= ~(3UL << (pin << 1)); // clear Mode
    GPIOx->MODER |= (2UL << (pin << 1));  // set Mode as AF mode

    /* 3. 設定 AF 暫存器 (AFRL 控制 0-7 腳, AFRH 控制 8-15 腳) */
    if (pin < 8)
    {
        GPIOx->AFRL &= ~(0xFUL << (pin << 2));
        GPIOx->AFRL |= ((uint32_t)af_num << (pin << 2));
    }
    else
    {
        GPIOx->AFRH &= ~(0xFUL << ((pin - 8) << 2));
        GPIOx->AFRH |= ((uint32_t)af_num << ((pin - 8) << 2));
    }
}

/**
 * @brief 彈性翻轉 LED 狀態 (使用 BSRR 確保原子性)
 * @param GPIOx : 指定 Port
 * @param pin   : 指定引腳編號
 * @param state : 指向狀態變數的指標
 */
void LED_Toggle(GPIO_TypeDef *GPIOx, uint8_t pin, uint8_t *state)
{
    if (*state == 0)
    {
        GPIOx->BSRR = (1UL << pin); // SET PC6
        *state = 1;
    }
    else
    {
        GPIOx->BSRR = (1UL << (16 + pin)); // RESET PC6
        *state = 0;
    }
}
```
## `include/systick.h`
```
#ifndef SYSTICK_H_
#define SYSTICK_H_

#include "stm32f072xb.h"

void SysTick_Init(uint32_t tick);
uint32_t get_tick(void);
void My_Delay_ms(uint32_t ms);

#endif /* SYSTICK_H_ */
```

## `drivers/systick.c`
```
#include "systick.h"

// static 只能放在 .c，因為作用在全域變數時，為這份檔案私有
static volatile uint32_t msTicks = 0; // 8000 clock as a 1ms(1 Tick)

/**
 * @brief  初始化 SysTick 計時器
 * @param  ticks: 觸發中斷所需的計數次數 (例如 8MHz 下 1ms 需要 8000 ticks)
 * @retval None
 */
void SysTick_Init(uint32_t ticks)
{
    /* 1. 設定重載值 (LOAD)
       由於計數器數到 0 才會觸發，所以實際次數要減 1 */
    SysTick->LOAD = (uint32_t)(ticks - 1UL);

    /* 2. 清空當前計數值 (VAL)
       寫入任何值都會將其歸零，並清除計數標誌 */
    SysTick->VAL = 0UL;

    /* 3. 設定控制暫存器 (CTRL)
       Bit 2: CLKSOURCE = 1 (使用處理器核心時鐘)
       Bit 1: TICKINT   = 1 (開啟 SysTick 中斷)
       Bit 0: ENABLE    = 1 (啟動計時器)
       7 的二進制是 111，剛好開啟上述三個功能 */
    SysTick->CTRL = (1UL << 2) | (1UL << 1) | (1UL << 0);
}

/* --- system timing ISR--- */
void SysTick_Handler(void)
{
    msTicks++;
}

uint32_t get_tick(void)
{
    return msTicks;
}

void My_Delay_ms(uint32_t ms)
{
    uint32_t start = get_tick(); // now time
    while ((get_tick() - start) < ms)
        ; // stuck ms
}
```
## `include/usart.h`
```
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
```

## `drivers/usart.c`
```
#include "usart.h"



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
    USART1->TDR = c;
}
```
## `include/dma.h`
```
#ifndef DMA_H_
#define DMA_H_

#include "stm32f072xb.h"

void DMA_Init(DMA_TypeDef *DMAx, uint8_t channel, uint32_t periph_addr, uint32_t mem_addr, uint16_t data_len);

uint16_t DMA_Get_Write_Index(DMA_TypeDef *DMAx, uint8_t channel, uint16_t total_size);

#endif
```


## `drivers/dma.c`
```
#include "dma.h"

/**
 * @brief  初始化 DMA 通道 (使用 DMAx 指標與通道索引)
 * @param  DMAx     : DMA 控制器指標 (如 DMA1)
 * @param  channel  : 通道索引 (如 2 代表 Channel 3)
 * @param  periph_addr : 外設 RDR/TDR 位址
 * @param  mem_addr    : 記憶體緩衝區位址
 * @param  data_len    : 緩衝區長度
 */
void DMA_Init(DMA_TypeDef *DMAx, uint8_t channel, uint32_t periph_addr, uint32_t mem_addr, uint16_t data_len)
{
    DMAx->CH[channel].CPAR = periph_addr; // 設定外設地址
    DMAx->CH[channel].CMAR = mem_addr;    // 設定記憶體地址
    DMAx->CH[channel].CNDTR = data_len;   // 設定傳輸數量

    /* CCR 配置: 記憶體遞增(MINC), 循環模式(CIRC), 開啟(EN) */
    DMAx->CH[channel].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);

    // // 5. DMA setting
    // DMA1->CH[2].CPAR = (uint32_t)&(USART1->RDR);
    // DMA1->CH[2].CMAR = (uint32_t)rx_buffer;
    // DMA1->CH[2].CNDTR = RX_BUF_SIZE;
    // DMA1->CH[2].CCR = (1UL << 7) | (1UL << 5) | (1UL << 0);
}

/**
 * @brief  獲取指定 DMA 通道的當前寫入位置
 */
uint16_t DMA_Get_Write_Index(DMA_TypeDef *DMAx, uint8_t channel, uint16_t total_size)
{
    return (uint16_t)(total_size - DMAx->CH[channel].CNDTR);
}
```









