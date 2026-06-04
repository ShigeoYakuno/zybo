#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "queue.h"

#define NO_FLUSH      1
#define LOG_MSG_LEN   256

typedef enum {
    DEBUG = 2,
    DEBUG_WIFI,
    INFO,
    INFO_ENET,
    WARN,
    ERR
} log_mode;

typedef enum {
    LOG_OUTPUT_UART = 0,
    LOG_OUTPUT_FILE,
    LOG_OUTPUT_BOTH
} log_output_t;

extern QueueHandle_t logQueue;

void syslog_set_output(int mode);
void start_log_task(void);
void log_printf_fromISR(const char *fmt, ...);
void log_printf(const char *fmt, ...);
void syslog(unsigned char mode, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
