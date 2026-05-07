## Bare-metal SSD Controller Simulator (ARM Cortex-M0)
Hand-authored bare-metal **boot sequence** and Linux-based build system. Implemented an **FTL** with **L2P** mapping and **GC** logic. Features a custom **HAL** and **interrupt-driven UART Ring Buffer** utilizing **DMA, IDLE** detection, and hardware **flow control** for robust **NVMe-like** protocol parsing and **ORE** recovery

從 **Linker Script** 與 **Startup** 純手寫開機流程，在 Linux 環境下建構自動化建置系統，並自主研發包含 **L2P 映射** 與 **GC** 機制的 **FTL 演算法**；底層架構整合自定義 **HAL** 與 **非阻塞式 SysTick**，並結合 **中斷驅動 (Interrupt-driven)** 架構搭配 **DMA、IDLE** 偵測 與 硬體流控技術，建構具備 **ORE 恢復機制** 與 **NVMe-like** 協定解析的高效能 **UART** 非同步通訊**環形緩衝區 (Ring Buffer)** 架構。


## Outline
#### [開發環境 (Environment Setup)](#一開發環境-Environment-Setup)
- [Hardware Requirements](#Hardware-Requirements)
- [Software Toolchain](#Software-Toolchain)
- [Linux to STM32 開發環境建置](#Linux-to-STM32-開發環境建置)
#### [STM32硬體啟動流程 (Startup Procedure)](#二STM32硬體啟動流程-Startup-Procedure)
- [查閱核心開發文件](#查閱核心開發文件)
- [撰寫腳本 linker_script.ld 實作](#linker_scriptld-實作)
- [撰寫開機導引 (Bootloader) startup.c 實作](#startupc-實作)
#### [主程式碼實作 (Implementation)](#三主程式碼實作-Implementation)
- [硬體抽象層：stm32f072xb.h 底層暫存器定義實作](#stm32f072xbh-底層暫存器定義實作)
- [應用邏輯層](#應用邏輯層-app)
  - [GPIO 原子性操作 (BSRR)、SysTick 非阻塞時基系統](#GPIO-原子性操作-BSRRSysTick-非阻塞時基系統)
  - [UART 非阻斷式、非同步收發資料處理 (UART、DMA、Ring Buffer、IDLE、Flow Control、System Tick)](#UART-非阻斷式非同步收發資料處理-UARTDMARing-BufferIDLEFlow-ControlSystem-Tick)
  - [NVMe-like 通訊協定規範 (Protocol Specification)：Producer Consumer model、Checksum、Endianness、ORE、Unit Testing](#模擬-NVMe-通訊協定規範-Protocol-SpecificationProducer-Consumer-modelChecksumEndiannessOREUnit-Testing)
  - [FTL 之 虛擬實體位址映射表(L2P)、異地更新(Out-of-place Update)、垃圾回收(Garbage Collection)、Over-Privisioning](#FTL-之-虛擬實體位址映射表L2P異地更新Out-of-place-Update垃圾回收Garbage-CollectionOver-Privisioning)
#### [自動化建置系統 STM32 Makefile](#四自動化建置系統-STM32-Makefile)
#### [除錯 (Debugging)](#五除錯-Debugging)

## 一、開發環境 (Environment Setup)
本專案在 無 IDE（如 Keil 或 STM32CubeIDE），採用業界底層開發常見的 GNU Arm Toolchain 與 CLI 工具鏈，掌握從編譯、連結到燒錄的完整硬體主控權
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
- #### [Linux to STM32 開發環境建置](README_FILE/env/environment_setup.md)

## 二、STM32硬體啟動流程 (Startup Procedure)
- #### 查閱核心開發文件
  |查找目標|建議手冊|關鍵章節 (Keywords)|
  |:---:|:---|:---|
  |各周邊 **暫存器位元 (Bit)** 定義|[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)|各周邊(Peripheral) 章節末尾的 **Register description**|
  |**時脈樹 (Clock Tree)** 頻率|[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)|**Reset and clock control (RCC)**/Clock tree、HSI clock|
  |引腳 **複用功能 (AF)** 對照表|[Datasheet (DS9826)](https://www.st.com/resource/en/datasheet/stm32f072c8.pdf)|Pinouts and pin descriptions / **Alternate functions**|
  |**系統控制** 相關的 **暫存器**|[Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)|**System control block (SCB)**/AIRCR|
  |處理器異常與中斷架構|[Programming Manual (PM0215)](https://www.st.com/resource/en/programming_manual/pm0215-stm32f0-series-cortexm0-programming-manual-stmicroelectronics.pdf)|Exception model / NVIC|

- **鎖定硬體邊界 (Memory Mapping)** ：根據 STM32F072 的 **[Reference Manual (RM0091)](https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)**，找出晶片的實體記憶體佈局
  - FLASH (唯讀儲存) : 起始於 `0x08000000`，容量 **128 KB**
  - SRAM (執行與存取) : 起始於 `0x20000000`，容量 **16 KB**
      <img width="1009" height="447" alt="image" src="https://github.com/user-attachments/assets/2ca2ff23-cefb-4a80-9369-6bccc6a4b9c1" />

- **撰寫 Linker Script 腳本**：告訴連結器（Linker）如何將編譯好的目標檔案（`.o`）組合，並擺放到正確的記憶體位址
   - #### [linker_script.ld 實作](README_FILE/BootLoader/linker_script.md)
     - **MEMORY** 程式碼區塊
     - **SECTIONS** 程式碼區塊
   - `.ld` 是 Linker Script（連結器腳本），使用 **GNU Linker Command Language**（GNU 連結器命令語言）
    
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


- **撰寫開機導引 (Bootloader)**：STM32 通電（Power-on）或按下 Reset 鍵的那一刻，CPU 並不具備執行 C 語言環境的能力，必須撰寫 `startup.c` 來手動配置硬體環境，並引導系統進入 `main()`
  - #### [startup.c 實作](README_FILE/BootLoader/startup.md)
    - **中斷向量表 (Vector Table)** : 根據 ARM Cortex-M 規範，CPU 啟動後會優先讀取 FLASH 起始處
    - **Reset_Handler 程式**： 資料搬家 與 環境初始化
    - **控制 SCB (System Control Block) 的 SYSRESETREQ** : 使異常時能自動恢復與軟體重置
  - #### UML
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
        
        %% 使用深藍色背景
        rect rgb(0, 0, 139)
            Note right of HW: 1. 複製 .data 初始值
            HW->>FL: 讀取 @_etext
            FL-->>RAM: 寫入 @_sdata ~ @_edata
        end
        
        %% 使用深紫色背景
        rect rgb(148, 0, 211)
            Note right of HW: 2. 清空 .bss 空間
            HW->>RAM: 填入 0 (@_sbss ~ @_ebss)
        end
    
        Note over HW, RAM: --- 進入應用層 ---
    
        HW->>HW: 呼叫 main()
        
        Note right of HW: 若 main() 意外結束...
    
        %% 使用靛藍色背景
        rect rgb(75, 0, 130)
            Note right of HW: 3. 觸發異常重置
            HW->>SC: 寫入 0x05FA0004
            SC-->>HW: 執行 SYSRESETREQ
        end
      ```
## 三、主程式碼實作 (Implementation)
- #### 硬體抽象層 (`include/`)
  - #### [stm32f072xb.h 底層暫存器定義實作](README_FILE/Main/hardware_abstraction.md)
- #### 應用邏輯層 (`app/`)
  - #### [GPIO 原子性操作 (BSRR)、SysTick 非阻塞時基系統](README_FILE/Main/GPIO/Advance_LED_blink_coding.md)
  - #### [UART 非阻斷式、非同步收發資料處理 (UART、DMA、Ring Buffer、IDLE、Flow Control、System Tick)](README_FILE/Main/UART/Advance_UART_coding.md)
  - #### [模擬 NVMe 通訊協定規範 (Protocol Specification)：Producer Consumer model、Checksum、Endianness、ORE、Unit Testing](README_FILE/Main/NVMe/nvme.md)
  - #### [FTL 之 虛擬實體位址映射表(L2P)、異地更新(Out of place Update)、垃圾回收(Garbage Collection)、Over Privisioning](README_FILE/L2P/L2P_GC.md)

## 四、自動化建置系統 STM32 Makefile
為 STM32 (Cortex-M0) 建置一套具備 **增量編譯 (Incremental Build)**、**空間優化 (Space Optimization)** 與 **OpenOCD一鍵燒錄** 功能的 Makefile
- #### [Makefile 兩階段編譯、燒入實作](README_FILE/Makefile/makefile.md)
- #### 核心硬體觀念：32 位元處理器與 16 位元指令
  - **32 位元處理器 (Cortex-M0)** ： 指的是 **資料寬度**。暫存器與 ALU 都是 32-bit，運算範圍達 $0$ 到 $2^{32}-1$
  - **16 位元指令集 (Thumb)** ： 指的是 **指令長度**。為了提高 **代碼密度 (Code Density)**，在有限的 Flash 空間內塞入更多指令
  - 運作原理 ： CPU 內部的 **指令解碼器 (Instruction Decoder)** 會在執行瞬間將 **16-bit Thumb 指令解譯為 32-bit** 的運算動作，兼顧空間與效能

## 五、除錯 (Debugging)
- #### [非侵入式除錯 (Non-intrusive Debugging)](README_FILE/Debug/Non-intrusive_Debugging.md)



