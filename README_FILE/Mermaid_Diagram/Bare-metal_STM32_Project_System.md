### 異常處理
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash


    rect rgb(255, 218, 185)
        Note over PC, HW: --- [異常處理] ORE Recovery ---
        PC->>HW: 模擬高流量導致 Overrun (ORE)
        HW->>Buffer: 將高流量資料寫入 Ring Buffer
        Note over HW: UART-IRQ 偵測到 ORE 旗標
        HW-->>PC: 回傳 [SYS] ORE_ERROR
        HW->>Buffer: 執行 DMA_Init() & 清空緩衝區<br>(將 CNDTR 可用空間重設回 RX_BUF_SIZE 且 rd_ptr 指回 0)
    end

    rect rgb(255, 240, 240)
        Note over Aircr, HW: --- [異常處理] SCB異常重置 ---
        HW->>Aircr: 寫入 0x05FA0004
        Aircr-->>HW: 執行 SYSRESETREQ
    end


```


### FTL
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash


    

    alt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 Len 從 Big Endian 翻轉為 little
        rect rgb(210, 240, 210)
            Note over PC, Storage: --- [階段四] FTL (Out-of-place update, L2P, GC ) 觸發 ---
            HW->>FTL: 由 handle_nvme_write/read() 傳送資料 LBA, len, data buffer of READ/WRITE
        
            Note right of FTL: 寫入邏輯 (Out-of-place Update)
            opt LBA 邊界檢查，是否超過 USER_PAGES
                FTL-->>PC: 回傳 [ERR] LBA Out of User Range
            end
            FTL->>FTL: allocate_page() 分配新頁面，檢查 Free List
            opt Free List 為空 (物理空間耗盡)
                FTL->>FTL: 觸發 Storage_GC()
                FTL->>Storage: 掃描 DIRTY 頁面並抹除 (0xFF)
                Storage-->>FTL: 回還頁面至 Free List
                opt GC 完，Free List 仍為空 (代表無 DIRTY 可以清還)
                    FTL-->>PC: 回傳 [ERR] DISK FULL ! Out of Physical Space!
                end
                
            end
            FTL->>FTL: 將 old PBA 在 page_status 中標記為 STATE_DIRTY (下一次 GC可清還)
            FTL->>FTL: 更新 L2P 表: LBA -> New PBA
            FTL->>Storage: 物理寫入資料至 New PBA
            FTL-->>PC: 回傳 [ACK] READ_OK DATA: ... / [ACK] WRITE_OK (透過 UART)
        end
        
    else Checksum 錯誤
        HW-->>PC: 回傳 [ERR] Checksum Mismatch
    end
    


```

### NVMe Protocol
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash

   

    
    rect rgb(240, 248, 255)
        Note over PC, Buffer: --- [階段三] 高效能通訊 (NVMe-like Protocol 指令解析 and UART傳輸) ---

        PC->>HW: 發送 Big-Endian 封包 7 Bytes(Op, LBA, CS)
        Note over HW: DMA 背景自動搬運資料 (非阻塞，不佔用 CPU)
        HW->>Buffer: 自動寫入資料至 rx_buffer
        HW->>HW: 更新 CNDTR (wr_ptr 變動)
        Note over HW: UART-IRQ 偵測到 IDLE 旗標(封包傳輸結束) or (wr_ptr != rd_ptr)
        opt buffer 資料數 >= PKT_SIZE
            HW->>Buffer: 檢查 rd_ptr 處是否有 0xA5
            Buffer-->>HW: 回傳資料
        end
    end
    

    opt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 Len 從 Big Endian 翻轉為 little
        HW->>FTL: 由 handle_nvme_write/read() 傳送資料 LBA, len, data buffer of READ/WRITE

  
    
    end
