#include "config.h"
#include "eth.h"
#include "net.h"
#include "sensors.h"
#include "uart_cmd.h"
#include "xil_printf.h"

int main(void)
{
    uart_init();
    sensors_init();

    xil_printf("\r\n=== ZYBO GEM0 Ethernet Sample ===\r\n");
    xil_printf("Board IP : %d.%d.%d.%d\r\n",
               BOARD_IP[0], BOARD_IP[1], BOARD_IP[2], BOARD_IP[3]);
    xil_printf("Board MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               BOARD_MAC[0], BOARD_MAC[1], BOARD_MAC[2],
               BOARD_MAC[3], BOARD_MAC[4], BOARD_MAC[5]);
    xil_printf("UDP dest : %d.%d.%d.%d:%u\r\n",
               DEST_IP[0], DEST_IP[1], DEST_IP[2], DEST_IP[3],
               (u32)UDP_DEST_PORT);
    xil_printf("Commands : i=info  s=stats  u=udp_send  h=help\r\n\r\n");

    if (emacps_setup(&EmacPs) != XST_SUCCESS) {
        xil_printf("ERROR: EMACPs init failed\r\n");
        return 1;
    }
    if (rx_ring_setup(&EmacPs) != XST_SUCCESS) {
        xil_printf("ERROR: RX ring setup failed\r\n");
        return 1;
    }
    if (tx_ring_setup(&EmacPs) != XST_SUCCESS) {
        xil_printf("ERROR: TX ring setup failed\r\n");
        return 1;
    }

    wait_for_phy_link(&EmacPs);
    emacps_start_hw(&EmacPs);

    xil_printf("Ready. Listening for ARP/ICMP...\r\n");

    while (1) {
        rx_poll(&EmacPs);
        handle_uart(&EmacPs);
    }

    return 0;
}
