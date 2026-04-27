#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// SSD 規格模擬：4 個物理區塊，每個區塊 4 個 Page，每個 Page 32 Byte
// 總物理頁面數 = 16，與原版 16 Blocks 記憶體用量一致
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     4    
#define PAGE_SIZE           32    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) 

#define INVALID_ADDR        0xFF  

// 物理頁面節點：管理空閒頁面 (Free Pool)
typedef struct PageNode {
    uint8_t id;                // 物理頁面 ID (0~15)
    struct PageNode* next;     
} PageNode_t;

// FTL 核心 API
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);

#endif
