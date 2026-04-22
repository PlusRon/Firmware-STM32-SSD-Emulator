import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"\n--- Running: {name} ---")
    
    # 打包封包
    actual_op = 0x99 if force_bad_op else opcode
    raw = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    # 計算 Checksum
    if force_bad_cs:
        checksum = (sum(raw) + 1) & 0xFF # 故意加 1 破壞校驗
    else:
        checksum = sum(raw) & 0xFF
        
    pkt = raw + struct.pack('B', checksum)
    ser.write(pkt)
    
    time.sleep(0.2)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore').strip()
        print(f"Result: {res}")

try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
    time.sleep(1)
    ser.reset_input_buffer()

    # 1. 成功案例 (Write & Read)
    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 100, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 100, 8)

    # 2. 錯誤案例：Checksum 錯誤
    test_nvme("CHECKSUM ERROR TEST", ser, 0x02, 200, 8, force_bad_cs=True)

    # 3. 錯誤案例：不支援的指令 (Invalid Opcode)
    test_nvme("INVALID OPCODE TEST", ser, 0x99, 0, 0, force_bad_op=True)

    # 4. 錯誤案例：模擬 ORE (一次噴大量垃圾數據塞爆 Buffer)
    print("\n--- Running: ORE OVERFLOW TEST ---")
    ser.write(b'X' * 2000) # 噴 2000 個字元，超過 rx_buffer 的 1024
    time.sleep(0.5)
    if ser.in_waiting > 0:
        res = ser.read_all().decode('ascii', errors='ignore')
        print(f"Result: {res}")

    ser.close()
except Exception as e:
    print(f"Error: {e}")
