# 連結器腳本 linker_script.ld 實作
## 一、程式碼 - 撰寫
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
  - **Regoin 結構名稱** : **.text**  **.data**  **.bss**，為慣用名稱，但連結器並不強制要求結構名稱 (可自訂)
    - `.text` :  在 FLASH 裡規劃一個 **.text-region**
      - 在所有編譯好的檔案中找到標籤為 `.isr_vector` 的檔案，放到 FLASH 最前面，且不准刪掉
        - `.isr_vector` 會在 `startup.c` 中定義的段落名稱，專門放 **中斷向量（Reset, NMI, HardFault 等位址）**
        - `KEEP(...)` : 因為連結器有 Garbage Collection 功能。如果連結器發現 `main()` 沒用到某個函式，為了省空間會把它刪掉
      - `*(.text*)` : 將標籤開頭是 **.text** 的機器碼(`.o`)排在向量表後面
        - 第一個 `*`（檔案過濾器）: 所有的輸入檔案(.o)
        - `.text*` : 不管是 **.text** 還是 **.text.什麼什麼**，全部一網打盡
      - `*(.rodata*)` : 唯讀資料 (常數、字串)
      - `. = ALIGN(4);` : 將存放 程式碼的 **.text-region** 起始處 與 結束處 對齊 4 的倍數 (FLASH 對齊)，以提高效率
      - `_etext = .;` : 取出經過對齊後的 **.text-region** 之結束位址，作為 下一個 存放 已初始化全域變數 **.data-region** 在 FLASH 中的起始位址對齊
      - `> FLASH` : **.text-region** 的 **LMA 等於 VMA**
    - `.data` : LMA 存放於 FLASH，程式執行時會透過 Reset_Handler 的函式將資料 VMA 搬移至 RAM (C 語言中，有初始值的全域變數，會被歸類到這裡)
      - `_sdata = .` : `.` 代表RAM 當前位置的地址，記錄下未來程式執行階段時，將會分配至 RAM 資料段之起點地址 `0x20000000`
      - `*(.data*)` : 收集所有 **初始值的全域變數**，分配空間給他們
      - `_edata = .` : 放完所有 初始值之全域變數的檔案資料後，再次記錄 **當前位置**。
        - `startup.c` 計算 `_edata - _sdata`，就知道總共要從 FLASH 搬多少 Byte 到 RAM 裡
      - `> RAM` : 執行地址 **VMA (Virtual Memory Address, 程式執行)**，因為 RAM 才能寫入資料，程式跑起來後，必須去 RAM 找這些變數
      - `AT > FLASH` : 載入地址 **LMA (Load Memory Address, 斷電、燒錄)**，告訴燒錄器，有初始值的全域變數在斷電時必須儲存在 FLASH
        ```
        處理大型專案時，有時會用 LOADADDR(.data) 函數抓取 AT 所指定的 FLASH 地址
        _sidata = LOADADDR(.data);  /* 抓取 .data 在 FLASH 裡的 倉庫地址 */
        ```
  - **符號 (Symbols)** : Region 中的變數名
    - `_stext`, `_etext`, `_sdata`, `_edata`, `_estack` （可自訂）
    - 提供給 `startup.c` 使用的 地址變數名
  - **COMMON 段落** : 撰寫 `int x;` 卻沒給初值，它一定會進 .bss 嗎?
    - 某些 GCC 配置下，**未初始化的全域變數** 會先被放在一個叫 COMMON 的暫時段落
    - 最後 Linker 進行連結時，才會根據 Linker Script 的指示，把 COMMON 併入 .bss 段落中
    - 所以在 Linker Script 的 .bss 結構裡通常會寫 `*(COMMON)`

## 二、程式碼 - 理論問題探討
#### 為何 .data-section 需要同時定義 **VMA (虛擬位址)** 與 **LMA (載入位址)** ?
- **燒錄階段 (Static Storage)**
  - FLASH 是唯一的倉庫，所有的 **.text** 與 **.data** 的初始值都儲存在 FLASH
    - **.text** ： 存放在 FLASH 前面
    - **.data** ： 緊跟在 **.text** 後面，也存在 FLASH 裡
  - 透過 **SWD/JTAG** 介面，使用 OpenOCD 或燒錄器完成
  - 燒錄器會下達 **特殊的硬體命令**（**FLASH Controller**）來 **解鎖 FLASH** 並寫入資料，這時不受 Linker Script 的權限屬性限制
- **啟動階段 (Move Process)**
  - 按下 Reset 後，CPU 執行 `startup.c` 中的 `Reset_Handler()`
  - 根據 Linker Script 提供的符號（`_sidata`, `_sdata`, `_edata`），跑 while 迴圈，將變數初始值從 FLASH 搬移到 RAM
  - 順便把 **.bss** 段，在 RAM 裡全部填 0
- **執行階段 (Execution)**
  - 搬家完成後，`Reset_Handler()` 才會呼叫 `main()`
  - 進入 `main()` (**Code in RAM**) 後，CPU 直接存取 RAM 中的變數
  - 程式碼執行時，此刻 FLASH 對 CPU 來說就是 **唯讀**，即使修改變數，FLASH 裡的原始值也不會變
  
