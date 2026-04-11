# 阻斷式 Polling
  ```
  /* --- 1. 阻斷 : 等待讀取 --- */
  char uart_receive_char(void) {
      while (!(USART1->ISR & (1UL << 5))); // CPU 在這裡死等，什麼都不能做
      return (char)(USART1->RDR);
  }
  
  /* --- 2. 主程式 --- */
  int main(void) {
    // UART 初始化 

    while (1) {
        /* --- 任務 B：阻斷 UART 處理 --- */
        char received = uart_receive_char();
        // ... 處理資料 ...
    }
  }
  ```
