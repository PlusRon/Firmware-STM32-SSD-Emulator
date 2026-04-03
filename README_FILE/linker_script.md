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

/* 2. Define Order of Code-Section */
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
### 語言撰寫
- **記憶體(MEMORY)** 
  - 區塊名稱 : `FLASH`, `RAM`, `SRAM`, `ITCM`， (可改自行取的代號)
    - `FLASH` : 程式執行期間，該房間的家具無法移動，僅供查看
    - `RAM` : 程式執行期間，該房間的家具可移動，可讀寫
  - 區塊屬性 (程式 **執行階段**，CPU 對該區域的 **訪問權限**)
    |關鍵字|意義|功能說明|
    |:---:|:---:|:---|
    |`r`|Read-only|唯讀(FLASH常用)|
    |`w`|Read/Write|可讀寫(RAM必須有)|
    |`x`|Excutable|可執行(放程式碼的地方必須有)|
    |`a`|Allocatable|可分配空間|
    
- **段落(SECTIONS)**
  - **Regoin 結構名稱** : `.text`, `.data`, `.bss`，為慣用名稱，但連結器並不強制要求結構名稱 (可自訂)
    - `.text` :  在 FLASH 裡規劃一個 .text-region
      - 在所有編譯好的檔案中找到標籤為 `.isr_vector` 的檔案，放到 FLASH 最前面，且不准刪掉
        - `.isr_vector` 會在 `startup.c` 中定義的段落名稱，專門放 **中斷向量（Reset, NMI, HardFault 等位址）**
        - `KEEP(...)` : 因為連結器有 Garbage Collection 功能。如果連結器發現 `main()` 沒用到某個函式，為了省空間會把它刪掉
      - `*(.text*)`將標籤開頭是 `.text` 的機器碼(`.o`)排在向量表後面
        - 第一個 `*`（檔案過濾器）: 所有的輸入檔案(.o)
        - `.text*` : 不管是 `.text` 還是 `.text.什麼什麼`，全部一網打盡
      - `*(.rodata*)` : 常數
      - `. = ALIGN(4);` : 將存放 程式碼的 .text-region 起始處 與 結束處 對齊 4 的倍數 (FLASH 對齊)，以提高效率
  - **符號 (Symbols)** : Region 中的變數名
    - `_stext`, `_etext`, `_sdata`, `_edata`, `_estack` （可自訂）
    - 提供給 `startup.c` 使用的 地址變數名
  - **COMMON 段落** : 撰寫 `int x;` 卻沒給初值，它一定會進 .bss 嗎?
    - 某些 GCC 配置下，**未初始化的全域變數** 會先被放在一個叫 COMMON 的暫時段落
    - 最後 Linker 進行連結時，才會根據 Linker Script 的指示，把 COMMON 併入 .bss 段落中
    - 所以在 Linker Script 的 .bss 結構裡通常會寫 `*(COMMON)`
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

#### 檢查編譯後的某個目標檔案，其各個 抽屜(`.text`, `.data`, `.bss`) 的 大小與分配
```
arm-none-eabi-objdump -h build/main.o
```
- 顯示 .text, .data, .bss 各自佔用的空間
- `-h` 代表查看 Header（標頭）



