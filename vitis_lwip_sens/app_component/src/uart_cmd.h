#pragma once

/* FreeRTOS タスク関数。xTaskCreate() に渡して使う。
 * 内部で uart_init() を呼ぶため、外部から事前呼び出し不要。 */
void uart_task(void *pvParameters);
