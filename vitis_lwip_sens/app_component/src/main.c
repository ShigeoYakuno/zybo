#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "sensors.h"
#include "uart_cmd.h"
#include "log_task.h"
#include "gpio_intr.h"

/* lwIP - SOCKET_API mode (lwip220_api_mode = SOCKET_API in BSP settings) */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "netif/xadapter.h"

#define EMAC_BASEADDR       XPAR_XEMACPS_0_BASEADDR
#define SENSOR_UDP_PORT     5000

static u8_t mac_addr[] = {0x00, 0x0A, 0x35, 0x00, 0x01, 0x02};
static struct netif netif;

/* I2C ハードウェアアクセスを排他するミューテックス。
 * sensors_read() / sensors_init() / iic_scan() を呼ぶ前に必ず取得すること。 */
SemaphoreHandle_t g_iic_mutex;

/* sens_task が定期的に更新するセンサーキャッシュ。
 * uart_task の 't' コマンドはここを読み出す (32bit アライン済みで安全)。 */
volatile int g_sens_temp  = 0;
volatile int g_sens_hum   = 0;
volatile int g_sens_press = 0;

/* ---- sens_task ---- */
static void sens_task(void *pvParameters)
{
    (void)pvParameters;

    /* 起動直後に I2C アクセスすると net_task と競合し不安定になるため
     * ネットワーク初期化が完了するまで待機する。 */
    xil_printf("[SENS] Task started. Waiting 3s before I2C init...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* センサー初期化 (AXI IIC が存在しない場合はここで Data Abort が発生する) */
    xil_printf("[SENS] Starting I2C sensor init...\r\n");
    if (xSemaphoreTake(g_iic_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        sensors_init();
        xSemaphoreGive(g_iic_mutex);
    }

    xil_printf("[SENS] Task started (5 s interval)\r\n");

    for (;;) {
        int t, h, p;

        if (xSemaphoreTake(g_iic_mutex, portMAX_DELAY) == pdTRUE) {
            sensors_read(&t, &h, &p);
            g_sens_temp  = t;
            g_sens_hum   = h;
            g_sens_press = p;
            xSemaphoreGive(g_iic_mutex);
        }

        if (t < 0) {
            int ta = -t;
            xil_printf("[SENS] T=-%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       ta/10, ta%10, h/10, h%10, p/10, p%10);
        } else {
            xil_printf("[SENS] T=%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                       t/10, t%10, h/10, h%10, p/10, p%10);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ---- led_task: LEDシーケンサ (最低優先度 = システム生存確認用) ---- */
static void led_task(void *pvParameters)
{
    (void)pvParameters;

    /* LED点灯パターン: led0→led1→led2→led3→全消灯 */
    static const u32 s_pat[]  = { 0x1U, 0x2U, 0x4U, 0x8U, 0x0U };
    /* モード別ステップ周期 (ms): mode0=1000, 1=500, 2=250, 3=125 */
    static const TickType_t s_ms[] = { 1000, 500, 250, 125 };

    int step = 0;
    for (;;) {
        gpio_led_write(s_pat[step]);
        step = (step + 1) % (int)(sizeof(s_pat) / sizeof(s_pat[0]));
        vTaskDelay(pdMS_TO_TICKS(s_ms[g_led_mode]));
    }
}

/* ---- UDP echo task (task2 から分離) ---- */
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
        if (len <= 0) continue;
        buf[len] = '\0';
        xil_printf("[UDP] RX %d bytes: %s\r\n", len, buf);

        /* 受信データをエコーバック */
        lwip_sendto(sock, buf, len, 0,
                    (struct sockaddr *)&remote, remote_len);
    }
}

/* ---- net_task: lwIP + GEM0 初期化後に自己削除 ---- */
static void net_task(void *pvParameters)
{
    (void)pvParameters;
    ip_addr_t ipaddr, netmask, gw;

    IP4_ADDR(&ipaddr,  192, 168,   1, 100);
    IP4_ADDR(&netmask, 255, 255, 255,   0);
    IP4_ADDR(&gw,      192, 168,   1,   1);

    xil_printf("[NET] Initializing lwIP (SOCKET_API mode)...\r\n");

    lwip_init();
    tcpip_init(NULL, NULL);

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

    sys_thread_new("RECV", (void(*)(void*))xemacif_input_thread,
                   &netif,
                   configMINIMAL_STACK_SIZE * 8,
                   tskIDLE_PRIORITY + 2);

    xTaskCreate(udp_sensor_task, "UDP",
                configMINIMAL_STACK_SIZE * 8,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    vTaskDelete(NULL);
}

int main(void)
{
    g_iic_mutex = xSemaphoreCreateMutex();

    /* Queue-based log task: log_printf / log_printf_fromISR route messages here */
    start_log_task();

    /* net_task: lwIP + GEM0 初期化、UDP echo タスクを生成後に自己削除
     * 他タスクより高優先度にして先にネットワークを立ち上げる */
    xTaskCreate(net_task, "NET",
                configMINIMAL_STACK_SIZE * 16,
                NULL,
                tskIDLE_PRIORITY + 2,
                NULL);

    /* uart_task: UARTコマンド受付 (i/t/r/b/h) */
    xTaskCreate(uart_task, "UART",
                configMINIMAL_STACK_SIZE * 4,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    /* sens_task: AHT20 + BME280 を 5 s 毎に読み出してキャッシュ更新
     * 内部で 3 s 待機後に I2C 初期化するため net_task 完了後に動作する */
    xTaskCreate(sens_task, "SENS",
                configMINIMAL_STACK_SIZE * 8,
                NULL,
                tskIDLE_PRIORITY + 1,
                NULL);

    /* led_task: LED シーケンサ。最低優先度でシステム生存確認を兼ねる。
     * gpio_intr_init は uart_task 内で行うため GPIO_1 は起動直後から使用可能。 */
    xTaskCreate(led_task, "LED",
                configMINIMAL_STACK_SIZE * 2,
                NULL,
                tskIDLE_PRIORITY,
                NULL);

    vTaskStartScheduler();

    for (;;);
    return 0;
}
