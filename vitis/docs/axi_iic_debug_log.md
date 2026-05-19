# AXI IIC デバッグ記録 — Zybo I2C Sensor Ethernet

## 構成

- **ボード**: Zybo (Xilinx Zynq-7000)
- **I2C センサー**: BMP280 (アドレス 0x77, chip ID 0x58) + AHT20 (アドレス 0x38)
- **I2C インターフェース**: AXI IIC コア (Dynamic Mode)
- **開発環境**: Vitis (Standalone / Baremetal)
- **主要ファイル**: `app_component/src/sensors.c`

---

## AXI IIC Dynamic Mode の基礎知識

### DTR フォーマット
```
bit9 = STOP フラグ  (XIIC_TX_DYN_STOP_MASK  = 0x200)
bit8 = START フラグ (XIIC_TX_DYN_START_MASK = 0x100)
bit7:0 = データ/アドレスバイト
```

### IISR ビット定義
| ビット | マスク | 意味 |
|--------|--------|------|
| 0 | 0x01 | ARB_LOST: バスアービトレーション敗北 |
| 1 | 0x02 | TX_ERROR: NACKまたは読み出し完了(マスターNACK) |
| 2 | 0x04 | TX_EMPTY: TX FIFO 空 |
| 3 | 0x08 | RX_FULL: RX FIFO がしきい値到達 |
| 4 | 0x10 | BNB: Bus Not Busy (STOP後にセット) |
| 5 | 0x20 | AAS: スレーブとしてアドレスされた |
| 6 | 0x40 | NAAS: スレーブアドレス不一致 |
| 7 | 0x80 | TX_HALF: TX FIFO ハーフ空 |

IISR は **W1C (Write-1-to-Clear)**: 1を書くとそのビットをクリア。

### SOFTR後の初期状態
- IISR = 0xD0 (TX_HALF | NAAS | BNB)
- SR   = 0xC0 (TX_FIFO_EMPTY | RX_FIFO_EMPTY)
- CR   = 0x01 (ENABLE)

### TX_ERROR の二重の意味
- **書き込みトランザクション**: スレーブNACK = エラー
- **読み出しトランザクション**: マスターNACK (最終バイト) = **正常完了**

---

## 発見された問題と解決策

### 問題1: `b` コマンド (iic_scan) でI2C波形が出ない

**症状**: オシロスコープで波形ゼロ。  
**根本原因**: `iic_tx_reset()` 内のIISR W1Cクリアが1回リードバックのみで、AXIバスへの伝播が完了する前にDTR書き込みが行われていた。  
**修正**: `iic_tx_reset()` のW1Cクリア後のリードバックを2回に増やした。また、全I2C関数のDTR書き込み後にもIISR W1Cクリア+リードバックを追加。

---

### 問題2: BME280 アドレス誤り

**症状**: スキャンで 0x38 (AHT20) と 0x77 のみ検出。0x76に応答なし。  
**修正**: `BME280_ADDR` を `0x76u` → `0x77u` に変更。

---

### 問題3: `iic_read` / `iic_recv` がタイムアウト (BNBが立たない)

**症状**: 読み出しが常に失敗。`[W]TO=CA` ログ。  
**根本原因**: AXI IIC は読み出し完了時に自動でSTOPを生成せず、BNBが立たない。TX_ERROR (マスターNACK) が読み出し完了を示す。  
**修正**:
- `wait_iisr` の ok_mask を `BNB` → `TX_ERROR | BNB` に変更
- err_mask から `TX_ERROR` を削除 (読み出しでは正常完了を意味するため)
- RX FIFO空チェックでアドレスNACKと正常完了を区別:
  ```c
  if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK)
      return -1;  // アドレスNACK = データなし
  ```

---

### 問題4: 連続読み出し失敗 (スタックTX_ERROR)

**症状**: chip ID読み出し成功後、キャリブレーション読み出しが即座に失敗。  
**根本原因**: 読み出し完了でTX_ERRORがセットされた後、`iic_tx_reset()` (SOFTR無し) ではTX_ERRORがクリアされずスタック。次のI2C操作の `wait_iisr` が即座に -1 を返す。  
**修正**: 全I2C操作関数 (`iic_send`, `iic_recv`, `iic_read`, `iic_probe`) で `iic_tx_reset()` を `iic_reset()` (SOFTR経由 `XIic_DynInit`) に置き換え。

---

