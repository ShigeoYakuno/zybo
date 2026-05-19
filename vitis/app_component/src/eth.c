#include "eth.h"
#include "config.h"
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "sleep.h"

#define EMACPS_BASEADDR  XPAR_XEMACPS_0_BASEADDR

/* ===================== DMA バッファ ===================== */
u8 RxBdSpace[4096]              __attribute__((aligned(XEMACPS_BD_ALIGNMENT)));
u8 RxFrames[RXBD_CNT][MAX_FRAME] __attribute__((aligned(64)));
u8 TxFrame[MAX_FRAME]            __attribute__((aligned(64)));

static u8 TxBdSpace[4096]       __attribute__((aligned(XEMACPS_BD_ALIGNMENT)));

XEmacPs EmacPs;
u32     g_tx_total = 0;

/* ===================== フレーム送信 ===================== */

void emacps_send(XEmacPs *ep, u8 *buf, u32 len)
{
    XEmacPs_BdRing *txring = &XEmacPs_GetTxRing(ep);
    XEmacPs_Bd     *bd;
    LONG rc;

    rc = XEmacPs_BdRingAlloc(txring, 1, &bd);
    if (rc != XST_SUCCESS) { xil_printf("[TX] alloc failed\r\n"); return; }

    Xil_DCacheFlushRange((UINTPTR)buf, len);
    XEmacPs_BdSetAddressTx(bd, (UINTPTR)buf);
    XEmacPs_BdSetLength(bd, len);
    XEmacPs_BdClearTxUsed(bd);
    XEmacPs_BdSetLast(bd);

    rc = XEmacPs_BdRingToHw(txring, 1, bd);
    if (rc != XST_SUCCESS) { xil_printf("[TX] to hw failed\r\n"); return; }
    Xil_DCacheFlushRange((UINTPTR)bd, sizeof(XEmacPs_Bd));
    /* 前回の TX 完了フラグを送信前にクリア */
    XEmacPs_WriteReg(EMACPS_BASEADDR, XEMACPS_TXSR_OFFSET,
                     XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_TXSR_OFFSET));
    XEmacPs_Transmit(ep);

    /* TXSR の TXCMPL ビットで完了を待つ（BD ring の FromHwTx は使わない） */
    u32 txsr = 0;
    for (u32 timeout = 200000U; timeout; timeout--) {
        txsr = XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_TXSR_OFFSET);
        if (txsr & XEMACPS_TXSR_TXCOMPL_MASK)
            break;
    }
    /* ステータスクリア */
    XEmacPs_WriteReg(EMACPS_BASEADDR, XEMACPS_TXSR_OFFSET, txsr);

    /* BD ring を送信済み状態に進める */
    XEmacPs_Bd *done;
    if (XEmacPs_BdRingFromHwTx(txring, 1, &done) == 1)
        XEmacPs_BdRingFree(txring, 1, done);
    else
        XEmacPs_BdRingFree(txring, 1, bd);

    if (!(txsr & XEMACPS_TXSR_TXCOMPL_MASK))
        xil_printf("[TX] timeout TXSR=0x%08X\r\n", (u32)txsr);
    else
        g_tx_total++;
}

/* ===================== EMACPs 初期化 ===================== */

int emacps_setup(XEmacPs *ep)
{
    XEmacPs_Config *cfg = XEmacPs_LookupConfig(EMACPS_BASEADDR);
    if (!cfg) return XST_FAILURE;

    LONG rc = XEmacPs_CfgInitialize(ep, cfg, cfg->BaseAddress);
    if (rc != XST_SUCCESS) return (int)rc;

    XEmacPs_SetOptions(ep, XEMACPS_DEFAULT_OPTIONS);
    XEmacPs_SetMacAddress(ep, (void *)BOARD_MAC, 1);
    usleep(2000);
    return XST_SUCCESS;
}

