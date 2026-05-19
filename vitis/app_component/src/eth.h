#pragma once

#include "xemacps.h"

/* ===================== DMA バッファサイズ ===================== */
#define RXBD_CNT  16
#define TXBD_CNT   4
#define MAX_FRAME  1536   /* 64 バイトアライン */

/* eth.c で定義されるグローバル変数 */
extern XEmacPs EmacPs;
extern u8  RxBdSpace[4096];
extern u8  RxFrames[RXBD_CNT][MAX_FRAME];
extern u8  TxFrame[MAX_FRAME];
extern u32 g_tx_total;

/* ドライバ初期化 */
int  emacps_setup(XEmacPs *ep);
int  rx_ring_setup(XEmacPs *ep);
int  tx_ring_setup(XEmacPs *ep);
void wait_for_phy_link(XEmacPs *ep);

/* DMA 開始（RXQBASE/TXQBASE 書き込み → Start → IntDisable → デバッグダンプ） */
void emacps_start_hw(XEmacPs *ep);

/* フレーム送信 */
void emacps_send(XEmacPs *ep, u8 *buf, u32 len);
