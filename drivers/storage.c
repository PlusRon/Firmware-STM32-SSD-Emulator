#include "storage.h"
#include "usart.h"

// [物理層模擬]：真正的資料儲存區
static uint8_t flash_memory[TOTAL_BLOCKS][BLOCK_SIZE];

// [邏輯層映射]：索引是 LBA，值是 PBA (Physical Block Address)
static uint8_t l2p_table[TOTAL_BLOCKS];

// [空間管理]：Linked List 結構
static BlockNode_t block_pool[TOTAL_BLOCKS];
static BlockNode_t* free_list_head = 0;

void Storage_Init(void) {
    // 1. 初始化 L2P 表，全部設為無效地址
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        l2p_table[i] = INVALID_ADDR;
    }

    // 2. 初始化空閒鏈表：將所有區塊串接起來
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        block_pool[i].id = i;
        block_pool[i].next = (i < TOTAL_BLOCKS - 1) ? &block_pool[i+1] : 0;
    }
    free_list_head = &block_pool[0];
    
    UART_Send(USART1, "[FTL] L2P & Linked List Ready.\r\n");
}

// 私有函數：從 Linked List 取出一個空閒塊 (Pop)
static uint8_t allocate_block(void) {
    if (free_list_head == 0) return INVALID_ADDR;
    uint8_t id = free_list_head->id;
    free_list_head = free_list_head->next;
    return id;
}

void Storage_Write(uint16_t lba, uint8_t* data) {
    // 邊界檢查：目前僅支援 16 個 LBA
    if (lba >= TOTAL_BLOCKS) {
        UART_Send(USART1, "[ERR] LBA out of range!\r\n");
        return;
    }

    // 若該 LBA 尚未映射，則分配一個實體塊
    if (l2p_table[lba] == INVALID_ADDR) {
        uint8_t pba = allocate_block();
        if (pba == INVALID_ADDR) {
            UART_Send(USART1, "[ERR] DISK FULL (No Free PBA)\r\n");
            return;
        }
        l2p_table[lba] = pba;
    }

    // 寫入資料到物理位置
    uint8_t target_pba = l2p_table[lba];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        flash_memory[target_pba][i] = data[i];
    }
    UART_Send(USART1, "[FTL] Write Done.\r\n");
}

void Storage_Read(uint16_t lba, uint8_t* out_buf) {
    if (lba >= TOTAL_BLOCKS || l2p_table[lba] == INVALID_ADDR) {
        UART_Send(USART1, "[ERR] Read Invalid/Unmapped LBA\r\n");
        // 若未映射，回傳全 0
        for(int i=0; i<BLOCK_SIZE; i++) out_buf[i] = 0;
        return;
    }

    uint8_t pba = l2p_table[lba];
    for (int i = 0; i < BLOCK_SIZE; i++) {
        out_buf[i] = flash_memory[pba][i];
    }
}
