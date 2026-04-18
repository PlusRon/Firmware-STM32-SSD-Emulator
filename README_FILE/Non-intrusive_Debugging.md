# 非侵入式除錯 (Non-intrusive Debugging)
直接利用 ST-Link (Mini USB) 透視晶片的大腦

## 一、環境準備
### 安裝 GDB 多架構版
支援包括 ARM 在內的多種處理器架構
```
sudo apt update
sudo apt install gdb-multiarch
```
### 安裝後，後續可使用此指令啟動 (取代原本的 `arm-none-eabi-gdb`)：
```
gdb-multiarch build/project.elf
```

## 二、非侵入式除錯步驟
### (1) 準備 OpenOCD (後端橋樑)
確保板子已連上電腦，並另外開啟一個終端機視窗執行。**視窗必須一直開著，不要關掉**
```
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg
```
- 建立一個 GDB Server
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
  - 重置並停在開機第一行
    ```
    (gdb) monitor reset halt    # 讓晶片重啟並停在開機第一行
    ```
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
      - 如果程式直接卡住沒反應，就證實了 printf 內部發生死循環
      - 按下 **Ctrl + C**，強制暫停
      - 輸入 `bt` (backtrace)，GDB 會直接秀出到底死在哪個函式的哪一行







