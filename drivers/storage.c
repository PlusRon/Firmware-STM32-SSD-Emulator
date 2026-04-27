#include "storage.h"
#include "usart.h"
#include <string.h>

/* 物理空間與映射表 */
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
/* L2P 表僅需對應使用者可見的 LBA 數量 */
__attribute__((aligned(4))) static uint8_t l2p_table[USER_PAGES];
/* 物理頁面池包含所有頁面 (含預留空間) */
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

void Storage_Init(void) {
    /* 1. 初始化 L2P 表 (映射 64 個 LBA) */
    for (int i = 0; i < USER_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    /* 2. 初始化空閒頁面鏈表 (128 個物理頁面全部進入 Free List) */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
    }
    free_list_head = &page_pool[0];
    
    /* 3. 清除 Flash 內容 */
    for(int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for(int p = 0; p < PAGES_PER_BLOCK; p++) {
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
        }
    }
    
    UART_Send(USART1, "[FTL] 8KB SSD Initialized. User: 50%, Reserved: 50%.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    /* 邊界檢查：僅允許存取 0 ~ USER_PAGES-1 */
    if (lba >= USER_PAGES) {
        UART_Send(USART1, "[ERR] LBA Out of User Range\r\n");
        return;
    }

    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_page();
        if (pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL\r\n");
            return;
        }
        l2p_table[lba] = pba;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;

    for(int i = 0; i < PAGE_SIZE; i++) {
        flash_memory[b][p][i] = data[i];
    }
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    /* 邊界檢查 */
    if (lba >= USER_PAGES || l2p_table[lba] == INVALID_ADDR) {
        for(int i = 0; i < PAGE_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;
    
    for(int i = 0; i < PAGE_SIZE; i++) {
        out_buf[i] = flash_memory[b][p][i];
    }
}