```
### GPIO_BSSR_LED


```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash


    Note over Systick, HW: 每 1ms 觸發一次中斷，VAL 1 減 0， msTicks++
    loop 無窮迴圈 while(1)
        Note over HW, Systick: --- [階段二] 非阻塞時間檢查(預防系統當機) ---
        HW->>Systick: 讀取 get_tick()
        Systick-->>HW: 回傳當前 msTicks
        
        alt (get_tick - last_blink) >= 500ms
            HW->>HW: 執行 LED_Toggle() 並 更新 last_blink
        else 時間未到
            HW->>HW: 執行其他背景任務 (UART/DMA...)
        end
    end
    
    
```

### Booting
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash

    rect rgb(245,245,245)
        Note over HW, Storage: --- [階段一] 手寫 Boot Sequence 與環境建立 --
        HW->>Startup: Power On / Reset 觸發
        Startup->>Ram: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
        Ram-->>Startup: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
        Startup->>Storage: Data Relocation (將 .data 從 Flash 搬移至 RAM)
        Storage-->>Ram: Data Relocation (將 .data 從 Flash 搬移至 RAM)
        Startup->>Ram: BSS Zeroing (將未初始化區域清零)

        Startup->>HW: 跳轉至 main() 進入主迴圈
        HW->>HW: 系統初始化 RCC, GPIO, UART, DMA, NVIC, SysTick 配置
        HW->>FTL: 執行 Storage_Init() (建立 L2P 表與 Free List)
    end
    
    
  
    

   
    rect rgb(255, 240, 240)
        Note over Aircr, HW: --- [異常處理] SCB異常重置 ---
        HW->>Aircr: 寫入 0x05FA0004
        Aircr-->>HW: 執行 SYSRESETREQ
    end

```

### 系統時序圖 (Sequence Diagram)
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash

    rect rgb(245,245,245)
        Note over HW, Storage: --- [階段一] 手寫 Boot Sequence 與環境建立 --
        HW->>Startup: Power On / Reset 觸發
        Startup->>Ram: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
        Ram-->>Startup: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
        Startup->>Storage: Data Relocation (將 .data 從 Flash 搬移至 RAM)
        Storage-->>Ram: Data Relocation (將 .data 從 Flash 搬移至 RAM)
        Startup->>Ram: BSS Zeroing (將未初始化區域清零)

        Startup->>HW: 跳轉至 main() 進入主迴圈
        HW->>HW: 系統初始化 RCC, GPIO, UART, DMA, NVIC, SysTick 配置
        HW->>FTL: 執行 Storage_Init() (建立 L2P 表與 Free List)
    end
    Note over Systick, HW: 每 1ms 觸發一次中斷，VAL 1 減 0， msTicks++
    loop 無窮迴圈 while(1)
        Note over HW, Systick: --- [階段二] 非阻塞時間檢查(預防系統當機) ---
        HW->>Systick: 讀取 get_tick()
        Systick-->>HW: 回傳當前 msTicks
        
        alt (get_tick - last_blink) >= 500ms
            HW->>HW: 執行 LED_Toggle() 並 更新 last_blink
        else 時間未到
            HW->>HW: 執行其他背景任務 (UART/DMA...)
        end
    end
    
    rect rgb(240, 248, 255)
        Note over PC, Buffer: --- [階段三] 高效能通訊 (NVMe-like Protocol 指令解析 and UART傳輸) ---

        PC->>HW: 發送 Big-Endian 封包 7 Bytes(Op, LBA, CS)
        Note over HW: DMA 背景自動搬運資料 (非阻塞，不佔用 CPU)
        HW->>Buffer: 自動寫入資料至 rx_buffer
        HW->>HW: 更新 CNDTR (wr_ptr 變動)
        Note over HW: UART-IRQ 偵測到 IDLE 旗標(封包傳輸結束) or (wr_ptr != rd_ptr)
        opt buffer 資料數 >= PKT_SIZE
            HW->>Buffer: 檢查 rd_ptr 處是否有 0xA5
            Buffer-->>HW: 回傳資料
        end
    end
    

    alt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 Len 從 Big Endian 翻轉為 little
        rect rgb(210, 240, 210)
            Note over PC, Storage: --- [階段四] FTL (Out-of-place update, L2P, GC ) 觸發 ---
            HW->>FTL: 由 handle_nvme_write/read() 傳送資料 LBA, len, data buffer of READ/WRITE
        
            Note right of FTL: 寫入邏輯 (Out-of-place Update)
            opt LBA 邊界檢查，是否超過 USER_PAGES
                FTL-->>PC: 回傳 [ERR] LBA Out of User Range
            end
            FTL->>FTL: allocate_page() 分配新頁面，檢查 Free List
            opt Free List 為空 (物理空間耗盡)
                FTL->>FTL: 觸發 Storage_GC()
                FTL->>Storage: 掃描 DIRTY 頁面並抹除 (0xFF)
                Storage-->>FTL: 回還頁面至 Free List
                opt GC 完，Free List 仍為空 (代表無 DIRTY 可以清還)
                    FTL-->>PC: 回傳 [ERR] DISK FULL ! Out of Physical Space!
                end
                
            end
            FTL->>FTL: 將 old PBA 在 page_status 中標記為 STATE_DIRTY (下一次 GC可清還)
            FTL->>FTL: 更新 L2P 表: LBA -> New PBA
            FTL->>Storage: 物理寫入資料至 New PBA
            FTL-->>PC: 回傳 [ACK] READ_OK DATA: ... / [ACK] WRITE_OK (透過 UART)
        end
        
    else Checksum 錯誤
        HW-->>PC: 回傳 [ERR] Checksum Mismatch
    end
    rect rgb(255, 218, 185)
        Note over PC, HW: --- [異常處理] ORE Recovery ---
        PC->>HW: 模擬高流量導致 Overrun (ORE)
        HW->>Buffer: 將高流量資料寫入 Ring Buffer
        Note over HW: UART-IRQ 偵測到 ORE 旗標
        HW-->>PC: 回傳 [SYS] ORE_ERROR
        HW->>Buffer: 執行 DMA_Init() & 清空緩衝區<br>(將 CNDTR 可用空間重設回 RX_BUF_SIZE 且 rd_ptr 指回 0)
    end
    rect rgb(255, 240, 240)
        Note over Aircr, HW: --- [異常處理] SCB異常重置 ---
        HW->>Aircr: 寫入 0x05FA0004
        Aircr-->>HW: 執行 SYSRESETREQ
    end

