#pragma once

#include "xemacps.h"
#include "xil_types.h"

/* 受信・プロトコル統計 */
extern u32 g_rx_total;
extern u32 g_arp_rep;
extern u32 g_ping_rep;
extern u32 g_udp_tx;

/* RX ポーリング（フレーム受信 → ARP/ICMP 自動応答） */
void rx_poll(XEmacPs *ep);

/* UDP センサーデータ送信（T=21.1, H=59.1, P=1011.1 → DEST_IP:UDP_DEST_PORT） */
void send_udp_sensor(XEmacPs *ep);
