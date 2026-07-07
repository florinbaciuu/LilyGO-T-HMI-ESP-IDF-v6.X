#pragma once

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"

extern esp_lcd_panel_io_handle_t lcd_io_handle;
extern lv_display_t* disp;
extern esp_lcd_panel_handle_t    panel_handle;

/**********************
 *   DISPLAY FUNCTIONS
 **********************/
void bsp_lcd_init();


void display_io_i80_config();
void display_panel_config();
bool panel_io_trans_done_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx);

