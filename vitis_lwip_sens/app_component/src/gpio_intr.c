#include "FreeRTOS.h"
#include "task.h"
#include "xgpio.h"
#include "xscugic_hw.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "log_task.h"
#include "gpio_intr.h"
#include "xinterrupt_wrap.h"

/* フォールバック定義 — 旧 XSA ベースのプラットフォームでは未定義のため */
#ifndef XPAR_AXI_GPIO_0_BASEADDR
#define XPAR_AXI_GPIO_0_BASEADDR  0x41200000U
#endif
#ifndef XPAR_AXI_GPIO_1_BASEADDR
#define XPAR_AXI_GPIO_1_BASEADDR  0x41210000U
#endif
#ifndef XPAR_FABRIC_AXI_GPIO_0_INTR
#define XPAR_FABRIC_AXI_GPIO_0_INTR  30U
#endif
#ifndef XPAR_XSCUGIC_0_BASEADDR
#define XPAR_XSCUGIC_0_BASEADDR  0xF8F01000U
#endif

/* AXI GPIO_0: SW on CH1, BTN on CH2 (INTERRUPT_PRESENT=1, IS_DUAL=1) */
/* AXI GPIO_1: LED on CH1 (INTERRUPT_PRESENT=0, IS_DUAL=0) */
#define GPIO0_BASE  XPAR_AXI_GPIO_0_BASEADDR
#define GPIO1_BASE  XPAR_AXI_GPIO_1_BASEADDR

/* GIC distributor base (= XPAR_XSCUGIC_0_BASEADDR = 0xF8F01000) */
#define GIC_DIST_BASE   XPAR_XSCUGIC_0_BASEADDR

/* GPIO interrupt ID in GIC fabric */
#define GPIO0_INTR_ID   XPAR_FABRIC_AXI_GPIO_0_INTR

/*
 * Priority 0xA0 (160) is below configMAX_API_CALL_INTERRUPT_PRIORITY (18<<3=144),
 * so xQueueSendFromISR is safe to call from this ISR.
 */
#define GPIO_INTR_PRIORITY  0xA0U
#define GPIO_INTR_TRIGGER   0x03U  /* rising-edge */

static XGpio g_gpio0;
static XGpio g_gpio1;

static volatile u32 g_sw_prev  = 0;
static volatile u32 g_btn_prev = 0;

/* SW0-3 で切り替えるLEDモード (0=1000ms, 1=500ms, 2=250ms, 3=125ms) */
volatile int g_led_mode = 0;

static void gpio0_isr(void *pvArg)
{
    (void)pvArg;
    u32 status = XGpio_InterruptGetStatus(&g_gpio0);

    if (status & XGPIO_IR_CH1_MASK) {
        u32 sw_now = XGpio_DiscreteRead(&g_gpio0, 1);
        u32 changed = sw_now ^ g_sw_prev;
        for (int i = 0; i < 4; i++) {
            if (changed & (1u << i)) {
                if (sw_now & (1u << i)) {
                    g_led_mode = i;
                    log_printf_fromISR("[GPIO] SW%d ON  -> led_mode=%d\r\n", i, i);
                } else {
                    log_printf_fromISR("[GPIO] SW%d OFF\r\n", i);
                }
            }
        }
        g_sw_prev = sw_now;
    }

    if (status & XGPIO_IR_CH2_MASK) {
        u32 btn_now = XGpio_DiscreteRead(&g_gpio0, 2);
        u32 changed = btn_now ^ g_btn_prev;
        for (int i = 0; i < 4; i++) {
            if (changed & (1u << i)) {
                if (btn_now & (1u << i))
                    log_printf_fromISR("[GPIO] BTN%d PRESSED\r\n", i);
                else
                    log_printf_fromISR("[GPIO] BTN%d RELEASED\r\n", i);
            }
        }
        g_btn_prev = btn_now;
    }

    XGpio_InterruptClear(&g_gpio0, status);
}

int gpio_intr_init(void)
{
    /* --- GPIO_0 init (SW + BTN, both channels input) --- */
    XGpio_Config *cfg0 = XGpio_LookupConfig(GPIO0_BASE);
    if (!cfg0) return -1;
    if (XGpio_CfgInitialize(&g_gpio0, cfg0, cfg0->BaseAddress) != XST_SUCCESS) return -1;
    XGpio_SetDataDirection(&g_gpio0, 1, 0xFU);  /* CH1 SW: all input */
    XGpio_SetDataDirection(&g_gpio0, 2, 0xFU);  /* CH2 BTN: all input */

    /* --- GPIO_1 init (LED, channel 1 output) --- */
    XGpio_Config *cfg1 = XGpio_LookupConfig(GPIO1_BASE);
    if (!cfg1) return -1;
    if (XGpio_CfgInitialize(&g_gpio1, cfg1, cfg1->BaseAddress) != XST_SUCCESS) return -1;
    XGpio_SetDataDirection(&g_gpio1, 1, 0x0U);  /* CH1 LED: all output */
    XGpio_DiscreteWrite(&g_gpio1, 1, 0x0U);     /* LEDs off */

    /* Snapshot initial SW/BTN state to detect first-change correctly */
    g_sw_prev  = XGpio_DiscreteRead(&g_gpio0, 1);
    g_btn_prev = XGpio_DiscreteRead(&g_gpio0, 2);

    /*
     * Register GPIO ISR via FreeRTOS port API.
     * xPortInstallInterruptHandler writes to XScuGic_ConfigTable[0].HandlerTable,
     * which is the same table dispatched by vApplicationIRQHandler in portZynq7000.c.
     */
    BaseType_t ret = xPortInstallInterruptHandler(
                         (uint16_t)GPIO0_INTR_ID,
                         (XInterruptHandler)gpio0_isr,
                         NULL);
    if (ret != pdPASS) {
        xil_printf("[GPIO] ERROR: xPortInstallInterruptHandler failed\r\n");
        return -1;
    }

    /* Set priority and rising-edge trigger.
     * xilinterrupt encodes SPI IntrId as (SPI_number | type_bits); the absolute
     * GIC ID = SPI_number + 32. XSetPriorityTriggerType handles this offset. */
    XSetPriorityTriggerType((u32)GPIO0_INTR_ID, GPIO_INTR_PRIORITY, GIC_DIST_BASE);

    /* Enable GPIO interrupt in GIC */
    vPortEnableInterrupt((uint16_t)GPIO0_INTR_ID);

    /* Enable per-channel interrupts in AXI GPIO IP */
    XGpio_InterruptEnable(&g_gpio0, XGPIO_IR_CH1_MASK | XGPIO_IR_CH2_MASK);
    XGpio_InterruptGlobalEnable(&g_gpio0);

    xil_printf("[GPIO] Interrupt init done (SW=0x%X BTN=0x%X)\r\n",
               (unsigned)g_sw_prev, (unsigned)g_btn_prev);
    return 0;
}

void gpio_led_write(u32 value)
{
    XGpio_DiscreteWrite(&g_gpio1, 1, value & 0xFU);
}

u32 gpio_sw_read(void)
{
    return XGpio_DiscreteRead(&g_gpio0, 1);
}

u32 gpio_btn_read(void)
{
    return XGpio_DiscreteRead(&g_gpio0, 2);
}
