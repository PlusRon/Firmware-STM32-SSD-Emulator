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
