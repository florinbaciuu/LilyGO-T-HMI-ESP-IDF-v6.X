#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "misc/lv_types.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************
 *   LVGL FUNCTIONS PROTOTYPES
 ******************************/

//+++++++++++++++++++++++++++++++++++++++//

void lvgl_bsp_framework_init(void);

//+++++++++++++++++++++++++++++++++++++++//

/**
 * This function initializes the LVGL kernel and starts the necessary FreeRTOS tasks for LVGL to operate.
 * It ensures that the LVGL main task and, if configured, the tick task are created and pinned to the appropriate core.
 * The function also includes a delay to allow the tasks to start properly before returning.
 * It uses a static variable to ensure that the initialization code is only executed once, preventing multiple initializations
 * if the function is called multiple times.
 */
void lvgl_bsp_kernel_start(void);

//+++++++++++++++++++++++++++++++++++++++//

void lvgl_execute_locked(void (*func)(void));

//+++++++++++++++++++++++++++++++++++++++//

void s_lvgl_display_panel_set_initial_rotation_config();

void lvgl_displ_rotate_now();
void lvgl_displ_rotate_now_v2(lv_display_t* display_handle);

//+++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++//
#ifdef __cplusplus
}
#endif