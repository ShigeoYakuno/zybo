import serial

ser = serial.Serial('COM13', 230400)

# デバッグモード開始
ser.write(b'D')

# 最初の10サンプル確認
for i in range(10):
    raw_high = ser.read(1)[0]    # SPI生データ上位
    raw_low = ser.read(1)[0]     # SPI生データ下位  
    adc_high = ser.read(1)[0]    # ADC値上位
    adc_low = ser.read(1)[0]     # ADC値下位
    
    raw_data = (raw_high << 8) | raw_low
    adc_data = (adc_high << 8) | adc_low
    
    print(f"Sample {i}: Raw=0x{raw_data:04X}, ADC={adc_data}")