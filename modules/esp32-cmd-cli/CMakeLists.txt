
# ==================================== #
set(hello_cmd_srcs # Se adauga modulul hello
    "modules/hello_cmd/hello_cmd.c")
set(hello_cmd_includes
    "modules/hello_cmd")
# ==================================== #
set(restart_srcs # Se adauga restart
    "modules/restart_cmd/restart_cmd.c")
set(restart_includes "modules/restart_cmd")
# ==================================== #
set(tasks_srcs # Se adauga task info
    "modules/tasks_cmd/tasks_cmd.c")
set(tasks_includes
    "modules/tasks_cmd")
# ==================================== #
set(uptime_srcs # Se adauga uptime
    "modules/uptime_cmd/uptime_cmd.c")
set(uptime_includes
    "modules/uptime_cmd")
# ==================================== #
set(timers_cmd_srcs # Se adauga modulul hello
    "modules/timers_module_cmd/timers_module_cmd.c")
set(timers_cmd_includes
    "modules/timers_module_cmd")
# ==================================== #
set(sys_cmd_srcs
    "modules/sys_cmd/sys_cmd.c")
set(sys_cmd_includes
    "modules/sys_cmd")
# ==================================== #
set(log_cmd_srcs
    "modules/log_cmd/log_cmd.c")
set(log_cmd_includes
    "modules/log_cmd")
# ==================================== #
set(mem_cmd_srcs
    "modules/mem_cmd/mem_cmd.c")
set(mem_cmd_includes
    "modules/mem_cmd")
# ==================================== #
set(part_cmd_srcs
    "modules/part_cmd/part_cmd.c")
set(part_cmd_includes
    "modules/part_cmd")
# ==================================== #
set(nvs_cmd_srcs
    "modules/nvs_cmd/nvs_cmd.c")
set(nvs_cmd_includes
    "modules/nvs_cmd")
# ==================================== #
set(gpio_cmd_srcs
    "modules/gpio_cmd/gpio_cmd.c")
set(gpio_cmd_includes
    "modules/gpio_cmd")
# ==================================== #
set(wifi_cmd_srcs
    "modules/wifi_cmd/wifi_cmd.c")
set(wifi_cmd_includes
    "modules/wifi_cmd")
# ==================================== #
set(net_cmd_srcs
    "modules/net_cmd/net_cmd.c")
set(net_cmd_includes
    "modules/net_cmd")
# ==================================== #
set(adc_cmd_srcs
    "modules/adc_cmd/adc_cmd.c")
set(adc_cmd_includes
    "modules/adc_cmd")
# ==================================== #
# ==================================== #
# ==================================== #
# ==================================== #
# ==================================== #

# ------------------------------ #

# Aduna toate modulele intr-un singur #
set(modules_srcs
    ${hello_cmd_srcs}
    ${restart_srcs}
    ${tasks_srcs}
    ${uptime_srcs}
    ${timers_cmd_srcs}
    ${sys_cmd_srcs}
    ${log_cmd_srcs}
    ${mem_cmd_srcs}
    ${part_cmd_srcs}
    ${nvs_cmd_srcs}
    ${gpio_cmd_srcs}
    ${wifi_cmd_srcs}
    ${net_cmd_srcs}
    ${adc_cmd_srcs}

)
## ------------------
# Aduna toate modulele intr-un singur #
set(modules_includes
    ${hello_cmd_includes}
    ${restart_includes}
    ${tasks_includes}
    ${uptime_includes}
    ${timers_cmd_includes}
    ${sys_cmd_includes}
    ${log_cmd_includes}
    ${mem_cmd_includes}
    ${part_cmd_includes}
    ${nvs_cmd_includes}
    ${gpio_cmd_includes}
    ${wifi_cmd_includes}
    ${net_cmd_includes}
    ${adc_cmd_includes}
)
## ------------------
set(modules_priv_includes
    ${modules_includes}
)
## ------------------

# ------------------------------- #

idf_component_register(
    SRCS
    "esp32-cmd-cli.c"
    ${modules_srcs}
    ## ------------------
    INCLUDE_DIRS
    "modules"
    "."
    ${modules_includes}
    ## ------------------
    PRIV_INCLUDE_DIRS
    ${modules_priv_includes}
    ## ------------------
    REQUIRES
    ## ------------------
    PRIV_REQUIRES
    perfmon
    esp_timer
    driver
    freertos
    console
    esp_hw_support
    spi_flash
    esp_system
    heap
    esp_common
    esp_psram
    esp_hw_support
    nvs_flash
    esp_wifi
    esp_netif
    esp_event
    lwip
    esp_adc
    board_pins
    app_update
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    perfmon
    esp_common
    driver
    console
    log
)



# ------------------------------- #

set( # Asta adaug-o separat:
    KCONFIG_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig"
)

# ------------------------------- #

# dezactivează tratarea warningurilor ca erori pentru componenta asta
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-error=type-limits -Wno-unused-variable -Wno-unused-function -Wno-error=cpp)
set_source_files_properties(
    esp32-cmd-cli.c
    PROPERTIES COMPILE_FLAGS "-Wno-error=cpp -Wno-error"
)

# ==================== #
# mylibs/one-cli-v005/CMakeLists.txt
