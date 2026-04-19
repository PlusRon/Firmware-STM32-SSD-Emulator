# HOST
## 一、環境建置
### 查看 python 版本
```
python3 --version
```
### 下載 Python 的套件管理工具
```
sudo apt install python3-pip -y
pip --version
```

### 安裝 pyserial
  - **Linux 系統的安全性與穩定性 安裝**
    ```
    # 針對個人使用者安裝（建議）
    pip install pyserial --break-system-packages
    
    # 或者使用 apt 安裝系統層級的套件（某些 Linux 發行版推薦）
    sudo apt install python3-serial
    ```
  - **虛擬環境 (Virtual Environment, venv) 安裝**
    
    ```
    # 1. 在 host 資料夾下建立一個虛擬空間 (.venv)
    python3 -m venv .venv
    
    # 2. 啟動這個空間 (啟動後你的終端機前面會出現 (.venv) 字樣)
    source .venv/bin/activate
    
    # 3. 這時候你就可以「直接」安裝，不用加任何危險參數！
    pip install -r requirements.txt
    ```
    - pyserial 只會存在於 .venv 資料夾裡，完全不會碰到 Linux 系統
### 列出清單, 搜尋已安裝的套件
```
pip list | grep pyserial
```

### 檢查 pyserial 是否可執行
```
python3 -c 'import serial; print("Serial version:", serial.__version__)'
python3 -c 'import serial; print("SUCCESS: Pyserial is ready to use!")'
```
```
$ python3
>>> import serial
>>> print(serial.__version__)
3.5
>>> exit() 
```
### 查看套件詳細資訊
認它安裝在哪個路徑（是否裝在正在用的 Python 版本下）
```
pip show pyserial
:
Name: pyserial
Version: 3.5
Summary: Python Serial Port Extension
Home-page: https://github.com/pyserial/pyserial
Author: Chris Liechti
Author-email: cliechti@gmx.net
License: BSD
Location: /usr/lib/python3/dist-packages
Requires: 
Required-by: 
```

## 二、開發板權限設定 
### 確認目前帳號
確認目前這個終端機視窗是以哪個使用者身分在執行, 確保沒有誤用 `root`
```
whoami
```
### 確認帳號擁有控制哪些群組的權限
```
groups
:
dino adm cdrom sudo dip plugdev lpadmin lxd dialout
```
### 查看詳細身分資訊
一次看完 **使用者 ID (uid)**、**主要群組 (gid)** 以及 **所有附加群組**
```
dino$ id
:
uid=1000(dino) gid=1000(dino) groups=1000(dino),4(adm),24(cdrom),27(sudo),30(dip),46(plugdev),100(users),114(lpadmin)
```
```
root$ id
:
uid=0(root) gid=0(root) groups=0(root)
```






