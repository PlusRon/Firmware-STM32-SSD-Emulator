#include <stdint.h>

/* import the defined-symbol(external variable) from linker_script.ld */
/* Notice： they are the address-label, use their address in C-langue */
extern uint32_t _etext;   // In Flash, be the start-point of .data for LMA
extern uint32_t _sdata;   // In RAM, be the start-point of .data for VMA
extern uint32_t _edata;   // In RAM, be the end-point of .data
extern uint32_t _sbss;    // In RAM, be the start-point of .bss
extern uint32_t _ebss;    // In RAM, be the end-point of .bss

/* declare the external main function */
extern int main(void);


/* A function that force trigger software reset (using the kernel registers of ARM Cortex-M) */
void system_soft_reset(void) {
    // Application Interrupt and Reset Control Register (AIRCR) from ARM Cortex-M's kernel
    // Key-0x05FA and SYSRESETREQ-bit will be writen in
    uint32_t *aircr = (uint32_t *)0xE000ED0C;
    *aircr = (0x05FA << 16) | (1 << 2);

    // Enter the infinite-loop to waiting for reset
    while(1);
}
/* Execute this first function while CPU-reset or power-on */
void Reset_Handler(void) {

    // 1. move .data-section : copy initial-global-variable from Flash to RAM
    // Ex : int count = 100; this 100 have to copy from FLASH to RAM, then program to be read/write correctly
    uint32_t *src = &_etext;   // LMA
    uint32_t *dest = &_sdata;  // VMA

    while (dest < &_edata) {
        *dest++ = *src++;
    }

    // 2. initialize the .bss-section : clear the uninitial-global-variable to be zero
    // Ex : int buffer[10]; all of them be zero when power on
    dest = &_sbss;
    while (dest < &_ebss) {
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
    while (1);
}

/* Interrupt Vector Table */
/* Use the attribute ensure the data is placed in the .isr_vector-region that specified by Linker Script */
__attribute__ ((section(".isr_vector")))
uint32_t vector_table[] = {
    0x20004000,               // 0. initial Main Stack Point(MSP) : top of RAM (0x20000000 + 16KB)
    (uint32_t)Reset_Handler   // 1. Reset Vector : CPU first jump to this location after power-on
};

