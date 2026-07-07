#pragma once

#define CONSOLE_MAX_CMDLINE_ARGS (8)
#define CONSOLE_MAX_CMDLINE_LENGTH (256)
#define CONSOLE_PROMPT_MAX_LEN (32)

#define CONFIG_CONSOLE_STORE_HISTORY (1)
#define CONFIG_CONSOLE_IGNORE_EMPTY_LINES (1)
#define PROMPT_STR CONFIG_IDF_TARGET

#define MY_ESP_CONSOLE_CONFIG_DEFAULT() \
    {.max_cmdline_length = 256, .max_cmdline_args = 32, .heap_alloc_caps = MALLOC_CAP_DEFAULT, .hint_color = 39, .hint_bold = 0}

/**
 * SETTINGS
 * comment all to chose internal MMC as filesystem
 */
#define SDCARD_USE
//#define LITTLEFS_USE
//#define SPIFFS_USE
//#define FFAT_USE

/**********
 * END SETTINGS
 */

#ifdef SDCARD_USE

    #define MOUNT_PATH "/sdcard"
    #define HISTORY_PATH MOUNT_PATH "/history.txt"

#elif defined(LITTLEFS_USE)

    #define BASE_PATH "/littlefs"
    #define PARTITION_LABEL "littlefs"
    #define MOUNT_PATH "/littlefs"
    #define HISTORY_PATH MOUNT_PATH "/history.txt"

#elif defined(SPIFFS_USE)

    #define BASE_PATH "/spiffs"
    #define PARTITION_LABEL "spiffs"
    #define MOUNT_PATH "/spiffs"
    #define HISTORY_PATH MOUNT_PATH "/history.txt"

#elif defined(FFAT_USE)  // fallback -> FFAT

    #define BASE_PATH "/fatfs"
    #define PARTITION_LABEL "fatfs"
    #define MOUNT_PATH "/fatfs"
    #define HISTORY_PATH MOUNT_PATH "/history.txt"

#endif

extern char prompt[CONSOLE_PROMPT_MAX_LEN];

//extern int linenoiseHistoryMaxLen;

#define LINENOISE_MAX_LINE_LEN (100)