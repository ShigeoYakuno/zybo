// ZYBO Z7 PmodAD1 (AD7476A) SPI制御モジュール
// ZYBO Z7 PmodAD1 (AD7476A) SPI制御モジュール
module pmod_ad1_controller (
    input  logic        clk,            // システムクロック (125MHz)
    input  logic        rst_n,          // リセット (アクティブLow)
    
    // PmodAD1 インターフェース (JA1-JA4に接続)
    output logic        ad_cs_n,        // ~CS (JA1)
    input  logic        ad_d0,          // D0  (JA2)
    input  logic        ad_d1,          // D1  (JA3) - 未使用 (AD7476Aでは)
    output logic        ad_sclk,        // SCLK (JA4)
    
    // 制御・データインターフェース
    input  logic        start_conv,     // 変換開始トリガー
    output logic        conv_done,      // 変換完了フラグ
    output logic [11:0] adc_data,       // ADC変換結果 (12bit)
    output logic        data_valid      // データ有効フラグ
);

    // パラメータ
    parameter CLK_FREQ = 125_000_000;   // システムクロック周波数
    parameter SPI_FREQ = 10_000_000;    // SPI通信周波数 (10MHz)
    parameter CLK_DIV = CLK_FREQ / (2 * SPI_FREQ); // クロック分周比
    
    // 状態定義
    typedef enum logic [2:0] {
        IDLE,
        CS_SETUP,
        SHIFT_DATA,
        CS_HOLD,
        DONE
    } state_t;
    
    // 内部信号
    state_t current_state, next_state;
    logic [$clog2(CLK_DIV)-1:0] clk_div_cnt;
    logic spi_clk_en;
    logic [4:0] bit_cnt;
    logic [15:0] shift_reg;
    logic cs_n_reg;
    logic sclk_reg;
    
    // クロック分周器 (SPI通信クロック生成)
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            clk_div_cnt <= 0;
            spi_clk_en <= 1'b0;
        end else begin
            if (clk_div_cnt >= CLK_DIV - 1) begin
                clk_div_cnt <= 0;
                spi_clk_en <= 1'b1;
            end else begin
                clk_div_cnt <= clk_div_cnt + 1;
                spi_clk_en <= 1'b0;
            end
        end
    end
    
    // 状態遷移
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_state <= IDLE;
        end else begin
            current_state <= next_state;
        end
    end
    
    // 次状態決定
    always_comb begin
        next_state = current_state;
        
        case (current_state)
            IDLE: begin
                if (start_conv) begin
                    next_state = CS_SETUP;
                end
            end
            
            CS_SETUP: begin
                if (spi_clk_en) begin
                    next_state = SHIFT_DATA;
                end
            end
            
            SHIFT_DATA: begin
                if (spi_clk_en && bit_cnt >= 16) begin
                    next_state = CS_HOLD;
                end
            end
            
            CS_HOLD: begin
                if (spi_clk_en) begin
                    next_state = DONE;
                end
            end
            
            DONE: begin
                next_state = IDLE;
            end
            
            default: next_state = IDLE;
        endcase
    end
    
    // 制御信号生成
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cs_n_reg <= 1'b1;
            sclk_reg <= 1'b0;
            bit_cnt <= 0;
            shift_reg <= 16'h0000;
            conv_done <= 1'b0;
            adc_data <= 12'h000;
            data_valid <= 1'b0;
        end else begin
            conv_done <= 1'b0;
            data_valid <= 1'b0;
            
            case (current_state)
                IDLE: begin
                    cs_n_reg <= 1'b1;
                    sclk_reg <= 1'b0;
                    bit_cnt <= 0;
                    shift_reg <= 16'h0000;
                end
                
                CS_SETUP: begin
                    cs_n_reg <= 1'b0;  // CSを下げる
                    sclk_reg <= 1'b0;
                end
                
                SHIFT_DATA: begin
                    if (spi_clk_en) begin
                        if (bit_cnt < 16) begin
                            // クロック立ち上がりでデータ取得
                            sclk_reg <= 1'b1;
                            shift_reg <= {shift_reg[14:0], ad_d0};
                            bit_cnt <= bit_cnt + 1;
                        end else begin
                            sclk_reg <= 1'b0;
                        end
                    end else if (sclk_reg) begin
                        // クロック立ち下がり
                        sclk_reg <= 1'b0;
                    end
                end
                
                CS_HOLD: begin
                    cs_n_reg <= 1'b1;  // CSを上げる
                    sclk_reg <= 1'b0;
                end
                
                DONE: begin
                    // AD7476Aの出力フォーマット：最初の4ビットは無効、次の12ビットが有効データ
                    adc_data <= shift_reg[11:0];  // 下位12ビットを取得
                    data_valid <= 1'b1;
                    conv_done <= 1'b1;
                end
            endcase
        end
    end
    
    // 出力信号割り当て
    assign ad_cs_n = cs_n_reg;
    assign ad_sclk = sclk_reg;

endmodule