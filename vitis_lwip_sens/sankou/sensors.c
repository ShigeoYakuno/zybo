#include "sensors.h"
#include "xiic_l.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "sleep.h"

#define IIC_BASE    XPAR_XIIC_0_BASEADDR
#define BME280_ADDR 0x77u
#define AHT20_ADDR  0x38u


static int bme280_ok;
static int aht20_ok;

/* ===== BME280 校正データ ===== */
typedef struct {
    u16 T1; s16 T2, T3;
    u16 P1; s16 P2, P3, P4, P5, P6, P7, P8, P9;
    u8  H1; s16 H2; u8 H3; s16 H4, H5; s8 H6;
} bme280_cal_t;

static bme280_cal_t cal;
static s32 t_fine;

/* ===== IIC 直接レジスタ制御 (無限ループなし、タイムアウト付き) =====
 *
 * XIic_DynSend / XIic_DynRecv は SR_BUS_BUSY を無限にポーリングするため、
 * NACK 後にコアが応答しないとハングする。
 * ここでは DTR / IISR を直接操作し、IISR の BNB (Bus Not Busy) を
 * タイムアウト付きで待つことでハングを回避する。
 */

#define IIC_TIMEOUT_US 50000u  /* 50 ms: タイミングレジスタリセット後の低速I2C対策 */

/* IISR をポーリング。ok_mask が立てば 0, err_mask が立てば -1, タイムアウトでも -1 */
/* トランザクション完了を待つ。
 * is_read=0 (write): SR.BB=0 (STOP後) で完了。TX_ERROR は後でエラー確認。
 * is_read=1 (read) : AXI IIC Dynamic Mode では受信完了時に TX_ERROR (マスターNACK)
 *   が立つが STOP は遅延または発行されず SR.BB=0 にならない場合がある。
 *   TX_ERROR が立った時点で RX FIFO にデータがあれば受信完了とみなす。*/
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
        if (is_read && (iisr & XIIC_INTR_TX_ERROR_MASK)) return 0;
        usleep(1);
    }
    xil_printf("[TO SR=%02X IISR=%02X]\r\n",
               (unsigned)XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET),
               (unsigned)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET));
    return -1;
}

/* TX FIFO リセット + RX FIFO ドレイン + IISR クリア
 * XIic_DynInit と同じ順序: ENABLE をオフにして TX FIFO リセット→ENABLE オン。
 * ENABLE と TX_FIFO_RESET を同時に立てると FIFO がリセットされず
 * DTR 書き込みが無視されて波形が出ない原因になる。 */
static void iic_tx_reset(void)
{
    XIic_WriteReg(IIC_BASE, XIIC_CR_REG_OFFSET, XIIC_CR_TX_FIFO_RESET_MASK);
    XIic_WriteReg(IIC_BASE, XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);
    while (!(XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK))
        (void)XIic_ReadReg(IIC_BASE, XIIC_DRR_REG_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, 0xFFu);
    /* IISR クリア書き込みの AXI 伝播を確実に待つため 2 回リードバックする。
     * 1 回だけでは DTR 書き込み時に START が生成されないことがある。 */
    (void)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    (void)XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
}

/* NACK 等エラー後はコアがエラー状態に残るため SOFTR リセット */
static void iic_reset(void)
{
    XIic_DynInit(IIC_BASE);
    usleep(1000);   /* 1 ms: バス安定待ち */
}

/* START + addr+W + buf[len] + STOP */
static int iic_send(u8 dev, const u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);   /* 0xFFではなく実際の値でW1C */
    u32 _ic = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    xil_printf("[SND d=%02X b=%02X a=%02X]\r\n", (u32)dev, _ib, _ic);
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1));
    for (u8 i = 0; i < len; i++) {
        u32 dtr = buf[i];
        if (i == (u8)(len - 1u)) dtr |= XIIC_TX_DYN_STOP_MASK;
        XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET, dtr);
    }
    if (wait_idle(0) != 0) return -1;
    /* 書き込み: TX_ERROR はスレーブ NACK を意味する */
    return (XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET) &
            XIIC_INTR_TX_ERROR_MASK) ? -1 : 0;
}