```

### Booting (old)

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

### Origin
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Aircr as AIRCR暫存器 (SCB)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash

    Note over HW, Storage: --- [階段一] 手寫 Boot Sequence 與環境建立 ---
    
    HW->>Startup: Power On / Reset 觸發
    Startup->>Ram: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
    Ram-->>Startup: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
    Startup->>Storage: Data Relocation (將 .data 從 Flash 搬移至 RAM)
    Storage-->>Ram: Data Relocation (將 .data 從 Flash 搬移至 RAM)
    Startup->>Ram: BSS Zeroing (將未初始化區域清零)

    Startup->>HW: 跳轉至 main() 進入主迴圈
    HW->>HW: 系統初始化 RCC, GPIO, UART, DMA, NVIC, SysTick 配置
    HW->>FTL: 執行 Storage_Init() (建立 L2P 表與 Free List)

    Note over Systick, HW: 每 1ms 觸發一次中斷，VAL 1 減 0， msTicks++
    loop 無窮迴圈 while(1)
        Note over HW, Systick: --- [階段二] 非阻塞時間檢查(預防系統當機) ---
        HW->>Systick: 讀取 get_tick()
        Systick-->>HW: 回傳當前 msTicks
        
        alt (get_tick - last_blink) >= 500ms
            HW->>HW: 執行 LED_Toggle() 並 更新 last_blink
        else 時間未到
            HW->>HW: 執行其他背景任務 (UART/DMA...)
        end
    end
    

    Note over PC, Buffer: --- [階段三] 高效能通訊 (NVMe-like Protocol 指令解析 and UART傳輸) ---

    PC->>HW: 發送 Big-Endian 封包 7 Bytes(Op, LBA, CS)
    Note over HW: DMA 背景自動搬運資料 (非阻塞，不佔用 CPU)
    HW->>Buffer: 自動寫入資料至 rx_buffer
    HW->>HW: 更新 CNDTR (wr_ptr 變動)
    Note over HW: UART-IRQ 偵測到 IDLE 旗標(封包傳輸結束) or (wr_ptr != rd_ptr)
    opt buffer 資料數 >= PKT_SIZE
        HW->>Buffer: 檢查 rd_ptr 處是否有 0xA5
        Buffer-->>HW: 回傳資料
    end

    alt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 Len 從 Big Endian 翻轉為 little
        % Note over HW: Handle READ / WRITE
        Note over Buffer, Storage: --- [階段四] FTL (Out-of-place update, L2P, GC ) 觸發 ---
        HW->>FTL: 由 handle_nvme_write/read() 傳送資料 LBA, len, data buffer of READ/WRITE
        rect rgb(0, 0, 139)
            Note right of FTL: 寫入邏輯 (Out-of-place Update)
            opt LBA 邊界檢查，是否超過 USER_PAGES
                FTL-->>PC: 回傳 [ERR] LBA Out of User Range
            end
            FTL->>FTL: allocate_page() 分配新頁面，檢查 Free List
            opt Free List 為空 (物理空間耗盡)
                FTL->>FTL: 觸發 Storage_GC()
                FTL->>Storage: 掃描 DIRTY 頁面並抹除 (0xFF)
                Storage-->>FTL: 回還頁面至 Free List
                opt GC 完，Free List 仍為空 (代表無 DIRTY 可以清還)
                    FTL-->>PC: 回傳 [ERR] DISK FULL ! Out of Physical Space!
                end
                
            end
            FTL->>FTL: 將 old PBA 在 page_status 中標記為 STATE_DIRTY (下一次 GC可清還)
            FTL->>FTL: 更新 L2P 表: LBA -> New PBA
            FTL->>Storage: 物理寫入資料至 New PBA
        end
        FTL-->>PC: 回傳 [ACK] READ_OK DATA: ... / [ACK] WRITE_OK (透過 UART)
    else Checksum 錯誤
        HW-->>PC: 回傳 [ERR] Checksum Mismatch
    end
    
    Note over PC, HW: --- [異常處理] ORE Recovery ---
    PC->>HW: 模擬高流量導致 Overrun (ORE)
    HW->>Buffer: 將高流量資料寫入 Ring Buffer
    Note over HW: UART-IRQ 偵測到 ORE 旗標
    HW-->>PC: 回傳 [SYS] ORE_ERROR
    HW->>Buffer: 執行 DMA_Init() & 清空緩衝區<br>(將 CNDTR 可用空間重設回 RX_BUF_SIZE 且 rd_ptr 指回 0)
    rect rgb(0, 0, 139)
        Note over Aircr, HW: --- [異常處理] SCB異常重置 ---
        HW->>Aircr: 寫入 0x05FA0004
        Aircr-->>HW: 執行 SYSRESETREQ
    end
```

