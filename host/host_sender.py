import serial
import struct

# 請根據實際狀況修改路徑
DEV_PATH = '/dev/ttyUSB0'
BAUD = 115200

def send_nvme_read_cmd(lba, length):
    try:
        # 增加 timeout 確保 ser.readline() 不會永遠卡死
        with serial.Serial(DEV_PATH, BAUD, timeout=2) as ser:
            # 1. 準備封包
            # >BBHH: 大端序, Start(A5), Op(1), LBA(H,L), Len(H,L)
            raw_pkt = struct.pack('>BBHH', 0xA5, 0x01, lba, length)
            
            # 2. 計算 Checksum (前 6 bytes 累加取低 8 位)
            checksum = sum(raw_pkt) & 0xFF
            final_pkt = raw_pkt + struct.pack('B', checksum)
            
            # 3. 清除接收緩衝區 (避免讀到舊的殘留資料)
            ser.reset_input_buffer()
            
            # 4. 送出指令
            ser.write(final_pkt)
            print(f"[*] 指令已送出: {final_pkt.hex().upper()}")
            print(f"[*] 目標 LBA: {lba}, 預計讀取長度: {length}")

            # 5. --- 新增：接收 STM32 的回報 ---
            print("[*] 等待 STM32 回應...")
            # readline 會讀到 '\n' 為止
            response = ser.readline().decode('ascii', errors='replace').strip()
            
            if response:
                print(f"[+] 收到回應: {response}")
            else:
                print("[!] 警告：超時未收到回應，請檢查 STM32 是否有跑進 handle_nvme_read")
                
    except Exception as e:
        print(f"[!] 錯誤: {e}")

if __name__ == "__main__":
    # 測試發送
    send_read_lba = 10
    send_nvme_read_cmd(lba=send_read_lba, length=256)
