

set(
    srcs
    "bsp_lvgl_framework.c"
    "bsp_lvgl_rotation.c"
    "bsp_lvgl_freertos_utils.c"
    "bsp_lvgl_mutex_utils.c"
    "bsp_lvgl_indev.c"
    "bsp_lvgl_buffers.c"
)

set(
    include_dirs
    "."
    "include"
)

set(
    requires
)

set(
    priv_requires
    esp_lcd
    esp_system
    esp_timer
    esp_common
    log
    lvgl_framework_config
    board_pins
    board_config
    lvgl
    bsp_lcd_interface
    bsp_touch_interface
)

idf_component_register(
    SRCS 
    ${srcs}
    INCLUDE_DIRS 
    ${include_dirs}
    REQUIRES
    ${requires}
    PRIV_REQUIRES
    ${priv_requires}
)

# dezactivează tratarea warningurilor ca erori pentru componenta asta
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-variable
    -Wno-unused-function
)
