#include "storage.h"
#include "usart.h"
#include <string.h>

// 1. 加入 aligned(4) 確保硬體存取安全
// 2. 避免使用 memset(sizeof)，改用明確的迴圈初始化，這是最保險的做法
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
__attribute__((aligned(4))) static uint8_t l2p_table[TOTAL_PAGES];
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

void Storage_Init(void) {
    // 分段初始化，避免 memset 造成的大範圍記憶體踩踏
    for (int i = 0; i < TOTAL_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 初始化鏈表
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
    }
    free_list_head = &page_pool[0];
    
    // 手動清除 Flash 內容，不使用 memset(sizeof)
    for(int b = 0; b < PHYSICAL_BLOCKS; b++) {
        for(int p = 0; p < PAGES_PER_BLOCK; p++) {
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
        }
    }
    
    UART_Send(USART1, "[FTL] SSD Logic Initialized.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    if (lba >= TOTAL_PAGES) return;

    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_page();
        if (pba == INVALID_ADDR) return;
        l2p_table[lba] = pba;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;

    for(int i=0; i<PAGE_SIZE; i++) {
        flash_memory[b][p][i] = data[i];
    }
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_PAGES || l2p_table[lba] == INVALID_ADDR) {
        for(int i=0; i<PAGE_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba_id = l2p_table[lba];
    uint8_t b = pba_id / PAGES_PER_BLOCK;
    uint8_t p = pba_id % PAGES_PER_BLOCK;
    
    for(int i=0; i<PAGE_SIZE; i++) {
        out_buf[i] = flash_memory[b][p][i];
    }
}
