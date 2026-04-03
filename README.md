# Firmware-STM32-SSD-Emulator
Firmware-STM32-SSD-Emulator

## 建立開發環境 (Environment Setup)
本專案捨棄笨重的 IDE（如 Keil 或 STM32CubeIDE），採用業界底層開發常見的 GNU Arm Toolchain 與 CLI 工具鏈，掌握從編譯、連結到燒錄的完整硬體主控權
- #### [Linux to STM32 開發環境建置](README_FILE/environment_setup.md)

## 硬體啟動流程與連結器腳本 (Boot Sequence & Linker Script)
#### 查閱硬體邊界 (Memory Mapping)
根據 STM32F072 的 **[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)**，找出晶片的實體記憶體佈局
- Flash (唯讀儲存) : 起始於 `0x08000000`，容量 **128 KB**
- SRAM (執行與存取) : 起始於 `0x20000000`，容量 **16 KB**
#### 撰寫 linker_script.ld 腳本
告訴連結器（Linker）如何將編譯好的目標檔案（.o）組合，並擺放到正確的記憶體位址
- 核心語法與關鍵字
  - `MEMORY` : 定義實體硬體的 **可用空間** 與 **權限**（`rx` 為唯讀執行，`rwx` 為讀寫執行）
  - `SECTIONS` : 定義程式碼段落（Sections）的擺放順序。
  - `KEEP` : 強制連結器保留特定區段（如 **中斷向量表**），防止被 **Garbage Collection** 刪除
  - `ALIGN(4)` : 強迫資料從 **4 位元組對齊** 的位址開始，確保 **CPU 最高效率存取**
- [Linker Script 實作](README_FILE/linker_script.md)
