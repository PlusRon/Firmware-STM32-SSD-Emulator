#include "include/systick.h"

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