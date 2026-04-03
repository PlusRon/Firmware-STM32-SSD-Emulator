# 開機程式 startup.c 實作
```
#include <stdint.h>

/* import the defined-symbol(external variable) from linker_script.ld */
/* Notice： they are the address-label, use their address in C-langue */
extern uint32_t _etext;   // In Flash, be the start-point of .data for LMA
extern uint32_t _sdata;   // In RAM, be the start-point of .data for VMA
extern uint32_t _edata;   // In RAM, be the end-point of .data
extern uint32_t _sbss;    // In RAM, be the start-point of .bss
extern uint32_t _ebss;    // In RAM, be the end-point of .bss

/* declare the external main function */
extern int main(void);


/* A function that force trigger software reset (using the kernel registers of ARM Cortex-M) */
void system_soft_reset(void) {
    // Application Interrupt and Reset Control Register (AIRCR) from ARM Cortex-M's kernel
    // Key-0x05FA and SYSRESETREQ-bit will be writen in
    uint32_t *aircr = (uint32_t *)0xE000ED0C;
    *aircr = (0x05FA << 16) | (1 << 2);

    // Enter the infinite-loop to waiting for reset
    while(1);
}
/* 當 CPU 復位 (Reset) 或上電時執行的第一個函式 */
void Reset_Handler(void) {

    // 1. 搬移 .data 段：將「有初始值的全域變數」從 Flash 複製到 RAM
    // 例如：int count = 100; 這個 100 必須從 Flash 搬到 RAM，程式才能正確讀寫
    uint32_t *src = &_etext;
    uint32_t *dest = &_sdata;

    while (dest < &_edata) {
        *dest++ = *src++;
    }

    // 2. 初始化 .bss 段：將「未初始化的全域變數」清零
    // 例如：int buffer[10]; 這裡會確保它們開機時全是 0
    dest = &_sbss;
    while (dest < &_ebss) {
        *dest++ = 0;
    }

    // 3. 系統初始化完成，跳轉到 C 語言層級的 main 函式
    main();
    /* --- 保護機制強化 --- */

    // 如果 main 意外結束，代表系統邏輯發生重大異常
    // 在進入死迴圈之前，我們可以選擇：
    // A. 觸發軟體重置 (讓系統即刻重啟)
    system_soft_reset(); 

    // B. 或者維持原地踏步，等待看門狗 (Watchdog) 發現這裡沒有餵狗動作，進而發動硬體重置
    // 4. 保護機制：如果 main() 意外結束返回，讓 CPU 進入無窮迴圈（防止跑飛）
    while (1);
}

/* 中斷向量表 (Vector Table) */
/* 使用 attribute 確保這段資料被放在 Linker Script 指定的 .isr_vector 抽屜裡 */
__attribute__ ((section(".isr_vector")))
uint32_t vector_table[] = {
    0x20004000,              // 0. 初始堆疊指標 (MSP): RAM 頂端 (0x20000000 + 16KB)
    (uint32_t)Reset_Handler  // 1. Reset 向量：開機後 CPU 第一步會跳轉到這裡
};

```
## 
