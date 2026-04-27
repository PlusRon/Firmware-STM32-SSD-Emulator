#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

/* SSD 規格：4 Blocks, 32 Pages per Block, 64 Bytes per Page */
/* 總物理容量 = 4 * 32 * 64 = 8192 Bytes (8KB) */
#define PHYSICAL_BLOCKS     4     
#define PAGES_PER_BLOCK     32    
#define PAGE_SIZE           64    
#define TOTAL_PAGES         (PHYSICAL_BLOCKS * PAGES_PER_BLOCK) // 128 Pages

/* Over-Provisioning: 50% 空間留給 GC，僅開放 50% 給使用者 */
#define USER_PAGES          (TOTAL_PAGES / 2) // 64 Pages

#define INVALID_ADDR        0xFF  

/* 物理頁面節點 */
typedef struct PageNode {
    uint8_t id;                
    struct PageNode* next;     
} __attribute__((aligned(4))) PageNode_t;

/* FTL 核心 API */
void Storage_Init(void);
void Storage_Write(uint16_t lba, uint8_t* data);
void Storage_Read(uint16_t lba, uint8_t* out_buf);

#endif
