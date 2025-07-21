import serial
import time

ser = serial.Serial('COM13', 230400, timeout=2)  # 1秒タイムアウト

ser.write(b'F')
time.sleep(0.1)  # 少し待機

data_list = []
try:
    for i in range(10000):
        raw_high = ser.read(1)
        raw_low = ser.read(1)
        
        if len(raw_high) == 0 or len(raw_low) == 0:
            print(f"データ終了: {i}サンプル受信")
            break
            
        adc_data = (raw_high[0] << 8) | raw_low[0]
        data_list.append(adc_data)

    # ファイル書き込み
    with open('raw_ad_fir.txt', 'w') as f:
        for data in data_list:
            f.write(f"{data}\n")
            
    print(f"データ取得完了: {len(data_list)}サンプル保存")
    
except KeyboardInterrupt:
    print("ユーザーによる中断")
finally:
    ser.close()