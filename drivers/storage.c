#include "storage.h"
#include "usart.h"
#include <string.h>

/* 物理空間與映射表 */
__attribute__((aligned(4))) static uint8_t flash_memory[PHYSICAL_BLOCKS][PAGES_PER_BLOCK][PAGE_SIZE];
/* L2P 表僅需對應使用者可見的 LBA 數量 */
__attribute__((aligned(4))) static uint8_t l2p_table[USER_PAGES];
/* 物理頁面池包含所有頁面 (含預留空間) */
__attribute__((aligned(4))) static PageNode_t page_pool[TOTAL_PAGES];

/* 新增：追蹤每個物理頁面 (PBA) 的狀態 */
__attribute__((aligned(4))) static uint8_t page_status[TOTAL_PAGES];

static PageNode_t* free_list_head = 0; 

/* 輔助函數：將頁面丟回 Free List 前端 */
static void release_to_free_list(uint8_t pba) {
    page_status[pba] = STATE_FREE;
    page_pool[pba].next = free_list_head;
    free_list_head = &page_pool[pba];
}

void Storage_Init(void) {
    /* 1. 初始化 L2P 表 (映射 64 個 LBA) */
    for (int i = 0; i < USER_PAGES; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    /* 2. 初始化空閒頁面鏈表 (128 個物理頁面全部進入 Free List) */
    for (int i = 0; i < TOTAL_PAGES; i++) {
        page_pool[i].id = (uint8_t)i;
        page_pool[i].next = (i < TOTAL_PAGES - 1) ? &page_pool[i+1] : 0;
        page_status[i] = STATE_FREE; // 初始化全部為 Free
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
    
    UART_Send(USART1, "[FTL] 2KB SSD Initialized. User: 50%, Reserved: 50%. Out-of-place Write Enabled.\r\n");
}

static uint8_t allocate_page(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

/* 簡易垃圾回收：抹除所有 DIRTY 頁面並還給 Free List */
void Storage_GC(void) {
    UART_Send(USART1, "[GC] Starting Garbage Collection...\r\n");
    int reclaimed = 0;
    for (int i = 0; i < TOTAL_PAGES; i++) {
        if (page_status[i] == STATE_DIRTY) {
            // 模擬抹除動作：將資料填回 0xFF
            uint8_t b = i / PAGES_PER_BLOCK;
            uint8_t p = i % PAGES_PER_BLOCK;
            for(int i = 0; i < PAGE_SIZE; i++) {
                flash_memory[b][p][i] = 0xFF;
            }
            
            // 還給空閒鏈表
            release_to_free_list(i);
            reclaimed++;
        }
    }
    UART_Send(USART1, "[GC] Finished. Reclaimed pages.\r\n");
}



void Storage_Write(uint16_t lba, uint8_t* data) {
    /* 邊界檢查：僅允許存取 0 ~ USER_PAGES-1 */
    if (lba >= USER_PAGES) {
        UART_Send(USART1, "[ERR] LBA Out of User Range\r\n");
        return;
    }

    /* 1. 每次寫入都先嘗試分配新頁面 */
    uint8_t new_pba = allocate_page();

    /* 2. 如果沒有空閒頁面，觸發 GC */
    if (new_pba == INVALID_ADDR) {
        Storage_GC();
        new_pba = allocate_page();
        if (new_pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL ! Out of Physical Space!\r\n");
            return;
        }
    }

    /* 3. 處理舊資料：如果 LBA 之前有對應的 PBA，標記舊 PBA 為 DIRTY */
    if (l2p_table[lba] != INVALID_ADDR) {
        uint8_t old_pba = l2p_table[lba];
        page_status[old_pba] = STATE_DIRTY;
        // 注意：這裡不馬上釋放回 Free List，因為 Flash 需要 Block Erase 才能再寫
    }

    /* 4. 更新 L2P 與狀態 */
    l2p_table[lba] = new_pba;
    page_status[new_pba] = STATE_VALID;

    /* 5. 執行物理寫入 */
    uint8_t b = new_pba / PAGES_PER_BLOCK;
    uint8_t p = new_pba % PAGES_PER_BLOCK;

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
