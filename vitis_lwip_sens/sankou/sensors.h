#pragma once
#include "xil_types.h"

/* AXI IIC経由でBME280(気圧)とAHT20(温湿度)を読み出す。
 * sensors_init() を一度だけ呼んでから sensors_read() を使う。
 *   temp_c10    : 温度 [0.1°C]  例: 211 = 21.1°C, -50 = -5.0°C
 *   hum_rh10    : 湿度 [0.1%RH] 例: 591 = 59.1%
 *   press_hpa10 : 気圧 [0.1hPa] 例: 10111 = 1011.1 hPa
 */
int  sensors_init(void);
void sensors_read(int *temp_c10, int *hum_rh10, int *press_hpa10);
void iic_scan(void);    /* I2Cバス全アドレスをスキャンしてACKデバイスを列挙 */
