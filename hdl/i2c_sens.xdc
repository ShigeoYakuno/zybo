# 修正版 ZYBO Z7 制約ファイル（XDCファイルに合わせて修正）


# PmodAD1 接続 (JDコネクタ)
set_property PACKAGE_PIN T14 [get_ports IIC_0_scl_io]   ; # JD1_P
set_property PACKAGE_PIN T15 [get_ports IIC_0_sda_io]     ; # JD1_N  
set_property IOSTANDARD LVCMOS33 [get_ports IIC_0_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports IIC_0_sda_io]

