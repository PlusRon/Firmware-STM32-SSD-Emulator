# Makefile 兩階段編譯、燒入實作
採用了 **兩階段編譯（`.c` → `.o` → `.elf`）**，並整合了 `wildcard` 與 `patsubst` 函數
```
# 1. 工具鏈與工具定義
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
OPENOCD = openocd

# 2. 路徑與檔案定義
BUILD_DIR = build
LINKER_SCRIPT = linker/stm32.ld

# [Step 1] 自動化搜尋抓取所有的 .c 檔案清單
SRCS = $(wildcard app/*.c) $(wildcard hw/*.c)

# [Step 2] 自動化轉換使用 patsubst 把 .c 清單轉換成對應的 .o 檔案清單，並放入 build 目錄 (app/main.c -> build/app/main.o)
# 邏輯：將 "app/main.c" 轉為 "build/app/main.o"
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

# 3. OpenOCD 設定檔路徑 (針對 STM32F0 系列)
OCD_INTERFACE = interface/stlink.cfg
OCD_TARGET    = target/stm32f0x.cfg

# 4. 編譯參數 (CFLAGS) - 讓程式碼 又小又快
# -ffunction-sections : 將每個函式獨立裝箱，方便 Linker 刪除未使用的代碼
CFLAGS  = -mcpu=cortex-m0 -mthumb -Iinclude -g -O0 -Wall -ffunction-sections -fdata-sections

# 5. 連結參數 (LDFLAGS) - 斷捨離
# -nostdlib: 拒絕電腦標準庫，掌握啟動程式 (Startup) 主導權
# --gc-sections: 垃圾回收，自動刪除沒被呼叫到的函式區塊 (配合 -ffunction-sections)
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -Wl,--gc-sections

# 5. 宣告偽目標
.PHONY: all clean flash erase

# --- 任務目標 (Targets) ---

# 預設目標
all: $(BUILD_DIR)/project.bin

# 步驟 A：模式規則 (Pattern Rule) - 定義如何將 任何一個 .c 變成 對應的 .o (不連結)
# % 代表匹配的檔名，$< 代表來源 .c，$@ 代表目標 .o
$(BUILD_DIR)/%.o: %.c
    @mkdir -p $(dir $@)
    @echo "Compiling $< -> $@"
    $(CC) $(CFLAGS) -c $< -o $@

# 步驟 B：最後將所有編譯好的 .o 檔案連結成一個 ELF (包含符號表與除錯資訊)
$(BUILD_DIR)/project.elf: $(OBJS) $(LINKER_SCRIPT)
    @echo "Linking all object files into $@"
    $(CC) $(LDFLAGS) $(OBJS) -o $@



# 步驟 C：從 ELF 轉成 BIN 純機器碼 (燒錄用)
$(BUILD_DIR)/project.bin: $(BUILD_DIR)/project.elf
    $(OBJCOPY) -O binary $< $@

# 步驟 D: OpenOCD 一鍵燒錄 (驗證 + 重啟)、擦除、清理指令
flash: $(BUILD_DIR)/project.bin
    $(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
    -c "program $< verify reset exit 0x08000000"

erase:
    $(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
    -c "init; halt; stm32f0x mass_erase 0; exit"

clean:
    rm -rf $(BUILD_DIR)

```
## 一、運作機制 ： Makefile 為 有向無環圖 (DAG)
- #### 依賴關係鏈 (Dependency Chain)
  - `all` → `project.bin` → `project.elf` → `main.o` → `main.c`
  - 當你下達 make all 時，會像剝洋蔥一樣，從最終產物往回追溯原料，由上而下的 樹狀結構
- #### 時間戳記檢查 (Timestamp Checking)
  - **增量編譯 (Incremental Build)** ： Make 會比對 main.c 與 main.o 的修改時間
  - 如果 **依賴檔 `.c`** 的更新時間新於 **目標檔 `.o`**，代表修改過程式碼，需針對該檔重新編譯
  - 依賴項比目標新，顯示 up to date，可跳過該編譯步驟，可大幅縮短開發迴圈時間(Iteration Time)
