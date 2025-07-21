module fir_direct #(
    parameter int tap_len = 63,
    parameter int data_width = 16,
    parameter int coef_width = 16
)(
    input  logic clk,
    input  logic rst,
    input  logic cke,//拡張用未使用(常に1)
    input  logic signed [data_width-1:0] din,
    output logic signed [data_width-1:0] dout,
    input  logic signed [tap_len-1:0][coef_width-1:0] fir_coef
);

// フィルタタップ数、ADCビット数、フィルタ係数ビット数を変更した場合に備え、演算に必要な値を計算
localparam int mul_width = data_width + coef_width; // 16 + 16=32
localparam int acc_width = mul_width + $clog2(tap_len); // 32 + clog2(63) = 32 + 6=38
localparam int num_stages = $clog2(tap_len); // 6

// シフトレジスタ
logic signed [data_width-1:0] sr [tap_len-1:0];
always_ff @(posedge clk) begin
    if (rst) begin
        for (integer i = 0; i < tap_len; i = i + 1) begin
            sr[i] <= '0;
        end
    end else if (cke) begin
        for (integer i = 0; i < tap_len - 1; i = i + 1) begin
            sr[i] <= sr[i+1]; // sr[0]にsr[1]のデータが来る（古いデータを捨てる）
        end
        sr[tap_len-1] <= din; // 最新のdinをsrの最後尾に追加
    end
end

// ステージ0: 並列で乗算処理 「*」を使うとコンパイラがDSPブロックを並列に配置してくれるはず...
logic signed [mul_width-1:0] mul_results [tap_len-1:0];
always_ff @(posedge clk) begin
    if (cke) begin
        for (int i = 0; i < tap_len; i = i + 1) begin
            mul_results[i] <= sr[tap_len - 1 - i] * fir_coef[i];
        end
    end
end

//  ステージ1以降: パイプライン加算ツリー
//  タップ数が変更する場合も考え、自動生成させる
logic signed [acc_width-1:0] p_sums [num_stages:0][tap_len-1:0];
genvar stage, i;
generate
    // ✨ 変更点: assign文をalways_ffブロックに置き換えました ✨
    for (i = 0; i < tap_len; i = i + 1) begin : gen_init_sum
        always_ff @(posedge clk) begin // ステージ0もクロック同期
            if (cke) begin
                p_sums[0][i] <= mul_results[i];
            end
        end
    end

    for (stage = 0; stage < num_stages; stage = stage + 1) begin : gen_adder_stages

        //ステージごとの入力数を計算 stage=0なら63,1なら32,2なら16...
        localparam int num_inputs = (tap_len + (1 << stage) - 1) >> stage;
        //ステージごとの出力数を計算 stage=0なら32,1なら16,2なら8...
        localparam int num_outputs = (num_inputs + 1) >> 1;

        always_ff @(posedge clk) begin
            if (cke) begin
                for (int j = 0; j < num_outputs; j = j + 1) begin
                    if ( (2*j + 1) < num_inputs ) begin //ペアが存在する＝偶数の場合
                        p_sums[stage+1][j] <= p_sums[stage][2*j] + p_sums[stage][2*j+1];
                    end else begin  //ペアが存在しない＝最後の一つ は加算せずそのまま代入
                        p_sums[stage+1][j] <= p_sums[stage][2*j];
                    end
                end
            end
        end
    end
endgenerate

// 最終ステージ(ステージ数＋２):フィルタ通過後の結果を出力
logic signed [acc_width-1:0] mac_out;
assign mac_out = p_sums[num_stages][0];
logic signed [data_width-1:0] dout_reg;
logic signed [acc_width-1:0] scaled_out_reg;

// パイプライン遅延カウンタ
localparam int PIPELINE_LATENCY = 1 + num_stages; // 乗算ステージ(1) + 加算ツリーのステージ数

//サチュレーション用
localparam signed [data_width-1:0] MAX_VAL = (1 << (data_width - 1)) - 1;
localparam signed [data_width-1:0] MIN_VAL = -(1 << (data_width - 1));

//電源ON後、フィルタが通りきるまでは不定になる可能性あるので、出力を0に固定
logic [$clog2(PIPELINE_LATENCY):0] latency_cnt;
logic output_valid;

always_ff @(posedge clk) begin
    if (rst) begin
        latency_cnt <= '0;
        output_valid <= 1'b0;
        dout_reg <= '0;

    end else if (cke) begin
        if (latency_cnt < PIPELINE_LATENCY) begin
            latency_cnt <= latency_cnt + 1;
            dout_reg <= '0; // 電源ON時のみ、パイプラインが満たされるまで0を出力
            output_valid <= 1'b0;
        end else begin
            output_valid <= 1'b1;

            // スケーリング
            scaled_out_reg = mac_out >>> 15; // Q15フォーマットで正規化しているので、ここで元に戻す

            // サチュレーション(飽和処理) 最大最小を超えた場合、丸める
            if (scaled_out_reg > MAX_VAL) begin
                dout_reg <= MAX_VAL;    // max 32767
            end else if (scaled_out_reg < MIN_VAL) begin
                dout_reg <= MIN_VAL;    // min -32768
            end else begin
                dout_reg <= scaled_out_reg[data_width-1:0];
            end
        end
    end
end
assign dout = dout_reg;
endmodule