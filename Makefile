# 1. 工具鏈與工具定義
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
OPENOCD = openocd

# 2. 路徑與檔案定義
BUILD_DIR = build
LINKER_SCRIPT = linker/stm32.ld
LOG_DIR = logs
LOG_FILE = $(LOG_DIR)/flash_$(shell date +%Y%m%d_%H%M%S).log


# [Step 1] 自動化搜尋抓取所有的 .c 檔案清單
SRCS = $(wildcard app/*.c) $(wildcard hw/*.c) $(wildcard drivers/*.c)

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
	@echo " --- Compiling $< -> $@ --- "
	$(CC) $(CFLAGS) -c $< -o $@

# 步驟 B：最後將所有編譯好的 .o 檔案連結成一個 ELF (包含符號表與除錯資訊)
$(BUILD_DIR)/project.elf: $(OBJS) $(LINKER_SCRIPT)
	@echo " --- Linking all object files into $@ --- "
	$(CC) $(LDFLAGS) $(OBJS) -o $@



# 步驟 C：從 ELF 轉成 BIN 純機器碼 (燒錄用)
$(BUILD_DIR)/project.bin: $(BUILD_DIR)/project.elf
	@echo " --- Tranform ELF ($@) to BIN ($<) --- "
	$(OBJCOPY) -O binary $< $@

# 步驟 D: OpenOCD 一鍵燒錄 (驗證 + 重啟)、擦除、清理指令
flash: $(BUILD_DIR)/project.bin
	@echo " --- Flashing $< into STM32 (verify and reset) --- "
	@mkdir -p $(LOG_DIR)
	$(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
	-c "program $< verify reset exit 0x08000000" 2>&1 | tee $(LOG_FILE)

erase:
	@echo " --- Erase BIN at STM32--- "
	$(OPENOCD) -f $(OCD_INTERFACE) -f $(OCD_TARGET) \
	-c "init; halt; stm32f0x mass_erase 0; exit"

clean:
	@echo " --- Clean  $(BUILD_DIR) at OS --- "
	rm -rf $(BUILD_DIR)