/* START + addr+R + len バイト受信 + STOP
 * Dynamic mode では START+addr+R の後に STOP|len を TX FIFO に書く必要がある。
 * 書かないと IIC コアが受信バイト数を知らずタイムアウトする。len <= 16 であること */
static int iic_recv(u8 dev, u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);   /* 0xFFではなく実際の値でW1C */
    u32 _ic = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    xil_printf("[RCV d=%02X b=%02X a=%02X]\r\n", (u32)dev, _ib, _ic);
    XIic_WriteReg(IIC_BASE, XIIC_RFD_REG_OFFSET, (u32)(len - 1u));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1) | 1u);  /* START+addr+R */
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_STOP_MASK | (u32)len);                /* STOP|ByteCount */
    if (wait_idle(1) != 0) return -1;
    /* RX FIFO が空 = アドレス NACK (データ未受信) */
    if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK)
        return -1;
    for (u8 i = 0; i < len; i++)
        buf[i] = (u8)XIic_ReadReg(IIC_BASE, XIIC_DRR_REG_OFFSET);
    return 0;
}

static int iic_write_reg(u8 dev, u8 reg, u8 val)
{
    u8 buf[2] = {reg, val};
    return iic_send(dev, buf, 2);
}

/* START + addr+W + reg + rSTART + addr+R + len バイト受信 + STOP
 * Dynamic mode では rSTART+addr+R の後に STOP|len を TX FIFO に書く必要がある。
 * len <= 16 であること (RX FIFO 深さ制限) */
static int iic_read(u8 dev, u8 reg, u8 *buf, u8 len)
{
    iic_reset();
    u32 _ib = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    XIic_WriteReg(IIC_BASE, XIIC_IISR_OFFSET, _ib);   /* 0xFFではなく実際の値でW1C */
    u32 _ic = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    xil_printf("[RD d=%02X r=%02X b=%02X a=%02X]\r\n", (u32)dev, (u32)reg, _ib, _ic);
    XIic_WriteReg(IIC_BASE, XIIC_RFD_REG_OFFSET, (u32)(len - 1u));
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1));          /* START+addr+W */
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET, reg);                /* レジスタアドレス */
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)dev << 1) | 1u);    /* rSTART+addr+R */
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_STOP_MASK | (u32)len);                  /* STOP|ByteCount */
    if (wait_idle(1) != 0) return -1;
    /* RX FIFO が空 = アドレス/レジスタ NACK (データ未受信) */
    if (XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET) & XIIC_SR_RX_FIFO_EMPTY_MASK)
        return -1;
    for (u8 i = 0; i < len; i++)
        buf[i] = (u8)XIic_ReadReg(IIC_BASE, XIIC_DRR_REG_OFFSET);
    return 0;
}

/* START+addr+STOP のみ送出してACK/NACKを返す (バススキャン用)
 * 0=ACK(デバイスあり), -1=NACK(デバイスなし) */
static int iic_probe(u8 addr)
{
    iic_reset();
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | ((u32)addr << 1) | XIIC_TX_DYN_STOP_MASK);
    if (wait_idle(0) != 0) return -1;
    /* TX_ERROR = スレーブ NACK = デバイスなし */
    return (XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET) &
            XIIC_INTR_TX_ERROR_MASK) ? -1 : 0;
}

/* I2C バス全アドレス(0x08-0x77)をスキャンしてACKを返すデバイスを列挙 */
void iic_scan(void)
{
    xil_printf("[SCAN] I2Cバス スキャン中 (0x08-0x77)...\r\n");
    int found = 0;
    for (u32 addr = 0x08u; addr <= 0x77u; addr++) {
        if (iic_probe((u8)addr) == 0) {
            xil_printf("[SCAN] ACK addr=0x%02X\r\n", addr);
            found++;
        }
    }
    if (found == 0)
        xil_printf("[SCAN] デバイスなし -- 配線/電源を確認\r\n");
    xil_printf("[SCAN] 完了 %d台\r\n", found);
}

