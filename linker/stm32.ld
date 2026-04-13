/* 1. Define Memory Block */
MEMORY
{
    /* 128KB Flash, start from 0x08000000 */
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
    /* 16KB RAM, start from 0x20000000 */
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

/* 定義堆疊頂端 (RAM 結束位址)，供 startup.c 使用 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 2. Define Order of Code-Section */
SECTIONS
{
    /* .text-section have to start at FLASH head */
    .text : {
        . = ALIGN(4);         /* 4-bytes align be sure at the begin*/
        _stext = .;           /* record the start-point of .text-section */
        KEEP(*(.isr_vector))  /* put interrupt-vector-table at the FLASH's head will be sure */
        *(.text*)             /* put all c-code */
        *(.text.*)            /* 包含所有子段 */
        *(.rodata*)           /* put read-only-data(string, constant) */
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;           /* record the end-point of .text-section (will be the start-point of .data-section's LMA) */
    } > FLASH

    /* _la_data 是 .data 段在 FLASH 中的載入位址 (LMA) */
    /* 它會緊跟在 .text 之後 */
    _la_data = LOADADDR(.data);

    /* .data-section(be initialized global-variable), LMA stored in FLASH, and VMA be move into RAM while executing */
    .data : {
        . = ALIGN(4);         /* VMA's begin align be sure */
        _sdata = .;           /* record the start-point of .data-section in RAM */
        *(.data*)
        *(.data.*)
        . = ALIGN(4);         /* VMA's final align be sure */
        _edata = .;           /* record the end-point of .data-section in RAM */
    } > RAM AT > FLASH        /* AT represent the original position(LMA) at FLASH */

    /* .bss-section(be un-initialized global-variable), stored in RAM(LMA = VMA) directly */
    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(.bss.*)
        *(COMMON)             /* collect the global-variable of Tentative Definition */
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}