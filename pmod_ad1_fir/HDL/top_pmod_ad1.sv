// 修正版トップモジュール (ZYBO Z7ボード全体の制御)
module zybo_z7_top (
    // システムクロック (K17 - 125MHz)
    input  logic        sys_clk,
    
    // リセット (BTN0)
    input  logic        btn_reset,
    
    // PmodAD1 接続 (JD1-JD4) - XDCファイルに合わせて修正
    output logic        ad_cs_n,        // JD1_P (T14)
    input  logic        ad_d0,          // JD2_P (P14)
    input  logic        ad_d1,          // JD3_P (U14) - 未使用
    output logic        ad_sclk,        // JD4_P (V17)
    
    // UART インターフェース
    input  logic        uart_rx_i,      // UART RX入力 (J15)
    output logic        uart_tx_o,      // UART TX出力 (H15)
    
    // ユーザースイッチ
    input  logic [3:0]  sw,             // SW0-SW3
    
    // ユーザーLED
    output logic [3:0]  led,            // LED0-LED3
    
    // ユーザーボタン
    input  logic [3:0]  btn,            // BTN0-BTN3
    
    // デバッグ出力 (XDCにある追加LED)
    output logic        data_ready,     // LED6_G (F17)
    output logic        buffer_full     // LED6_R (V16) - 現在は未使用
);

    // 内部信号
    logic clk;
    logic rst_n;
    logic btn_reset_sync1, btn_reset_sync2;
    logic btn_reset_edge;
    logic system_reset_n;
    
    // ステータス信号
    logic [11:0] current_adc_data;
    logic data_ready_int;
    logic [13:0] sample_count;
    logic sampling_active;
    logic uart_led;
    
    // システムクロック
    assign clk = sys_clk;
    
    // ボタン同期化とエッジ検出（非同期リセット対策）
    always_ff @(posedge clk) begin
        btn_reset_sync1 <= btn_reset;
        btn_reset_sync2 <= btn_reset_sync1;
    end
    
    assign btn_reset_edge = btn_reset_sync1 & ~btn_reset_sync2;  // 立ち上がりエッジ
    
    // システムリセット制御（BTN0押下でリセット解除、以降は動作継続）
    initial begin
        system_reset_n = 1'b0;  // 初期状態はリセット
    end
    
    always_ff @(posedge clk) begin
        if (btn_reset_edge) begin
            system_reset_n <= 1'b1;  // リセット解除
        end
    end
    
    assign rst_n = system_reset_n;
    
    // リアルタイムADC + UART送信モジュール
    adc_realtime_uart u_adc_uart (
        .clk(clk),
        .rst_n(rst_n),
        .ad_cs_n(ad_cs_n),
        .ad_d0(ad_d0),
        .ad_d1(ad_d1),              // 接続するが未使用
        .ad_sclk(ad_sclk),
        .uart_rx(uart_rx_i),
        .uart_tx(uart_tx_o),
        .current_adc_data(current_adc_data),
        .data_ready(data_ready_int),
        .sample_count(sample_count),
        .sampling_active(sampling_active),
        .uart_led(uart_led)
    );
    
    // LED表示
    always_comb begin
        led[0] = data_ready_int;                // LED0: データ更新 (10kHzでフリッカー)
        led[1] = sampling_active;               // LED1: サンプリング状態
        led[2] = (sample_count[7:0] != 8'h00);  // LED2: サンプルカウント表示
        led[3] = uart_led;                      // LED3: UART制御LED (C/c コマンド)
    end
    
    // デバッグ出力
    assign data_ready = data_ready_int;         // 緑LED: データ更新
    assign buffer_full = 1'b0;                  // 赤LED: 未使用（バッファなし）

endmodule