/* ===== BME280 補正計算 (データシート Appendix B 整数演算版) ===== */

static s32 comp_T(s32 adc_T)
{
    s32 v1 = ((((adc_T >> 3) - ((s32)cal.T1 << 1))) * (s32)cal.T2) >> 11;
    s32 v2 = (((((adc_T >> 4) - (s32)cal.T1) * ((adc_T >> 4) - (s32)cal.T1)) >> 12)
              * (s32)cal.T3) >> 14;
    t_fine = v1 + v2;
    return (t_fine * 5 + 128) >> 8;  /* 0.01 °C */
}

static u32 comp_P(s32 adc_P)
{
    s64 v1 = (s64)t_fine - 128000;
    s64 v2 = v1 * v1 * (s64)cal.P6;
    v2 = v2 + ((v1 * (s64)cal.P5) << 17);
    v2 = v2 + ((s64)cal.P4 << 35);
    v1 = ((v1 * v1 * (s64)cal.P3) >> 8) + ((v1 * (s64)cal.P2) << 12);
    v1 = (((s64)1 << 47) + v1) * (s64)cal.P1 >> 33;
    if (v1 == 0) return 0;
    s64 p = 1048576 - adc_P;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = ((s64)cal.P9 * (p >> 13) * (p >> 13)) >> 25;
    v2 = ((s64)cal.P8 * p) >> 19;
    p = ((p + v1 + v2) >> 8) + ((s64)cal.P7 << 4);
    return (u32)p;  /* Pa * 256 (Q24.8) */
}

/* ===== BME280 初期化 ===== */

static int bme280_init(void)
{
    u8 buf[16];
    int rc;

    rc = iic_write_reg(BME280_ADDR, 0xE0, 0xB6);
    xil_printf("[BME] rst=%d\r\n", rc);
    if (rc != 0) return -1;
    usleep(5000);

    rc = iic_read(BME280_ADDR, 0xD0, buf, 1);
    xil_printf("[BME] id=%d %02X\r\n", rc, rc==0 ? (u32)buf[0] : 0xFFu);
    if (rc != 0 || (buf[0] != 0x60 && buf[0] != 0x58)) return -1;

    /* 温度・気圧校正データ 0x88-0x97 (16 bytes): T1-T3, P1-P5 */
    if (iic_read(BME280_ADDR, 0x88, buf, 16) != 0) return -1;
    cal.T1 = (u16)(buf[1]  << 8 | buf[0]);
    cal.T2 = (s16)(buf[3]  << 8 | buf[2]);
    cal.T3 = (s16)(buf[5]  << 8 | buf[4]);
    cal.P1 = (u16)(buf[7]  << 8 | buf[6]);
    cal.P2 = (s16)(buf[9]  << 8 | buf[8]);
    cal.P3 = (s16)(buf[11] << 8 | buf[10]);
    cal.P4 = (s16)(buf[13] << 8 | buf[12]);
    cal.P5 = (s16)(buf[15] << 8 | buf[14]);

    /* 温度・気圧校正データ 0x98-0x9F (8 bytes): P6-P9
     * RX FIFO は 16 バイト制限のため 24 バイト読み出しを分割 */
    if (iic_read(BME280_ADDR, 0x98, buf, 8) != 0) return -1;
    cal.P6 = (s16)(buf[1] << 8 | buf[0]);
    cal.P7 = (s16)(buf[3] << 8 | buf[2]);
    cal.P8 = (s16)(buf[5] << 8 | buf[4]);
    cal.P9 = (s16)(buf[7] << 8 | buf[6]);

    /* 湿度校正 H1 (0xA1) */
    if (iic_read(BME280_ADDR, 0xA1, buf, 1) != 0) return -1;
    cal.H1 = buf[0];

    /* 湿度校正 H2-H6 (0xE1-0xE7, 7 bytes) */
    if (iic_read(BME280_ADDR, 0xE1, buf, 7) != 0) return -1;
    cal.H2 = (s16)(buf[1] << 8 | buf[0]);
    cal.H3 = buf[2];
    cal.H4 = (s16)((buf[3] << 4) | (buf[4] & 0x0F));
    cal.H5 = (s16)((buf[5] << 4) | (buf[4] >> 4));
    cal.H6 = (s8)buf[6];

    /* osrs_h=1x → ctrl_hum を先に書く */
    if (iic_write_reg(BME280_ADDR, 0xF2, 0x01) != 0) return -1;
    /* osrs_t=1x, osrs_p=1x, mode=normal (0x27) */
    if (iic_write_reg(BME280_ADDR, 0xF4, 0x27) != 0) return -1;

    return 0;
}

