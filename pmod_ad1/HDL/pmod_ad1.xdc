# 修正版 ZYBO Z7 制約ファイル（XDCファイルに合わせて修正）

# システムクロック (125MHz from K17)
set_property PACKAGE_PIN K17 [get_ports sys_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk]
create_clock -period 8.000 -name sys_clk [get_ports sys_clk]

# PmodAD1 接続 (JDコネクタ)
set_property PACKAGE_PIN T14 [get_ports ad_cs_n]   ; # JD1_P
set_property PACKAGE_PIN T15 [get_ports ad_d0]     ; # JD1_N  
set_property PACKAGE_PIN P14 [get_ports ad_d1]     ; # JD2_P (未使用)
set_property PACKAGE_PIN R14 [get_ports ad_sclk]   ; # JD2_N
set_property IOSTANDARD LVCMOS33 [get_ports ad_cs_n]
set_property IOSTANDARD LVCMOS33 [get_ports ad_d0]
set_property IOSTANDARD LVCMOS33 [get_ports ad_d1]
set_property IOSTANDARD LVCMOS33 [get_ports ad_sclk]

# ユーザーボタン (リセット)
set_property PACKAGE_PIN K18 [get_ports btn_reset]     ; # BTN0
set_property PACKAGE_PIN P16 [get_ports {btn[1]}]      ; # BTN1
set_property PACKAGE_PIN K19 [get_ports {btn[2]}]      ; # BTN2
set_property PACKAGE_PIN Y16 [get_ports {btn[3]}]      ; # BTN3
set_property IOSTANDARD LVCMOS33 [get_ports btn_reset]
set_property IOSTANDARD LVCMOS33 [get_ports {btn[*]}]

# ユーザースイッチ
set_property PACKAGE_PIN G15 [get_ports {sw[0]}]       ; # SW0
set_property PACKAGE_PIN P15 [get_ports {sw[1]}]       ; # SW1
set_property PACKAGE_PIN W13 [get_ports {sw[2]}]       ; # SW2
set_property PACKAGE_PIN T16 [get_ports {sw[3]}]       ; # SW3
set_property IOSTANDARD LVCMOS33 [get_ports {sw[*]}]

# ユーザーLED
set_property PACKAGE_PIN M14 [get_ports {led[0]}]      ; # LED0 (データ更新)
set_property PACKAGE_PIN M15 [get_ports {led[1]}]      ; # LED1 (サンプリング状態)
set_property PACKAGE_PIN G14 [get_ports {led[2]}]      ; # LED2 (サンプルカウント)
set_property PACKAGE_PIN D18 [get_ports {led[3]}]      ; # LED3 (UART制御LED)
set_property IOSTANDARD LVCMOS33 [get_ports {led[*]}]

# デバッグ用LED出力 (追加)
set_property PACKAGE_PIN F17 [get_ports data_ready]    ; # LED6_G
set_property PACKAGE_PIN V16 [get_ports buffer_full]   ; # LED6_R (未使用)
set_property IOSTANDARD LVCMOS33 [get_ports data_ready]
set_property IOSTANDARD LVCMOS33 [get_ports buffer_full]

# UART インターフェース (230400bps)
set_property PACKAGE_PIN J15 [get_ports uart_rx_i]     ; # USB-UART RX J15
set_property PACKAGE_PIN H15 [get_ports uart_tx_o]     ; # USB-UART TX H15
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_i]
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_o]

# 初期値設定（重要：system_reset_nの初期状態）
# set_property INIT 1'b0 [get_cells system_reset_n_reg]


# 分周しているので制約は不要
set_false_path -from [get_ports uart_rx_i]
set_false_path -to [get_ports uart_tx_o]

set_false_path -from [get_ports ad_d0]
set_false_path -to [get_ports ad_cs_n]
set_false_path -to [get_ports ad_sclk]

# ボタン・スイッチの非同期入力制約
set_false_path -from [get_ports {btn[*]}]
set_false_path -from [get_ports btn_reset]
set_false_path -from [get_ports {sw[*]}]

# LEDの出力制約
set_false_path -to [get_ports {led[*]}]
set_false_path -to [get_ports data_ready]
set_false_path -to [get_ports buffer_full]

# クロックドメイン制約
set_false_path -from [get_pins btn_reset_sync*_reg/C] -to [get_pins system_reset_n_reg/D]