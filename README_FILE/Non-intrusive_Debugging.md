# 非侵入式除錯 (Non-intrusive Debugging)
直接利用 ST-Link (Mini USB) 透視晶片的大腦

### 安裝 GDB 多架構版
支援包括 ARM 在內的多種處理器架構
```
sudo apt update
sudo apt install gdb-multiarch
```
### 安裝後，使用此指令啟動 (取代原本的 `arm-none-eabi-gdb`)：
```
gdb-multiarch build/project.elf
```

### 準備 OpenOCD (後端橋樑)
確保板子已連上電腦，並另外開啟一個終端機視窗執行。**視窗必須一直開著，不要關掉**
```
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg
```
- 建立一個 GDB Server
- 看到 `Listening on port 3333 for gdb connections` 代表成功
