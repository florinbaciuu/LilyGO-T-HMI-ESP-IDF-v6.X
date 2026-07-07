
#include "lvgl_port.h"

#include "board_pins.h"

#include "esp_log.h"

#include "lvgl.h"
#include <lv_conf.h>
#include "touch_bsp_interface.h"
#include "lvgl_framework_config.h"
#include "esp_lcd_panel_ops.h"
#include "lcd_bsp_interface.h"

// ========================================== //

/**********************
 *   LVGL FUNCTIONS
 **********************/

/* Display flushing function callback */
void lv_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) { /* #if LVGL_BENCH_TEST */
    esp_lcd_panel_draw_bitmap(
        panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, (const void*) px_map);
#ifdef flush_ready_in_disp_flush
    lv_disp_flush_ready(disp);
#endif /* #ifdef (flush_ready_in_disp_flush) */
}

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
    static uint16_t      last_x          = 0;         // Ultima poziție X
    static uint16_t      last_y          = 0;         // Ultima poziție Y
    static uint16_t      stable_x        = 0;         // Poziția stabilă X
    static uint16_t      stable_y        = 0;         // Poziția stabilă Y
    static uint8_t       touch_cnt       = 0;         // Numărul de atingeri stabile
    static const uint8_t touch_tolerance = 8;         // Poți crește sau micșora după feeling //
                                                      // Distanța permisă între citiri succesive
    static const uint8_t TOUCH_STABLE_THRESHOLD = 0;  // Threshold pentru stabilitate  // De câte ori trebuie
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

// -------------------------------

#if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK
uint32_t lv_get_rtos_tick_count_callback(void) {
    return xTaskGetTickCount();
}  // Callback pentru a obține numărul de tick-uri RTOS
#endif /* #if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK */

// -------------------------------

uint32_t    bufSize;           // Dimensiunea buffer-ului
lv_color_t* disp_draw_buf;     // Buffer LVGL
lv_color_t* disp_draw_buf_II;  // Buffer LVGL secundar

void s_lvgl_set_buffers_config() {
#if (BUFFER_MODE == BUFFER_FULL)
    bufSize = ((LCD_WIDTH * LCD_HEIGHT) * lv_color_format_get_size(lv_display_get_color_format(disp)));
#elif (BUFFER_MODE == BUFFER_60LINES)
    bufSize = ((LCD_WIDTH * 60) * lv_color_format_get_size(lv_display_get_color_format(disp)));
#elif (BUFFER_MODE == BUFFER_40LINES)
    bufSize = ((LCD_WIDTH * 40) * lv_color_format_get_size(lv_display_get_color_format(disp)));
#elif (BUFFER_MODE == BUFFER_20LINES)
    bufSize = ((LCD_WIDTH * 20) * lv_color_format_get_size(lv_display_get_color_format(disp)));
#elif (BUFFER_MODE == BUFFER_DEVIDED4)
    bufSize = ((LCD_WIDTH * LCD_HEIGHT) * lv_color_format_get_size(lv_display_get_color_format(disp)) / 4);
#endif
#if (BUFFER_MEM == BUFFER_SPIRAM)
#    if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#    else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
#    endif
#elif (BUFFER_MEM == BUFFER_INTERNAL)
#    if (DMA_ON == 1)
#        if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#        else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#        endif
#    else
#        if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#        else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
#        endif
#    endif
#endif /* #if (BUFFER_MEM == BUFFER_SPIRAM) */

    // --- memset pentru curățare frame-uri ---
    // Asta îți garantează că primul frame e complet „negru”

    if (disp_draw_buf) {
        memset(disp_draw_buf, 0, bufSize);
    }
    if (disp_draw_buf_II) {
        memset(disp_draw_buf_II, 0, bufSize);
    }

    if (!disp_draw_buf) {  // VERIFICA DACA PRIMUL BUFFER ESTE CREAT
        ESP_LOGE("LVGL", "LVGL disp_draw_buf allocate failed!");
    }
#if (DOUBLE_BUFFER_MODE == 1)
    if (!disp_draw_buf_II) {  // VERIFICA DACA AL DOILEA BUFFER ESTE CREAT
        ESP_LOGE("LVGL", "LVGL disp_draw_buf_II allocate failed!");
    }
#endif

#if (DOUBLE_BUFFER_MODE == 1)
    lv_display_set_buffers(
        disp, disp_draw_buf, disp_draw_buf_II, bufSize, (lv_display_render_mode_t) RENDER_MODE);
    ESP_LOGI("LVGL", "LVGL buffers set");
#else
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize, (lv_display_render_mode_t) RENDER_MODE);
#endif
}

