# Zybo FreeRTOS サンプル集

Xilinx Zybo (Zynq-7010) ボード向けの FreeRTOS サンプルプロジェクト集です。  
Vivado でハードウェアを設計し、Vitis でソフトウェアを開発します。

---

## ハードウェア構成

| ツール | バージョン |
|--------|-----------|
| Vivado | 2024.x    |
| Vitis  | 2024.x    |
| ボード | Digilent Zybo (Zynq-7010) |

### ブロックデザイン (`project_1.srcs/`)

| IP | 用途 |
|----|------|
| Processing System 7 (PS7) | ARM Cortex-A9 コア |
| AXI GPIO 0 | LED 出力 |
| AXI GPIO 1 | プッシュボタン入力 |
| AXI IIC 0 | I2C センサー接続 |
| AXI SmartConnect | AXI バスインターコネクト |
| Proc Sys Reset | リセット管理 |

### HDL ファイル (`hdl/`)

| ファイル | 内容 |
|----------|------|
| `freeRTOS_simple.xsa` | Vitis インポート用ハードウェア仕様ファイル |
| `freertos_simple.bit` | FPGA ビットストリーム |
| `freertos_zybo.xdc`   | ピン制約ファイル (XDC) |

---

## Vitis プロジェクト

### 1. `vitis/` — FreeRTOS 最小構成

2つのタスクが 1 秒周期で UART にメッセージを出力するシンプルなサンプルです。

```
Task1: "hello! zybo from task1"
Task2: "hello! zybo from task2"
```

| 機能 | 詳細 |
|------|------|
| FreeRTOS | タスク2本 (tskIDLE_PRIORITY+1) |
| 通信 | UART (115200 bps) |

---

### 2. `vitis_lwip/` — FreeRTOS + lwIP UDP イーサネット

lwIP の SOCKET_API モードを使い、UDP でイーサネット通信するサンプルです。

| 機能 | 詳細 |
|------|------|
| FreeRTOS | マルチタスク |
| ネットワーク | lwIP (SOCKET_API), GEM0 |
| プロトコル | UDP |

---

### 3. `vitis_lwip_sens/` — FreeRTOS + lwIP + I2C センサー

I2C 接続の温湿度・気圧センサー (AHT20 / BME280) の値を取得し、  
UDP で送信するサンプルです。

| 機能 | 詳細 |
|------|------|
| FreeRTOS | 5 タスク構成 (NET / UART / SENS / LED / UDP) |
| ネットワーク | lwIP (SOCKET_API), GEM0 |
| プロトコル | UDP (ポート 5000) |
| センサー | AHT20 (温湿度) / BME280 (温湿度・気圧) via AXI IIC |
| IP アドレス | 192.168.1.100 (固定) |

**タスク構成:**

| タスク | 役割 |
|--------|------|
| `net_task`       | lwIP 初期化、GEM0 セットアップ (起動後に自己削除) |
| `udp_sensor_task`| UDP ポート 5000 で受信→センサー値をエコーバック |
| `sens_task`      | 5 秒周期で I2C センサー読み出し、グローバルキャッシュ更新 |
| `uart_task`      | UART コマンド受付 (i/t/r/b/h) |
| `led_task`       | LED シーケンサ (システム動作確認用) |

---

## 使い方

### Vivado でハードウェアを再ビルドする場合

1. `project_1.xpr` を Vivado で開く
2. Generate Bitstream を実行
3. `hdl/` 以下のファイルをエクスポートした XSA で更新

### Vitis でアプリをビルドする場合

1. Vitis を起動し、使いたいフォルダ (`vitis` / `vitis_lwip` / `vitis_lwip_sens`) をワークスペースとして開く
2. `hdl/freeRTOS_simple.xsa` を使ってプラットフォームを作成 (初回のみ)
3. ビルド & 実行

> **Note:** ビットストリーム (`hdl/freertos_simple.bit`) は既にリポジトリに含まれているため、  
> Vivado を再実行しなくても FPGA をプログラムできます。

---

## ディレクトリ構成

```
freertos_simple/
├── hdl/                        # ハードウェア成果物 (XSA / BIT / XDC)
├── project_1.xpr               # Vivado プロジェクトファイル
├── project_1.srcs/
│   └── sources_1/bd/design_1/ # ブロックデザイン (*.bd, *.xci)
├── vitis/                      # FreeRTOS 最小サンプル
├── vitis_lwip/                 # FreeRTOS + lwIP UDP サンプル
└── vitis_lwip_sens/            # FreeRTOS + lwIP + I2C センサーサンプル
```
