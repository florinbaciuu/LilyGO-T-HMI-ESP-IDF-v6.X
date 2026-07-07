#include "lcd_bsp_interface.h"


#include "lvgl_framework_config.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"              // 🔥 ASTA E CHEIA
#include "esp_lcd_types.h"
#include "esp_lcd_panel_st7789.h"  // Sau driverul real folosit de tine
#include "i80_parallel_bsp_interface.h"

static const char* TAG = "ST7789";
//================================================================

esp_lcd_panel_io_handle_t lcd_io_handle   = NULL;
esp_lcd_panel_handle_t    panel_handle    = NULL;
lv_display_t* disp;  // Display LVGL

//================================================================

/**********************
 *   DISPLAY FUNCTIONS
 **********************/
// ------------------------
void display_io_i80_config() {
    esp_lcd_panel_io_i80_config_t lcd_io_config = {
        .cs_gpio_num = (gpio_num_t) BOARD_TFT_CS,
        //.pclk_hz             = LCD_PIXEL_CLOCK_HZ,
        //.pclk_hz             = 30000000,
        //.pclk_hz             = 26000000,
        .pclk_hz             = 20000000,
        .trans_queue_depth   = 10,
        .on_color_trans_done = panel_io_trans_done_callback,
        .user_ctx            = disp,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .dc_levels           = {
                      .dc_idle_level  = 0,
                      .dc_cmd_level   = 0,
                      .dc_dummy_level = 0,
                      .dc_data_level  = 1,
        },
        .flags = {
            .cs_active_high     = 0,
            .reverse_color_bits = 0,
            .swap_color_bytes   = 1,
            .pclk_active_neg    = 0,
            .pclk_idle_low      = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &lcd_io_config, &lcd_io_handle));
    ESP_LOGI(TAG, "I80 bus IO configured successfully");
}
//========================================

//========================================
void display_panel_config() {
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = (gpio_num_t) BOARD_TFT_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags          = {
                     .reset_active_high = 1,
        },
        .vendor_config = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_io_handle, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "ST7789 panel created");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_LOGI(TAG, "ST7789 panel reset done");
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_LOGI(TAG, "ST7789 panel gap set");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "ST7789 panel initialized");
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_LOGI(TAG, "ST7789 panel color inversion set");
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));
    ESP_LOGI(TAG, "ST7789 panel mirror set");
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_LOGI(TAG, "ST7789 panel swap xy set %bool", true);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "ST7789 panel display on");
}
//========================================
bool panel_io_trans_done_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx) {
#ifdef flush_ready_in_io_trans_done
    lv_display_t* d = (lv_display_t*) user_ctx;
    if (d)
        lv_disp_flush_ready(d);
#endif /* #ifdef flush_ready_in_io_trans_done */
    return false;
}
//===============================================

void bsp_lcd_init() {
    display_bus_config();
    display_io_i80_config();
    display_panel_config();
    ESP_LOGI(TAG, "LCD BSP initialization done");
}