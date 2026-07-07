
/***
 * Code c
 */

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "esp32-cmd-cli.h"
#include "banner.h"
#include "modules.h"

//---------------

static const char *TAG = "CLI";

//---------------

char prompt[CONSOLE_PROMPT_MAX_LEN];

// ------------------------------------

void initialize_console_peripheral(void)
{
    fflush(stdout); /* Drain stdout before reconfiguring it */
    fsync(fileno(stdout));
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR); /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF); /* Move the caret to the beginning of the next line on '\n' */
    fcntl(fileno(stdout), F_SETFL, 0); /* Enable blocking mode on stdin and stdout */
    fcntl(fileno(stdin), F_SETFL, 0); /* Enable blocking mode on stdin and stdout */

    usb_serial_jtag_driver_config_t jtag_config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&jtag_config)); /* Install USB-SERIAL-JTAG driver for interrupt-driven reads and writes */
    usb_serial_jtag_vfs_use_driver(); /* Tell vfs to use usb-serial-jtag driver */

    setvbuf(stdin, NULL, _IONBF, 0); /* Disable buffering on stdin */
}

// ------------------------------------

void initialize_console_library(const char *history_path) {
  esp_console_config_t console_config = {
    .max_cmdline_length = CONSOLE_MAX_CMDLINE_LENGTH,
    .max_cmdline_args = CONSOLE_MAX_CMDLINE_ARGS,
    .heap_alloc_caps = MALLOC_CAP_SPIRAM,
#if CONFIG_LOG_COLORS
    .hint_color = atoi(LOG_COLOR_CYAN),
#endif
    .hint_bold = 1,
  };
  ESP_ERROR_CHECK(esp_console_init(&console_config)); // Initialize the console
  ESP_LOGI(TAG, "Console initialized");

  linenoiseSetMultiLine(1); // Enable multiline editing. If not set, long commands will scroll within single line .
  ESP_LOGI(TAG, "Linenoise Multi-line editing initialized");

  linenoiseSetCompletionCallback(&esp_console_get_completion); // Tell linenoise where to get command completions
  ESP_LOGI(TAG, "Linenoise completion callback initialized");

  linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint); // Tell linenoise where to get command hints
  ESP_LOGI(TAG, "Linenoise hints callback initialized");

  
  int linenoiseHistoryMaxLen = LINENOISE_MAX_LINE_LEN;
  linenoiseHistorySetMaxLen(linenoiseHistoryMaxLen); // Set command history size
  ESP_LOGI(TAG, "Command history size set to %d", linenoiseHistoryMaxLen);

  linenoiseSetMaxLineLen(console_config.max_cmdline_length); // Set command maximum length
  ESP_LOGI(TAG, "Command maximum length set to %d", console_config.max_cmdline_length);

  linenoiseAllowEmpty(false); // Don't return empty lines

#if CONFIG_CONSOLE_STORE_HISTORY
  linenoiseHistoryLoad(history_path); // Load command history from filesystem
#endif  // CONFIG_CONSOLE_STORE_HISTORY

  const int probe_status = linenoiseProbe(); // Figure out if the terminal supports escape sequences
  if (probe_status) { // zero indicates success
    linenoiseSetDumbMode(1);
  }
  ESP_LOGI(TAG, "Console library initialized with history path: %s", history_path);
}

// ------------------------------------

char *setup_prompt(const char *prompt_str) {
  const char *prompt_temp = "esp>"; // set command line prompt
  if (prompt_str) {
    prompt_temp = prompt_str;
  }
  snprintf(prompt, CONSOLE_PROMPT_MAX_LEN - 1, LOG_COLOR_I "%s " LOG_RESET_COLOR, prompt_temp);
  if (linenoiseIsDumbMode()) {
#if CONFIG_LOG_COLORS
    snprintf(prompt, CONSOLE_PROMPT_MAX_LEN - 1, "%s ", prompt_temp); /* Since the terminal doesn't support escape sequences, don't use color codes in the s_prompt.*/
#endif  //CONFIG_LOG_COLORS
  }
  return prompt;
}

// ------------------------------------
// ------------------------------------
// ------------------------------------

// -------------------------------------------------

void cli_register_all_commands(void) {
    cli_register_hello_command();
    cli_register_restart_command();
    cli_register_tasks_command();
    cli_register_uptime_command();
    cli_register_timers_command();
    cli_register_sys_command();
    cli_register_log_command();
    cli_register_mem_command();
    cli_register_part_command();
    cli_register_nvs_command();
    cli_register_gpio_command();
    cli_register_wifi_command();
    cli_register_net_command();
    cli_register_adc_command();
    ESP_LOGI(TAG, "All CLI modules commands registered.");
    return;
}

