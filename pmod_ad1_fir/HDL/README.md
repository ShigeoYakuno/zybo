# ZYBO Z7 PmodAD1 FIR Filter — HDL Design

ZYBO Z7 ボード上で AD7476A (12-bit ADC) からアナログ信号を取得し、FIR ローパスフィルタを通した結果を UART 経由で PC に送信する SystemVerilog 設計です。

## 概要

```
[アナログ入力] → PmodAD1 (AD7476A) → FIR ローパスフィルタ → UART → [PC]
                    SPI 10MHz              63 tap               230400 bps
```

- サンプリングレート: **10 kHz** (125 MHz / 12500)
- ADC 分解能: **12 bit**
- FIR フィルタ: **63 タップ**, 直接形, Q15 係数
- UART: **230400 bps**, 8N1
- ターゲットボード: **Digilent ZYBO Z7** (Zynq-7020, 125 MHz クロック)

---

## ファイル構成

| ファイル | モジュール名 | 説明 |
|---|---|---|
| `top_pmod_ad1.sv` | `zybo_z7_top` | トップモジュール。全サブモジュールの接続とリセット制御 |
| `uart_controller.sv` | `adc_realtime_uart` | ADC サンプリング・FIR フィルタ・UART 送信の統合制御 |
| `ad7476.sv` | `pmod_ad1_controller` | AD7476A SPI 制御 (10 MHz SPI, 12-bit 取得) |
| `fir_direct.sv` | `fir_direct` | パイプライン加算ツリー型 FIR フィルタ本体 |
| `fir_coef.sv` | `filter_fir` | FIR フィルタ係数ラッパ (63 tap ローパスフィルタ) |
| `tx.sv` | `tx` | UART 送信モジュール (8N1, パラメータ化ボーレート) |
| `rx.sv` | `rx` | UART 受信モジュール (8N1, メタステーブル対策付き) |
| `pmod_ad1.xdc` | — | ZYBO Z7 ピン割り当て・タイミング制約 |

---

## モジュール詳細

### top_pmod_ad1.sv — `zybo_z7_top`

ZYBO Z7 全体の最上位モジュール。

- **クロック**: K17 (125 MHz)
- **リセット**: BTN0 の立ち上がりエッジでシステムリセットを解除
  - 初期状態はリセット中。BTN0 を一度押すことで動作開始
- **PmodAD1**: JD コネクタ (CS_N: T14, D0: T15, SCLK: R14)
- **UART**: J15 (RX) / H15 (TX)
- **LED0–2**: ADC FSM ステート表示、**LED3**: リセット状態

### uart_controller.sv — `adc_realtime_uart`

ADC・FIR・UART を統合するメインコントローラ。

**UART コマンド**

| コマンド (ASCII) | 動作 |
|---|---|
| `A` (0x41) | Raw ADC データのストリーミング開始 |
| `F` (0x46) | FIR フィルタ済みデータのストリーミング開始 |
| `S` (0x53) | サンプリング停止 |
| `C` (0x43) | LED ON |
| `c` (0x63) | LED OFF |

**データ送信フォーマット**

1 サンプルを 2 バイトで送信 (12-bit データ):

```
Byte 1: 0x0X  (上位 4 bit = 0, 下位 4 bit = data[11:8])
Byte 2: 0xYY  (data[7:0])
```

**ADC → FIR の接続**

```
ADC 12-bit → 16-bit ゼロ拡張 → fir_din (signed Q15 入力)
fir_dout (signed Q15 出力) → クリッピング (0–4095) → 12-bit 出力
```

### ad7476.sv — `pmod_ad1_controller`

AD7476A を SPI で制御する FSM。

| ステート | 動作 |
|---|---|
| `IDLE` | `start_conv` 待機 |
| `CS_SETUP` | CS_N を Low に落とす |
| `SHIFT_DATA` | 16 クロックで MISO データをシフトイン |
| `CS_HOLD` | CS_N を High に戻す |
| `DONE` | `shift_reg[11:0]` を `adc_data` に出力 |

- SPI クロック: 125 MHz / (2 × 10 MHz) = **6.25 MHz 相当**  
  ※ CLK_DIV = 6 で分周
- CPOL=0, CPHA=0 (SCLK の立ち上がりでサンプリング)
- AD7476A 出力フォーマット: 先頭 4 ビットは無効、以降 12 ビットが有効データ

### fir_direct.sv — `fir_direct`

パイプライン加算ツリーを用いた直接形 FIR フィルタ。

**パラメータ**

| パラメータ | デフォルト | 説明 |
|---|---|---|
| `tap_len` | 63 | フィルタタップ数 |
| `data_width` | 16 | データビット幅 |
| `coef_width` | 16 | 係数ビット幅 |

**パイプライン構造**