/* ===== AHT20 初期化 ===== */

static int aht20_init(void)
{
    usleep(40000);  /* 電源投入後 40 ms 待機 */

    u8 status;
    int rc = iic_recv(AHT20_ADDR, &status, 1);
    xil_printf("[AHT] st=%d %02X\r\n", rc, rc==0 ? (u32)status : 0xFFu);
    if (rc != 0) return -1;

    /* キャリブレーションビット (bit3) が未セットなら初期化コマンド送出 */
    if (!(status & 0x08)) {
        u8 cmd[3] = {0xBE, 0x08, 0x00};
        if (iic_send(AHT20_ADDR, cmd, 3) != 0) return -1;
        usleep(10000);
    }
    return 0;
}

/* ===== 診断: AXI IIC コアが I2C 活動を生成できるか確認 ===== */

static void iic_diag(void)
{
    /* ① reset後のレジスタ確認 (%X を使用, %lX は xil_printf で8バイト読みになる)
     *   正常値: CR=0x01, SR=0xC0, IISR=0xD0 */
    u32 cr   = XIic_ReadReg(IIC_BASE, XIIC_CR_REG_OFFSET);
    u32 sr   = XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET);
    u32 iisr = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    xil_printf("[DIAG] reset後: CR=0x%02X(exp 01) SR=0x%02X(exp C0) IISR=0x%02X(exp D0)\r\n",
               (unsigned)cr, (unsigned)sr, (unsigned)iisr);

    /* ② write-readback: CR に 0x01 を書いて読み返す
     *   0x00 が返ったら AXI バスアクセス失敗 */
    XIic_WriteReg(IIC_BASE, XIIC_CR_REG_OFFSET, XIIC_CR_ENABLE_DEVICE_MASK);
    u32 cr_rb = XIic_ReadReg(IIC_BASE, XIIC_CR_REG_OFFSET);
    xil_printf("[DIAG] CR readback=0x%02X %s\r\n",
               (unsigned)cr_rb, (cr_rb == 0x01u) ? "OK" : "NG-AXI失敗");

    /* ③ 存在しないアドレス(0x0F)を START+STOP で送出し 500μs後に確認 */
    iic_tx_reset();
    u32 iisr0 = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);  /* DTR前の状態を記録 */
    XIic_WriteReg(IIC_BASE, XIIC_DTR_REG_OFFSET,
                  XIIC_TX_DYN_START_MASK | (0x0Fu << 1) | XIIC_TX_DYN_STOP_MASK);
    usleep(500);
    u32 sr2   = XIic_ReadReg(IIC_BASE, XIIC_SR_REG_OFFSET);
    u32 iisr2 = XIic_ReadReg(IIC_BASE, XIIC_IISR_OFFSET);
    xil_printf("[DIAG] DTR前IISR=0x%02X  500us後: SR=0x%02X IISR=0x%02X\r\n",
               (unsigned)iisr0, (unsigned)sr2, (unsigned)iisr2);

    if (iisr2 & XIIC_INTR_TX_ERROR_MASK)
        xil_printf("[DIAG] → TX_ERROR: NACK → I2C波形OK (センサー配線/アドレス要確認)\r\n");
    else if ((iisr2 & XIIC_INTR_BNB_MASK) && (iisr0 & XIIC_INTR_BNB_MASK))
        xil_printf("[DIAG] → BNB常時セット: 待機条件の見直しが必要\r\n");
    else if (iisr2 & XIIC_INTR_BNB_MASK)
        xil_printf("[DIAG] → BNBのみ(iisr0クリア済み): ACK受信またはタイミング\r\n");
    else
        xil_printf("[DIAG] → 変化なし: STARTが出ていない → クロック/リセット要確認\r\n");

    iic_reset();  /* 後続処理のためリセット */
}

