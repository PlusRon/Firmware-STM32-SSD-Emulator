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
