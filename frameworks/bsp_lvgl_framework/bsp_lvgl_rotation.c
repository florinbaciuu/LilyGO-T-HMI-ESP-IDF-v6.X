
#include "esp_log.h"
#include "lvgl_framework.h"
#include "lvgl_framework_internals.h"
#include "display/lv_display.h"
#include "misc/lv_area.h"
#include "lcd_bsp_interface.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl_framework_config.h"
#include "touch_bsp_interface.h"
// -------------------------------
// -------------------------------
#if LILYGO_T_HMI_ESP_IDF_V5_X == 1
/***
 * Rotation in runtime
 */

 // lilygo T-HMI-ESP32-V5.X specific function to update the display and touch rotation based on the current LVGL display rotation setting.
void lvgl_display_rotation_update_callback_thmi(lv_display_t* display_handle) {
    switch (lv_display_get_rotation(display_handle)) {
        case LV_DISPLAY_ROTATION_0:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, false, false);  /// ROTATIA INITIALA
            esp_lcd_touch_set_swap_xy(touch_handle, false);
            esp_lcd_touch_set_mirror_x(touch_handle, true);
            esp_lcd_touch_set_mirror_y(touch_handle, false);
            break;
        case LV_DISPLAY_ROTATION_90:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, true, false);
            esp_lcd_touch_set_swap_xy(touch_handle, false);
            esp_lcd_touch_set_mirror_x(touch_handle, false);
            esp_lcd_touch_set_mirror_y(touch_handle, true);
            break;
        case LV_DISPLAY_ROTATION_180:
            esp_lcd_panel_swap_xy(panel_handle, false);
            esp_lcd_panel_mirror(panel_handle, true, true);
            esp_lcd_touch_set_swap_xy(touch_handle, false);
            esp_lcd_touch_set_mirror_x(touch_handle, true);
            esp_lcd_touch_set_mirror_y(touch_handle, false);
            break;
        case LV_DISPLAY_ROTATION_270:
            esp_lcd_panel_swap_xy(panel_handle, true);
            esp_lcd_panel_mirror(panel_handle, false, true);
            esp_lcd_touch_set_swap_xy(touch_handle, false);
            esp_lcd_touch_set_mirror_x(touch_handle, false);
            esp_lcd_touch_set_mirror_y(touch_handle, true);
            break;
    }
}
#endif  // LILYGO_T_HMI_ESP_IDF_V5_X == 1
// -------------------------------

void lvgl_display_rotation_update_callback(lv_display_t* display_handle) {
#if LILYGO_T_HMI_ESP_IDF_V5_X == 1
    lvgl_display_rotation_update_callback_thmi(display_handle);
#endif  // LILYGO_T_HMI_ESP_IDF_V5_X == 1
}

void s_lvgl_display_panel_set_initial_rotation_config() {
#if (LVGL_DISPLAY_PANEL_ROTATION == DISPLAY_ROTATION_0)
    ESP_LOGI("LVGL", "Set display rotation to 0 degrees");
    lv_display_set_rotation(disp, (lv_display_rotation_t) LV_DISPLAY_ROTATION_0);  // Seteaza rotatia lvgl LV_DISPLAY_ROTATION_0
    lvgl_display_rotation_update_callback(disp);
#elif (LVGL_DISPLAY_PANEL_ROTATION == DISPLAY_ROTATION_90)
    ESP_LOGI("LVGL", "Set display rotation to 90 degrees");
    lv_display_set_rotation(disp, (lv_display_rotation_t) LV_DISPLAY_ROTATION_90);  // Seteaza rotatia lvgl LV_DISPLAY_ROTATION_90
    lvgl_display_rotation_update_callback(disp);
#elif (LVGL_DISPLAY_PANEL_ROTATION == DISPLAY_ROTATION_180)
    ESP_LOGI("LVGL", "Set display rotation to 180 degrees");
    lv_display_set_rotation(disp, (lv_display_rotation_t) LV_DISPLAY_ROTATION_180);  // Seteaza rotatia lvgl LV_DISPLAY_ROTATION_180
    lvgl_display_rotation_update_callback(disp);
#elif (LVGL_DISPLAY_PANEL_ROTATION == DISPLAY_ROTATION_270)
    ESP_LOGI("LVGL", "Set display rotation to 270 degrees");
    lv_display_set_rotation(disp, (lv_display_rotation_t) LV_DISPLAY_ROTATION_270);  // Seteaza rotatia lvgl LV_DISPLAY_ROTATION_270
    lvgl_display_rotation_update_callback(disp);
#endif  // LVGL_DISPLAY_PANEL_ROTATION
}

// -------------------------------

void lvgl_displ_rotate_now() {
    lv_display_t* current_display     = lv_display_get_default();
    int           rotation = (int) lv_display_get_rotation(current_display);
    rotation++;
    if (rotation > (int) LV_DISPLAY_ROTATION_270) {
        rotation = (int) LV_DISPLAY_ROTATION_0;
    }
    lv_display_set_rotation(current_display,(lv_display_rotation_t) rotation);
    lvgl_display_rotation_update_callback(current_display); // sincronizare hardware display
    ESP_LOGI("UI", "Display rotation changed to %d", rotation);
}

void lvgl_displ_rotate_now_v2(lv_display_t* display_handle) {
    int           rotation = (int) lv_display_get_rotation(display_handle);
    rotation++;
    if (rotation > (int) LV_DISPLAY_ROTATION_270) {
        rotation = (int) LV_DISPLAY_ROTATION_0;
    }
    lv_display_set_rotation(display_handle,(lv_display_rotation_t) rotation);
    lvgl_display_rotation_update_callback(display_handle); // sincronizare hardware display
    ESP_LOGI("UI", "Display rotation changed to %d", rotation);
}
