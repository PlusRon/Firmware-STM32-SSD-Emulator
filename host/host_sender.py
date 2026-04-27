import serial
import struct
import time

def test_nvme(name, ser, opcode, lba, length, force_bad_cs=False, force_bad_op=False):
    print(f"-> {name:20} (LBA={lba:<3})", end=': ')
    actual_op = 0x99 if force_bad_op else opcode
    pkt = struct.pack('>BBHH', 0xA5, actual_op, lba, length)
    
    if force_bad_cs:
        checksum = (sum(pkt) + 1) & 0xFF 
    else:
        checksum = sum(pkt) & 0xFF        
        
    full_pkt = pkt + struct.pack('B', checksum)  
    
    try:
        ser.write(full_pkt)
        time.sleep(0.3) 
        
        if ser.in_waiting > 0:
            raw_data = ser.read_all()
            text_part = raw_data.decode('ascii', errors='ignore').strip()
            
            if "DATA:" in text_part:
                header_len = raw_data.find(b"DATA:") + 5
                payload = raw_data[header_len:]
                print(f"Result: [ACK] DATA: {payload.hex(' ').upper()}")
            else:
                clean_text = "".join(ch for ch in text_part if ch.isprintable() or ch in "\r\n")
                print(f"Result: {clean_text.strip()}")
        else:
            print("Result: [Timeout]")
    except Exception as e:
        print(f"Result: [Exception] {e}")

try:
    ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1) 
    time.sleep(1)
    ser.reset_input_buffer()

    print("="*50)
    print("  SSD Simulator: Page-Based FTL Test")
    print("="*50)

    test_nvme("SUCCESSFUL WRITE", ser, 0x02, 5, 8)
    test_nvme("SUCCESSFUL READ",  ser, 0x01, 5, 8)

    print("\n--- Filling SSD ---")
    for i in range(16):
        test_nvme(f"Fill-Test-{i}", ser, 0x02, i, 8)

    ser.close()
    print("\n" + "="*50)
    print("           Tests Completed")
    print("="*50)
except Exception as e:
    print(f"\n[FATAL ERROR]: {e}")
