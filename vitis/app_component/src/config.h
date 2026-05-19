#pragma once

#include "xil_types.h"

/* ===================== ボード設定 ===================== */
#define BOARD_IP0   192
#define BOARD_IP1   168
#define BOARD_IP2     1
#define BOARD_IP3   100

/* ===================== UDP 送信先 ===================== */
#define DEST_IP0    192
#define DEST_IP1    168
#define DEST_IP2      1
#define DEST_IP3     20

#define UDP_SRC_PORT   5000
#define UDP_DEST_PORT  5000

/* ===================== PHY ===================== */
#define PHY_ADDR  0   /* Zybo Z7: RTL8211E */

/* 配列は net.c で定義 */
extern const u8 BOARD_MAC[6];
extern const u8 BOARD_IP[4];
extern const u8 DEST_IP[4];
extern u8       dest_mac[6];   /* ARP 受信時に自動学習、初期値はブロードキャスト */