### 問題5: iic_send / iic_recv / iic_read が全て失敗 (`[W]E0=EF`)

**症状**: `iic_reset()` 後にIISR=0xD0 (正常) だが、DTR書き込み + IISR W1Cクリア後にIISR=0xEF が即座に現れる。  
**デバッグで判明した事実**:
```
SR=C0 IISR0=D0 / dtr1 IISR=D0 TFO=0 / dtr_all IISR=D0 TFO=2 / clr->EF
```
- TFO=0: DTR1書き込み直後にTX FIFOが空(コアがエントリを即座にポップ)
- TFO=2: 3エントリ書き込み後、DTR2+DTR3が残留
- `clr->EF`: W1Cクリアの瞬間に TX_EMPTY(0x04) + ARB_LOST(0x01) + TX_ERROR(0x02) 等が即立つ

**根本原因**:  
AXI IIC コアはDTR書き込みと同時にトランザクションを開始する。**進行中のトランザクションの状態でIISRに0xFFを書き込む (W1Cクリア) と、コアがトランザクションをアボートする**。その結果、TX FIFOが即座に空になりARB_LOSTとTX_ERRORが同時に発生する。

`iic_probe` は動作する理由: START+STOP+アドレスを1つのDTRワードに書くため、コアが一括処理し、W1Cクリアの時点でFIFOはすでに空。アボートするデータが存在しない。

**修正**: IISR W1Cクリアを **DTR書き込みの前** に移動。
```c
// Before (壊れている)
// ... DTR書き込み3回 ...
XIic_WriteReg(IISR, 0xFF);  // ← 進行中トランザクションをアボートする
(void)XIic_ReadReg(IISR);

// After (修正後)
XIic_WriteReg(IISR, 0xFF);  // ← SOFTRの起動BNBフラグのみをクリア
(void)XIic_ReadReg(IISR);
// ... DTR書き込み3回 ...
// wait_iisr で実際の完了を待つ
```

**理由**: SOFTR後のIISR=0xD0にはBNB(0x10)が含まれ、これをクリアせずにwait_iisr(BNB,...)を呼ぶと即座に0が返る(偽の成功)。W1Cで一度だけクリアしておけば、次のBNBセットはトランザクションのSTOP完了時のみ発生するため、正確な完了検出ができる。

---

## 問題6: IISR W1Cクリアに 0xFF を書くと全ビットがセットされる

### 症状

Problem5の修正 (W1CをDTR前に移動) 後もセンサー検出失敗。W1C前後のIISRを確認:

```
[IIC] センサー再初期化...
[SND d=77 b=D0 a=FF]   ← b=W1C前=0xD0(正常), a=W1C後=0xFF(異常!)
[BME] rst=-1
[SENS] BME280 not found
[RCV d=38 b=D0 a=FF]
[AHT] st=-1 FF
[SENS] AHT20 not found
```

- `b=0xD0`: `iic_reset()` 後のIISRは正常（TX_HALF|NAAS|BNB）
- `a=0xFF`: `XIic_WriteReg(IISR, 0xFF)` + リードバック後に **全ビットが立っている**

W1Cなら `0xD0` のビットがクリアされて `0x00`（またはBNBが即再立して `0x10`）になるはずが、逆に全ビットが立っている。これは **W1Cとして機能していない**。

`wait_iisr` の `err_mask = TX_ERROR(0x02) | ARB_LOST(0x01)` に即ヒットして `-1` を返す。

### 根本原因の考察

**`0xFF` を IISR に書き込むことが問題の本質**。考えられる動作:

1. **W1S的副作用**: AXI IIC コアは IISR に書いた値を「セットすべきビット」として処理している可能性。`0xFF` を書く → 全ビットがセット。
2. **コアへの副作用**: `0xFF` 書き込みがコア内部でリセットや abort と等価な操作を引き起こし、その結果 TX_EMPTY/TX_ERROR/ARB_LOST が同時にセットされる（問題5の `clr->EF` と同じメカニズム）。
3. **AXI IIC の IISR は真のW1Cではない**: Xilinx のドキュメントでは W1C とされているが、実際には書き込んだビットが反映される可能性。

**問題5との一致**: 問題5で `DTR後にIISR=0xFF書き込み → IISR=0xEF` になった。0xEF は TX_EMPTY(0x04) だけ立っていない。なぜなら問題5のときは DTR 書き込み後でTX FIFOが空でなかったため TX_EMPTY(0x04) は立たなかった。今は DTR 前なので TX FIFO 空 → TX_EMPTY も立って `0xFF`。**同一原因**。

