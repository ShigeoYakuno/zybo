# 設計履歴

## GPIO割り込み + Logタスク実装

### 概要

Zybo Z7020 / Cortex-A9 / FreeRTOS / Vitis 2024.2 (SDTフロー) 上で、AXI GPIO の SW/BTN 押下をGIC割り込みで検出し、キューベースの `log_task` 経由でUARTに出力する仕組みを実装した。

---

### 対象ファイル

| ファイル | 種別 | 内容 |
|---|---|---|
| `app_component/src/gpio_intr.h` | 新規 | GPIO初期化・LED書き込み・SW/BTN読み出しAPI |
| `app_component/src/gpio_intr.c` | 新規 | AXI GPIO ISR + GIC登録 |
| `app_component/src/log_task.h` | 新規 | キューベースlogシステムAPI |
| `app_component/src/log_task.c` | 新規 | log_task / log_printf / log_printf_fromISR |
| `app_component/src/user_common.h` | 新規 | 型エイリアス・DLONG構造体・数値変換宣言 |
| `app_component/src/user_common.c` | 新規 | ltoa/ultoa/dlong_mul/dlong_div実装 |
| `app_component/src/main.c` | 修正 | log_task起動追加、gpio_intr_init移動 |
| `app_component/src/uart_cmd.c` | 修正 | gpio_intr_init呼び出し追加 |
| `app_component/src/UserConfig.cmake` | 修正 | GPIO/IICソース・インクルードパス追加 |

---

### ハードウェア構成

```
AXI_GPIO_0 (0x41200000)  CH1: SW[3:0] 入力, CH2: BTN[3:0] 入力, INTERRUPT_PRESENT=1, IS_DUAL=1
AXI_GPIO_1 (0x41210000)  CH1: LED[3:0] 出力, INTERRUPT_PRESENT=0
GIC distributor base: 0xF8F01000
GPIO割り込みID (SDK encoded): XPAR_FABRIC_AXI_GPIO_0_INTR = 30 → GIC絶対ID = 30+32 = 62
```

---

### 実装詳細

#### gpio_intr.c

- `gpio0_isr`: AXI GPIO ISR。`XGpio_InterruptGetStatus` でCH1(SW)/CH2(BTN)を判別し、変化したビットのみ `log_printf_fromISR` でキューに積む。最後に `XGpio_InterruptClear` でIPの割り込みフラグをクリア。
- `gpio_intr_init`: GPIO初期化 → GICハンドラ登録 → 優先度設定 → GIC有効化 → GPIO IP割り込み有効化の順に実行。**タスク内から呼ぶこと（後述）。**

#### log_task.c

- `logQueue`: `char[256]` × 16エントリのキュー。
- `log_printf_fromISR`: ISRコンテキスト用。バッファを `static` にしてタスクSVCスタックへの積み上げを回避（後述）。
- `log_printf` / `syslog`: タスクコンテキスト用。`xQueueSend` を使用。
- `log_task`: `xQueueReceive` でメッセージを取り出し `xil_printf` で出力。

---

### 発生したバグと解決

#### バグ1: ビルドエラー `xgpio.h: No such file or directory`

**原因**: `app_component` が旧プラットフォーム (`vitis_lwip/platform`) にリンクされており、旧XSAにAXI GPIOが存在しないため `xgpio.h` がなかった。

**修正**: `UserConfig.cmake` に新プラットフォームのGPIO/IICソースとインクルードパスを直接追加してコンパイル対象に含める。

```cmake
set(USER_INCLUDE_DIRECTORIES
  ".../bsp/libsrc/iic/src"
  ".../bsp/libsrc/gpio/src"
)
set(USER_COMPILE_SOURCES
  ...
  ".../gpio/src/xgpio.c"
  ".../gpio/src/xgpio_intr.c"
  ".../gpio/src/xgpio_sinit.c"
  ".../gpio/src/xgpio_g.c"
)
```

---

#### バグ2: SW押下で portASM.S `PUSH {r0-r4, r12}` (line 177) にてクラッシュ

**原因**: `log_printf_fromISR` 内の `char msg[256]` がISR実行中に割り込まれたタスクのSVCスタック上に確保され、スタックオーバーフローを起こしていた。

FreeRTOS ARM_CA9 の portASM.S は IRQ発火後にSVCモードへ切り替え、割り込まれたタスクのSVCスタックを使用する。`configMINIMAL_STACK_SIZE = 200 words = 800バイト` しかないIDLEタスクが割り込まれた場合、ISR全体のスタック消費（256B msg + vsnprintf + その他）が800Bを超えてオーバーフローした。

**修正**: `log_printf_fromISR` 内のバッファを `static` 化。シングルコアA9でGPIO ISRはネストしないため安全。

```c
// 修正前
char msg[LOG_MSG_LEN];

// 修正後
static char msg[LOG_MSG_LEN];
```

---

#### バグ3: SW押下でシステムハング（StubHandler呼び出し、各所でクラッシュ）

**症状**:
- クラッシュ箇所がランダム（`PUSH {r0-r4, r12}`, `exit_without_switch` の `POP`, `StubHandler` 内）
- コマンド応答なし、センサ値停止

**根本原因1（致命的）: `ScuGicInitialized` フラグが `FALSE` のためハンドラ未登録**

`xPortInstallInterruptHandler` → `XConnectToInterruptCntrl` の内部で `ScuGicInitialized == FALSE` の場合、ハンドラ登録をスキップして `XST_SUCCESS` を返す（サイレントなno-op）。

