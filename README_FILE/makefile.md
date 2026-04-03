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
  - 依賴項比目標新，顯示 up to date，可跳過該編譯步驟，可大幅縮短開發迴圈時間
- #### 標準語法格式
  - **目標** ： 想產出的 **檔案(project.elf)** 或一個 **任務名稱(clean)**
  - **依賴項** ： 要完成這個目標，必須先存在的東西，**子函式**的概念
  - **指令** ： 必須**以 Tab 鍵開頭**，告訴系統 當依賴項準備好後，請執行這些 **Shell 指令**
      ```
      目標 (Target): 依賴項1 依賴項2 ...
      [Tab鍵] Shell 指令1
      [Tab鍵] Shell 指令2
      ```
## 二、關鍵函數與參數解析
- #### 自動化函數
  - **wildcard** (搜集員)
    - `SRCS = $(wildcard app/*.c)` 會掃描硬碟，找出所有**真實存在的原始碼**
  - **patsubst** (計畫員)
    - `OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))` 在記憶體中預先畫好一張 待產清單，**定義未來 `.o` 的存放位置**
- #### 連結器的斷捨離
  - **-nostdlib**
    - 韌體開發中，需要 **極致控制權**，避開龐大的電腦標準庫，使二進位檔體積從 **幾百 KB** 壓縮至 **幾 KB**
  - **-Wl,--gc-sections**
    - Linker 會掃描所有被獨立裝箱 (**-ffunction-sections**) 的函式，若發現沒被呼叫（例如沒用到的 `delay()`），就會直接將其剔除，節省 Flash
## 三、從 ELF 到 BIN
- #### ELF (Executable and Linkable Format)
  - 包含 **機器碼** + **符號表** + **除錯資訊**
  - 檔案很大，是給 **除錯器（GDB）** 看的
- #### BIN (Binary)
  - 純粹的機器碼
  - **丟掉** 所有人類 **可讀標籤**，只留下 0 與 1
  - 能**燒進 STM32 Flash** 執行（從 **0x08000000** 開始）的東西





