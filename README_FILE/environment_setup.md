# Linux 開發環境


### 系統更新
```
sudo apt update
```
### 安裝交叉編譯工具鏈 (Toolchain)
在 x86 架構的電腦上開發 ARM 架構的韌體，需要安裝交叉編譯器
```
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi gdb-multiarch
```
- `gcc-arm-none-eabi`: **編譯器**
- `binutils-arm-none-eabi`: 包含 **連結器（ld）** 與 **二進位轉換工具（objcopy）**
- `gdb-multiarch`: 跨架構 之 除錯器
### 安裝燒錄與除錯橋樑 (Debugging Bridge)
```
sudo apt install openocd stlink-tools
```
- OpenOCD: 強大的除錯與自動化測試工具，作為 GDB 與硬體間的橋樑（預設 Port 3333）
- stlink-tools: 提供極其簡便的 st-flash 工具，適合快速上傳 .bin 檔案

### 安裝專案管理與通訊工具
```
sudo apt install make git minicom
```
### 硬體連線確認
將 **STM32F072 Discovery** 開發板透過 USB 連接至電腦，使用 `lsusb` 指令確認系統是否成功偵測到 ST-LINK 偵錯器
```
lsusb
: Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2.1
```
- 記下 `VID: 0483` 與 `PID: 3748`，這將用於下一步的權限設定

### 設定 USB 存取權限 (udev rules)
Linux 預設僅允許 `root` 存取底層硬體。為了讓開發者能以一般使用者身分進行燒錄，需設定 `udev` 規則
- #### 建立規則檔案
  - 在系統設定目錄 /etc/udev/rules.d/ 下建立一個新規則
    ```
    sudo vim /etc/udev/rules.d/45-stlinkv2.rules
    ```
- #### 寫入設備描述符
  - 透過屬性過濾器（Filter List）匹配 ST-LINK，並賦予 `0666` (讀寫) 權限
    ```
    # ST-Link V2.1 權限設定
    SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666"
    ```
- #### 重新載入守護進程 (Daemon)
  - 叫醒負責管理硬體裝置的管家 `udevd`，讓新規則立即生效
    ```
    sudo udevadm control --reload-rules
    ```
  - 強制對現有裝置觸發規則(模擬硬體重插拔)
    ```
    sudo udevadm trigger
    ```
- #### 權限檢查
  - 先透過 lsusb 找到裝置所在的 Bus 與 Device 編號
    ```
    lsusb
    : Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2.1
    ```
  - 查看檔案權限
    ```
    # 假設裝置在 Bus 001, Device 005
    ls -l /dev/bus/usb/001/005 或 ll /dev/bus/usb/001/005 或 ls -lh /dev/bus/usb/001/005
    : crw-rw-rw- 1 root root ...
    ```
    - 檔案屬性第一字元代表檔案類型
      - `c` (Character Device) ： 字元設備檔案，傳輸資料以字元為單位，例如 `/dev/tty`
      - `d` (Directory) ： 目錄，用於組織檔案的目錄結構
      - `-` (Regular File) ： 普通檔案，如文字檔、執行檔、影像檔等
      - `b` (Block Device)：區塊設備檔案，儲存資料以區塊為單位，可隨機存取，例如硬碟 `/dev/sda`
      - `p` (Pipe)：具名管道，用於程序間通信（FIFO）
      - l (Symbolic Link)：符號連結，類似 Windows 的捷徑
    
    - 最後的 `rw-rw-rw-`(**User-Group-Others**) 代表現在不需要 sudo 也能直接進行燒錄








