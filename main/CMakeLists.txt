

cmake_minimum_required(VERSION 3.5)

# Set usual component variables
set(app_sources
    "main.cpp"
)

set(app_include_dirs
    "."
    ""
)

set(app_requires
    freertos
    filesystem
    littlefs
    lvgl
    esp32-cmd-cli
)
set(app_priv_requires
    ${app_requires}
    bsp_touch_interface
    bsp_i80_parallel_interface
    bsp_lcd_interface
    board_config
    board_pins
    esp_lcd
    lvgl_framework_config
    bsp_lvgl_framework
)

# Set my components
# These are components that are not part of the IDF 
# but are used in this project.
# Put this components in /lib after set 
# set(EXTRA_COMPONENT_DIRS lib) in root/CMakeLists.txt
set(my_components
   # filesystem
    #simple-cli-thmi-v001
)


idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${app_include_dirs}
    REQUIRES ${app_requires} ${my_components}
    PRIV_REQUIRES ${app_priv_requires}
    WHOLE_ARCHIVE
)

add_compile_options(-Wno-error)

idf_build_get_property(components BUILD_COMPONENTS)
message(STATUS "@@@ ALL COMPONENTS seen from MAIN  = ${components}")

# target_compile_options(${COMPONENT_LIB} PUBLIC
#     -Wno-error
#     -Wno-error=cpp
#     -Wno-error=type-limits
#     -Wno-unused-variable
#     -Wno-unused-function
#     -Wno-error=deprecated-declarations
# )

# set_source_files_properties(
#     main.cpp rtos.cpp temp_sensor_cpu.cpp
#     PROPERTIES COMPILE_FLAGS "-Wno-error=cpp -Wno-error "
# )


# Note: you must have a partition named the first argument (here it's "littlefs")
# in your partition table csv file.
# littlefs_create_partition_image(littlefs littlefs_data FLASH_IN_PROJECT)
# spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)