### 確認結果

`XIic_WriteReg(IISR, _ib)` (0xD0 を書く) → `b=D0 a=D0`:

```
[SND d=77 b=D0 a=D0]   ← W1C後も変化なし
[BME] rst=0            ← wait_iisr が偽の成功を返した
[RD d=77 r=D0 b=D0 a=D0]
[BME] id=-1 FF         ← RX FIFO 空 = 実際には通信していない
```

**W1Cが全く効かない = IISR.BNBはレベルシグナル**。バスがアイドルである限り常に1。

### 根本原因の確定

**IISR.BNB（および TX_HALF, NAAS）はレベルシグナル** として機能している:
- バスがアイドル → BNB=1（常時）
- W1Cで書き込んでも即座に再立（物理状態が変わらないので）
- DTR 書き込み後、コアがSTARTを発行してバスを占有すると BNB=0 になる

**偽の成功の流れ**:
1. `iic_reset()` → IISR=0xD0 (BNB=1)
2. W1C → 変化なし (a=D0, BNB=1のまま)
3. DTR 書き込み複数回
4. `wait_iisr(BNB, ...)` → BNBが既に立っているので **即 0 を返す（偽の成功）**
5. 実際のI2C通信は開始すらされていない → RX FIFO 空

`[BME] rst=0` はこの偽の成功。実際にはBME280へのソフトリセットが届いていない。

### IISRと割り込みピンの関係（FAQ）

IISR = **IIC Interrupt Status Register（IIC割り込みステータスレジスタ）**。
- IISRのビットは割り込みピンが未接続でも正常にセット・ポーリングできる
- 割り込みピンは「IISRのビットが立ったときにPSにハードウェア割り込みを通知する」ためのもの
- ポーリングモード（割り込みなし）では割り込みピンは不要
- **W1Cが効かない原因は割り込みピンとは無関係**

### 問題6の修正: wait_iisr に SR.BB 確認を追加

**修正前の問題**: DTR書き込み直後に `wait_iisr` を呼ぶと、コアがまだSTARTを発行していない間に IISR.BNBを拾って偽の成功を返す。

**解決策**: `wait_iisr` の先頭に「SR.BB=1 になるまで待つ」ステップを追加。  
`SR.BB (XIIC_SR_BUS_BUSY_MASK = 0x04)` はコアがSTARTを発行してバスを物理的に占有した状態を示し、レベルシグナル問題に影響されない。

```c
static int wait_iisr(u32 ok_mask, u32 err_mask)
{
    /* SR.BB=1 でコアが実際にバスを占有したことを確認してからポーリング */
    for (u32 i = 0; i < 500; i++) {
        if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_BUS_BUSY_MASK) break;
        usleep(1);
    }
    for (u32 i = 0; i < IIC_TIMEOUT_US; i++) {
        u32 s = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
        if (s & err_mask) return -1;
        if (s & ok_mask)  return 0;
        usleep(1);
    }
    return -1;
}
```

`iic_probe` は DTR 後に W1C をしているが、1DTRエントリなのでコアが即座にSTART→STOP を処理し、W1C時点でバスがアイドルに戻っている。SR.BB=1 の待機でも短時間で通過する（またはタイムアウト後にアイドル状態の BNB を正しく検出）。

### さらに発覚: SR.BB タイムアウト後もポーリング継続していたため偽の成功が続いた

最初の SR.BB 修正では「BB が 500us 内に立たなくても IISRポーリングを継続」していた。
そのため IISR.BNBが立ったままでポーリングが即0を返し続けていた（`[BME] rst=0` だが偽）。

**修正**: SR.BB=1 を検出できなかった場合に `[noB]` をプリントして即 -1 を返す。

```c
if (!bb) {
    xil_printf("[noB]\r\n");
    return -1;  /* コアがSTARTを発行していない */
}
```

**確認結果**:

```
[SND d=77 b=D0 a=D0]
[BME] rst=0             ← [noB] なし → SR.BB=1 を検出 → コアはSTARTを発行している
[RD d=77 r=D0 b=D0 a=D0]
[BME] id=-1 FF          ← iic_read が -1 (原因不明)
[SCAN] デバイスなし     ← iic_scan が 0 台に退行！
```

