#include "stm32f072xb.h"

/**
 * @brief simple delay function
 * Use __asm("nop") to prevent the compiler from optimizing away null-loop
 */
void delay(int32_t count) {
    while (count--) {
        __asm("nop");
    }
}

/**
 * @brief Entry of main
 */
int main(void) {
    /* 1. Open the GPIOC's Clock (IOPC EN is the 19-th bit in AHBENR) */
    RCC->AHBENR |= (1UL << 19);

    /* 2. Setting PC6 be the Output-Mode ( MODER6's 12, 13-th bit can control PC6's mode ) 
     * 00: Input, 01: Output, 10: Alternate, 11: Analog */
    GPIOC->MODER &= ~(3UL << 12); // Firstly, Clear [13:12]-bits to 00
    GPIOC->MODER |= (1UL << 12);  // Secondly, Set [13:12]-bits to 01 can be output-pin 

    
    
    /* ODR (Output Data Register) */
    // while (1) {
         /* 3. use XOR operation to invert PC6 Voltage */
    //     GPIOC->ODR ^= (1UL << 6); 

        /* Flashing by the delay() */
    //     delay(100000); 
    // }
    
    /* BSRR (Bit Set Reset Register)  */
    uint8_t led_state = 0; // software trace state

    while(1){
    	if(led_state == 0){
    		GPIOC->BSRR = (1UL << 6);
		led_state = 1;
	}else{
		GPIOC->BSRR = (1UL << 22);
		led_state = 0;
	}

	delay(100000);
    }

    return 0;
}