### Old Sequence Diagram
```mermaid
sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup (.ld/.c)
    participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    participant Storage as Physical Flash

    Note over HW, Startup: --- [階段一] 手寫 Boot Sequence 與環境建立 ---
    
    HW->>Startup: Power On / Reset 觸發
    Startup->>Ram: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
    Ram-->>Startup: 讀取 Linker Script 定義之 Main Stack Pointer(MSP)
    Startup->>Storage: Data Relocation (將 .data 從 Flash 搬移至 RAM)
    Storage-->>Ram: Data Relocation (將 .data 從 Flash 搬移至 RAM)
    Startup->>Ram: BSS Zeroing (將未初始化區域清零)

    Startup->>HW: 跳轉至 main() 進入主迴圈
    HW->>HW: 系統初始化 RCC, GPIO, UART, DMA, NVIC, SysTick 配置
    HW->>FTL: 執行 Storage_Init() (建立 L2P 表與 Free List)

    Note over Systick, HW: 每 1ms 觸發一次中斷，VAL 1 減 0， msTicks++
    loop 無窮迴圈 while(1)
        Note over HW, Systick: --- [階段二] 非阻塞時間檢查(預防系統當機) ---
        HW->>Systick: 讀取 get_tick()
        Systick-->>HW: 回傳當前 msTicks
        
        alt (get_tick - last_blink) >= 500ms
            HW->>HW: 執行 LED_Toggle() 並 更新 last_blink
        else 時間未到
            HW->>HW: 執行其他背景任務 (UART/DMA...)
        end
    end
    

    Note over PC, Buffer: --- [階段三] 高效能通訊 (NVMe-like Protocol) ---

    PC->>HW: 發送 Big-Endian 封包 7 Bytes(Op, LBA, CS)
    Note over HW: DMA 背景自動搬運資料 (非阻塞，不佔用 CPU)
    HW->>Buffer: 自動寫入資料至 rx_buffer
    HW->>HW: 更新 CNDTR (wr_ptr 變動)
    Note over HW: UART-IRQ 偵測到 IDLE 旗標(封包傳輸結束) or (wr_ptr != rd_ptr)
    opt buffer 資料數 >= PKT_SIZE
        HW->>Buffer: 檢查 rd_ptr 處是否有 0xA5
        Buffer-->>HW: 回傳資料
    end

    alt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 Len 從 Big Endian 翻轉為 little
        % Note over HW: Handle READ / WRITE
        Note over Buffer, Storage: --- [階段四] FTL 指令解析與 GC 觸發 ---
        HW->>FTL: LBA, len, data buffer of READ/WRITE
        rect rgb(0, 0, 139)
            Note right of FTL: 寫入邏輯 (Out-of-place Update)
            FTL->>FTL: allocate_page() 檢查 Free List
            alt Free List 為空 (物理空間耗盡)
                FTL->>FTL: 觸發 Storage_GC()
                FTL->>Storage: 掃描 DIRTY 頁面並抹除 (0xFF)
                FTL->>FTL: 回還頁面至 Free List
            end
            FTL->>Storage: 物理寫入資料至新 PBA
            FTL->>FTL: 更新 L2P 表: LBA -> New PBA
            FTL->>FTL: 將舊 PBA 標記為 STATE_DIRTY
        end
        FTL->>PC: 回傳 [ACK] READ_OK DATA: ... / [ACK] WRITE_OK (透過 UART)
    else Checksum 錯誤
        HW->>PC: 回傳 [ERR] Checksum Mismatch
    end

    Note over Buffer, Storage: --- [階段四] FTL 指令解析與 GC 觸發 ---

    Buffer->>FTL: 讀取 0xA5 起始字元，驗證 Checksum
    FTL->>FTL: 端序轉換 (Big to Little Endian)
    
    rect rgb(0, 0, 139)
        Note right of FTL: 寫入邏輯 (Out-of-place Update)
        FTL->>FTL: allocate_page() 檢查 Free List
        alt Free List 為空 (物理空間耗盡)
            FTL->>FTL: 觸發 Storage_GC()
            FTL->>Storage: 掃描 DIRTY 頁面並抹除 (0xFF)
            FTL->>FTL: 回還頁面至 Free List
        end
        FTL->>Storage: 物理寫入資料至新 PBA
        FTL->>FTL: 更新 L2P 表: LBA -> New PBA
        FTL->>FTL: 將舊 PBA 標記為 STATE_DIRTY
    end

    FTL-->>PC: 回傳 ACK / DATA (透過 UART)
    
    Note over PC, HW: --- [異常處理] ORE Recovery ---
    PC->>HW: 模擬高流量導致 Overrun (ORE)
    HW->>Buffer: 偵測到 ORE 旗標
    Buffer->>HW: 執行 DMA_Reset() & 清空緩衝區
    Buffer-->>PC: 回傳 "SYSTEM RECOVERED"
```


