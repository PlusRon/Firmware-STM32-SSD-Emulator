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
