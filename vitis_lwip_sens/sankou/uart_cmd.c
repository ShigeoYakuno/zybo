#include "uart_cmd.h"
#include "net.h"
#include "eth.h"
#include "config.h"
#include "sensors.h"
#include "xuartps.h"
#include "xuartps_hw.h"
#include "xil_printf.h"
#include "xparameters.h"

static XUartPs Uart;

void uart_init(void)
{
    XUartPs_Config *cfg = XUartPs_LookupConfig(XPAR_UART1_BASEADDR);
    XUartPs_CfgInitialize(&Uart, cfg, cfg->BaseAddress);
    XUartPs_SetBaudRate(&Uart, 115200);
}

void handle_uart(XEmacPs *ep)
{
    u32 base = Uart.Config.BaseAddress;
    if (!XUartPs_IsReceiveData(base)) return;

    u8 c = XUartPs_RecvByte(base);
    switch (c | 0x20U) {   /* 小文字化 */
    case 'i':
        xil_printf("\r\nBoard IP : %d.%d.%d.%d\r\n",
                   BOARD_IP[0], BOARD_IP[1], BOARD_IP[2], BOARD_IP[3]);
        xil_printf("Board MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   BOARD_MAC[0], BOARD_MAC[1], BOARD_MAC[2],
                   BOARD_MAC[3], BOARD_MAC[4], BOARD_MAC[5]);
        xil_printf("UDP dest : %d.%d.%d.%d:%u\r\n",
                   DEST_IP[0], DEST_IP[1], DEST_IP[2], DEST_IP[3],
                   (u32)UDP_DEST_PORT);
        break;
    case 's':
        xil_printf("\r\nRX=%u  ARP_REP=%u  PING_REP=%u  UDP_TX=%u  TX=%u\r\n",
                   g_rx_total, g_arp_rep, g_ping_rep, g_udp_tx, g_tx_total);
        xil_printf("DEST_MAC: %02X:%02X:%02X:%02X:%02X:%02X %s\r\n",
                   dest_mac[0], dest_mac[1], dest_mac[2],
                   dest_mac[3], dest_mac[4], dest_mac[5],
                   (dest_mac[0] == 0xFF) ? "(broadcast/unresolved)" : "(resolved)");
        break;
    case 'u': {
        int t, h, p;
        sensors_read(&t, &h, &p);
        if (t < 0) {
            int ta = -t;
            xil_printf("\r\nT=-%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       ta/10, ta%10, h/10, h%10, p/10, p%10);
        } else {
            xil_printf("\r\nT=%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       t/10, t%10, h/10, h%10, p/10, p%10);
        }
        send_udp_sensor(ep);
        break;
    }
    case 't': {
        int t, h, p;
        sensors_read(&t, &h, &p);
        if (t < 0) {
            int ta = -t;
            xil_printf("\r\nT=-%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       ta/10, ta%10, h/10, h%10, p/10, p%10);
        } else {
            xil_printf("\r\nT=%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       t/10, t%10, h/10, h%10, p/10, p%10);
        }
        break;
    }
    case 'r':
        xil_printf("\r\n[IIC] センサー再初期化...\r\n");
        sensors_init();
        break;
    case 'b':
        xil_printf("\r\n");
        iic_scan();
        break;
    case 'h': case '?':
        xil_printf("\r\nCommands: i=info  s=stats  u=udp_send(+print)  t=sensor_print  r=iic_reinit  b=i2c_scan  h=help\r\n");
        break;
    default:
        break;
    }
}
