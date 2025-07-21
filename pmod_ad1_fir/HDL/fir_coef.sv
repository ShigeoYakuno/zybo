module filter_fir(
    input clk,rst,
    input signed [15:0]din,
    output signed [15:0]dout
    );

    parameter tap_len=63;

    /*フィルタ係数をmatlabなどで求める。FPGAで実行するため、Q15フォーマットで正規化*/
    parameter [tap_len-1:0][15:0]fir_coef = '{77, 50, 66, 84, 104, 128, 154, 183, 215, 249, 
                                            286, 325, 366, 408, 453, 498, 543, 589, 635, 680, 
                                            723, 765, 805, 842, 875, 906, 932, 954, 971, 984, 
                                            991, 994, 991, 984, 971, 954, 932, 906, 875, 842, 
                                            805, 765, 723, 680, 635, 589, 543, 498, 453, 408, 
                                            366, 325, 286, 249, 215, 183, 154, 128, 104, 84, 
                                            66, 50, 77};

    fir_direct #(.tap_len(tap_len))fir(
        .clk(clk),
        .rst(rst),
        .cke(1'b1), //今は常にON。拡張用
        .din(din),
        .dout(dout),
        .fir_coef(fir_coef)
        );

endmodule