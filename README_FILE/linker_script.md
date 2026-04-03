## 連結器腳本 Linker_Script.ld 實作
```
/* 1. Define Memory Block */
MEMORY
{
    /* 128KB Flash, start from 0x08000000 */
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    /* 16KB RAM, start from 0x20000000 */
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

/* 2. Define Order of Code-Text Section */
SECTIONS
{
    /* .text-section have to start at FLASH head */
    .text : {
        . = ALIGN(4);         /* 4-bytes align be sure at the begin*/
        _stext = .;           /* record the start-point of .text-section */
        KEEP(*(.isr_vector))  /* put interrupt-vector-table at the FLASH's head will be sure */
        *(.text*)             /* put all c-code */
        *(.rodata*)           /* put read-only-data(string, constant) */
        . = ALIGN(4);
        _etext = .;           /* record the end-point of .text-section (will be the start-point of .data-section's LMA) */
    } > FLASH

    /* .data-section(be initialized global-variable), LMA stored in FLASH, and VMA be move into RAM while executing */
    .data : {
        . = ALIGN(4);         /* VMA's begin align be sure */
        _sdata = .;           /* record the start-point of .data-section in RAM */
        *(.data*)
        . = ALIGN(4);         /* VMA's final align be sure */
        _edata = .;           /* record the end-point of .data-section in RAM */
    } > RAM AT > FLASH        /* AT represent the original position(LMA) at FLASH */

    /* .bss-section(be un-initialized global-variable), stored in RAM(LMA = VMA) directly */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)             /* collect the global-variable of Tentative Definition */
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
```
#### 語言撰寫
- 記憶體(MEMORY)中的區塊名稱（可以改）
  - `FLASH`, `RAM`, `SRAM`, `ITCM`，自行取的代號
- 段落(SECTIONS)中的結構名稱 （可自訂）
  -`.text`, `.data`, `.bss`，為慣用名稱，但連結器並不強制要求結構名稱
- 符號 (Symbols) （可自訂）
  - `_stext`, `_etext`, `_sdata`, `_edata`, `_estack`
  - 提供給 `startup.c` 使用的 地址變數名
#### 為何 .data-section 需要同時定義 **VMA (虛擬位址)** 與 **LMA (載入位址)** ?
- 燒錄階段 (Static Storage)
  - 所有的 .text 與 .data 的初始值都儲存在 FLASH。這時 FLASH 是唯一的倉庫
- 啟動階段 (Move Process)
  - 按下 Reset 後，CPU 執行 `startup.c` 中的 `Reset_Handler()`
  - 根據 Linker Script 提供的符號（`_sidata`, `_sdata`, `_edata`），將變數初始值從 FLASH 搬移到 RAM
- 執行階段 (Execution)
  - 進入 main() 後，CPU 直接存取 RAM 中的變數
  - RAM 的存取速度遠高於 FLASH
#### 為何 SSD 韌體關鍵程式碼要跑在 RAM？
- FLASH 必須先 **擦除** 才能 **寫入**，且讀取速度較慢
- 為了讓 SSD 達極限讀寫效能，關鍵的 **FTL 查表** 與 **ECC 糾錯演算法** 都會像 .data 一樣，在**開機後搬移至 RAM（或 ITCM）中執行**

#### 檢查編譯後的某個目標檔案，其各個 抽屜 的 大小與分配
```
arm-none-eabi-objdump -h build/main.o
```
- 顯示 .text, .data, .bss 各自佔用的空間



