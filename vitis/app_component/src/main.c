#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"

static void task1(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        xil_printf("hello! zybo from task1\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void task2(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        xil_printf("hello! zybo from task2\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    xTaskCreate(task1, "Task1", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(task2, "Task2", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();

    for (;;);
    return 0;
}