int rx_ring_setup(XEmacPs *ep)
{
    XEmacPs_BdRing *ring = &XEmacPs_GetRxRing(ep);
    XEmacPs_Bd bd_tmpl, *bd;
    LONG rc;

    rc = XEmacPs_BdRingCreate(ring, (UINTPTR)RxBdSpace, (UINTPTR)RxBdSpace,
                               XEMACPS_BD_ALIGNMENT, RXBD_CNT);
    if (rc != XST_SUCCESS) return (int)rc;

    XEmacPs_BdClear(&bd_tmpl);
    rc = XEmacPs_BdRingClone(ring, &bd_tmpl, XEMACPS_RECV);
    if (rc != XST_SUCCESS) return (int)rc;

    rc = XEmacPs_BdRingAlloc(ring, RXBD_CNT, &bd);
    if (rc != XST_SUCCESS) return (int)rc;

    XEmacPs_Bd *cur = bd;
    for (int i = 0; i < RXBD_CNT; i++) {
        XEmacPs_BdSetAddressRx(cur, (UINTPTR)RxFrames[i]);
        cur = XEmacPs_BdRingNext(ring, cur);
    }

    LONG rc2 = XEmacPs_BdRingToHw(ring, RXBD_CNT, bd);
    Xil_DCacheFlushRange((UINTPTR)RxBdSpace, sizeof(RxBdSpace));
    return (int)rc2;
}

int tx_ring_setup(XEmacPs *ep)
{
    XEmacPs_BdRing *ring = &XEmacPs_GetTxRing(ep);
    XEmacPs_Bd bd_tmpl;
    LONG rc;

    rc = XEmacPs_BdRingCreate(ring, (UINTPTR)TxBdSpace, (UINTPTR)TxBdSpace,
                               XEMACPS_BD_ALIGNMENT, TXBD_CNT);
    if (rc != XST_SUCCESS) return (int)rc;

    XEmacPs_BdClear(&bd_tmpl);
    rc = XEmacPs_BdRingClone(ring, &bd_tmpl, XEMACPS_SEND);
    Xil_DCacheFlushRange((UINTPTR)TxBdSpace, sizeof(TxBdSpace));
    return (int)rc;
}

/* ===================== PHY リンク確認 ===================== */

void wait_for_phy_link(XEmacPs *ep)
{
    xil_printf("Waiting for PHY link");
    for (int i = 0; i < 10; i++) {
        u32 bmsr;
        XEmacPs_PhyRead(ep, PHY_ADDR, 1, &bmsr);
        if (bmsr & 0x0004U) {
            xil_printf(" LINK UP\r\n");
            u32 phy_sr;
            XEmacPs_PhyRead(ep, PHY_ADDR, 0x11, &phy_sr);
            u32 speed_code = (phy_sr >> 14) & 0x3U;
            u16 speed = (speed_code == 2U) ? 1000U :
                        (speed_code == 1U) ? 100U : 10U;
            xil_printf("PHY speed: %uMbps (PHYSR=0x%04X)\r\n",
                       (u32)speed, (u32)(phy_sr & 0xFFFFU));
            XEmacPs_SetOperatingSpeed(ep, speed);
            usleep(2000);
            return;
        }
        xil_printf(".");
        sleep(1);
    }
    xil_printf(" No link (default 100Mbps)\r\n");
    XEmacPs_SetOperatingSpeed(ep, 100);
}

/* ===================== DMA 開始 ===================== */

void emacps_start_hw(XEmacPs *ep)
{
    /* RXQBASE は受信有効中に書いても無視されるため一旦 RXEN を落とす */
    u32 nc = XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_NWCTRL_OFFSET);
    XEmacPs_WriteReg(EMACPS_BASEADDR, XEMACPS_NWCTRL_OFFSET,
                     nc & ~XEMACPS_NWCTRL_RXEN_MASK);
    XEmacPs_WriteReg(EMACPS_BASEADDR, XEMACPS_RXQBASE_OFFSET, (u32)(UINTPTR)RxBdSpace);
    XEmacPs_WriteReg(EMACPS_BASEADDR, XEMACPS_TXQBASE_OFFSET, (u32)(UINTPTR)TxBdSpace);

    XEmacPs_Start(ep);
    XEmacPs_IntDisable(ep, 0xFFFFFFFFU);

    xil_printf("[DBG] NWCTRL=0x%08X\r\n",
               XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_NWCTRL_OFFSET));
    xil_printf("[DBG] NWCFG =0x%08X\r\n",
               XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_NWCFG_OFFSET));
    xil_printf("[DBG] NWSR  =0x%08X\r\n",
               XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_NWSR_OFFSET));
    xil_printf("[DBG] RXQBA =0x%08X (expect 0x%08X)\r\n",
               XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_RXQBASE_OFFSET),
               (u32)(UINTPTR)RxBdSpace);
    xil_printf("[DBG] TXQBA =0x%08X (expect 0x%08X)\r\n",
               XEmacPs_ReadReg(EMACPS_BASEADDR, XEMACPS_TXQBASE_OFFSET),
               (u32)(UINTPTR)TxBdSpace);
}