オシロで波形確認済み → コアはI2C信号を生成している。

### iic_scan 退行の原因

`iic_probe` が依然として W1C に `0xFF` を書いていたため:

1. `iic_probe`: DTR (START+addr+STOP) → コアがSTART発行 → **W1C(0xFF)** → IISR=0xFF
2. `wait_iisr` の SR.BB=1 は検出できる（コアはSTARTを出している）
3. IISRポーリング: IISR=0xFF → TX_ERROR(0x02) が `err_mask` にヒット → **全アドレスで -1**

**修正**: `iic_probe` の W1C (0xFF) を削除。IISR.BNBはレベルシグナルなのでW1C不要。

### iic_read の失敗原因調査

`iic_read` の `wait_iisr` 戻り値と完了後の IISR/SR を確認するプリントを追加:

```
[RDW wrc=N iisr=XX sr=XX]
```

**確認結果 `[RDW wrc=0 iisr=D0 sr=44]`**:

```
[RDW wrc=0 iisr=D0 sr=44]
```

- `wrc=0`: wait_iisr は「成功」を返した（IISR.BNB=0x10を検出）
- `iisr=D0` = BNB(0x10)+NAAS(0x40)+TX_HALF(0x80) → SOFTR 後の初期値がそのまま残存
- `sr=44` = RX_FIFO_EMPTY(0x40) + **BB(0x04)** → バスがまだビジー！

**バスがビジーなのに wait_iisr が成功を返した = IISR.BNBはバス状態と無関係に常時1**。

iic_scan が全アドレスACK（112台）も同一原因：BNBが常に1なので全アドレスで偽ACK。

### 問題6の根本解決: IISR.BNBを完全廃止、SR.BB=0 で完了判断

**確定した事実**:
- IISR.BNB は SOFTR 後に 1 に初期化され、W1C で消えない
- バスがビジーの間も BNB=1 のまま（バス状態を反映していない）
- IISR.BNB によるトランザクション完了判断は根本的に不可能

**解決策**: `wait_iisr` を廃止し `wait_idle` を新設。SR.BB=0（バスアイドル）でトランザクション完了を確認。

```c
static int wait_idle(void)
{
    /* SR.BB=1 (コアがSTART発行) を最大500us待つ */
    int bb = 0;
    for (u32 i = 0; i < 500; i++) {
        if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_BUS_BUSY_MASK) {
            bb = 1; break;
        }
        usleep(1);
    }
    if (!bb) { xil_printf("[noB]\r\n"); return -1; }
    /* SR.BB=0 (バスアイドル = STOP後) まで待つ */
    for (u32 i = 0; i < IIC_TIMEOUT_US; i++) {
        u32 sr   = XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET);
        u32 iisr = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
        if (iisr & XIIC_INTR_ARB_LOST_MASK) return -1;
        if (!(sr & XIIC_SR_BUS_BUSY_MASK))  return 0;
        usleep(1);
    }
    return -1;
}
```

各関数の完了判断:
- **iic_send**: `wait_idle()` 後に `IISR.TX_ERROR` を確認。立てばNACK=エラー
- **iic_recv/iic_read**: `wait_idle()` 後に `SR.RX_FIFO_EMPTY` を確認。空ならNACK=エラー
- **iic_probe**: `wait_idle()` 後に `IISR.TX_ERROR` を確認。立てばNACK、なければACK

### iic_scan 復活確認

修正後、iic_scan は正常に 0x38 と 0x77 の 2 台のみを検出するようになった。

### 問題7: iic_recv / iic_read が受信でタイムアウト

```
[SND d=77 b=D0 a=D0]
[BME] rst=0            ← iic_send (write only) は成功
[RD d=77 r=D0 b=D0 a=D0]
[TO]                   ← iic_read が 5ms でタイムアウト
[BME] id=-1 FF
[RCV d=38 b=D0 a=D0]
[TO]                   ← iic_recv も 5ms でタイムアウト
[AHT] st=-1 FF
```

- `[noB]` なし → SR.BB=1 は検出できた（コアはSTARTを発行している）
- SR.BB=0 にならずタイムアウト → コアが受信状態でSTOPを発行しない

**write トランザクション (iic_send) は成功、receive トランザクション (iic_recv/iic_read) がタイムアウト**。

**タイムアウト後の SR/IISR 確認ログ (IIC_TIMEOUT_US=50ms で試験)**:

```
[TO SR=8C IISR=DA]
```