/* ===== 公開 API ===== */

int sensors_init(void)
{
    bme280_ok = 0;
    aht20_ok  = 0;

    iic_reset();
    if (bme280_init() == 0) {
        bme280_ok = 1;
        xil_printf("[SENS] BME280 OK (0x77)\r\n");
    } else {
        xil_printf("[SENS] BME280 not found\r\n");
        iic_reset();  /* NACK 後にコアをリセットして次の初期化に備える */
    }

    if (aht20_init() == 0) {
        aht20_ok = 1;
        xil_printf("[SENS] AHT20 OK (0x38)\r\n");
    } else {
        xil_printf("[SENS] AHT20 not found\r\n");
        iic_reset();
    }

    xil_printf("[SENS] BME280=%s AHT20=%s -- 続行\r\n",
               bme280_ok ? "OK" : "NG", aht20_ok ? "OK" : "NG");
    return 0;  /* センサー未検出でも常に続行 */
}

void sensors_read(int *temp_c10, int *hum_rh10, int *press_hpa10)
{
    *temp_c10    = 0;
    *hum_rh10    = 0;
    *press_hpa10 = 0;

    /* AHT20: 温度 + 湿度 */
    if (aht20_ok) {
        u8 trig[3] = {0xAC, 0x33, 0x00};
        iic_send(AHT20_ADDR, trig, 3);
        usleep(80000);  /* 最大 75 ms 待機 */

        u8 d[6];
        if (iic_recv(AHT20_ADDR, d, 6) == 0 && !(d[0] & 0x80)) {
            u32 rh = ((u32)d[1] << 12) | ((u32)d[2] << 4) | (d[3] >> 4);
            u32 rt = ((u32)(d[3] & 0x0F) << 16) | ((u32)d[4] << 8) | d[5];
            /* RH [0.1%]  : rh / 2^20 * 100 * 10 = rh * 1000 / 1048576 */
            *hum_rh10  = (int)(rh * 1000u / 1048576u);
            /* T  [0.1°C] : rt / 2^20 * 200 * 10 - 500 = rt*2000/1048576 - 500 */
            *temp_c10  = (int)(rt * 2000u / 1048576u) - 500;
        }
    }

    /* BME280: 気圧 (AHT20 不在時は温度も取得) */
    if (bme280_ok) {
        u8 d[8];
        if (iic_read(BME280_ADDR, 0xF7, d, 8) == 0) {
            s32 adc_P = ((s32)d[0] << 12) | ((s32)d[1] << 4) | (d[2] >> 4);
            s32 adc_T = ((s32)d[3] << 12) | ((s32)d[4] << 4) | (d[5] >> 4);
            s32 T100  = comp_T(adc_T);  /* t_fine を更新してから comp_P を呼ぶ */
            u32 Pq    = comp_P(adc_P);  /* Pa * 256 (Q24.8) */
            /* 0.1 hPa : Pa*256 / 2560 */
            *press_hpa10 = (int)(Pq / 2560u);
            if (!aht20_ok)
                *temp_c10 = T100 / 10;  /* 0.01°C → 0.1°C */
        }
    }
}