#### 為何 SSD 韌體關鍵程式碼 .data 搬移並跑在 RAM？
- RAM 的存取速度遠高於 FLASH
- RAM 為電晶體構造，CPU 可以隨時用一個指令改寫某個位址
- 避開 I-Bus 爭用 ： 讓 **指令抓取專用 I-Bus**，**資料存取專用 D-Bus**
- FLASH 必須先 **擦除(Erase)** 指令，才能再執行 **寫入(Program)** 指令，且 **讀取速度較慢**
  - **擦除(Erase)** : 將 **0 變 1**，**釋放電子**，將所有 Cell 清空為全 1 的準備動作
  - **寫入(Program)** : 將 **1 變 0**，**注入電子**，選擇性地把某幾個 Cell 的 1 改成 0 來存入資訊
  - **無法單獨** 將已經為 **0 的位元變回 1**，，必須通過 **擦除整個區域** 還原
- 為了讓 SSD 達極限讀寫效能，利用 **RAM 的低延遲** 特性
  - 讓關鍵的 **FTL 查表** 與 **ECC 糾錯演算法** 像 .data 一樣，在**開機時從SSD搬移至 RAM（或 ITCM）中執行**
  - 達成高 IOPS 的硬體基礎

#### 檢查編譯後的某個目標檔案，其各個 抽屜(`.text`, `.data`, `.bss`) 的 大小與分配
```
arm-none-eabi-objdump -h build/main.o
```
- 顯示 **.text**  **.data**  **.bss** 各自佔用的空間
- `-h` 代表查看 Header（標頭）

## 三、計算機架構 - 硬體架構與記憶體映射 (Memory Mapping)
撰寫 Linker Script 時，定義的 `FLASH (rx)` 與 `RAM (rwx)`，不只是軟體設定，而是對應到微處理器底層的 **匯流排架構 (Bus Architecture)** 與 **位址解碼機制**
#### 改良型哈佛架構 (Modified Harvard Architecture)
- 多數 STM32 (ARM Cortex-M 晶片) 採用改良型哈佛架構，其核心特徵在於物理路徑 **完全獨立**，**程式碼** 和 **資料** 分開走
  - **指令匯流排 (I-Bus)** ： 物理線路直接連接至 Internal Flash，CPU 透過此路徑進行 **取指 (Fetch)**，Flash 定義為 (rx) 為單向
  - **資料匯流排 (D-Bus)** ： 物理線路直接連接至 SRAM，CPU 透過此路徑進行變數的 **讀取與寫入 (LDR 從記憶體讀進暫存 / STR 從暫存器寫進記憶體)**
- **零爭用 (Zero Contention)**
  - 讓 CPU 能夠在 **左手拿指令** 的同時， **右手拿資料**，在硬體電路上互不干擾，實現 **真正的平行處理**
#### STM32 vs. 現代 PC (x86)
PC 也有 I-Cache 跟 D-Cache，不也是分開的嗎？
|比較維度|STM32 (純哈佛架構)|現代 PC / x86 (馮紐曼架構)|
|:---|:---|:---|
|外部物理路徑|分開，擁有獨立的 Flash 與 RAM 匯流排|統一，指令與資料共用**一條主記憶體匯流排**(程式碼和資料混在一起)|
|平行處理手段|硬體隔離，先天具備兩條水管|邏輯分流，依靠 **分裂式 L1 Cache (I-Cache / D-Cache)**|
|執行確定性|極高，每次**存取時間固定**，無抖動|具隨機性，效能高度依賴 **快取命中率 (Cache Hit Rate)**|
|適用場景|追求精確時序的 **SSD 韌體控制器**|追求**極致吞吐量的通用運算**
- PC 是 表面分開，底層合一
  - CPU 核心內部，模擬了哈佛架構的行為
  - 但只要發生 **Cache Miss**，**指令 與 資料** 仍必須排 **隊通過同一條物理匯流排**
- STM32 是 從頭到尾都分開
  - 確保了 FTL 演算法在處理高併發讀寫時，不會因為抓取下一行指令而產生任 1 個時脈週期的延遲

#### 記憶體映射與位址解碼 (Address Decoding)
CPU 只認得 位址 (Address)，硬體設計者要透過位址解碼電路，將不同的物理晶片掛載到特定位址
- **0x0800 0000 區間** ： 硬體設計者解碼（Decode）連接至 FLASH 晶片 (預設 `rx`)
- **0x2000 0000 區間** ： 連接至 SRAM 晶片 (預設 `rwx`)
```
* 在 .ld 裡定義權限中沒有 w 
當 CPU 執行 STR 指令試圖寫入 0x0800 0000 時，Flash 控制器會因為硬體電路限制直接拒絕
這在架構上確保了「 程式碼段 (.text) 」不會被意外的指標錯誤 (Pointer Bug) 所篡改
```
#### 系統安全防護：MPU (Memory Protection Unit)
在 STM32 中，可以進一步啟用 MPU 來強化 Linker Script 的屬性，使在 **Bare-metal** 環境下也能實現如 **OS 級別的記憶體隔離保護**
- STM32 異常捕獲 ： 若發生非法寫入，MPU 會觸發 **Memory Management Fault**，
- 對標 Linux ： 在 Linux 系統的 MMU 中對應為著名的 **Segmentation Fault**

### 程式碼
```
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
```
