#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xparameters.h"

/* lwIP - SOCKET_API mode (lwip220_api_mode = SOCKET_API in BSP settings) */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "netif/xadapter.h"

#define EMAC_BASEADDR       XPAR_XEMACPS_0_BASEADDR
#define SENSOR_UDP_PORT     5000

/* MAC address for Zybo - must be unique on the network */
static u8_t mac_addr[] = {0x00, 0x0A, 0x35, 0x00, 0x01, 0x02};

static struct netif netif;

/* UDP sensor task: receives sensor data from PC and echoes back */
static void udp_sensor_task(void *pvParameters)
{
    (void)pvParameters;

    int sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        xil_printf("[UDP] ERROR: socket() failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local = {0};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(SENSOR_UDP_PORT);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (lwip_bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        xil_printf("[UDP] ERROR: bind() failed\r\n");
        lwip_close(sock);
        vTaskDelete(NULL);
        return;
    }

    xil_printf("[UDP] Listening on port %d\r\n", SENSOR_UDP_PORT);

    char buf[256];
    struct sockaddr_in remote;
    socklen_t remote_len;

    for (;;) {
        remote_len = sizeof(remote);
        int len = lwip_recvfrom(sock, buf, sizeof(buf) - 1, 0,
                                (struct sockaddr *)&remote, &remote_len);
        if (len <= 0) {
            continue;
        }
        buf[len] = '\0';
        xil_printf("[UDP] RX %d bytes: %s\r\n", len, buf);

        /* Echo back (or replace with your own sensor response) */
        lwip_sendto(sock, buf, len, 0,
                    (struct sockaddr *)&remote, remote_len);
    }
}

static void task1(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        xil_printf("hello! zybo from task1\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* task2: initializes lwIP (SOCKET_API) and brings up the Ethernet interface */
static void task2(void *pvParameters)
{
    (void)pvParameters;
    ip_addr_t ipaddr, netmask, gw;

    IP4_ADDR(&ipaddr,  192, 168,   1, 100);
    IP4_ADDR(&netmask, 255, 255, 255,   0);
    IP4_ADDR(&gw,      192, 168,   1,   1);

    xil_printf("[NET] Initializing lwIP (SOCKET_API mode)...\r\n");

    /* LWIP_XINIT is not defined in this BSP, so lwip_init() must be called explicitly */
    lwip_init();

    /* tcpip_init spawns lwIP's tcpip_thread (priority TCPIP_THREAD_PRIO=3) */
    tcpip_init(NULL, NULL);

    /* Add GEM0 as a network interface */
    if (!xemac_add(&netif, &ipaddr, &netmask, &gw,
                   mac_addr, EMAC_BASEADDR)) {
        xil_printf("[NET] ERROR: xemac_add failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    netif_set_default(&netif);

    if (netif_is_link_up(&netif)) {
        netif_set_up(&netif);
        xil_printf("[NET] Link UP  -- IP: 192.168.1.100\r\n");
    } else {
        netif_set_down(&netif);
        xil_printf("[NET] Link is DOWN -- check Ethernet cable\r\n");
    }

    /* Interrupt-driven Ethernet receive thread (Xilinx BSP, SOCKET_API mode) */
    sys_thread_new("RECV", (void(*)(void*))xemacif_input_thread,
                   &netif,
                   configMINIMAL_STACK_SIZE * 8,
                   tskIDLE_PRIORITY + 2);

    /* UDP sensor data task */
    xTaskCreate(udp_sensor_task, "UDP",
                configMINIMAL_STACK_SIZE * 8,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    vTaskDelete(NULL);
}

int main(void)
{
    xTaskCreate(task1, "Task1",
                configMINIMAL_STACK_SIZE,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    /* task2 needs a larger stack for lwIP + socket initialization */
    xTaskCreate(task2, "Task2",
                configMINIMAL_STACK_SIZE * 16,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    vTaskStartScheduler();

    for (;;);
    return 0;
}