- `SR=0x8C` = BB(0x04) + RX_FIFO_EMPTY_N(0x08) → **バスがまだビジー**、RX FIFOにデータあり
- `IISR=0xDA` = TX_HALF(0x80) + NAAS(0x40) + RX_FULL(0x08) + TX_ERROR(0x02) → **RX_FULL + TX_ERROR 同時立ち**

**根本原因の確定: AXI IIC Dynamic Mode の受信完了シグナルは TX_ERROR**

AXI IIC コアの仕様:
- 受信モードでマスターが最終バイト後にNACKを送出 → **TX_ERROR(0x02)** が IISR に立つ（マスターNACKは正常な受信終了）
- ただし **STOP の実際の生成が遅延または不確実** → SR.BB=0 になるまでに時間がかかる、または全くならない場合がある
- データは **TX_ERROR 発生時点で RX FIFO に格納済み** → SR.BB=0 を待つ必要がない

IISR=0xDA の分解:
| ビット | 意味 | 解釈 |
|--------|------|------|
| TX_ERROR (0x02) | マスターNACK送出 | 最終バイト受信後の正常NACKを送出 → **受信完了** |
| RX_FULL  (0x08) | RX FIFOがしきい値到達 | **データが届いている** |
| NAAS     (0x40) | スレーブアドレス不一致 | SOFTRリセット後の初期値 |
| TX_HALF  (0x80) | TX FIFO ハーフ空 | SOFTRリセット後の初期値 |

**SR.BB=0 を完了条件にしていたことが誤り**。受信トランザクションでは **TX_ERROR が完了シグナル**。

### 問題7の修正: wait_idle に is_read パラメータを追加

```c
/* is_read=0 (write): SR.BB=0 で完了
 * is_read=1 (read) : TX_ERROR が立った時点で完了（STOP生成は遅延する場合あり）*/
static int wait_idle(int is_read)
{
    int bb = 0;
    for (u32 i = 0; i < 500; i++) {
        if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_BUS_BUSY_MASK) {
            bb = 1; break;
        }
        usleep(1);
    }
    if (!bb) { xil_printf("[noB]\r\n"); return -1; }
    for (u32 i = 0; i < IIC_TIMEOUT_US; i++) {
        u32 sr   = XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET);
        u32 iisr = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
        if (iisr & XIIC_INTR_ARB_LOST_MASK) return -1;
        if (!(sr & XIIC_SR_BUS_BUSY_MASK))  return 0;
        if (is_read && (iisr & XIIC_INTR_TX_ERROR_MASK)) return 0;  /* ← 追加 */
        usleep(1);
    }
    xil_printf("[TO SR=%02X IISR=%02X]\r\n", ...);
    return -1;
}
```

コールサイトの対応:
| 関数 | is_read | 理由 |
|------|---------|------|
| `iic_send`  | `wait_idle(0)` | 書き込み専用 → SR.BB=0 で完了 |
| `iic_recv`  | `wait_idle(1)` | 受信あり → TX_ERROR を完了とみなす |
| `iic_read`  | `wait_idle(1)` | 受信あり → TX_ERROR を完了とみなす |
| `iic_probe` | `wait_idle(0)` | START+STOP のみ（受信なし）→ SR.BB=0 で完了 |

**注意**: `iic_probe` の TX_ERROR はスレーブNACK（デバイスなし）を意味するため、`wait_idle(0)` のままにする。`wait_idle` 戻り後に IISR.TX_ERROR を確認してACK/NACKを判定する。

---

## 現在の iic_send / iic_recv / iic_read / iic_probe 実装 (最終版)

