

set(
    srcs 
    "touch_bsp_interface.c"
)

set(
    include_dirs
    "."
)

set(
    requires
    esp_lcd_touch
)

set(
    priv_requires
    esp_system
    esp_common
    log
    board_pins
    board_config
    bsp_spi_interface
    esp_lcd
    esp_lcd_touch
    esp_lcd_touch_xpt2046
    
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