// -------------------------------

void s_lvgl_display_panel_setup_config_properties() {
    // Configurare panou LVGL
    lv_display_set_resolution(disp, LCD_WIDTH, LCD_HEIGHT);           // Seteaza rezolutia software
    lv_display_set_physical_resolution(disp, LCD_WIDTH, LCD_HEIGHT);  // Actualizeaza rezolutia reala
    lv_display_set_rotation(disp, (lv_display_rotation_t) 0);         // Seteaza rotatia lvgl
    lv_display_set_render_mode(disp,
        (lv_display_render_mode_t) RENDER_MODE);  // Seteaza (lv_display_render_mode_t)
    lv_display_set_antialiasing(disp, true);      // Antialiasing DA sau NU
    ESP_LOGI("LVGL", "LVGL display settings done");
}

// -------------------------------

void s_lvgl_input_device_config() {
    lv_indev_t* indev = lv_indev_create();           /*Initialize the (dummy) input device driver*/
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
    // lv_indev_set_read_cb(indev, lv_touchpad_read);    // old version
    lv_indev_set_read_cb(indev, lv_touchpad_read_v2);
    ESP_LOGI("LVGL", "LVGL Setup done");
}

// -------------------------------

void s_lvgl_port_init(void) {
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

    lv_display_set_flush_cb(disp, lv_disp_flush);  // Set the flush callback which will be called to
                                                   // copy the rendered image to the display.
    ESP_LOGI("LVGL", "LVGL display flush callback set");

    s_lvgl_input_device_config();
}

// -------------------------------
// -------------------------------

/*
███████ ██████  ███████ ███████ ██████ ████████  ██████  ███████ 
██      ██   ██ ██      ██      ██   ██   ██    ██    ██ ██      
█████   ██████  █████   █████   ██████    ██    ██    ██ ███████ 
██      ██   ██ ██      ██      ██   ██   ██    ██    ██      ██ 
██      ██   ██ ███████ ███████ ██   ██   ██     ██████  ███████ 
*/

static SemaphoreHandle_t s_lvgl_mutex = NULL;

// ========================================== //

// -------------------------------

bool s_lvgl_port_init_locking_mutex(void) {
    if (!s_lvgl_mutex)
        s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    return (s_lvgl_mutex != NULL);
}

// -------------------------------

// -------------------------------

bool s_lvgl_lock(TickType_t timeout_ms) {
    if (!s_lvgl_mutex) {
        return false;
    }
    return (xSemaphoreTakeRecursive(s_lvgl_mutex, timeout_ms) == pdTRUE);
}

// -------------------------------

void s_lvgl_unlock(void) {
    if (s_lvgl_mutex)
        xSemaphoreGiveRecursive(s_lvgl_mutex);
}

// -------------------------------

/*********************
 *  rtos variables
 *********************/
TaskHandle_t xHandle_lv_main_task;
TaskHandle_t xHandle_lv_main_tick_task;

/************************************************** */

#if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER

static void lv_tick_timer_callback_func(void* arg) {
    lv_tick_inc((TICK_INCREMENTATION));  // 5 ms ticklv_timer_handler();
}