`ScuGicInitialized` は `XSetupInterruptSystem`（= tickタイマー初期化）内でのみ `TRUE` にセットされる。これは `vTaskStartScheduler()` → `FreeRTOS_SetupTickInterrupt()` → `XTimer_SetHandler()` → `XSetupInterruptSystem()` の流れで実行される。

`main()` の `vTaskStartScheduler()` **前**に `gpio_intr_init()` を呼んでいたため、フラグが `FALSE` のまま `xPortInstallInterruptHandler` が呼ばれ、`gpio0_isr` は `XScuGic_ConfigTable[0].HandlerTable[62]` に書き込まれなかった。

結果として、SW押下 → GIC ID 62 発火 → `HandlerTable[62] == StubHandler` → GPIOクリアなし → 無限割り込みループ → スタック崩壊 → 各所クラッシュ。

**根本原因2（副次）: `XScuGic_SetPriTrigTypeByDistAddr` に誤ったGIC IDを渡していた**

```c
// 修正前: IDの意味を誤解 (30はSDK encoded ID, GIC絶対IDではない)
XScuGic_SetPriTrigTypeByDistAddr(GIC_DIST_BASE, 30, 0xA0, 0x03);
// → GIC絶対ID 30 (ウォッチドッグPPI) の優先度を設定してしまう
```

SDTモードのxilinterruptでは、SPI割り込みのSDK IDはSPI番号（オフセットなし）で表現され、`XConnectToInterruptCntrl` 内で +32 のオフセットを加算してGIC絶対IDに変換する。しかし `XScuGic_SetPriTrigTypeByDistAddr` はGIC絶対IDを直接受け取る生APIなので、オフセット加算を自前で行わなければならない。

GPIO (SPI 30) の正しいGIC絶対ID = 30 + 32 = **62**。

**修正**:

1. `gpio_intr_init()` 呼び出しを `main()` から `uart_task` 先頭に移動する（スケジューラ起動後 = `ScuGicInitialized == TRUE` が保証された後）。

```c
// uart_cmd.c の uart_task 先頭
void uart_task(void *pvParameters) {
    uart_init();
    gpio_intr_init();  // ← スケジューラ起動後に呼ぶ
    ...
}
```

2. `XScuGic_SetPriTrigTypeByDistAddr` を `XSetPriorityTriggerType` に置き換え（オフセット加算を内部で処理）。

```c
// 修正前
XScuGic_SetPriTrigTypeByDistAddr(GIC_DIST_BASE, (u32)GPIO0_INTR_ID,
                                  GPIO_INTR_PRIORITY, GPIO_INTR_TRIGGER);

// 修正後: XSetPriorityTriggerType が内部で +32 を加算してGIC絶対ID 62 に設定
XSetPriorityTriggerType((u32)GPIO0_INTR_ID, GPIO_INTR_PRIORITY, GIC_DIST_BASE);
```

---

### SDTモードの割り込み構造（調査メモ）

#### ディスパッチチェーン

```
IRQ発火
 └─ FreeRTOS_IRQ_Handler (portASM.S)
     ├─ SVCモードに切替、タスクSVCスタックを使用
     ├─ ICCIAR読み取り (GIC絶対ID取得)
     └─ vApplicationIRQHandler(ulICCIAR) (portZynq7000.c)
         └─ XScuGic_ConfigTable[0].HandlerTable[ulICCIAR & 0x3FF]
             └─ gpio0_isr / StubHandler / ...
```

#### xilinterrupt のSDK ID エンコーディング

```
SDK IntrId ビット構成:
  [11:0]  XINTC_INTRID_MASK   = 割り込み番号 (SPI番号)
  [15:12] XINTC_TRIGGER_MASK  = トリガタイプ
  [20]    XINTC_INTR_TYPE_MASK = 割り込み種別 (0=SPI, 1=PPI)
  [22]    XINTC_IS_SGI_INTR_MASK = SGIフラグ

GIC絶対ID変換:
  SGI: SDK ID そのまま (オフセット 0)
  PPI: SDK ID + 16
  SPI: SDK ID + 32   ← AXI GPIO はここ
```

#### `ScuGicInitialized` の設定タイミング

```
vTaskStartScheduler()
 └─ FreeRTOS_SetupTickInterrupt() [portZynq7000.c]
     └─ XTimer_SetHandler() [xiltimer]
         └─ XTimer_ScutimerSetIntrHandler() [scutimer.c]
             └─ XSetupInterruptSystem() [xinterrupt_wrap.c]
                 ├─ XConfigInterruptCntrl()   ← GIC初期化 (1回のみ)
                 ├─ ScuGicInitialized = TRUE  ← ★ここで初めてTRUEになる
                 └─ XConnectToInterruptCntrl() ← tickハンドラ登録
```

`ScuGicInitialized == FALSE` の間に `XConnectToInterruptCntrl` を呼んでも `XST_SUCCESS` を返すだけで何もしない。`gpio_intr_init()` はタスク内（スケジューラ起動後）から呼ぶこと。

---

### 割り込み優先度設定

```
configMAX_API_CALL_INTERRUPT_PRIORITY = 18
portPRIORITY_SHIFT                    = 3
閾値                                   = 18 << 3 = 144

GPIO割り込み優先度 = 0xA0 = 160 > 144
→ ISR内で xQueueSendFromISR などの FromISR API 呼び出し可能
```
