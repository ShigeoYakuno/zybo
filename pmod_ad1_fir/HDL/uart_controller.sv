// FIRフィルタ対応版 リアルタイムUART送信ADCコントローラ
module adc_realtime_uart (
    input  logic        clk,            // システムクロック (125MHz)
    input  logic        rst_n,          // リセット (アクティブLow)
    
    // PmodAD1 インターフェース
    output logic        ad_cs_n,
    input  logic        ad_d0,
    input  logic        ad_d1,
    output logic        ad_sclk,
    
    // UART インターフェース
    input  logic        uart_rx,
    output logic        uart_tx,
    
    // ステータス信号
    output logic [11:0] current_adc_data,
    output logic        data_ready,
    output logic [13:0] sample_count,   // 送信済みサンプル数
    output logic        sampling_active,
    output logic        uart_led        // UART制御LED
);

    // 内部信号
    logic start_conv;
    logic conv_done;
    logic [11:0] adc_data;
    logic data_valid;
    logic rst_p;                        // リセット (アクティブHigh)
    
    // FIRフィルタ関連信号
    logic signed [15:0] fir_din;        // FIRフィルタ入力 (ADCデータを16bitに拡張)
    logic signed [15:0] fir_dout;       // FIRフィルタ出力
    logic [11:0] fir_output_12bit;      // フィルタ出力を12bitに変換
    
    // 変換間隔制御 (10kHz = 100μs間隔)
    logic [15:0] conv_timer;
    parameter CONV_INTERVAL = 12500;    // 125MHz / 12500 = 10kHz
    
    // サンプリング制御
    logic [13:0] sample_cnt;
    parameter MAX_SAMPLES = 20000;
    logic sampling_enable;
    logic filter_mode;                  // 0: Raw ADC data (A command), 1: Filtered data (F command)
    
    // UART関連信号
    logic [7:0] rx_data;
    logic rx_valid;
    logic [7:0] tx_data;
    logic tx_act;
    logic tx_busy;
    
    // コマンド処理
    logic cmd_start_sampling;           // 'A' コマンド (Raw ADCデータ)
    logic cmd_start_filtered;           // 'F' コマンド (フィルタ済みデータ)
    logic cmd_led_on;                   // 'C' コマンド
    logic cmd_led_off;                  // 'c' コマンド
    
    // データ送信制御
    typedef enum logic [1:0] {
        TX_IDLE,
        TX_HIGH_BYTE,
        TX_LOW_BYTE,
        TX_WAIT
    } tx_state_t;
    
    tx_state_t tx_state;
    logic [7:0] tx_wait_cnt;
    logic [15:0] tx_data_16bit;         // 16bitデータ (上位4bit=0, 下位12bit=ADC/フィルタデータ)
    
    assign rst_p = ~rst_n;
    
    // PmodAD1 制御モジュール
    pmod_ad1_controller u_ad1_ctrl (
        .clk(clk),
        .rst_n(rst_n),
        .ad_cs_n(ad_cs_n),
        .ad_d0(ad_d0),
        .ad_d1(ad_d1),
        .ad_sclk(ad_sclk),
        .start_conv(start_conv),
        .conv_done(conv_done),
        .adc_data(adc_data),
        .data_valid(data_valid)
    );
    
    // FIRフィルタモジュール
    // ADCデータを16bitに拡張してフィルタに入力
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fir_din <= 16'h0000;
        end else begin
            if (data_valid) begin
                // ADCデータ(12bit)を16bitに拡張（0-4095の範囲をそのまま保持）
                fir_din <= {4'b0000, adc_data};
            end
        end
    end
    
    // FIRフィルタインスタンス化
    filter_fir u_fir_filter (
        .clk(clk),
        .rst(rst_p),
        .din(fir_din),
        .dout(fir_dout)
    );
    
    // フィルタ出力を12bitに変換（0-4095の範囲を保持）
    logic fir_data_valid;  // フィルタデータの有効フラグ
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fir_output_12bit <= 12'h000;
            fir_data_valid <= 1'b0;
        end else begin
            // ADCデータが有効な時、フィルタデータも有効とする
            // （実際にはフィルタのパイプライン遅延があるが、連続データなので同期として扱う）
            fir_data_valid <= data_valid;
            
            if (data_valid) begin
                // フィルタ出力をそのまま使用し、サチュレーション処理のみ適用
                if (fir_dout > 16'd4095) begin
                    fir_output_12bit <= 12'hFFF;
                end else if (fir_dout < 16'd0) begin
                    fir_output_12bit <= 12'h000;
                end else begin
                    fir_output_12bit <= fir_dout[11:0];
                end
            end
        end
    end
    
    // UART RXモジュール
    rx #(.div_ratio(542)) u_uart_rx (   // 125MHz / 230400 = 542
        .clk(clk),
        .rst(rst_p),
        .rx_line(uart_rx),
        .rx_data(rx_data),
        .busy(),
        .valid(rx_valid),
        .err()
    );
    
    // UART TXモジュール
    tx #(.div_ratio(542)) u_uart_tx (   // 125MHz / 230400 = 542
        .clk(clk),
        .rst(rst_p),
        .act(tx_act),
        .tx_data(tx_data),
        .tx_line(uart_tx),
        .busy(tx_busy)
    );
    
    // コマンド処理
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cmd_start_sampling <= 1'b0;
            cmd_start_filtered <= 1'b0;
            cmd_led_on <= 1'b0;
            cmd_led_off <= 1'b0;
            uart_led <= 1'b0;
            filter_mode <= 1'b0;
        end else begin
            cmd_start_sampling <= 1'b0;
            cmd_start_filtered <= 1'b0;
            cmd_led_on <= 1'b0;
            cmd_led_off <= 1'b0;
            
            if (rx_valid) begin
                case (rx_data)
                    8'h41: begin  // 'A' - Raw ADCデータのリアルタイム送信開始
                        cmd_start_sampling <= 1'b1;
                        filter_mode <= 1'b0;  // Rawデータモード
                    end
                    8'h46: begin  // 'F' - フィルタ済みデータのリアルタイム送信開始
                        cmd_start_filtered <= 1'b1;
                        filter_mode <= 1'b1;  // フィルタモード
                    end
                    8'h43: begin  // 'C' - LED ON
                        cmd_led_on <= 1'b1;
                        uart_led <= 1'b1;
                    end
                    8'h63: begin  // 'c' - LED OFF
                        cmd_led_off <= 1'b1;
                        uart_led <= 1'b0;
                    end
                endcase
            end
        end
    end
    
    // サンプリング制御
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sampling_enable <= 1'b0;
            sampling_active <= 1'b0;
        end else begin
            if (cmd_start_sampling || cmd_start_filtered) begin
                sampling_enable <= 1'b1;
                sampling_active <= 1'b1;
            end else if (sample_cnt >= MAX_SAMPLES) begin
                sampling_enable <= 1'b0;
                sampling_active <= 1'b0;
            end
        end
    end
    
    // 変換タイミング制御
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            conv_timer <= 0;
            start_conv <= 1'b0;
        end else begin
            start_conv <= 1'b0;
            
            if (sampling_enable) begin
                if (conv_timer >= CONV_INTERVAL - 1) begin
                    conv_timer <= 0;
                    start_conv <= 1'b1;
                end else begin
                    conv_timer <= conv_timer + 1;
                end
            end else begin
                conv_timer <= 0;
            end
        end
    end
    
    // 16bitデータ準備（RawまたはフィルタデータをMUXで選択）
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_data_16bit <= 16'h0000;
        end else begin
            // データの有効性をチェックしてから設定
            if (filter_mode) begin
                // フィルタモード：フィルタ済みデータを送信
                if (fir_data_valid && sampling_enable) begin
                    tx_data_16bit <= {4'b0000, fir_output_12bit};
                end
            end else begin
                // Rawモード：生ADCデータを送信
                if (data_valid && sampling_enable) begin
                    tx_data_16bit <= {4'b0000, adc_data};
                end
            end
        end
    end
    
    // リアルタイムデータ送信FSM
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_state <= TX_IDLE;
            tx_act <= 1'b0;
            tx_data <= 8'h00;
            tx_wait_cnt <= 0;
        end else begin
            tx_act <= 1'b0;
            
            case (tx_state)
                TX_IDLE: begin
                    // データの有効性をモードに応じてチェック
                    logic data_ready_for_tx;
                    if (filter_mode) begin
                        data_ready_for_tx = fir_data_valid && sampling_enable && sample_cnt < MAX_SAMPLES;
                    end else begin
                        data_ready_for_tx = data_valid && sampling_enable && sample_cnt < MAX_SAMPLES;
                    end
                    
                    if (data_ready_for_tx) begin
                        tx_state <= TX_HIGH_BYTE;
                        tx_data <= tx_data_16bit[15:8];  // 上位8bit送信
                        tx_act <= 1'b1;
                    end
                end
                
                TX_HIGH_BYTE: begin
                    if (!tx_busy) begin
                        tx_state <= TX_LOW_BYTE;
                        tx_data <= tx_data_16bit[7:0];   // 下位8bit送信
                        tx_act <= 1'b1;
                    end
                end
                
                TX_LOW_BYTE: begin
                    if (!tx_busy) begin
                        tx_state <= TX_WAIT;
                        tx_wait_cnt <= 5;  // 少し待機
                    end
                end
                
                TX_WAIT: begin
                    if (tx_wait_cnt > 0) begin
                        tx_wait_cnt <= tx_wait_cnt - 1;
                    end else begin
                        tx_state <= TX_IDLE;
                    end
                end
            endcase
        end
    end
    
    // サンプルカウンタ（データ送信完了時にインクリメント）
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sample_cnt <= 0;
        end else begin
            if (cmd_start_sampling || cmd_start_filtered) begin
                sample_cnt <= 0;  // リセット
            end else if (tx_state == TX_LOW_BYTE && !tx_busy && sample_cnt < MAX_SAMPLES) begin
                sample_cnt <= sample_cnt + 1;  // データ送信完了時にインクリメント
            end
        end
    end
    
    // 現在のADCデータ保持
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_adc_data <= 12'h000;
            data_ready <= 1'b0;
        end else begin
            if (data_valid) begin
                current_adc_data <= adc_data;
                data_ready <= 1'b1;
            end else begin
                data_ready <= 1'b0;
            end
        end
    end
    
    // 出力信号割り当て
    assign sample_count = sample_cnt;

endmodule