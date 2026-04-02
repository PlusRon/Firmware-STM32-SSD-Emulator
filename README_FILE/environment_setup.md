# Linux 開發環境

### 系統更新與核心套件安裝
  ```
  sudo apt update
  ```
### 安裝交叉編譯工具鏈 (Toolchain)
```
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi gdb-multiarch
```
- 在 x86 架構的電腦上開發 ARM 架構的韌體，需要安裝交叉編譯器
  - `gcc-arm-none-eabi`: **編譯器**
  - `binutils-arm-none-eabi`: 包含 **連結器（ld）** 與 **二進位轉換工具（objcopy）**
  - `gdb-multiarch`: 跨架構 之 除錯器
### 安裝燒錄與除錯橋樑 (Debugging Bridge)
```
sudo apt install openocd stlink-tools
```
- OpenOCD: 強大的除錯與自動化測試工具，作為 GDB 與硬體間的橋樑（預設 Port 3333）
- stlink-tools: 提供極其簡便的 st-flash 工具，適合快速上傳 .bin 檔案

### 專案管理與通訊工具
```
sudo apt install make git minicom
```
