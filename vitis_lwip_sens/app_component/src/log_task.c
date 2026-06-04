#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
#include "xil_printf.h"
#include "log_task.h"

#define LOG_QUEUE_LEN 16

QueueHandle_t logQueue = NULL;
static TaskHandle_t logTaskHandle = NULL;
static TimerHandle_t logTimer = NULL;

static int syslog_force_mode = -1;

void syslog_set_output(int mode)
{
    syslog_force_mode = mode;
}

/* Called from task context */
void log_printf(const char *fmt, ...)
{
    if (!logQueue) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    xQueueSend(logQueue, msg, 0);
}

/* Called from ISR context — uses FromISR queue send.
 * Static buffer avoids placing 256 B on the interrupted task's SVC stack,
 * which would overflow when portASM.S re-enters via PUSH {r0-r4, r12}.
 * Safe on single-core A9: no nested ISR at the same priority. */
void log_printf_fromISR(const char *fmt, ...)
{
    if (!logQueue) return;
    static char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    BaseType_t hpTaskWoken = pdFALSE;
    xQueueSendFromISR(logQueue, msg, &hpTaskWoken);
    portYIELD_FROM_ISR(hpTaskWoken);
}

/* Called from task context only — ISRs must use log_printf_fromISR */
void syslog(unsigned char mode, const char *fmt, ...)
{
    if (!logQueue) return;
    if (mode < NO_FLUSH) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    xQueueSend(logQueue, msg, 0);
}

static void log_task(void *pvParameters)
{
    (void)pvParameters;
    char msg[LOG_MSG_LEN];
    for (;;) {
        if (xQueueReceive(logQueue, msg, portMAX_DELAY)) {
            xil_printf("%s", msg);
        }
    }
}

static void log_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
}

void start_log_task(void)
{
    logQueue = xQueueCreate(LOG_QUEUE_LEN, sizeof(char[LOG_MSG_LEN]));
    xTaskCreate(log_task, "LOG", configMINIMAL_STACK_SIZE * 4, NULL,
                tskIDLE_PRIORITY + 1, &logTaskHandle);
    logTimer = xTimerCreate("LogTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, log_timer_cb);
    if (logTimer) xTimerStart(logTimer, 0);
}
