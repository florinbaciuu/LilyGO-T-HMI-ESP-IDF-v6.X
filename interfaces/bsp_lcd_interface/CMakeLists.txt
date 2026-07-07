

set(
    srcs 
    "lcd_bsp_interface.c"
)

set(
    include_dirs
    "."
)

set(
    requires
    esp_lcd
)

set(
    priv_requires
    esp_lcd
    esp_system
    esp_common
    log
    bsp_spi_interface
    board_pins
    board_config
    lvgl
    bsp_lcd_interface
    lvgl_framework_config
    bsp_i80_parallel_interface
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
