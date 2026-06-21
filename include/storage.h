#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* SSD 規格：4 Blocks, 16 Pages per Block, 32 Bytes per Page */
/* 總物理容量 = 4 * 16 * 32 = 2048 Bytes (2KB) */
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     16    
#define PAGE_SIZE           32    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) // 64 Pages

/* Over-Provisioning: 50% 空間留給 GC，僅開放 50% 給使用者 */
#define USER_PAGES          (TOTAL_PAGES / 2) // 32 Pages

#define INVALID_ADDR        0xFF

/* 新增頁面狀態 */
#define STATE_FREE          0  // 乾淨且可用
#define STATE_VALID         1  // 存有最新資料 (L2P 映射中)
#define STATE_DIRTY         2  // 舊資料，可被回收 (Invalid)

/* 物理頁面節點 */
typedef struct PageNode {
    uint8_t id;                
    struct PageNode* next;     
} __attribute__((aligned(4))) PageNode_t;

/* FTL 核心 API */
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);
void Storage_GC(void); // 暴露 GC 接口供手動或自動觸發

#endif
