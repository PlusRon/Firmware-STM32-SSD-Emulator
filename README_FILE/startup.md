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
/* Execute this first function while CPU-reset or power-on */
void Reset_Handler(void) {

    // 1. move .data-section : copy initial-global-variable from Flash to RAM
    // Ex : int count = 100; this 100 have to copy from FLASH to RAM, then program to be read/write correctly
    uint32_t *src = &_etext;   // LMA
    uint32_t *dest = &_sdata;  // VMA

    while (dest < &_edata) {
        *dest++ = *src++;
    }

    // 2. initialize the .bss-section : clear the uninitial-global-variable to be zero
    // Ex : int buffer[10]; all of them be zero when power on
    dest = &_sbss;
    while (dest < &_ebss) {
        *dest++ = 0;
    }

    // 3. Finish system initialization, jump to main() function of C-language-layer
    main();


    /* --- Strengthen the protection machanism --- */
    // if main() unexpected ending, represent the the major anomaly occured in the system logic
    // Before enter in the dead-loop, we can :

    // A. Trigger the software-reset (Let system restart immediately)
    system_soft_reset(); 

    // B. Alternatively remain stationary, waiting for Watchdog detect to trigger the hardware reset
    while (1);
}

/* Interrupt Vector Table */
/* Use the attribute ensure the data is placed in the .isr_vector-region that specified by Linker Script */
__attribute__ ((section(".isr_vector")))
uint32_t vector_table[] = {
    0x20004000,               // 0. initial Main Stack Point(MSP) : top of RAM (0x20000000 + 16KB)
    (uint32_t)Reset_Handler   // 1. Reset Vector : CPU first jump to this location after power-on
};

```
## 一、中斷向量表 (Vector Table)
根據 ARM Cortex-M 規範，CPU 啟動後會優先讀取 Flash 起始處的兩個數值
- **MSP (Main Stack Pointer)** ： 定義堆疊的起點（通常位於 RAM 頂端）
- **Reset Vector** ： 開機後第一條指令的入口地址
```
__attribute__ ((section(".isr_vector")))   // 確保下方表格精確擺放在 Linker Script 指定的 .isr_vector 區段
uint32_t vector_table[] = {
    0x20004000,                            // 0. 初始堆疊指標 (MSP): RAM 頂端 (16KB)
    (uint32_t)Reset_Handler                // 1. Reset 向量：CPU 開機後的跳轉起點
};
```
## 二、實作 Reset_Handler ： 資料搬家與環境初始化
是韌體執行的起點，負責將 Linker Script 中規劃的 **虛擬地圖** 轉化為 **物理地址**
- 搬移 **.data** 段 (LMA to VMA)
  - 將具有初始值的全域變數 從 Flash (唯讀倉庫) 複製到 RAM (工作區)
- 初始化 **.bss** 段
  - 將未初始化的全域變數 **清零**，確保 **C 語言** 規範中的 **變數預設值為 0**
```
void Reset_Handler(void) {
    // 1. 複製 .data：從 Flash (&_etext) 到 RAM (&_sdata)
    uint32_t *src = &_etext;
    uint32_t *dest = &_sdata;
    while (dest < &_edata) {
        *dest++ = *src++;
    }

    // 2. 清零 .bss：初始化 RAM 空間
    dest = &_sbss;
    while (dest < &_ebss) {
        *dest++ = 0;
    }

    // 3. 環境就緒，進入主程式
    main();

    // 4. 保護機制：若 main 意外退出
    system_soft_reset(); 
    while (1);
}
```
## 三、防禦性編程 (Defensive Programming) : 自動恢復與軟體重置
在 SSD 韌體開發中，**資料可用性 (Data Availability)** 至關重要。若程式邏輯發生**異常意外從 `main()` 退出**，必須追求 **即時恢復 (Instant Recovery)**，而非空轉 `while(1)` 等待 看門狗(Watchdog)
- **軟體觸發重置 (AIRCR 操作)**
  - 接操作 ARM Cortex-M 內核的 **系統控制區 (SCB)**
  - 寫入 **AIRCR 暫存器** 要求晶片立即重啟
```
void system_soft_reset(void) {
    // AIRCR 暫存器位址: 0xE000ED0C
    uint32_t *aircr = (uint32_t *)0xE000ED0C;
    // 寫入 0x05FA 密碼鑰匙 (VECTKEY) 並設定第 2 位元 (SYSRESETREQ)
    *aircr = (0x05FA << 16) | (1 << 2);
    while(1);
}
```
- 為何不能只靠看門狗 (Watchdog)?
  - **重置延遲 (Reset Latency)** ： 若系統發生邏輯崩潰，原地踏步等看門狗咬人等待時間會太長
    - **Watchdog** 通常設定為數百 **毫秒** 的週期
    - 主動呼叫 **`system_soft_reset()`** 能將 **系統斷線時間 (Downtime)** 縮減至 **微秒** 等級

### 系統控制區 SCS (System Control Space) 的 AIRCR 暫存器
- #### 查閱 [Cortex-M0 Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)
  - ARM 將 **系統控制相關的暫存器** 集中在 **SCB (System Control Block)** 區域
  - SCB 的基底位址 (Base Address) 是 0xE000 ED00
    <img width="769" height="71" alt="image" src="https://github.com/user-attachments/assets/5f4dcfcf-4bee-49e0-a905-579dc841cae8" />

  - AIRCR (Application Interrupt and Reset Control Register) 的偏移量 (Offset) 是 0x0C
    <img width="1303" height="981" alt="image" src="https://github.com/user-attachments/assets/5d9c7f76-c93c-4414-a040-5d5e6c6d52ab" />


  - 計算結果 **AIRCR 暫存器位址** ： 0xE000 ED00 + 0x0C = **0xE000ED0C**