```c
static int iic_send(u8 dev, const u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);   /* 0xFFではなく実際の値でW1C */
    xil_printf("[SND d=%02X b=%02X a=%02X]\r\n", (u32)dev, _ib,
               (unsigned)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1));
    for (u8 i = 0; i < len; i++) {
        u32 dtr = buf[i];
        if (i == (u8)(len - 1u)) dtr |= XIIC_TX_DYN_STOP_MASK;
        XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET, dtr);
    }
    if (wait_idle(0) != 0) return -1;
    return (XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET) &
            XIIC_INTR_TX_ERROR_MASK) ? -1 : 0;
}

static int iic_recv(u8 dev, u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);
    xil_printf("[RCV d=%02X b=%02X a=%02X]\r\n", (u32)dev, _ib,
               (unsigned)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET));
    XIic_WriteReg(IIC_BASE, XIIC_RFD_REG_OFFSET, (u32)(len - 1u));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1) | 1u);
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_STOP_MASK | (u32)len);
    if (wait_idle(1) != 0) return -1;   /* TX_ERROR = 受信完了 */
    if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK)
        return -1;
    for (u8 i = 0; i < len; i++)
        buf[i] = (u8)XIic_ReadReg(IIC_BASE, XIIC_DRR_REG_OFFSET);
    return 0;
}

static int iic_read(u8 dev, u8 reg, u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);
    xil_printf("[RD d=%02X r=%02X b=%02X a=%02X]\r\n", (u32)dev, (u32)reg, _ib,
               (unsigned)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET));
    XIic_WriteReg(IIC_BASE, XIIC_RFD_REG_OFFSET, (u32)(len - 1u));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET, reg);
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1) | 1u);
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_STOP_MASK | (u32)len);
    if (wait_idle(1) != 0) return -1;   /* TX_ERROR = 受信完了 */
    if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK)
        return -1;
    for (u8 i = 0; i < len; i++)
        buf[i] = (u8)XIic_ReadReg(IIC_BASE, XIIC_DRR_REG_OFFSET);
    return 0;
}

static int iic_probe(u8 addr)
{
    iic_reset();
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)addr << 1) | XIIC_TX_DYN_STOP_MASK);
    if (wait_idle(0) != 0) return -1;
    return (XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET) &
            XIIC_INTR_TX_ERROR_MASK) ? -1 : 0;
}
```

---

## 最終動作確認ログ (2026-05-20)

```
[IIC] センサー再初期化...
[SND d=77 b=D0 a=D0]
[BME] rst=0
[RD d=77 r=D0 b=D0 a=D0]
[BME] id=0 58          ← chip ID=0x58 確認 → BME280 OK
[RD d=77 r=88 b=D0 a=D0]   ← 校正データ T1-P5
[RD d=77 r=98 b=D0 a=D0]   ← 校正データ P6-P9
[RD d=77 r=A1 b=D0 a=D0]   ← 湿度校正 H1
[RD d=77 r=E1 b=D0 a=D0]   ← 湿度校正 H2-H6
[SND d=77 b=D0 a=D0]        ← ctrl_hum 書き込み
[SND d=77 b=D0 a=D0]        ← ctrl_meas 書き込み
[SENS] BME280 OK (0x77)
[RCV d=38 b=D0 a=D0]
[AHT] st=0 18          ← status=0x18 (キャリブレーション済み bit3=1) → AHT20 OK
[SENS] AHT20 OK (0x38)
[SENS] BME280=OK AHT20=OK -- 続行
```

**sensors_init() 完全成功**。全7問題を解決。

---

## 解決済みの疑問点

1. **W1Cクリア後のBNB再アサート** → **確認済み**: IISR.BNBはレベルシグナルで常に1。W1Cで書いても即再立。wait_iisr(BNB,...) は常に偽の成功を返す。SR.BB ベースの wait_idle に移行済み。

2. **iic_probe との動作差異** → **確認済み**: probe が動作した理由は W1C なし（iic_probe には W1C を削除済み）。1DTRワードのトランザクションは STOP 生成が速いため SR.BB=0 に到達する。

3. **受信タイムアウトの原因** → **確認済み**: AXI IIC の受信完了シグナルは TX_ERROR。SR.BB=0 (STOP) は遅延する。wait_idle(is_read=1) で TX_ERROR を完了とみなすことで解決。

---

## キーレジスタオフセット一覧

| 名前 | オフセット | 説明 |
|------|----------|------|
| DGIER | 0x1C | Global Interrupt Enable |
| IISR  | 0x20 | IIC Interrupt Status (W1C) |
| IIER  | 0x28 | IIC Interrupt Enable |
| SOFTR | 0x40 | Software Reset |
| CR    | 0x100 | Control Register |
| SR    | 0x104 | Status Register |
| DTR   | 0x108 | TX Data Register / TX FIFO |
| DRR   | 0x10C | RX Data Register / RX FIFO |
| ADR   | 0x110 | Slave Address Register |
| TFO   | 0x114 | TX FIFO Occupancy |
| RFO   | 0x118 | RX FIFO Occupancy |
| RFD   | 0x120 | RX FIFO Depth (PIRQ threshold) |
