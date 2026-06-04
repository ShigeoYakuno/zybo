# i2c_sens_eth — Zybo Z7-20 I2C センサー + Ethernet UDP 送信

Zybo Z7-20 (Xilinx Zynq-7000) で I2C センサーの値を読み取り、UDP で送信するベアメタルプロジェクト。

## ハードウェア構成

| 項目 | 内容 |
|------|------|
| ボード | Zybo Z7-20 |
| センサー 1 | BMP280 (温度・気圧・湿度) — I2C アドレス 0x77 |
| センサー 2 | AHT20 (温度・湿度) — I2C アドレス 0x38 |
| I2C コア | AXI IIC (Dynamic Mode) |
| Ethernet | GEM0 (RTL8211E PHY) |

### ピン接続

| 信号 | Zybo Z7-20 ピン |
|------|----------------|
| IIC SDA | JE1 (MIO / EMIO 経由) |
| IIC SCL | JE2 (MIO / EMIO 経由) |

## ソフトウェア構成

```
vitis/app_component/src/
├── main.c        — エントリポイント、初期化・メインループ
├── sensors.c/h   — AXI IIC 直接制御、BMP280/AHT20 ドライバ
├── eth.c/h       — EMACPs (GEM0) 初期化・DMA リングバッファ
├── net.c/h       — ARP / ICMP / UDP 手組みスタック
├── uart_cmd.c/h  — UART コマンドハンドラ
└── config.h      — IP アドレス・MACアドレス・ポート設定
```

- **開発環境**: Vitis 2024.1 (Standalone / Baremetal)
- **ビルドシステム**: CMake (Vitis 自動生成)

## ネットワーク設定

[config.h](vitis/app_component/src/config.h) を編集して使用環境に合わせてください。

```c
#define BOARD_IP0  192
#define BOARD_IP1  168
#define BOARD_IP2    1
#define BOARD_IP3  100   // ボードの IP アドレス

#define DEST_IP0   192
#define DEST_IP1   168
#define DEST_IP2     1
#define DEST_IP3    20   // UDP 送信先 IP

#define UDP_SRC_PORT  5000
#define UDP_DEST_PORT 5000
```

MAC アドレスは `net.c` 内の `BOARD_MAC` で設定。

## UART コマンド (115200 bps)

| キー | 動作 |
|------|------|
| `i` | IP / MAC / 送信先アドレス表示 |
| `s` | 送受信統計表示 |
| `t` | センサー値をシリアルに表示 |
| `u` | センサー値をシリアルに表示 + UDP 送信 |
| `r` | I2C センサー再初期化 |
| `b` | I2C バススキャン (0x08–0x77) |
| `h` | ヘルプ表示 |

## UDP ペイロード形式

```
T=25.3,H=48.2,P=1013.2
```

| フィールド | 単位 | ソース |
|-----------|------|--------|
| T | °C (小数第1位) | AHT20 (優先) / BMP280 |
| H | % (小数第1位) | AHT20 |
| P | hPa (小数第1位) | BMP280 |

## 受信側サンプル (Python)

```python
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 5000))
while True:
    data, addr = sock.recvfrom(256)
    print(addr, data.decode())
```

## ビルド手順

1. Vivado で `project_1.xpr` を開きビットストリームを生成、`design_1_wrapper_2.xsa` をエクスポート
2. Vitis で `vitis/` ディレクトリをワークスペースとして開く
3. `app_component` をビルド
4. JTAG 経由でボードに書き込み

## AXI IIC 実装メモ

AXI IIC Dynamic Mode の動作で複数のハマりポイントがありました。詳細は
[vitis/docs/axi_iic_debug_log.md](vitis/docs/axi_iic_debug_log.md) を参照してください。

主なポイント:
- `IISR.BNB` はレベルシグナルで W1C が効かない → トランザクション完了判定に `SR.BB` を使用
- IISR に `0xFF` を書き込むと全ビットがセット (W1C の逆) → 実際の IISR 値のみを書く
- 受信完了時に `STOP` が遅延する → `TX_ERROR` (マスター NACK) を受信完了シグナルとして使用
