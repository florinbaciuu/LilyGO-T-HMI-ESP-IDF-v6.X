

#include "i80_parallel_bsp_interface.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"  // 🔥 ASTA E CHEIA
#include "esp_lcd_types.h"
#include "esp_lcd_panel_st7789.h"  // Sau driverul real folosit de tine
#include "board_pins.h"

static const char* TAG           = "i80_BUS";

esp_lcd_i80_bus_handle_t i80_bus = NULL;

/**********************
 *   DISPLAY FUNCTIONS
 **********************/

void display_bus_config() {
    esp_lcd_i80_bus_config_t lcd_bus_config = {.dc_gpio_num = (gpio_num_t) BOARD_TFT_DC,
        .wr_gpio_num                                        = (gpio_num_t) BOARD_TFT_WR,
        .clk_src                                            = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums                                     = {
            (gpio_num_t) BOARD_TFT_DATA0,
            (gpio_num_t) BOARD_TFT_DATA1,
            (gpio_num_t) BOARD_TFT_DATA2,
            (gpio_num_t) BOARD_TFT_DATA3,
            (gpio_num_t) BOARD_TFT_DATA4,
            (gpio_num_t) BOARD_TFT_DATA5,
            (gpio_num_t) BOARD_TFT_DATA6,
            (gpio_num_t) BOARD_TFT_DATA7,
        },
        .bus_width          = 8,
        .max_transfer_bytes = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        .dma_burst_size     = 64,
        //.max_transfer_bytes = (size_t)((LCD_WIDTH * LCD_HEIGHT) * lv_color_format_get_size(lv_display_get_color_format(disp))),
    };
    ESP_LOGI(TAG, "Configuring I80 bus with DC GPIO: %d, WR GPIO: %d, Data GPIOs: %d-%d", BOARD_TFT_DC, BOARD_TFT_WR, BOARD_TFT_DATA0, BOARD_TFT_DATA7);
    // esp_lcd_new_i80_bus(&lcd_bus_config, &i80_bus);
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&lcd_bus_config, &i80_bus));
    ESP_LOGI(TAG, "I80 bus configured successfully");
}
