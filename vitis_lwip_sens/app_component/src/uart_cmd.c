#include "uart_cmd.h"
#include "sensors.h"
#include "gpio_intr.h"
#include "xuartps.h"
#include "xuartps_hw.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* main.c で定義されたI2C排他制御ミューテックスと共有センサーデータ */
extern SemaphoreHandle_t g_iic_mutex;
extern volatile int g_sens_temp;
extern volatile int g_sens_hum;
extern volatile int g_sens_press;

static XUartPs Uart;

static void uart_init(void)
{
    XUartPs_Config *cfg = XUartPs_LookupConfig(XPAR_UART1_BASEADDR);
    XUartPs_CfgInitialize(&Uart, cfg, cfg->BaseAddress);
    XUartPs_SetBaudRate(&Uart, 115200);
}

static void print_sensor_vals(int t, int h, int p)
{
    if (t < 0) {
        int ta = -t;
        xil_printf("T=-%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                   ta/10, ta%10, h/10, h%10, p/10, p%10);
    } else {
        xil_printf("T=%d.%dC  H=%d.%d%%  P=%d.%dhPa\r\n",
                   t/10, t%10, h/10, h%10, p/10, p%10);
    }
}

static void handle_cmd(u8 c)
{
    switch (c | 0x20U) {   /* 小文字化 */
    case 'i':
        xil_printf("\r\nBoard IP : 192.168.1.100\r\n");
        xil_printf("UDP port : 5000\r\n");
        break;
    case 't': {
        /* sens_task が更新した最新キャッシュ値を表示 */
        int t = g_sens_temp;
        int h = g_sens_hum;
        int p = g_sens_press;
        xil_printf("\r\n[UART] ");
        print_sensor_vals(t, h, p);
        break;
    }
    case 'r':
        xil_printf("\r\n[IIC] センサー再初期化...\r\n");
        if (xSemaphoreTake(g_iic_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            sensors_init();
            xSemaphoreGive(g_iic_mutex);
        }
        break;
    case 'b':
        xil_printf("\r\n");
        if (xSemaphoreTake(g_iic_mutex, pdMS_TO_TICKS(10000)) == pdTRUE) {
            iic_scan();
            xSemaphoreGive(g_iic_mutex);
        }
        break;
    case 'h': case '?':
        xil_printf("\r\nCommands: i=info  t=sensor_print  r=iic_reinit  b=i2c_scan  h=help\r\n");
        break;
    default:
        break;
    }
}

void uart_task(void *pvParameters)
{
    (void)pvParameters;
    uart_init();

    /* GPIO init must run after vTaskStartScheduler() so that XSetupInterruptSystem()
     * has already set ScuGicInitialized=TRUE inside xilinterrupt.  Calling
     * xPortInstallInterruptHandler before that returns XST_SUCCESS silently
     * without actually registering the handler. */
    gpio_intr_init();

    u32 base = Uart.Config.BaseAddress;
    xil_printf("[UART] Task started. Type 'h' for help.\r\n");

    for (;;) {
        if (XUartPs_IsReceiveData(base)) {
            u8 ch = XUartPs_RecvByte(base);
            handle_cmd(ch);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