- #### 編譯參數與連結腳本統一管理
  - 確保了團隊開發環境的 **一致性 (Consistency)** 與 **可重複性 (Reproducibility)**
- #### 標準語法格式
  - **目標** ： 想產出的 **檔案(`project.elf`)** 或一個 **任務名稱(`clean)**
  - **依賴項** ： 要完成這個目標，必須先存在的東西，**子函式**的概念
  - **指令** ： 必須**以 Tab 鍵開頭**，告訴系統 當依賴項準備好後，請執行這些 **Shell 指令**
      ```
      目標 (Target): 依賴項1 依賴項2 ...
      [Tab鍵] Shell 指令1
      [Tab鍵] Shell 指令2
      ```
  - **變數定義 (Variables)** : 別把路徑或工具名稱 **寫死(Hard-code)**，使用變數方便日後更換 **clang** 或不同版本的工具鏈
    ```
    CC      = arm-none-eabi-gcc
    OBJCOPY = arm-none-eabi-objcopy
    OPENOCD = openocd

    BUILD_DIR = build
    LINKER_SCRIPT = linker/stm32.ld

    OCD_INTERFACE = interface/stlink.cfg
    OCD_TARGET    = target/stm32f0x.cfg
    
    CFLAGS  = -mcpu=cortex-m0 -mthumb -Iinclude -g -O0 -Wall -ffunction-sections -fdata-sections
    ```
  - **預設目標 (Default Target)** : 放在最上面，**不加參數直接輸入 make** 時，會跑 **第一個看到的目標**，作為 **進入點**
    ```
    all: $(BUILD_DIR)/project.bin
    ```
  - **虛擬目標 (Phony Targets)** : 不產出檔案的純指令，為了 **執行動作(如燒錄、清理)**，為了避免目錄下剛好有個檔案叫 clean 而導致衝突，通常會宣告 `.PHONY`
    ```
    # 5. 宣告偽目標
    .PHONY: all clean flash erase

    # 步驟 D: OpenOCD 一鍵燒錄 (驗證 + 重啟)、擦除、清理指令
    flash: $(BUILD_DIR)/project.bin
        $(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
        -c "program $< verify reset exit 0x08000000"
        
    erase:
        $(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
        -c "init; halt; stm32f0x mass_erase 0; exit"
        
    clean:
        rm -rf $(BUILD_DIR)
    ```
## 二、關鍵函數,參數解析,程式邏輯
- #### 自動化函數
  - **wildcard** (搜集員, 萬用字元)
    - `SRCS = $(wildcard app/*.c)` 會掃描硬碟，找出所有**真實存在的原始碼**
    - `$` ： 代表呼叫 Makefile 的函數
    - `wildcard` ： 函數的名稱
    - `app/*.c` ： 搜尋模式，`*` 萬用符號，代表 任何字元。找出 `app/` 資料夾下，所有 `.c` 結尾的檔案
  - **patsubst** (計畫員, Pattern Substitution)
    - `OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))`
      - 建立 **模式規則 (Pattern Rules)**，讓每個 `.c` 獨立編譯成一個 `.o` 檔(建立`.c` 與 `.o` 之間的一對一標籤關係)
      -  在記憶體中預先畫好一張 待產清單，**定義未來 `.o` 的存放位置**
      -  **併行編譯** : 可以下 `make -j4` 同時啟動多個核心來編譯不同的 `.o`，在大型 SoC 專案中是必須的
      -  **增量編譯** : 只針對有變動的單元進行處理
      -  **錯誤定位** : 如果編譯報錯，可以立刻知道是哪個單元（Object）出的問題，而不是在很長的連結過程中找 Bug
    - `$(BUILD_DIR)/%.o: %.c`
      - `%` : 通用模板，變數捕捉器
      - 當 make 需要 `build/app/main.o` 時，會透過模板 `% = app/main` 自動去找到 `app/main.c` 來編譯
      - 當只改動 `main.c` 時，make 只會重新編譯 `main.o`，而不會去動其他的 `startup.o`，這能大幅縮短大型專案的編譯時間
- #### 空間優化參數 - 連結器的斷捨離
  -  **`-ffunction-sections`**
    - **函式獨立裝箱**，告訴編譯器把 **每個函式** 放在 **獨立的小隔間** (因為預設編譯器會把**同一個 `.c` 檔裡的所有函式**塞進同一個 **大隔間 Section**)
  - **`-fdata-sections`**
    - **資料獨立裝箱**，針對 **全域變數**
  - **`-nostdlib`**
    - 韌體開發中，需要 **極致控制權**，避開龐大的電腦標準庫，使二進位檔體積從 **幾百 KB** 壓縮至 **幾 KB**
  - **`-Wl,--gc-sections`**
    - 是 **Garbage Collection**
    - Linker 會掃描所有被獨立裝箱 (**-ffunction-sections**) 的函式，若發現沒被呼叫（例如沒用到的 `delay()`），就會直接將其剔除，節省 Flash
- #### 參數解析
  - **`$@`** : 為 **自動變數**，代表 **目標名稱**
  - **`$<`** : 為 **自動變數**，代表 **第一個依賴項**
  - **`-c`**  : 給 **GCC(`arm-none-eabi-gcc`)** 看的，只將 `.c` 原始碼 編譯成 **機器碼 `.o` 目標檔 (Object file)**，先不要連結
  - **`$(dir $@)`** : 內建函數，取出目標檔案的**路徑**
  - **`-p`** : 在 mkdir 中，自動建立父目錄，深層路徑一次建到位，指令重複執行時 **不會因為目錄已存在而報錯** 導致編譯中斷
- #### 編譯器參數
  - **`CFLAGS  = -mcpu=cortex-m0 -mthumb -Iinclude -g -O0 -Wall -ffunction-sections -fdata-sections`**
    - `-mcpu=cortex-m0` : 定義處理器核心 ARM Cortex-M0，讓編譯器根據核心支援的指令（如：有無硬體乘法器）來優化程式碼
    - `-mthumb` : 強制使用 **Thumb 指令集（16位元為主）**，Cortex-M 系列主要運行於 **Thumb 模式以節省 Flash 空間**。若沒加此參數，編譯器可能產生 M0 **無法執行的 ARM (32-bit) 指令**，導致 **HardFault**
    - `-Iinclude` : `-I` 代表 Include，告訴編譯器到 `include/` 資料夾下尋找 `.h` 檔案，就可以在程式碼中直接寫 `#include "main.h"` 而**不必寫冗長的全路徑**
    - `-g` : 產生除錯資訊，在 ELF 檔案中加入**符號表（Symbol Table）**。當使用 **GDB** 或 **ST-Link Debugger** 時，電腦才能將 **機器碼對應到 C 語言原始碼行號**
    - `-O0` : 優化等級零，**不進行任何優化**。在開發階段極重要，因為優化（如 **-O2, -Os**）會重排或刪除程式碼，導致除錯時跳行或變數被自動抹除
    - `-Wall` : 開啟所有常用警告，代表 Warnings all，因為專業工程師會追求 Zero Warnings
- #### 連結器參數
  - **`LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib -Wl,--gc-sections`**
    - `-T $(LINKER_SCRIPT)` : `-T` 代表 Script，按照指定的連結腳本 (`stm32.ld`) 來擺放程式碼與資料
    - `-nostdlib` : 不要連結 C 語言的標準函式庫 (`libc`, `libm`)，準庫是為有作業系統（Linux/Windows）的環境設計的
    - `-Wl,--gc-sections` : `-Wl` 告訴 GCC 將後面的參數傳遞給底層的 Linker (ld)，`--gc-sections` 代表 Garbage Collection of Sections
- #### 硬體操作指令
  - **`flash: $(BUILD_DIR)/project.bin`** : 當輸入 `make flash` 時 `make` 會先去檢查 `.bin` 是否為最新，若不是（你改了程式），會自動先跑編譯，編譯完才跑 openocd 燒錄，保證燒進去的一定是最新版
  - **`$(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET)`**
    - `-f` : 讀取配置，代表 File，OpenOCD 需先載入 **橋樑 (Interface)** 和 **目標晶片 (Target)** 的設定，才能 **正確識別硬體**
    - `OPENOCD = openocd` : 執行檔，指定燒錄工具
    - `OCD_INTERFACE = interface/stlink.cfg` : 燒錄器（Debug Probe），STM32 開發板內建 ST-Link，`.cfg` 檔包含如何與 ST-Link 通訊的底層參數
    - `OCD_TARGET = target/stm32f0x.cfg` : 告訴 OpenOCD 目標晶片型號，不同系列的晶片（F0, F1, F4） **Flash 大小**、**暫存器位址**都不同，這份檔案定義了 **STM32F0 系列的硬體特性**
  - **`-c "program $< verify reset exit 0x08000000"`**
    - `-c` : 執行命令 (Command)，完整包裹 丟給 **OpenOCD** 去解析
      - OpenOCD 的指令通常很長且包含空格
      - 將複雜的 **硬體調適指令（JTAG/SWD）** 封裝進單一 Shell 動作中
        |特性|GCC 的 `-c`|OpenOCD 的 `-c`|
        |:---|:---|:---|
        |**全稱**|Compile|Command|
        |**語法範例**|`gcc -c main.c`|`openocd -c "command"`|
        |**後面接什麼**|通常不接東西，或接檔名|必須接引號字串|
        |**功能**|產生 .o 檔（機器碼轉譯）|執行硬體操作（燒錄、重啟、擦除）|
        |**層次**|編譯階段 (Building)|部署階段 (Flashing)|
    - `program $<` : 燒錄第一個依賴檔，把最新的 `.bin` 二進位檔案搬進 STM32 的 Flash（地址 **0x08000000**）
    - `verify` : 防止燒錄失敗，燒完後會讀取 FLASH 內容並與電腦上的 `.bin` 比對是否100% 一致，確保資料沒寫錯
    - `reset` : 對晶片下達**硬體復位**指令，讓晶片**自動重啟**，讓程式立刻**從 `main()` 開始跑**，LED 才會立刻開始閃
    - `exit` : 燒完自動 **切斷與晶片連線** 並 **關閉 OpenOCD** 程序，將終端機控制權還給使用者
## 三、從 ELF 到 BIN 燒錄晶片中
- #### ELF (Executable and Linkable Format)
  - 包含 **機器碼** + **符號表** + **除錯資訊**
  - 檔案很大，是給 **除錯器（GDB）** 看的
- #### BIN (Binary)
  - 純粹的機器碼
  - **丟掉** 所有人類 **可讀標籤**，只留下 0 與 1
  - 能**燒進 STM32 Flash** 執行（從 **0x08000000** 開始）的東西
- #### 燒錄
  - 使用 OpenOCD 透過 ST-Link 將 `.bin` 搬移到 Flash 起始位址
  - STM32 硬體上電後，會固定去 Flash 的前幾個位置讀取 **SP (Stack Pointer)** 與 **PC (Program Counter)**，這就是程式跑起來的瞬間

## 四、32 位元的處理器為何跑 16 位元的指令？
- #### 32 位元處理器 (資料寬度) ：
  - STM32 內部的 **暫存器（R0-R15）**、**內部匯流排**、**算術邏輯單元(ALU)** 都是 32 bits
  - 一次可以運算 $0$ 到 $2^{32}-1$ 的數字
- #### 16 位元指令集 Thumb (指令長度)
  - 為了 **Code Density（代碼密度）** 而縮小，在 Flash 空間極其珍貴的 MCU 上，16 位元的指令可以比 32 位元省下近一倍的空間
  - 效能不會損失，因為 CPU 內部的 **指令解碼器 (Instruction Decoder)** 會在執行瞬間將 16 位元的 Thumb 指令解譯為 32 位元的運算動作

## 五、結論
- #### 解決當機問題
  - 當系統當掉時，你必須看懂 **Map file（連結器產生的報告）**，對照 Linker Script 找問題
- #### 節省成本
  - 透過 `-nostdlib` 和 `–gc-sections` 把程式壓小，讓公司換成更便宜（Flash 更小）的晶片，省下數百萬成本
- #### 自動化生產
  -  Makefile 可以直接丟上伺服器做 **自動測試（CI/CD）**，不需要依賴 Keil 的圖形介面






