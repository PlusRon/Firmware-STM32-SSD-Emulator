# 中斷驅動
- 由 中斷 (ISR) 在背景完成資料接收，並立即存放到 buffer 中。由 UART 的 ISR(Interrupt Status Register) 中的 RXNE 判斷 UART 的接收端是否已接收到 1 Byte 資料
- UART 的硬體 RDR 暫存器只有一個 且 只有 1 Byte，所以必須在中斷程式裡寫資料搬移邏輯至 buffer，下一個 Byte 進來才不會發生 Overrun Error (ORE)，導致後續資料全部遺失
- 在主程式中，以事件(當 buffer 有資料) 驅動的方式，讀出 buffer 中的資料
- 但傳統 UART 接收 是每當 **一個位元組(Byte)** 抵達時，硬體會觸發 **RXNE 中斷**，迫使 CPU 暫停當前工作，跳轉至 **ISR(Interrupt Service Routine)** 去讀取 UART 之 **RDR 暫存器**。但在 **高速通訊**(**Baud Rate : `115200 bps`**)下，會導致 CPU 被頻繁打斷，造成嚴重效能損耗與潛在資料丟失風險。
    ```
    /* --- 1. 中斷服務程式 (ISR) --- */
    // 在背景跑的，每當一個 Byte 被接收進來，CPU 就會暫停 main 跳進來這裡
    void USART1_IRQHandler(void) {
        // 檢查 RXNE (Bit 5)
        if (USART1->ISR & (1UL << 5)) {     // CPU 在這裡死等，什麼都不能做
            char data = (char)USART1->RDR;  // 讀取資料時 會自動清除 RXNE 標誌
            
            // 將資料放入環形緩衝區 (忽略溢位處理以簡化邏輯)
            uint16_t next_head = (head + 1) % BUFFER_SIZE;
            if (next_head != tail) {
                rx_fifo[head] = data;
                head = next_head;
            }
        }
    }

    /* --- 2. 非阻斷 : 檢測狀態 --- */
    int uart_available(void) {
        return (head != tail);
    }

    /* --- 3. 讀取資料 --- */
    char uart_read(void) {
        if (head == tail) return 0;
        char data = rx_fifo[tail];
        tail = (tail + 1) % BUFFER_SIZE;
        return data;
    }
    
    /* --- 4. 主程式 --- */
    int main(void) {
        // UART 初始化 
        // LED 初始化 

        while (1) {
            /* --- 任務 A：非阻斷 LED 閃爍 --- */
            // 利用一個硬體 SysTick 計數器、搭配背景計數、ISR，不使用阻斷式 delay
    
            /* --- 任務 B：非阻斷 UART 處理 --- */
            // 不再採用 阻斷式polling 等待資料，而是 非阻斷式檢查緩衝區 有沒有資料
            if (uart_available()) {   // 路過問一下，沒有就跳過
                char received = uart_read();
                // ... 處理資料 ...
            }
        }
    }
    ```
    
