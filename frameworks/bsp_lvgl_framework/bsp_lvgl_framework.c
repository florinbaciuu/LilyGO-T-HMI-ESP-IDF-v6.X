
#include "lvgl_framework.h"
#include "lvgl_framework_internals.h"
#include "lvgl_framework_config.h"
#include "freertos/idf_additions.h"
#include "board_pins.h"

#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

#include "lv_init.h"
#include "display/lv_display.h"
#include "misc/lv_area.h"
#include "misc/lv_types.h"

#include "lcd_bsp_interface.h"

// ========================================== //

/**********************************
 *   LVGL FRAMEWORK FUNCTIONS
 **********************************/

/* Display flushing function callback */
void lv_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) { /* #if LVGL_BENCH_TEST */
    esp_lcd_panel_draw_bitmap(
        panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (const void*) px_map);
#ifdef flush_ready_in_disp_flush
    lv_disp_flush_ready(disp);
#endif /* #ifdef (flush_ready_in_disp_flush) */
}

// -------------------------------
// -------------------------------
//===========================================//

void s_lvgl_display_panel_setup_config_properties() {
    // Configurare panou LVGL

    lv_display_set_resolution(disp, LCD_WIDTH, LCD_HEIGHT);           // Seteaza rezolutia software
    lv_display_set_physical_resolution(disp, LCD_WIDTH, LCD_HEIGHT);  // Actualizeaza rezolutia reala

    

    lv_display_set_render_mode(disp,
        (lv_display_render_mode_t) RENDER_MODE);  // Seteaza (lv_display_render_mode_t)
    lv_display_set_antialiasing(disp, true);      // Antialiasing DA sau NU
    ESP_LOGI("LVGL", "LVGL display settings done");
}

//===========================================//

void lvgl_bsp_framework_init(void) {
    lv_init();
#if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK
    // Next function comment because create problems with lvgl timers and esp32 timers
    lv_tick_set_cb(lv_get_rtos_tick_count_callback);
#endif /* #if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK */
    disp = lv_display_create(
        (int32_t) LCD_WIDTH,
        (int32_t) LCD_HEIGHT);

    s_lvgl_port_init_locking_mutex();
    s_lvgl_set_buffers_config();                     // configure buffers based on CONFIG settings
    s_lvgl_display_panel_setup_config_properties();  // configure display properties based on CONFIG settings

    // 

    lv_display_set_flush_cb(disp, lv_disp_flush);  // Set the flush callback which will be called to
                                                   // copy the rendered image to the display.
    ESP_LOGI("LVGL", "LVGL display flush callback set");

    s_lvgl_input_device_config();
}

/************************************************** */