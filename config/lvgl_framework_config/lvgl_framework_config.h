#pragma once

/*********************
 *    LVGL CONFIGS
 *********************/
//===========================
// LVGL Panel Rotation

#define LILYGO_T_HMI_ESP_IDF_V5_X 1

#define DISPLAY_ROTATION_0   0
#define DISPLAY_ROTATION_90  90
#define DISPLAY_ROTATION_180 180
#define DISPLAY_ROTATION_270 270

#define LVGL_DISPLAY_PANEL_ROTATION (DISPLAY_ROTATION_0)

//-----------------------------------------------------------
#define LV_TICK_SOURCE_TIMER    0
#define LV_TICK_SOURCE_TASK     1
#define LV_TICK_SOURCE_CALLBACK 2
#ifndef LV_TICK_SOURCE
#    define LV_TICK_SOURCE (LV_TICK_SOURCE_TIMER)
#endif /* #ifndef LV_TICK_SOURCE */
//---------
//---------
#if LV_TICK_SOURCE == LV_TICK_SOURCE_TIMER
#    define TICK_INCREMENTATION 5  // in ms(milliseconds) must be equal incrementation with delay
#    define LV_TIMER_INCREMENT  TICK_INCREMENTATION * 1000
#    define LV_DELAY            1
#else
#    define TICK_INCREMENTATION 5  // in ms(milliseconds) must be equal incrementation with delay
#    define LV_TIMER_INCREMENT  TICK_INCREMENTATION * 1000
#    define LV_DELAY            5
#endif
//---------
// #define flush_ready_in_disp_flush
#define flush_ready_in_io_trans_done

/* BUFFER MODE */
#define BUFFER_20LINES     1
#define BUFFER_40LINES     2
#define BUFFER_60LINES     3  // merge
#define BUFFER_DEVIDED4    4
#define BUFFER_FULL        5               // merge super ok
#define BUFFER_MODE        BUFFER_40LINES  // selecteaza modul de buffer , defaut este BUFFER_FULL
#define DOUBLE_BUFFER_MODE true
//---------
/* BUFFER MEMORY TYPE AND DMA */
#define BUFFER_INTERNAL 1
#define BUFFER_SPIRAM   2
#define BUFFER_MEM      BUFFER_SPIRAM
#if (BUFFER_MEM == BUFFER_SPIRAM)
#    define DMA_ON (true)
#endif /* #if (BUFFER_MEM == BUFFER_INTERNAL) */
//---------

//-----------------------------------------------------------
/* RENDER MODE */
// Aici e mod mai special pt ca se transmite direct functiei....
#define RENDER_MODE_PARTIAL (LV_DISPLAY_RENDER_MODE_PARTIAL)
#define RENDER_MODE_FULL    (LV_DISPLAY_RENDER_MODE_FULL)
#define RENDER_MODE_DIRECT  (LV_DISPLAY_RENDER_MODE_DIRECT)

#define RENDER_MODE (RENDER_MODE_PARTIAL)
//--------------------- --------------------------------------

/*********************
 *   END LVGL CONFIGS
 *********************/