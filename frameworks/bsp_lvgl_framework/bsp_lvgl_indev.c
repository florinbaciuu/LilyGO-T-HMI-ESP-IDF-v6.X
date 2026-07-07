#include "esp_log.h"
#include "indev/lv_indev.h"
#include "lvgl_framework.h"
#include "lvgl_framework_internals.h"
#include "lvgl_framework_config.h"
#include "touch_bsp_interface.h"

//===========================================//

void lv_touchpad_read(lv_indev_t* indev_drv, lv_indev_data_t* data) {
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;
    uint16_t        x, y;
    if (touch_read(&x, &y)) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        last_x        = x;
        last_y        = y;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;  // Nu e apăsat → eliberat
        data->point.x = last_x;                   // Păstrăm ultima poziție x (LVGL o cere chiar și în RELEASED)
        data->point.y = last_y;                   // Păstrăm ultima poziție y (LVGL o cere chiar și în RELEASED)
    }
}
//---------
void lv_touchpad_read_v2(lv_indev_t* indev_drv, lv_indev_data_t* data) {
    static uint16_t      last_x          = 0;
    static uint16_t      last_y          = 0;
    static uint16_t      stable_x        = 0;
    static uint16_t      stable_y        = 0;
    static const uint8_t touch_tolerance = 8;
    uint16_t             x, y;

    bool touched = touch_read(&x, &y);
    if (touched) {
        // smoothing simplu
        if (abs((int) x - (int) last_x) < touch_tolerance && abs((int) y - (int) last_y) < touch_tolerance) {
            stable_x = x;
            stable_y = y;
        }
        last_x        = x;
        last_y        = y;
        data->point.x = stable_x;
        data->point.y = stable_y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
//---------
void lv_touchpad_read_v3(lv_indev_t* indev_drv, lv_indev_data_t* data) {
    static uint16_t      last_x          = 0;         // Ultima poziție X
    static uint16_t      last_y          = 0;         // Ultima poziție Y
    static uint16_t      stable_x        = 0;         // Poziția stabilă X
    static uint16_t      stable_y        = 0;         // Poziția stabilă Y
    static uint8_t       touch_cnt       = 0;         // Numărul de atingeri stabile
    static const uint8_t touch_tolerance = 255;       // Poți crește sau micșora după feeling //
                                                      // Distanța permisă între citiri succesive
    static const uint8_t TOUCH_STABLE_THRESHOLD = 2;  // Threshold pentru stabilitate  // De câte ori trebuie
                                                      // să fie stabil ca să fie considerat apăsat
    uint16_t x, y;
    if (touch_read(&x, &y)) {
        if (abs(x - last_x) < touch_tolerance && abs(y - last_y) < touch_tolerance) {
            if (touch_cnt < 255) {
                touch_cnt++;  // Incrementăm numărul de atingeri stabile
            }
        } else {
            touch_cnt = 0;  // Resetăm dacă mișcarea e prea mare
        }
        last_x = x;
        last_y = y;
        if (touch_cnt >= TOUCH_STABLE_THRESHOLD) {
            data->state = LV_INDEV_STATE_PRESSED;
            stable_x    = x;
            stable_y    = y;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;  // Nu trimitem touch activ până nu e stabil
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = stable_x;  // Trimitem ultima poziție stabilă
    data->point.y = stable_y;  // Trimitem ultima poziție stabilă
}

//===========================================//

void s_lvgl_input_device_config() {
    lv_indev_t* indev = lv_indev_create();           /*Initialize the (dummy) input device driver*/
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/

    lv_indev_set_read_cb(indev, lv_touchpad_read);  // old version
    // lv_indev_set_read_cb(indev, lv_touchpad_read_v2); // again, old version with some smoothing
    // lv_indev_set_read_cb(indev, lv_touchpad_read_v3);   // new version, with more aggressive smoothing, no more jitter at the cost of a bit of responsiveness

    ESP_LOGI("LVGL", "LVGL input device configured");
}

//===========================================//