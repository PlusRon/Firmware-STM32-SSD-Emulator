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
