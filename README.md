# Firmware-STM32-SSD-Emulator
Firmware-STM32-SSD-Emulator

## 一、開發環境 (Environment Setup)
本專案捨棄笨重的 IDE（如 Keil 或 STM32CubeIDE），採用業界底層開發常見的 GNU Arm Toolchain 與 CLI 工具鏈，掌握從編譯、連結到燒錄的完整硬體主控權
- #### Hardware Requirements
  |開發版 (Dev Board) | **STM32F072B-DISCOVERY** |
  |:---|:---|
  |**調試器 (Debugger)**|**ST-Link V2**|
  |**傳輸線 (Cable)** | **Mini-USB**|
- #### Software Toolchain
  |編譯器 (Compiler) | **GNU Arm Embedded Toolchain** |
  |:---|:---|
  |**建置工具 (Build Tool)**|**GNU Make**|
  |**燒錄、除錯 (Flashing & Debugging)** | **OpenOCD**|
- #### [Linux to STM32 開發環境建置](README_FILE/environment_setup.md)

## 二、硬體 (STM32) 啟動流程 (Boot startup Procedure)
### 查閱硬體邊界 (Memory Mapping)
根據 STM32F072 的 **[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)**，找出晶片的實體記憶體佈局
- FLASH (唯讀儲存) : 起始於 `0x08000000`，容量 **128 KB**
- SRAM (執行與存取) : 起始於 `0x20000000`，容量 **16 KB**
  <img width="1009" height="447" alt="image" src="https://github.com/user-attachments/assets/2ca2ff23-cefb-4a80-9369-6bccc6a4b9c1" />

### 撰寫 Linker Script 腳本
告訴連結器（Linker）如何將編譯好的目標檔案（.o）組合，並擺放到正確的記憶體位址
- `.ld` 是 Linker Script（連結器腳本）
  - 使用 **GNU Linker Command Language**（GNU 連結器命令語言） 
    | 特性 | C語言 | `linker_script.ld` |
    |:---: | :---: | :---: |
    | 目標 | 運算、邏輯控制 | 記憶體分布、段落分配 |
    |基本單位|函式(Function)、變數|段落(Section)、區域(Region)|
    | 關鍵字 | `if`,`while`,`struct`,`return` | `MEMORY`,`SECTIONS`,`KEEP`,`ALIGN` |
- 核心語法與關鍵字
  - `MEMORY` : 定義實體硬體的 **可用空間** 與 **權限**（`rx` 為唯讀執行，`rwx` 為讀寫執行）
  - `SECTIONS` : 定義程式碼段落（Sections）的 分門別類 與 擺放順序
  - `KEEP` : 強制連結器保留特定區段（如 **中斷向量表`KEEP(*(.isr_vector))`**），防止被 **Garbage Collection** 刪除
  - `ALIGN(4)` : 強迫資料從 **4 位元組對齊** 的位址開始，確保 **CPU 最高效率存取**
  - `ORIGIN`, `LENGTH` : 記憶體區塊的起始位址與大小
- #### [linker_script.ld 實作](README_FILE/linker_script.md)
  - **MEMORY** 程式碼區塊
  - **SECTIONS** 程式碼區塊

### 撰寫開機導引 (Bootloader)
STM32 通電（Power-on）或按下 Reset 鍵的那一刻，CPU 並不具備執行 C 語言環境的能力，必須撰寫 `startup.c` 來手動配置硬體環境，並引導系統進入 `main()`
- #### [startup.c 實作](README_FILE/startup.md)
  - **中斷向量表 (Vector Table)** : 根據 ARM Cortex-M 規範，CPU 啟動後會優先讀取 FLASH 起始處
  - **Reset_Handler 程式**： 資料搬家 與 環境初始化
  - **控制 SCB (System Control Block) 的 SYSRESETREQ** : 使異常時能自動恢復與軟體重置
  - **流程圖**
    ```mermaid
    graph TD
      %% 定義節點樣式：淺底深字
      classDef hardware fill:#e0e0e0,stroke:#333,stroke-width:2px,color:#000;
      classDef logic fill:#f0f7ff,stroke:#0056b3,stroke-width:2px,color:#000;
      classDef critical fill:#fff9c4,stroke:#fbc02d,stroke-width:2px,color:#000;
      classDef danger fill:#ffebee,stroke:#d32f2f,stroke-width:2px,color:#000;
  
      %% 階段 1: 硬體行為
      Start([Power On / Hardware Reset]) --> Vector["硬體加載 Vector Table<br/>1. 載入 MSP<br/>2. 載入 PC (Reset Vector)"]:::hardware
      Vector --> RH["執行 Reset_Handler"]:::logic
  
      %% 階段 2: C Runtime 初始化
      subgraph CRT ["C Runtime Environment Setup"]
          direction TB
          RH --> CopyData["搬移 .data 段 (LMA to VMA)<br/>從 Flash 複製初始值到 RAM"]:::logic
          DataCopy --> BSSClear["清零 .bss 段<br/>將未初始化變數區塊填 0"]:::logic
      end
  
      %% 階段 3: 應用程式
      CRT --> Main[["跳轉至 main()"]]:::critical
  
      %% 階段 4: 異常防護
      Main --> Error{main 結束?}:::danger
      Error -- 邏輯異常 --> Reset["系統軟體重置<br/>寫入 AIRCR (0xE000ED0C)"]:::danger
      Reset --> Wait([等待硬體重啟]):::danger
  
      %% 連結樣式
      linkStyle 0,1,2,3,4 stroke:#333,stroke-width:2px;
    ```
  - **UML**
    ```mermaid
    sequenceDiagram
      autonumber
      
      %% 強制設定配色方案
      %% 雖然時序圖對 classDef 支援有限，但透過調整 rect 顏色能達到最佳視覺效果
      
      participant HW as ARM Cortex-M 核心
      participant FL as Flash (LMA)
      participant RAM as SRAM (VMA)
      participant SC as AIRCR 暫存器
  
      Note over HW, RAM: --- 系統初始化 (Reset_Handler) ---
      
      %% 使用極淺藍色背景
      rect rgb(245, 250, 255)
          Note right of HW: 1. 複製 .data 初始值
          HW->>FL: 讀取 @_etext
          FL-->>RAM: 寫入 @_sdata ~ @_edata
      end
      
      %% 使用極淺黃色背景
      rect rgb(255, 254, 240)
          Note right of HW: 2. 清空 .bss 空間
          HW->>RAM: 填入 0 (@_sbss ~ @_ebss)
      end
  
      Note over HW, RAM: --- 進入應用層 ---
  
      HW->>HW: 呼叫 main()
      
      Note right of HW: 若 main() 意外結束...
  
      %% 使用極淺紅色背景
      rect rgb(255, 245, 245)
          Note right of HW: 3. 觸發異常重置
          HW->>SC: 寫入 0x05FA0004
          SC-->>HW: 執行 SYSRESETREQ
      end
    ```

