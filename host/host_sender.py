import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    """
    精確測試函式：支援二進位資料與文字混合解析
    """
    print(f"-> {name:20} (LBA={lba:<3})", end=': ')
    
    # 建立封包
    actual_op = 0x99 if force_bad_op else opcode
    pkt = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # 計算 Checksum
    if force_bad_cs:
        checksum = (sum(pkt) + 1) & 0xFF
    else:
        checksum = sum(pkt) & 0xFF
        
    full_pkt = pkt + struct.pack('B', checksum)
    
    try:
        ser.write(full_pkt)
        
        # 增加延時確保 STM32 完成 FTL 操作與 UART 傳輸
        time.sleep(0.3)
        
        if ser.in_waiting > 0:
            raw_data = ser.read_all()
            
            # --- 混合解析邏輯 ---
            # 嘗試將原始資料轉為文字觀察
            text_part = raw_data.decode('ascii', errors='ignore').strip()
            
            # 狀況 A: 針對 [ACK] DATA: 的特殊處理 (解決資料看不見的問題)
            if "DATA:" in text_part:
                # 找出 DATA: 字串的位置，取出後面的二進位部分
                header_len = raw_data.find(b"DATA:") + 5
                payload = raw_data[header_len:]
                print(f"Result: [ACK] DATA: {payload.hex(' ').upper()}")
            
            # 狀況 B: 針對 Checksum Mismatch 的數值處理
            elif "Received: 0x" in text_part:
                # 重新格式化輸出，將不可見的數值轉為 Hex
                # 找出 Received: 0x 後面那個 Byte
                rx_idx = raw_data.find(b"Received: 0x") + 12
                ex_idx = raw_data.find(b"Expected: 0x") + 12
                # 確保不越界
                rx_val = raw_data[rx_idx] if rx_idx < len(raw_data) else 0
                ex_val = raw_data[ex_idx] if ex_idx < len(raw_data) else 0
                print(f"Result: [ERR] Checksum Mismatch! Received: 0x{rx_val:02X}, Expected: 0x{ex_val:02X}")

            # 狀況 C: 一般文字訊息
            else:
                # 過濾掉可能干擾顯示的控制字元
                clean_text = "".join(ch for ch in text_part if ch.isprintable() or ch in "\r\n")
                print(f"Result: {clean_text.strip()}")
        else:
            print("Result: [Timeout] No response from STM32")
            
    except Exception as e:
        print(f"Result: [Exception] {e}")

# ========================================
# 主測試流程
# ========================================
try:
    # 串口設定 (請確保 Minicom 已關閉)
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1) 
    ser.reset_input_buffer()

    print("="*50)
    print("  SSD Simulator Stage 2: FTL & Protocol Integrity Test")
    print("="*50)

    # 1. 基本功能測試
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 5, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 5, 8)

    # 2. FTL 空間管理測試 (PBA 分配與 LBA 限制)
    print("\n--- Filling SSD (PBA Allocation) ---")
    for i in range(17):
        # 注意：正確的參數順序是 (name, ser, opcode, lba, length)
        test_nvme(f"Fill-Test-{i}", ser, 0x02, i, 8)

    # 3. 邊界與異常測試
    print("\n--- Edge Case Test ---")
    test_nvme("Read Out of Range", ser, 0x01, 99, 8)
    test_nvme("Read Unmapped",    ser, 0x01, 30, 8)

    # 4. 通訊魯棒性測試
    print("\n--- Protocol Robustness Test ---")
    test_nvme("CHECKSUM ATTACK",  ser, 0x02, 6, 8, force_bad_cs=True)
    test_nvme("INVALID OPCODE",   ser, 0x99, 0, 0, force_bad_op=True)

    # 5. ORE 硬體溢位壓力測試
    print("\n--- ORE OVERFLOW TEST ---")
    print("-> Sending 2000 bytes garbage to trigger Overrun...")
    ser.write(b'X' * 2000)
    # 給予較長延時，因為 STM32 需要偵測錯誤、進入主迴圈、執行重置
    time.sleep(1.0) 
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")
    else:
        print("Result: No response (Check if LED stopped blinking - HardFault?)")

    ser.close()
    print("\n" + "="*50)
    print("           All Tests Completed")
    print("="*50)

except Exception as e:
    print(f"\n[FATAL ERROR]: {e}")
