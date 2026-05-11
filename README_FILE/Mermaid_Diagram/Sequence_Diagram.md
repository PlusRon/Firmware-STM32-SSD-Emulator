## Sequence Diagram
### Booting
```mermaid
%%{init: { 'themeVariables': { 'fontSize': '24px', 'fontFamily': 'Arial'}}}%%

sequenceDiagram
    autonumber
    
    %% participant PC as Host (Python Tester)
    participant Aircr as AIRCR (SCB)
    %% participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware<br/>(CPU /DMA / UART / GPIO)
    %% participant Buffer as Ring Buffer (RAM)
    participant Startup as  Linker & Startup ( .ld / .c )
    participant Ram as RAM
    participant FTL as FTL Logic ( L2P / GC )
    participant Storage as Physical Flash

    rect rgb(245,245,245)
        Note over HW, Storage: --- [階段一] 手寫 Boot Sequence 與環境建立 --
        HW->>Startup: Power On / Reset 觸發
        Startup->>Ram: <br/>
        %% 讀取 Linker Script 定義<br/>之 Main Stack Pointer(MSP)
        Ram-->>Startup: 讀取 Linker Script 定義<br/>之 Main Stack Pointer
        
        Startup->>Storage: <br/>
        %% Data Relocation <br/>(將 .data 從 Flash 搬移至 RAM)
        Storage-->>Ram: Data Relocation <br/>(將 .data 從 Flash 搬移至 RAM)
        Startup->>Ram: BSS Zeroing <br/>(將未初始化區域清零)

        Startup->>HW: 跳轉至 main() 進入主迴圈
        HW->>HW: 系統初始化 RCC, GPIO, UART, DMA, NVIC, SysTick 配置
        HW->>FTL: 執行 Storage_Init() 建立 L2P Table 與 Free List
    end
    
   
    rect rgb(255, 240, 240)
        Note over Aircr, HW: --- [異常處理] SCB 異常重置 ---
        HW->>Aircr: 寫入 <br/>0x05FA0004
        Aircr-->>HW: 執行 <br/>SYSRESETREQ
    end

```

### NVMe-like Protocol + UART + ORE Recovery
```mermaid
%%{init: { 'themeVariables': { 'fontSize': '24px', 'fontFamily': 'Arial'}}}%%

sequenceDiagram
    autonumber
    
    participant PC as Host (Python Tester)
    %% participant Aircr as AIRCR暫存器 (SCB)
    %% participant Systick as SysTick 硬體計數器
    participant HW as STM32 Hardware (CPU/DMA/UART/GPIO)
    participant Buffer as Ring Buffer (RAM)
    %% participant Startup as  Linker & Startup (.ld/.c)
    %% participant Ram as RAM
    participant FTL as FTL Logic (L2P/GC)
    %% participant Storage as Physical Flash

   

    
    rect rgb(240, 248, 255)
        Note over PC, Buffer: --- [階段三] 高效能通訊 ---<br/>(NVMe-like Protocol 指令解析 and UART 傳輸) 

        PC->>HW: 發送 Big-Endian 封包 <br/>7 Bytes (Op, LBA, CS)
        Note over HW: DMA 背景自動搬運資料 (非阻塞，不佔用 CPU)
        HW->>Buffer: 自動寫入資料至 rx_buffer
        HW->>HW: 更新 wr_ptr ( 透過 CNDTR 變動 )
        Note over HW: UART-IRQ 偵測到 IDLE 旗標(封包傳輸結束) or (wr_ptr != rd_ptr)
        opt rx_buffer 資料數 >= PKT_SIZE
            HW->>Buffer: 檢查 rd_ptr 處是否有 0xA5
            Buffer-->>HW: 回傳資料
        end
    end
    

    opt Checksum 正確
        Note over HW: __builtin_bswap16()
        HW->>HW: 將封包中的 LBA 與 len 從 Big Endian 翻轉為 little endian
        HW->>FTL: 由 handle_nvme_write/read() 傳送資料 LBA, len, data buffer of READ/WRITE
    end

    rect rgb(255, 218, 185)
        Note over PC, HW: --- [異常處理] ORE Recovery ---
        PC->>HW: 模擬高流量導致 Overrun (ORE)
        HW->>Buffer: 將高流量資料寫入 rx_buffer
        Note over HW: UART-IRQ 偵測到 ORE 旗標
        HW-->>PC: 回傳 [SYS] ORE_ERROR
        HW->>Buffer: 清空緩衝區<br/>(CNDTR 重設回 RX_BUF_SIZE)
    end

```