// -------------------------------------------------

TaskHandle_t xHandle__CLI;

void printStartupMessage() {
    // printf("\n%s\n", florios_banner);  // Afișează blazonul
    printf("\033[1;34m%s\033[0m\n", freertos_banner);
    printf("\033[1;34m%s\033[0m\n", esp32_cli_banner);  // albastru intens
    printf("FREERTOS Operating System by Espressif.\n");
    printf("ESP32 CLI.\n");

    printf(
        "\n"
        "╔═════════════════════════════════════════════════════════════════╗\n"
        "║                    🔧 ESP32  Console  Online 🔧                 ║\n"
        "╠═════════════════════════════════════════════════════════════════╣\n"
        "║  🕹️  Type 'help'           →  List all available commands        ║\n"
        "║  🔁  Use ↑ / ↓            →  Navigate command history           ║\n"
        "║  ⚡  Press [TAB]          →  Auto-complete command names        ║\n"
        "║  💣  Ctrl + C             →  Exit the console (if you dare)     ║\n"
        "╚═════════════════════════════════════════════════════════════════╝\n"
        "\n");

    printf("CLI Module Made by Florin Baciu.\n");
    printf("In East Europe. 2026\n");
    if (linenoiseIsDumbMode()) {
        printf(
            "⚠️  Terminal dumb mode detected!\n"
            "Line editing and history are disabled.\n"
            "💡 Try using a better terminal (like PuTTY or Minicom).\n\n");
    } else {
        printf("🧠 Terminal capabilities: FULLY ENABLED\n\n");
    }
    printf("🚀 Welcome, Commander. System is ready for input.\n");
    printf("💭 Remember: even the most powerful systems wait for a single command...\n");
    return;
}

// -------------------------------

static char s_history_path[64] = MOUNT_PATH "/history.txt"; // Variabilă globală pentru path-ul istoriei

/********************************************** */
/*                   TASK                       */
/********************************************** */
static const char cli_task_name[] = "CLI Task";
static const uint32_t CLI_TASK_STACK_SIZE = 8192;
static const BaseType_t cli_task_priority = tskIDLE_PRIORITY + 3;
static TaskHandle_t s_cli_task_handle;
static const BaseType_t cli_task_core = 0; // Run on core 0

void console_app_func(void* parameter) {
    (void) parameter;
    // vTaskDelay(300);
    initialize_console_peripheral(); // Initialize console output periheral (UART, USB_OTG, USB_JTAG)
    initialize_console_library(s_history_path); // Initialize linenoise library and esp_console
    const char* prompt = setup_prompt(PROMPT_STR ">"); // Prompt to be printed before each line. This can be customized, made dynamic, etc.
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_console_register_help_command(); // IDF help command
    cli_register_all_commands();  // Register my commands
    // vTaskDelay(300);
    printStartupMessage();
    while (true) {
        char* line = linenoise(prompt);
#if CONFIG_CONSOLE_IGNORE_EMPTY_LINES
        if (line == NULL) { // Ignore empty lines
            continue;
        }
#else
        if (line == NULL) { // Break on EOF or error
            break;
        }
#endif  // CONFIG_CONSOLE_IGNORE_EMPTY_LINES
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line); // Add the command to the history if not empty
#if CONFIG_CONSOLE_STORE_HISTORY
            if (s_history_path[0] != '\0') {  // avem path valid
                linenoiseHistorySave(s_history_path); // Save command history to filesystem
            }
#endif  // CONFIG_CONSOLE_STORE_HISTORY
        }
        int       ret;
        esp_err_t err = esp_console_run(line, &ret); // Try to run the command
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        linenoiseFree(line); // linenoise allocates line buffer on the heap, so need to free it
    }
    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}

// -------------------------------

void start_cli_task()
{
    if (s_cli_task_handle == NULL) {
        xTaskCreatePinnedToCore(console_app_func,         // Functia care ruleaza task-ul
            (const char*) cli_task_name,                // Numele task-ului
            (uint32_t) (CLI_TASK_STACK_SIZE),                       // Dimensiunea stack-ului
            (NULL),                                  // Parametri
            (UBaseType_t) cli_task_priority,  // Prioritatea task-ului
            &s_cli_task_handle,  // Handle-ul task-ului
            ((cli_task_core))           // Nucleul pe care ruleaza (ESP32 e dual-core)
        );
    }
}
