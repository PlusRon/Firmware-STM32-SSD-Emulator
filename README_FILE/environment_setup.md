# Linux 開發環境


### 系統更新
```
sudo apt update
```
### 安裝 交叉編譯工具鏈 (Cross-compiler Toolchain)
在 x86 架構的電腦上開發 ARM 架構的韌體，需要安裝交叉編譯器
```
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi gdb-multiarch
```
- `gcc-arm-none-eabi`: **交叉編譯器**，可以將在 Linux 寫的 C 語言 轉成STM32懂得機械器碼，`none`無作業系統之意
- `binutils-arm-none-eabi`: 包含 **連結器（ld）** 與 **二進位轉換工具（objcopy）**，將編譯後的零件組成完整檔案
- `gdb-multiarch`: 跨架構 之 除錯器，可在 x86 電腦上除錯開發板上（ARM）跑的程式
  - 透過 OpenOCD 從電腦開一個門(`port 3333`) 連上開發板
  - 就可以在電腦螢幕上看到開發板內部暫存器的數值、設斷點、單步執行
### 安裝燒錄與除錯橋樑 (Debugging Bridge)
```
sudo apt install openocd stlink-tools
```
- OpenOCD: 強大的除錯與自動化測試工具，可做燒錄
  - 可燒錄，將編譯好的機器碼(.bin)透過 USB 塞進開發版的 Flash 記憶體中，但指令比較長 `program filename.elf verify reset exit`
  - 作為 GDB(電腦) 與硬體(開發板)間的橋樑（預設的 GDB Server 為 `Port 3333`），非常穩定
  - GDB 支援各種複雜的斷點、讀取記憶體位址，甚至是透過腳本自動化測試硬體
- stlink-tools: 提供極其簡便的 st-flash 工具，適合快速上傳 .bin 檔案
  - `st-flash` 指令非常好用
  - `st-util` 指令會開一個 GDB Server `Port 4242`，讓 gdb-multiarch 也可連

### 安裝專案管理與通訊工具
```
sudo apt install make git minicom
```
- make : 自動化管理，根據清單跑
- minicom : 可在 Linux 視窗看到開發板用 UART 傳回來的文字訊息
### 硬體連線確認
將 **STM32F072 Discovery** 開發板透過 USB 連接至電腦，使用 `lsusb` 指令確認系統是否成功偵測到 ST-LINK 偵錯器
```
lsusb
: Bus 001 Device 005: ID 0483:3748 STMicroelectronics ST-LINK/V2.1
```
- 記下 `VID: 0483` 與 `PID: 3748`，這將用於下一步的權限設定(燒錄之權限)

### 設定 USB 存取權限 (udev rules)
Linux 預設不認識 STM32，只知道有裝置連接不知道是開發工具，且預設僅允許 `root` 存取底層硬體。為了讓開發者能以一般使用者身分進行燒錄，需設定 `udev` 規則
- #### 建立規則檔案
  - 在系統設定目錄 /etc/udev/rules.d/ 下建立一個新規則
    ```
    sudo vim /etc/udev/rules.d/45-stlinkv2.rules
    ```
    - **etc (Editable Text Configuration)** : 存放系統層級設定檔，裡面的檔案都是純文字檔，可以 vim 和 nano 編輯
      - ‵/etc/udev/`：定義 USB 裝置（如 ST-LINK）插進後，應該給的權限
      - ‵/etc/fstab`：定義硬碟、隨身碟開機時要掛載（Mount）到哪個資料夾
      - `45` 代表優先順序（數字越小越先執行），
      - 可用 `ls -F /etc/` 指令利用前綴分辨檔案屬性
        - `/` : 目錄
        - `*` : 程式執行檔(編譯器、腳本)
        - `@` : 軟連結、捷徑
      - 清單篩選 `ls -alF /usr/bin/ | grep arm-none-eabi`、`ll /usr/bin/ | grep arm-none-eabi` 
- #### 對 `45-stlinkv2.rules` 寫入設備描述符
  - 透過屬性過濾器（Filter List）匹配 ST-LINK，並賦予 `0666` (讀寫) 權限
    ```
    # ST-Link V2.1 權限設定
    SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666"
    ```
- #### 重新載入守護進程 (Daemon, 後台運行的程式)
  - **守護進程（Daemon，後台運行的程式）**: 以小寫字母 d 結尾
    - 可用 `ps`（Process Status）指令來抓出後台管家
      ```
      ps -ef | grep "d$"
      ````
      - `udevd`：負責處理硬體裝置（如你的 ST-LINK）插拔
      - `sshd`：負責處理遠端連線（SSH）的插拔
      - `systemd`：管理所有其他管家的 總管
  - 透過管理硬體裝置的管家，讓新規則立即生效
    ```
    sudo udevadm control --reload-rules
    ```
    - `udevadm`: 為 udev（裝置管理員）的控制工具（Admin）
    - `control`: 對 Daemo 下達控制命令
    - `--reload-rules`: 強制 Daemo 重新載入規則
  - 強制對現有裝置觸發規則(模擬硬體重插拔)
    ```
    sudo udevadm trigger
    ```
    - udev 是事件驅動（Event-driven）的，只會對之後插上的裝置生效
    - 對於 已經插著的裝置，必須手動執行 udevadm trigger 讓核心重新發送 uevent，強制系統套用新規則
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