### 系統生命週期與運作狀態圖 (State Diagram)
```mermaid  
stateDiagram-v2
    [*] --> PowerOn_Reset
    
    state "Startup (Bootloader)" as Startup {
        PowerOn_Reset --> Data_Relocation : Copy .data from Flash to RAM
        Data_Relocation --> BSS_Zeroing : Clear .bss
        BSS_Zeroing --> Call_Main : Branch to main()
    }

    state "Main_Loop (Runtime)" as Main {
        [*] --> Hardware_Init
        Hardware_Init --> Idle_Wait : Enable Interrupts, SysTick & DMA
        
        state Idle_Wait {
            [*] --> Check_UART_IDLE
            Check_UART_IDLE --> Protocol_Parsing : IDLE Detected (End of Pkt)
            Check_UART_IDLE --> LED_Blink : SysTick Timer Match
        }

        state Protocol_Parsing {
            [*] --> Verify_Checksum
            Verify_Checksum --> Dispatch_Op : Success
            Verify_Checksum --> Handle_ORE : Checksum Fail / Overrun
            
            state Dispatch_Op {
                [*] --> FTL_Read : 0x01 (Read)
                [*] --> FTL_Write : 0x02 (Write)
            }
        }

        state FTL_Logic {
            FTL_Read --> L2P_Lookup
            FTL_Write --> Check_Free_List
            Check_Free_List --> Out_of_Place_Update : Has Free Page
            Check_Free_List --> Garbage_Collection : Full (Trigger GC)
            Garbage_Collection --> Out_of_Place_Update : Reclaimed
        }
        
        Protocol_Parsing --> FTL_Logic
        FTL_Logic --> Idle_Wait : Response Sent via UART
        Handle_ORE --> Idle_Wait : Reset DMA/UART Buffer
    }
```

