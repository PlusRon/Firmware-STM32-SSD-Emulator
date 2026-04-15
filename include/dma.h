#ifndef DMA_H_
#define DMA_H_

#include "stm32f072xb.h"

void DMA_Init(DMA_TypeDef *DMAx, uint8_t channel, uint32_t periph_addr, uint32_t mem_addr, uint16_t data_len);

uint16_t DMA_Get_Write_Index(DMA_TypeDef *DMAx, uint8_t channel, uint16_t total_size);

#endif