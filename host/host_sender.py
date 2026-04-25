import serial  # 負責串口通訊 (pySerial 庫)
import struct  # 負責將 Python 資料型別轉換為 C 語言結構體二進位格式 (最關鍵)
import time    # 負責延時控制

# 定義測試主函式，參數包含：測試名稱、串口物件、指令碼、位址、長度、以及兩個錯誤注入旗標
def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"\n--- Running: {name} ---")
    
    # 模擬錯誤 Opcode 測試 (三元運算)
    actual_op = 0x99 if force_bad_op else opcode

    # 使用 Big-Endian (>) 封裝資料：Header(B), Opcode(B), LBA(H), Len(H)
    # struct.pack ：
    # '>' : 代表使用 Big-Endian (大端序) 編碼
    # 'B' : 1 Byte (unsigned char)，對應 start_byte
    # 'B' : 1 Byte (unsigned char)，對應 opcode
    # 'H' : 2 Bytes (unsigned short)，對應 lba
    # 'H' : 2 Bytes (unsigned short)，對應 length
    raw = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # # 模擬校驗錯誤測試，計算 Checksum
    if force_bad_cs:
        # 故意將正確的總和 + 1，STM32 收到後算出來會對不起來
        checksum = (sum(raw) + 1) & 0xFF # 故意讓校驗碼錯誤
    else:
        # sum(raw) 會累加前面 6 個 Byte 的數值，& 0xFF 是為了確保它只佔 1 Byte (0-255)
        checksum = sum(raw) & 0xFF       # 標準計算
        
    pkt = raw + struct.pack('B', checksum)  # 組合最終 7-byte 封包
    ser.write(pkt)                          # 透過實體串口送出 # 呼叫底層驅動，將二進位資料經由 USB 傳送到 STM32
    
    time.sleep(0.2)  # 等待 Device 處理回應 # 給 STM32 一點時間運算並回傳訊息
    if ser.in_waiting > 0:
        # read_all() 讀取所有回傳資料
        # decode('ascii') 將二進位轉回文字，errors='ignore' 防止因為亂碼導致程式崩潰
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")

try:
    # 初始化序列埠，115200, N, 8, 1
    # 初始化 /dev/ttyUSB0 (Linux 格式)，波特率 115200 必須與 STM32 一致
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)   # 硬體重置後通常需要 1 秒讓電位穩定
    ser.reset_input_buffer()  # 清除啟動時可能產生的雜訊資料

    # 1. 冒煙測試：基本讀寫功能，成功案例 (Write & Read)
    # 測試 A：正常寫入指令 (LBA 100, 長度 8)
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 100, 8)

    # 測試 B：正常讀取指令 (確認剛才寫入的資料)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 100, 8)

    # 2. 魯棒性測試：故意傳送 Checksum 錯誤的封包, Checksum 攻擊，測試 STM32 是否會被損壞的封包騙到
    test_nvme("CHECKSUM ERROR TEST", ser, 0x02, 200, 8, force_bad_cs=True)

    # 3. 語義測試：傳送未定義的指令、不支援的指令 (Invalid Opcode),非法指令攻擊，測試 STM32 的邊界檢查是否有效
    test_nvme("INVALID OPCODE TEST", ser, 0x99, 0, 0, force_bad_op=True)

    # 4. 硬體溢位 壓力與異常測試：模擬 Host 端產生過載 (Overrun)，模擬 ORE (一次噴大量垃圾數據塞爆 Buffer)
    print("\n--- Running: ORE OVERFLOW TEST ---")
    ser.write(b'X' * 2000) # 噴 2000 個字元，超過 rx_buffer 的 1024 緩衝區的數據量
    time.sleep(0.5)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore')
        print(f"Result: {res}")

    ser.close()  # 養成好習慣，結束後關閉資源
except Exception as e:
    print(f"Error: {e}")
