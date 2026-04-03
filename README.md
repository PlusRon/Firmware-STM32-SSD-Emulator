# Firmware-STM32-SSD-Emulator
Firmware-STM32-SSD-Emulator

## 建立開發環境 (Environment Setup)
本專案捨棄笨重的 IDE（如 Keil 或 STM32CubeIDE），採用業界底層開發常見的 GNU Arm Toolchain 與 CLI 工具鏈，掌握從編譯、連結到燒錄的完整硬體主控權
- #### [Linux to STM32 開發環境建置](README_FILE/environment_setup.md)

## 硬體 (STM32) 啟動流程 (Boot Startup Procedure)
#### 查閱硬體邊界 (Memory Mapping)
根據 STM32F072 的 **[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)**，找出晶片的實體記憶體佈局
- FLASH (唯讀儲存) : 起始於 `0x08000000`，容量 **128 KB**
- SRAM (執行與存取) : 起始於 `0x20000000`，容量 **16 KB**
  <img width="1009" height="447" alt="image" src="https://github.com/user-attachments/assets/2ca2ff23-cefb-4a80-9369-6bccc6a4b9c1" />

#### 撰寫 Linker Script 腳本
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

#### 撰寫開機導引 (Boot Startup)
STM32 通電（Power-on）或按下 Reset 鍵的那一刻，CPU 並不具備執行 C 語言環境的能力，必須撰寫 `startup.c` 來手動配置硬體環境，並引導系統進入 `main()`
- #### [startup.c 實作](README_FILE/startup.md)
  - **中斷向量表 (Vector Table)** : 根據 ARM Cortex-M 規範，CPU 啟動後會優先讀取 FLASH 起始處
  - **`Reset_Handler()` 程式**： 資料搬家 與 環境初始化
  - **控制 SCB (System Control Block) 的 SYSRESETREQ** : 使異常時能自動恢復與軟體重置






