#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**********************
 *   LVGL FUNCTIONS
 **********************/
bool s_lvgl_port_init_locking_mutex(void);
bool s_lvgl_lock(TickType_t timeout);  // functie mai buna decat cea de mai sus
void s_lvgl_unlock(void);
void s_lvgl_port_init(void);  // superfunction de init LVGL

#define LVGL_LOCK()   s_lvgl_lock(portMAX_DELAY)
#define LVGL_UNLOCK() s_lvgl_unlock()

void create_and_start_lvgl_tasks(void);

#ifdef __cplusplus
}
#endif