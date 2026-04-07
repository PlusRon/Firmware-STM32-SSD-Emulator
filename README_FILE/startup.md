# 開機程式 startup.c 實作
```
#include <stdint.h>

/* import the defined-symbol(external variable) from linker_script.ld */
/* Notice： they are the address-label, use their address in C-langue */
extern uint32_t _estack;   // 堆疊頂端位址
extern uint32_t _la_data;  // .data 在 FLASH 中的載入位址 (LMA)
extern uint32_t _etext;   // In Flash, be the start-point of .data for LMA
extern uint32_t _sdata;   // In RAM, be the start-point of .data for VMA
extern uint32_t _edata;   // In RAM, be the end-point of .data
extern uint32_t _sbss;    // In RAM, be the start-point of .bss
extern uint32_t _ebss;    // In RAM, be the end-point of .bss

/* declare the external main function */
extern int main(void);

/* 3. 定義預設中斷處理器 (保險絲) */
void Default_Handler(void) {
    while (1);
}

/* 4. 使用 Weak Alias 宣告中斷，增加相容性 */
/* 這樣你在 main.c 寫了同名函式，這裡就會被自動取代 */
void Reset_Handler(void);
void SysTick_Handler(void)    __attribute__ ((weak, alias ("Default_Handler")));
void USART1_IRQHandler(void)  __attribute__ ((weak, alias ("Default_Handler")));

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
    // uint32_t *src = &_etext;   // LMA
    // A. 搬運 .data 段：從 FLASH (LMA) 搬到 RAM (VMA)
    uint32_t *src = &_la_data;
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
    (uint32_t)&_estack,                            // 0. 初始堆疊指標 (MSP): RAM 頂端 (16KB)
    (uint32_t)Reset_Handler,                // 1. Reset 向量：CPU 開機後的跳轉起點
    (uint32_t)Default_Handler,  // 2. NMI
    (uint32_t)Default_Handler,  // 3. HardFault
    0, 0, 0, 0, 0, 0, 0,        // 4-10. 保留
    (uint32_t)Default_Handler,  // 11. SVCall
    0, 0,                       // 12-13. 保留
    (uint32_t)Default_Handler,  // 14. PendSV
    (uint32_t)SysTick_Handler,  // 15. SysTick (非阻塞計時核心)
    /* 外設中斷 (按硬體手冊 IRQ 順序排列) */
    (uint32_t)Default_Handler,  // 16. WWDG (IRQ 0)
    (uint32_t)Default_Handler,  // 17. PVD
    (uint32_t)Default_Handler,  // 18. RTC
    (uint32_t)Default_Handler,  // 19. FLASH
    (uint32_t)Default_Handler,  // 20. RCC
    (uint32_t)Default_Handler,  // 21. EXTI0_1
    (uint32_t)Default_Handler,  // 22. EXTI2_3
    (uint32_t)Default_Handler,  // 23. EXTI4_15
    (uint32_t)Default_Handler,  // 24. TSC
    (uint32_t)Default_Handler,  // 25. DMA_CH1
    (uint32_t)Default_Handler,  // 26. DMA_CH2_3
    (uint32_t)Default_Handler,  // 27. DMA_CH4_5
    (uint32_t)Default_Handler,  // 28. ADC1_COMP
    (uint32_t)Default_Handler,  // 29. TIM1_BRK_UP_TRG_COM
    (uint32_t)Default_Handler,  // 30. TIM1_CC
    (uint32_t)Default_Handler,  // 31. TIM2
    (uint32_t)Default_Handler,  // 32. TIM3
    (uint32_t)Default_Handler,  // 33. TIM6_DAC
    (uint32_t)Default_Handler,  // 34. TIM7
    (uint32_t)Default_Handler,  // 35. TIM14
    (uint32_t)Default_Handler,  // 36. TIM15
    (uint32_t)Default_Handler,  // 37. TIM16
    (uint32_t)Default_Handler,  // 38. TIM17
    (uint32_t)Default_Handler,  // 39. I2C1
    (uint32_t)Default_Handler,  // 40. I2C2
    (uint32_t)Default_Handler,  // 41. SPI1
    (uint32_t)Default_Handler,  // 42. SPI2
    (uint32_t)USART1_IRQHandler // 43. USART1 (STM32F072 的 IRQ 27)
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

## 四、系統控制區 SCB (System Control Block)
#### 查閱 Programming Manual
- #### [Cortex-M0 Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)
#### 計算出 AIRCR 暫存器位址
- ARM 將 **系統控制相關的暫存器** 集中在 **SCB (System Control Block)** 區域
- SCB 的基底位址 (**Base Address**) 是 **0xE000 ED00**
  <img width="769" height="71" alt="image" src="https://github.com/user-attachments/assets/5f4dcfcf-4bee-49e0-a905-579dc841cae8"/>
- **AIRCR (Application Interrupt and Reset Control Register)** 的 **偏移量 (Offset)** 是 **0x0C**
  <img width="1303" height="981" alt="image" src="https://github.com/user-attachments/assets/5d9c7f76-c93c-4414-a040-5d5e6c6d52ab" />
- 計算結果 **AIRCR 暫存器位址** ： 0xE000 ED00 + 0x0C = **0xE000ED0C**

#### 寫入 0x05FA 密碼鑰匙 (VECTKEY)
- 韌體開發中的 **保護機制 (Unlock Sequence)、鑰匙機制 (Key Mechanism)**
- 將 AIRCR 暫存器的高 **16 位元（[31:16]）** 被設計為 VECTKEY，才能執行系統重啟
    - 若你寫入系統暫存器時，高 16 位元不等於 **`0x05FA`**，硬體會直接忽略這次寫入請求
    - 重置（Reset）是極危險的操作，若沒有 VECTKEY，任何錯誤的指標操作（Pointer Bug）意外掃到這個位址，都可能導致 SSD 頻繁重啟，造成資料毀損
      <img width="1302" height="992" alt="image" src="https://github.com/user-attachments/assets/39083b79-be3a-41be-b4c6-f0b22f26ba16" />
#### 設定第 2 位元 SYSRESETREQ 觸發 System Reset Request (系統層級重置請求)
- 當此位元被設定為 `1` 時，Cortex-M Kernel 會向 **外部重置控制器** 發出訊號，強制晶片除了 Debug 模組 以外的所有部分執行重置
- 熱啟動 (Warm Reset) : 會 重新載入中斷向量表，並重新執行 Reset_Handler()
- 當韌體偵測到不可恢復的邏輯錯誤（如 FTL 表損壞且無法透過 ECC 修復），最安全的做法就是 **立刻啟動 SYSRESETREQ**，能確保所有硬體狀態（SRAM, DMA, Flash Controller）回到初始狀態，避免錯誤擴散
    <img width="1126" height="354" alt="image" src="https://github.com/user-attachments/assets/ca2f5d1e-dbb9-4435-be49-c6ea4e9682d6" />



