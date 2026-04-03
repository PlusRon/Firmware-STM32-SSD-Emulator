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
  - **目標** ： 想產出的 **檔案(project.elf)** 或一個 **任務名稱(clean)**
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
  - **wildcard** (搜集員)
    - `SRCS = $(wildcard app/*.c)` 會掃描硬碟，找出所有**真實存在的原始碼**
    - `$` ： 代表呼叫 Makefile 的函數
    - `wildcard` ： 函數的名稱
    - `app/*.c` ： 搜尋模式，`*` 萬用符號，代表 任何字元。找出 `app/` 資料夾下，所有 `.c` 結尾的檔案
  - **patsubst** (計畫員)
    - `OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))` 在記憶體中預先畫好一張 待產清單，**定義未來 `.o` 的存放位置**
    - 建立 **模式規則 (Pattern Rules)**，讓每個 .c 獨立編譯成一個 .o 檔
    - 當只改動 `main.c` 時，make 只會重新編譯 `main.o`，而不會去動其他的 `startup.o`，這能大幅縮短大型專案的編譯時間
- #### 連結器的斷捨離
  -  **`-ffunction-sections`**
    - 告訴編譯器把 **每個函式** 放在 **獨立的小隔間**
  - **`-nostdlib`**
    - 韌體開發中，需要 **極致控制權**，避開龐大的電腦標準庫，使二進位檔體積從 **幾百 KB** 壓縮至 **幾 KB**
  - **`-Wl,--gc-sections`**
    - 是 **Garbage Collection**
    - Linker 會掃描所有被獨立裝箱 (**-ffunction-sections**) 的函式，若發現沒被呼叫（例如沒用到的 `delay()`），就會直接將其剔除，節省 Flash
- #### 參數解析
  - **`$@`** : 為 **自動變數**，代表 **目標名稱**
  - **`$<`** : 為 **自動變數**，代表 **第一個依賴項**
  - **`-c`**  : 給 **GCC(`arm-none-eabi-gcc`)** 看的，只將 `.c` 原始碼 編譯成 **機器碼 `.o` 目標檔 (Object file)**，先不要連結
- #### 硬體操作指令
  - **`flash: $(BUILD_DIR)/project.bin`** : 當輸入 `make flash` 時 `make` 會先去檢查 `.bin` 是否為最新，若不是（你改了程式），會自動先跑編譯，編譯完才跑 openocd 燒錄，保證燒進去的一定是最新版
  - **`-c "program $< verify reset exit 0x08000000"`**
    - `-c` : 執行命令 (Command)，完整包裹 丟給 **OpenOCD** 去解析
      - OpenOCD 的指令通常很長且包含空格
        |特性|GCC 的 `-c`|OpenOCD 的 `-c`|
        |:---|:---|:---|
        |全稱|Compile|Command|
        |語法範例|`gcc -c main.c`|`openocd -c "command"`|
        |後面接什麼|通常不接東西，或接檔名|必須接引號字串|
        |功能|產生 .o 檔（機器碼轉譯）|執行硬體操作（燒錄、重啟、擦除）|
        |層次|編譯階段 (Building)|部署階段 (Flashing)|
    - `program $<` : 燒錄第一個依賴檔
    - `verify` : 燒完檢查對不對
    - `reset` : 讓晶片自動重啟，LED 才會立刻開始閃
    - `exit` : 燒完自動結束 OpenOCD，不會卡在終端機
## 三、從 ELF 到 BIN
- #### ELF (Executable and Linkable Format)
  - 包含 **機器碼** + **符號表** + **除錯資訊**
  - 檔案很大，是給 **除錯器（GDB）** 看的
- #### BIN (Binary)
  - 純粹的機器碼
  - **丟掉** 所有人類 **可讀標籤**，只留下 0 與 1
  - 能**燒進 STM32 Flash** 執行（從 **0x08000000** 開始）的東西