### 全系統架構圖 (System Architecture)
```mermaid
graph TB
    subgraph Host_System ["Host System (PC)"]
        Python[Python Tester / Host Driver]
    end

    subgraph STM32_Firmware ["STM32 Firmware (Cortex-M0)"]
        subgraph App_Layer ["Application Layer"]
            Parser[Protocol Parser]
            FTL[FTL Engine: L2P/GC]
            RB[UART Ring Buffer]
        end

        subgraph HAL_Boot_Layer ["HAL & Boot Layer"]
            Startup[startup.c / Reset Handler]
            HAL[HAL / stm32f072xb.h]
            LS[Linker Script .ld]
        end

        subgraph HW_Layer ["Hardware Layer"]
            DMA[DMA / UART Control]
            CPU[Cortex-M0 Core]
            MEM[SRAM / Flash]
        end
    end

    %% Relations
    Python <==>|UART Physical Link| DMA
    DMA <==>|Producer-Consumer| RB
    RB -->|Non-blocking| Parser
    Parser -->|LBA Commands| FTL
    FTL -->|Memory Access| MEM
    
    LS -.->|Memory Mapping| MEM
    Startup -.->|Init Environment| CPU
    HAL -.->|Register Definition| DMA
```
### 系統啟動與運行狀態機 (System State Machine)
```mermaid
stateDiagram-v2
    [*] --> Reset_Vector: Power On / Reset
    
    state "Boot & Init (startup.c)" as Boot {
        Reset_Vector --> Copy_Data: Relocate .data to RAM
        Copy_Data --> Zero_BSS: Clear .bss
        Zero_BSS --> System_Init: Init SysTick/DMA/UART
        System_Init --> Main: Branch to main()
    }

    state "Main Runtime (Execution)" as Execution {
        Main --> Idle: Main Loop Start
        
        state Idle {
            [*] --> Watchdog_Blink: Toggle LED
            Watchdog_Blink --> Check_Buffer: Check Ring Buffer
        }

        Check_Buffer --> Parsing: Pkt Received (IDLE Detected)
        
        state Parsing {
            [*] --> Checksum_Verify
            Checksum_Verify --> FTL_Command: Valid
            Checksum_Verify --> ORE_Recover: Error / Overrun
        }

        state FTL_Operation {
            FTL_Command --> L2P_Mapping
            L2P_Mapping --> Write_PBA: Out-of-place Update
            Write_PBA --> GC_Trigger: Free Page < Threshold
            GC_Trigger --> Write_PBA: After Reclaim
        }
        
        FTL_Operation --> Idle: Response Sent
        ORE_Recover --> Idle: Reset DMA Index
    }
```
