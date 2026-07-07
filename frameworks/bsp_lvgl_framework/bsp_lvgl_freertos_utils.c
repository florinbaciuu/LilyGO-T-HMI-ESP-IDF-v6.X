
#include "lvgl_framework.h"
#include "lvgl_framework_internals.h"
#include "lvgl_framework_config.h"
#include "freertos/idf_additions.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "misc/lv_timer.h"
// -------------------------------
// -------------------------------
/*
███████ ██████  ███████ ███████ ██████ ████████  ██████  ███████ 
██      ██   ██ ██      ██      ██   ██   ██    ██    ██ ██      
█████   ██████  █████   █████   ██████    ██    ██    ██ ███████ 
██      ██   ██ ██      ██      ██   ██   ██    ██    ██      ██ 
██      ██   ██ ███████ ███████ ██   ██   ██     ██████  ███████ 
*/

// ========================================== //

/*********************
 *  rtos variables
 *********************/
TaskHandle_t xHandle_lv_main_task;
TaskHandle_t xHandle_lv_main_tick_task;

// ========================================== //

#if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK
uint32_t lv_get_rtos_tick_count_callback(void) {
    return xTaskGetTickCount();
}  // Callback pentru a obține numărul de tick-uri RTOS
#endif /* #if LV_TICK_SOURCE == LV_TICK_SOURCE_CALLBACK */

// ========================================== //

#if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER

static void lv_tick_timer_callback_func(void* arg) {
    lv_tick_inc((TICK_INCREMENTATION));  // 5 ms ticklv_timer_handler();
}

// ========================================== //

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
        // vTaskDelay(LV_DELAY);
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
        // vTaskDelay(5);
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

void lvgl_bsp_kernel_start(void) {
    // =================================================================================================
    s_lvgl_display_panel_set_initial_rotation_config();
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
