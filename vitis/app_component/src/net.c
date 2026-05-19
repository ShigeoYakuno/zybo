#include "net.h"
#include "eth.h"
#include "config.h"
#include "sensors.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include <string.h>
#include <stdio.h>

/* ===================== 定数・変数定義 ===================== */

const u8 BOARD_MAC[6] = {0x00, 0x0A, 0x35, 0x00, 0x01, 0x02};
const u8 BOARD_IP[4]  = {BOARD_IP0, BOARD_IP1, BOARD_IP2, BOARD_IP3};
const u8 DEST_IP[4]   = {DEST_IP0,  DEST_IP1,  DEST_IP2,  DEST_IP3};
u8       dest_mac[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

u32 g_rx_total = 0;
u32 g_arp_rep  = 0;
u32 g_ping_rep = 0;
u32 g_udp_tx   = 0;

/* ===================== チェックサム ===================== */

static u16 inet_checksum(const u8 *data, u32 len)
{
    u32 sum = 0;
    while (len > 1) {
        sum += ((u32)data[0] << 8) | data[1];
        data += 2; len -= 2;
    }
    if (len) sum += (u32)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* ===================== ARP リプライ ===================== */

static void send_arp_reply(XEmacPs *ep, const u8 *rx)
{
    memset(TxFrame, 0, 60);

    memcpy(TxFrame + 0,  rx + 6,    6);   /* 宛先 = 要求元 MAC */
    memcpy(TxFrame + 6,  BOARD_MAC, 6);
    TxFrame[12] = 0x08; TxFrame[13] = 0x06;

    TxFrame[14] = 0x00; TxFrame[15] = 0x01;   /* HTYPE Ethernet */
    TxFrame[16] = 0x08; TxFrame[17] = 0x00;   /* PTYPE IPv4 */
    TxFrame[18] = 6;    TxFrame[19] = 4;
    TxFrame[20] = 0x00; TxFrame[21] = 0x02;   /* OPER Reply */
    memcpy(TxFrame + 22, BOARD_MAC, 6);
    memcpy(TxFrame + 28, BOARD_IP,  4);
    memcpy(TxFrame + 32, rx + 22,   6);
    memcpy(TxFrame + 38, rx + 28,   4);

    emacps_send(ep, TxFrame, 60);
    g_arp_rep++;
    xil_printf("[ARP] Reply to %d.%d.%d.%d\r\n",
               rx[28], rx[29], rx[30], rx[31]);
}

/* ===================== ICMP Echo リプライ ===================== */

static void send_icmp_reply(XEmacPs *ep, const u8 *rx, u32 rx_len)
{
    u32 ihl      = (rx[14] & 0x0FU) * 4;
    u32 ip_total = ((u16)rx[16] << 8) | rx[17];
    u32 tx_len   = 14 + ip_total;
    u32 icmp_off = 14 + ihl;

    if (tx_len > MAX_FRAME || rx_len < tx_len) return;

    memcpy(TxFrame, rx, tx_len);
    memcpy(TxFrame + 0, rx + 6,    6);
    memcpy(TxFrame + 6, BOARD_MAC, 6);
    TxFrame[22] = 64;
    memcpy(TxFrame + 26, BOARD_IP, 4);
    memcpy(TxFrame + 30, rx + 26,  4);
    TxFrame[24] = 0; TxFrame[25] = 0;
    u16 ip_cs = inet_checksum(TxFrame + 14, ihl);
    TxFrame[24] = (u8)(ip_cs >> 8); TxFrame[25] = (u8)(ip_cs & 0xFF);

    TxFrame[icmp_off] = 0;
    TxFrame[icmp_off + 2] = 0; TxFrame[icmp_off + 3] = 0;
    u16 icmp_cs = inet_checksum(TxFrame + icmp_off, ip_total - ihl);
    TxFrame[icmp_off + 2] = (u8)(icmp_cs >> 8);
    TxFrame[icmp_off + 3] = (u8)(icmp_cs & 0xFF);

    emacps_send(ep, TxFrame, tx_len);
    g_ping_rep++;
    xil_printf("[PING] Reply to %d.%d.%d.%d\r\n",
               rx[26], rx[27], rx[28], rx[29]);
}

/* ===================== UDP センサーデータ送信 ===================== */

void send_udp_sensor(XEmacPs *ep)
{
    int t, h, p;
    sensors_read(&t, &h, &p);

    char payload[48];
    int n;
    if (t < 0) {
        int ta = -t;
        n = sprintf(payload, "T=-%d.%d,H=%d.%d,P=%d.%d",
                    ta/10, ta%10, h/10, h%10, p/10, p%10);
    } else {
        n = sprintf(payload, "T=%d.%d,H=%d.%d,P=%d.%d",
                    t/10, t%10, h/10, h%10, p/10, p%10);
    }
    u16 payload_len = (u16)n;
    u16 udp_len  = 8 + payload_len;
    u16 ip_total = 20 + udp_len;
    u32 tx_len   = 14 + (u32)ip_total;

    memset(TxFrame, 0, tx_len);

    /* Ethernet ヘッダ */
    memcpy(TxFrame + 0, dest_mac,  6);
    memcpy(TxFrame + 6, BOARD_MAC, 6);
    TxFrame[12] = 0x08; TxFrame[13] = 0x00;

    /* IPv4 ヘッダ */
    TxFrame[14] = 0x45;
    TxFrame[16] = (u8)(ip_total >> 8);
    TxFrame[17] = (u8)(ip_total & 0xFF);
    TxFrame[18] = 0x00; TxFrame[19] = 0x01;
    TxFrame[20] = 0x40; TxFrame[21] = 0x00;   /* Flags: DF */
    TxFrame[22] = 64;
    TxFrame[23] = 0x11;                         /* Protocol: UDP */
    memcpy(TxFrame + 26, BOARD_IP, 4);
    memcpy(TxFrame + 30, DEST_IP,  4);
    u16 ip_cs = inet_checksum(TxFrame + 14, 20);
    TxFrame[24] = (u8)(ip_cs >> 8);
    TxFrame[25] = (u8)(ip_cs & 0xFF);

    /* UDP ヘッダ */
    TxFrame[34] = (u8)(UDP_SRC_PORT  >> 8);
    TxFrame[35] = (u8)(UDP_SRC_PORT  & 0xFF);
    TxFrame[36] = (u8)(UDP_DEST_PORT >> 8);
    TxFrame[37] = (u8)(UDP_DEST_PORT & 0xFF);
    TxFrame[38] = (u8)(udp_len >> 8);
    TxFrame[39] = (u8)(udp_len & 0xFF);
    /* TxFrame[40-41]: UDP checksum = 0 (IPv4 では省略可) */

    memcpy(TxFrame + 42, payload, payload_len);

    emacps_send(ep, TxFrame, tx_len);
    g_udp_tx++;
    xil_printf("[UDP] Sent to %d.%d.%d.%d:%u  \"%s\"\r\n",
               DEST_IP[0], DEST_IP[1], DEST_IP[2], DEST_IP[3],
               (u32)UDP_DEST_PORT, payload);
}

/* ===================== フレーム分類 ===================== */

static void process_frame(XEmacPs *ep, const u8 *buf, u32 len)
{
    if (len < 14) return;
    g_rx_total++;

    u16 etype = ((u16)buf[12] << 8) | buf[13];

    if (etype == 0x0806U && len >= 42) {
        /* ARP 送信元が DEST_IP なら MAC を学習 */
        if (memcmp(buf + 28, DEST_IP, 4) == 0 &&
            memcmp(dest_mac, buf + 22, 6) != 0) {
            memcpy(dest_mac, buf + 22, 6);
            xil_printf("[ARP] Learned %d.%d.%d.%d -> %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                       DEST_IP[0], DEST_IP[1], DEST_IP[2], DEST_IP[3],
                       dest_mac[0], dest_mac[1], dest_mac[2],
                       dest_mac[3], dest_mac[4], dest_mac[5]);
        }

        /* Request かつターゲット IP が自 IP → 返答 */
        u16 oper = ((u16)buf[20] << 8) | buf[21];
        if (oper == 1 && memcmp(buf + 38, BOARD_IP, 4) == 0)
            send_arp_reply(ep, buf);

    } else if (etype == 0x0800U && len >= 34) {
        if (memcmp(buf + 30, BOARD_IP, 4) != 0) return;

        u8  proto    = buf[23];
        u32 ihl      = (buf[14] & 0x0FU) * 4;
        u32 icmp_off = 14 + ihl;

        if (proto == 0x01U && len > icmp_off && buf[icmp_off] == 8)
            send_icmp_reply(ep, buf, len);
    }
}

/* ===================== RX ポーリング ===================== */

void rx_poll(XEmacPs *ep)
{
    XEmacPs_BdRing *rxring = &XEmacPs_GetRxRing(ep);
    XEmacPs_Bd     *bd_first, *bd;
    u32 n;

    Xil_DCacheInvalidateRange((UINTPTR)RxBdSpace, sizeof(RxBdSpace));

    n = XEmacPs_BdRingFromHwRx(rxring, RXBD_CNT, &bd_first);
    if (n == 0) return;

    bd = bd_first;
    for (u32 i = 0; i < n; i++) {
        if (XEmacPs_BdIsRxNew(bd)) {
            UINTPTR addr = XEmacPs_BdGetBufAddr(bd);
            u32     plen = XEmacPs_BdGetLength(bd);
            Xil_DCacheInvalidateRange(addr, MAX_FRAME);
            process_frame(ep, (const u8 *)addr, plen);
            XEmacPs_BdClearRxNew(bd);
        }
        bd = XEmacPs_BdRingNext(rxring, bd);
    }

    XEmacPs_BdRingFree(rxring, n, bd_first);

    XEmacPs_Bd *new_bd;
    if (XEmacPs_BdRingAlloc(rxring, n, &new_bd) == XST_SUCCESS) {
        static u32 rx_idx = 0;
        bd = new_bd;
        for (u32 i = 0; i < n; i++) {
            XEmacPs_BdSetAddressRx(bd, (UINTPTR)RxFrames[rx_idx]);
            rx_idx = (rx_idx + 1) % RXBD_CNT;
            bd = XEmacPs_BdRingNext(rxring, bd);
        }
        XEmacPs_BdRingToHw(rxring, n, new_bd);
        Xil_DCacheFlushRange((UINTPTR)RxBdSpace, sizeof(RxBdSpace));
    }
}