static void lv_tick_start_timer(void) {
    const esp_timer_create_args_t timer_args = {
        .callback              = &lv_tick_timer_callback_func,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "lv_tick",
        .skip_unhandled_events = true};
    esp_timer_handle_t tick_timer;

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LV_TIMER_INCREMENT));  // 1000 us = 1 ms
}

/********************************************** */
/*                   TASK                       */
/********************************************** */
void lv_main_task(void* parameter) {
    xHandle_lv_main_task   = xTaskGetCurrentTaskHandle();
    static TickType_t tick = 0;
    tick                   = xTaskGetTickCount();
    while (true) {
        if (s_lvgl_lock(portMAX_DELAY)) {
            lv_timer_handler();
            s_lvgl_unlock();  // <— ADD
        }
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(LV_DELAY));
         //vTaskDelay(LV_DELAY);
    }
}

#elif LV_TICK_SOURCE == LV_TICK_SOURCE_TASK

/********************************************** */
/*                   TASK                       */
/********************************************** */
void lv_main_tick_task(void* parameter) {
    static TickType_t tick = 0;
    tick                   = xTaskGetTickCount();
    while (true) {
        lv_tick_inc(TICK_INCREMENTATION);                 // Incrementeaza tick-urile LVGL
        xTaskDelayUntil(&tick, pdMS_TO_TICKS(LV_DELAY));  // Delay precis mult mai rapid asa
    }
}

/********************************************** */
/*                   TASK                       */
/********************************************** */
void lv_main_task(void* parameter) {
    xHandle_lv_main_task   = xTaskGetCurrentTaskHandle();
    static TickType_t tick = 0;
    tick                   = xTaskGetTickCount();  // Inițializare corectă
    while (true) {
        if (s_lvgl_lock(portMAX_DELAY)) {  // <— ADD
            lv_timer_handler();
            s_lvgl_unlock();  // <— ADD
        }
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(LV_DELAY));
    }
}

#elif LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK
/********************************************** */
/*                   TASK                       */
/********************************************** */
void lv_main_task(void* parameter) {
    xHandle_lv_main_task   = xTaskGetCurrentTaskHandle();
    static TickType_t tick = 0;
    tick                   = xTaskGetTickCount();  // Inițializare corectă
    while (true) {
        if (s_lvgl_lock(portMAX_DELAY)) {  // <— ADD
            lv_timer_handler();
            s_lvgl_unlock();  // <— ADD
        }
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(LV_DELAY));
    }
}

#endif /* #if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER */

/************************************************** */

void create_and_start_lvgl_tasks(void) {
    // =================================================================================================
    static bool started = false;
    if (started)
        return;
    started = true;
    esp_rom_delay_us(100);
    xTaskCreatePinnedToCore(lv_main_task,  // Functia task-ului
        (const char*) "LVGL Main Task",    // Numele task-ului
        (uint32_t) (8192),                 // Dimensiunea stack-ului
        (NULL),                            // Parametri (daca exista)
        (UBaseType_t) 5,                   // Prioritatea task-ului // 3
        &xHandle_lv_main_task,             // Handle-ul task-ului
        ((1))                              // Nucleul pe care ruleaza task-ul
    );
#if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER
    lv_tick_start_timer();
#elif LV_TICK_SOURCE == LV_TICK_SOURCE_TASK
    esp_rom_delay_us(100);
    xTaskCreatePinnedToCore(lv_main_tick_task,  // Functia care ruleaza task-ul
        (const char*) "LVGL Tick Task",         // Numele task-ului
        (uint32_t) (3072),                      // Dimensiunea stack-ului
        (NULL),                                 // Parametri
        (UBaseType_t) 3,                        // Prioritatea task-ului // 1
        &xHandle_lv_main_tick_task,             // Handle-ul task-ului
        ((1))                                   // Nucleul pe care ruleaza (ESP32 e dual-core)
    );
#endif /* #if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER */
    esp_rom_delay_us(100);
}

/************************************************** */

/************************************************** */

/************************************************** */

/************************************************** */