## 三、主程式碼實作 (Implementation)
- #### 硬體抽象層 (`include/`)
  - #### [stm32f072xb.h 底層暫存器定義實作](README_FILE/hardware_abstraction.md)
- #### 應用邏輯層 (`src/`)
  - #### [LED (PC6) 亮滅實作](README_FILE/LED_flashing_main.md)
    ```mermaid
    graph TD
      %% 定義節點樣式：淺底深字 (High Contrast)
      classDef init fill:#f0f7ff,stroke:#0056b3,stroke-width:2px,color:#000;
      classDef loop fill:#fffde7,stroke:#fbc02d,stroke-width:2px,color:#000;
      classDef action fill:#ffffff,stroke:#333,stroke-width:2px,color:#000;
  
      Start([<b>Entry: main</b>]) --> Clock["<b>1. 開啟時脈 (Clock Enable)</b><br/>RCC-AHBENR bit 19 = 1"]:::init
      Clock --> Config["<b>2. 配置輸出模式 (GPIO Mode)</b><br/>GPIOC-MODER6 = 01 (Output)"]:::init
  
      subgraph MainLoop ["無限迴圈 (Infinite Loop)"]
          direction TB
          Config --> Toggle["<b>3. 翻轉電位 (Toggle LED)</b><br/>GPIOC-ODR XOR (1 << 6)"]:::loop
          Toggle --> Delay["<b>4. 軟體延時 (delay)</b><br/>執行 NOP 指令迴圈"]:::action
          Delay --> Toggle
      end
  
      %% 連結樣式
      linkStyle default stroke:#333,stroke-width:2px;
    ```

    ```mermaid
    sequenceDiagram
      autonumber
      participant CPU as 核心 (main.c)
      participant RCC as RCC 暫存器
      participant GPIO as GPIOC 暫存器
      participant Pin as 硬體接腳 (PC6)
  
      Note over CPU, Pin: --- 初始化階段 ---
  
      %% 淺藍區代表硬體配置
      rect rgb(245, 250, 255)
          CPU->>RCC: 開啟 GPIOC 時脈 (AHBENR |= 1<<19)
          CPU->>GPIO: 設定 MODER6 為 01 (輸出模式)
      end
  
      Note over CPU, Pin: --- 循環閃爍階段 ---
  
      %% 淺黃區代表迴圈動作
      loop 每一個週期 (While 1)
          rect rgb(255, 254, 240)
              CPU->>GPIO: 翻轉 ODR 位元 (XOR 0x40)
              GPIO->>Pin: 電位切換 (High / Low)
              CPU->>CPU: 執行延時函式 (delay)
          end
      end
    ```

## 四、自動化建置系統 STM32 Makefile 
為 STM32 (Cortex-M0) 建置一套具備 **增量編譯 (Incremental Build)**、**空間優化 (Space Optimization)** 與 **OpenOCD一鍵燒錄** 功能的 Makefile
- #### 核心硬體觀念：32 位元處理器與 16 位元指令
  - **32 位元處理器 (Cortex-M0)** ： 指的是 **資料寬度**。暫存器與 ALU 都是 32-bit，運算範圍達 $0$ 到 $2^{32}-1$
  - **16 位元指令集 (Thumb)** ： 指的是 **指令長度**。為了提高 **代碼密度 (Code Density)**，在有限的 Flash 空間內塞入更多指令
  - 運作原理 ： CPU 內部的 **指令解碼器 (Instruction Decoder)** 會在執行瞬間將 **16-bit Thumb 指令解譯為 32-bit** 的運算動作，兼顧空間與效能
- #### [Makefile 兩階段編譯、燒入實作](README_FILE/makefile.md)




