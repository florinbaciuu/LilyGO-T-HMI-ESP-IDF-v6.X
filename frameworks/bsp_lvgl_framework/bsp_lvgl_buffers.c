#include "lvgl_framework_internals.h"
#include "lvgl_framework_config.h"
#include "board_pins.h"
#include "esp_log.h"
#include "display/lv_display.h"
#include "lcd_bsp_interface.h"
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
#if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
#endif
#elif (BUFFER_MEM == BUFFER_INTERNAL)
#if (DMA_ON == 1)
#if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
#else
#if (DOUBLE_BUFFER_MODE == 1)
    disp_draw_buf    = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
    disp_draw_buf_II = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
    ESP_LOGI("LVGL", "LVGL buffers created in SPIRAM");
#else
    disp_draw_buf = (lv_color_t*) heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL);
#endif
#endif
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