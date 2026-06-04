#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "xil_types.h"

/* SW押下で更新される LED動作モード (0-3)。ISR/タスク両方から参照可能 */
extern volatile int g_led_mode;

int  gpio_intr_init(void);
void gpio_led_write(u32 value);
u32  gpio_sw_read(void);
u32  gpio_btn_read(void);

#ifdef __cplusplus
}
#endif
