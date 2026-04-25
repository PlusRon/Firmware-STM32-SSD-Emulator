#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

// SSD 規格模擬：16 個物理區塊，每個區塊 32 Byte
#define TOTAL_BLOCKS   16       
#define BLOCK_SIZE     32       
#define INVALID_ADDR   0xFF     // 標記未映射狀態

// 物理區塊節點：用於 Linked List 管理空閒空間 (Free Pool)
typedef struct BlockNode {
    uint8_t id;                // 物理區塊 ID (PBA)
    struct BlockNode* next;    // 指向鏈結串列下一個空閒塊
} BlockNode_t;

// FTL 核心 API
void Storage_Init(void);                         // 初始化地圖與空閒鏈表
void Storage_Write(uint16_t lba, uint8_t* data); // 寫入：包含地圖分配
void Storage_Read(uint16_t lba, uint8_t* out_buf); // 讀取：包含地圖查找

#endif
