# 修正版 ZYBO Z7 制約ファイル（XDCファイルに合わせて修正）


# sens(JDコネクタ)
set_property PACKAGE_PIN T14 [get_ports iic_sens_scl_io]   ; # JD1_P
set_property PACKAGE_PIN T15 [get_ports iic_sens_sda_io]     ; # JD1_N  
set_property IOSTANDARD LVCMOS33 [get_ports iic_sens_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_sens_sda_io]

##Single LED
set_property -dict { PACKAGE_PIN M14   IOSTANDARD LVCMOS33 } [get_ports { led_tri_o[0] }];
set_property -dict { PACKAGE_PIN M15   IOSTANDARD LVCMOS33 } [get_ports { led_tri_o[1] }];
set_property -dict { PACKAGE_PIN G14   IOSTANDARD LVCMOS33 } [get_ports { led_tri_o[2] }];
set_property -dict { PACKAGE_PIN D18   IOSTANDARD LVCMOS33 } [get_ports { led_tri_o[3] }];

#スライドSW
set_property PACKAGE_PIN G15 [get_ports {sw_tri_i[0]}]       ; # SW0
set_property PACKAGE_PIN P15 [get_ports {sw_tri_i[1]}]       ; # SW1
set_property PACKAGE_PIN W13 [get_ports {sw_tri_i[2]}]       ; # SW2
set_property PACKAGE_PIN T16 [get_ports {sw_tri_i[3]}]       ; # SW3
set_property IOSTANDARD LVCMOS33 [get_ports {sw_tri_i[*]}]

# pushボタン
set_property PACKAGE_PIN K18 [get_ports {btn_tri_i[0]}]     ; # BTN0
set_property PACKAGE_PIN P16 [get_ports {btn_tri_i[1]}]      ; # BTN1
set_property PACKAGE_PIN K19 [get_ports {btn_tri_i[2]}]      ; # BTN2
set_property PACKAGE_PIN Y16 [get_ports {btn_tri_i[3]}]      ; # BTN3
set_property IOSTANDARD LVCMOS33 [get_ports {btn_tri_i[*]}]

