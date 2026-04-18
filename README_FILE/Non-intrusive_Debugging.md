# 非侵入式除錯 (Non-intrusive Debugging)
直接利用 ST-Link (Mini USB) 透視晶片的大腦；編譯器（GCC）負責產生代碼，而除錯器（GDB）負責控制 CPU

## 一、環境準備
### 安裝 GDB 多架構版
支援包括 ARM 在內的多種處理器架構
```
sudo apt update
sudo apt install gdb-multiarch
```
- 一般的 `gdb` 是用來除錯電腦 (x86) 程式的
- `gdb-multiarch` 支援 ARM 架構 (STM32 用的 Cortex-M0)，能理解 ARM 的指令集與暫存器結構

### 安裝後，後續可使用此指令啟動 (取代原本的 `arm-none-eabi-gdb`)：
```
gdb-multiarch build/project.elf
```
- 啟動 GDB 並讀取 **符號表**
- `.elf` 檔案除了包含機器碼，還包含 **地圖訊息**（哪一行 C 代碼對應哪個記憶體位址），沒有這個檔案，GDB 只能看到一堆 0 和 1 的機械碼

## 二、非侵入式除錯步驟
### (1) 準備 OpenOCD (後端橋樑，硬體與軟體的翻譯官)
確保板子已連上電腦，並另外開啟一個終端機視窗執行。**視窗必須一直開著，不要關掉**
```
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg
```
- 啟動 OpenOCD 並指定 **硬體介面 (ST-Link)** 與 **目標晶片 (STM32F0)**
- OpenOCD 扮演轉接頭。連接著 **USB 端的 GDB 協定 (Port 3333)** 與 **晶片端的 SWD 協定**
- 在電腦端建立一個 **GDB Server**，連線到晶片並賦予 Port 3333
- 看到 `Listening on port 3333 for gdb connections` 代表成功

### (2) 啟動 GDB (前端介面)
- 開啟另一個終端機視窗，進入 **專案目錄**，啟動 GDB 並讀取你的程式地圖
  ```
  gdb-multiarch build/project.elf
  ```
- 進入 GDB 畫面後，輸入以下指令

  - 連接到 OpenOCD (剛才那個 3333 埠)
    ```
    (gdb) target remote :3333
    ```
    - 告訴 GDB，除錯目標不在這台電腦，而是在網路埠 `3333` 的那顆晶片上
  - 重置並停在開機第一行
    ```
    (gdb) monitor reset halt    # 讓晶片重啟並停在開機第一行
    ```
  - 檢查啟動位址 (pc) 與 堆疊指標 (msp)，查看初始指標
    ```
    (gdb) i r pc msp
    ```
    - 正常數值：`pc` 應該在 `0x0800XXXX`，`msp` 應該在 `0x2000XXXX`（RAM 的位址）
    - 狀況若為 `pc: 0xfffffffe` 代表 CPU 一開始就迷路了，通常是因為 Startup File (`startup.o`) 或 Linker Script 設定錯誤，導致**中斷向量表（Vector Table）沒有排在 Flash 的開頭**
  - 剛編譯完，該指令直接把 code 燒進去
    ```
    (gdb) load    # 如果你剛編譯完，這會直接把 code 燒進去
    ```
  - 下斷點
    ```
    (gdb) b main
    ```
  - 讓程式繼續跑 (全速執行)
    ```
    (gdb) continue
    ```
    - 當程式停在 main 之後
    - 輸入 `n` (next) 一行一行往下走，當走到 **printf** 那一行，按一下 `n`

      1. 如果程式直接卡住沒反應，就證實了 printf 內部發生死循環
      2. 按下 **Ctrl + C**，強制暫停
  - 檢查當機點
    ```
    (gdb) backtrace    # 縮寫 bt
    ```
    - 輸入 `bt` (backtrace)，GDB 會直接秀出到底死在哪個函式的哪一行
## 三、查看變數 (Watch and Print)
### (1) 靜態查看 (程式暫停時)
查看變數目前的數值
```
(gdb) print rd_ptr          # 印出目前的數值
(gdb) print /x USART1->ISR  # 以 16 進位印出 UART 狀態暫存器 (看旗標很有用)
```
### (2) 動態監控 (Watchpoint)
當變數被改變時，程式自動停下來
```
(gdb) watch rd_ptr          # 設定一個寫入監控點
(gdb) continue              # 讓程式繼續跑 (全速執行)
```
- 程式會全速執行
- 一旦有人改了 `rd_ptr` 的值，GDB 會立刻攔截訊號，讓程式停在那一行
- 並顯示 **舊值** 與 **新值**

### (3) 即時觀察 (不需要下斷點)
不想停下程式，只是想每隔一段時間查看它的值
```
(gdb) display rd_ptr        # 每次程式停下或手動下 Ctrl+C 時，都會自動顯示這個值
```
## 四、強制跳轉到 Reset_Handler
若 continue 沒反應，可以手動推它一把
```
(gdb) monitor reset halt
(gdb) b Reset_Handler    # 這是程式真正的起點，在進入 main 之前
(gdb) continue
```
- 如果能 **停在 `Reset_Handler`**，代表硬體沒壞，是 **啟動流程有問題**
- 如果連 Reset_Handler 都進不去，那極有可能是 **Linker Script 的 Section 定義 出了問題**





## 結論、GDB vs. printf
|特性|Printf|GDB(SWD)|
|:---|:---|:---|
|**速度影響**|會變慢，甚至改變 Bug 的行為|幾乎無影響 (由硬體電路負責)|
|**依賴性**|依賴 UART、DMA、GPIO、時鐘|僅依賴 ST-Link 引腳|
|**當機處理**|當機時印不出東西|當機時能強制停下來看暫存器|
|**資訊量**|只能看你印出來的東西|全域、區域、暫存器想看哪就看哪|