```
入力シフトレジスタ (tap_len 段)
  ↓ 1クロック
並列乗算 (tap_len 個の乗算器) ← DSP ブロック使用
  ↓ 1クロック
パイプライン加算ツリー (log2(tap_len) = 6 段)
  ↓
スケーリング (>>> 15, Q15 正規化を元に戻す)
  ↓
サチュレーション処理 (±32767 クリッピング)
```

- **レイテンシ**: 1 (乗算) + 6 (加算ツリー) = **7 クロック**
- 電源投入後、パイプラインが満たされるまで出力を 0 に固定

### fir_coef.sv — `filter_fir`

63 タップ ローパスフィルタの係数を保持するラッパモジュール。

- 係数は **Q15 フォーマット** (32767 = 1.0) で正規化
- 対称係数 (FIR 線形位相特性)
- ピーク係数: 994 (ほぼ直流成分のみ通過)

係数例 (先頭/末尾は同一、中央が最大):
```
77, 50, 66, ..., 991, 994, 991, ..., 66, 50, 77
```

### tx.sv — `tx`

8N1 UART 送信モジュール。

- `act` 信号 (1 クロックパルス) で送信開始
- `busy` 信号で送信中を通知
- FSM: `IDLE → START → DATA → STOP → FINISH`
- デフォルト `div_ratio=434` (50 MHz / 115200 bps)  
  本設計では `div_ratio=542` (125 MHz / 230400 bps) で使用

### rx.sv — `rx`

8N1 UART 受信モジュール。

- 2 段 FF でメタステーブル対策
- `valid` 信号 (1 クロックパルス) で受信完了を通知
- `err` 信号でストップビットエラーを通知
- デフォルト `div_ratio=868` (100 MHz / 115200 bps)  
  本設計では `div_ratio=542` で使用

---

## ピン割り当て (pmod_ad1.xdc)

| 信号 | ピン | 説明 |
|---|---|---|
| `sys_clk` | K17 | 125 MHz システムクロック |
| `ad_cs_n` | T14 (JD1_P) | ADC チップセレクト |
| `ad_d0` | T15 (JD1_N) | ADC データ出力 |
| `ad_sclk` | R14 (JD2_N) | ADC SPI クロック |
| `uart_rx_i` | J15 | UART RX |
| `uart_tx_o` | H15 | UART TX |
| `btn_reset` | K18 (BTN0) | システムリセット |
| `led[0:3]` | M14, M15, G14, D18 | ユーザー LED |
| `data_ready` | F17 (LED6_G) | データ更新インジケータ |

---

## 使用方法

1. Vivado でプロジェクトを作成し、上記ファイルをすべて追加
2. `pmod_ad1.xdc` を制約ファイルとして設定
3. トップモジュールを `zybo_z7_top` に設定してビットストリーム生成
4. ZYBO Z7 に書き込み後、**BTN0 を押してシステム起動**
5. シリアルターミナル (230400 bps, 8N1) を接続
6. コマンド送信:
   - `A` → Raw ADC データのストリーミング開始
   - `F` → FIR フィルタ済みデータのストリーミング開始
   - `S` → 停止

---

## ブロック図

```
                    ┌─────────────────────────────────────────┐
                    │             zybo_z7_top                 │
                    │                                         │
  BTN0 (K18) ──────┤ btn_reset    rst_n                      │
  CLK (K17)  ──────┤ sys_clk      clk                        │
                    │                                         │
                    │    ┌────────────────────────────────┐   │
                    │    │       adc_realtime_uart         │   │
                    │    │                                 │   │
  JD (T14/T15/R14)─┤────┤ PmodAD1  ┌─────────────────┐   │   │
                    │    │ SPI I/F  │pmod_ad1_ctrl     │   │   │
                    │    │          │  AD7476A 12-bit  │   │   │
                    │    │          └────────┬────────┘   │   │
                    │    │                   │ adc_data   │   │
                    │    │          ┌────────▼────────┐   │   │
                    │    │          │   filter_fir     │   │   │
                    │    │          │  FIR 63-tap LPF  │   │   │
                    │    │          └────────┬────────┘   │   │
                    │    │                   │ fir_dout   │   │
                    │    │          ┌────────▼────────┐   │   │
  UART RX (J15) ───┤────┤──── rx   │  TX FSM         │───┤───┤── UART TX (H15)
                    │    │          └─────────────────┘   │   │
                    │    └────────────────────────────────┘   │
                    └─────────────────────────────────────────┘
```

---

## 開発環境

- **EDA ツール**: Xilinx Vivado 2020.x 以降
- **言語**: SystemVerilog (IEEE 1800-2012)
- **ターゲット**: Digilent ZYBO Z7-20 (Zynq XC7Z020-1CLG400C)
