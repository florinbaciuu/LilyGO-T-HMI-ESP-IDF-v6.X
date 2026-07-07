#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

extern esp_lcd_panel_io_handle_t touch_io_handle;
extern esp_lcd_touch_handle_t touch_handle;

void bsp_touchscreen_init(void);

int touch_map_value(int val, int in_min, int in_max, int out_min, int out_max);
void touch_get_calibrated_point(int16_t xraw, int16_t yraw, int16_t* x_out, int16_t* y_out);
bool touch_read(uint16_t* x_out, uint16_t* y_out);
bool touch_panel_is_touched(void);

void spi_bus_config();

void touch_io_config();
void touch_panel_config();