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

    /* .data 段是「有初始值的全域變數」，存於 Flash，執行時搬到 RAM */
    .data : {
        . = ALIGN(4); /* 確保 VMA 起始位址對齊 */
        _sdata = .;         /* 紀錄在 RAM 的起始點 */
        *(.data*)
        . = ALIGN(4); /* 確保 VMA 結束位址對齊 */
        _edata = .;         /* 紀錄在 RAM 的結束點 */
    } > RAM AT > FLASH      /* AT 代表在 Flash 裡的原始位置 (LMA) */

    /* .bss 段是「未初始化的全域變數」，直接放在 RAM */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)           /* 【重要修正】收集「嘗試性定義」的全域變數 */
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}

```
