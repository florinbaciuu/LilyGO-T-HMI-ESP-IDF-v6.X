#include <stdio.h>
#include "esp_timer.h"
#include "esp_console.h"
#include "esp_log.h"
#include "timers_module_cmd.h"

static const char *TAG = "ESP32_CLI";

static int timers_command_callback(int argc, char **argv)
{
    printf("\n=========================================== TIMERS ===================================================\n");
    //        Name                  Period      Alarm         Times_armed   Times_trigg   Times_skip    Cb_exec_time
    esp_timer_dump(stdout);
    printf("======================================================================================================\n\n");
    return 0;
}

static void register_timers_command_function(void)
{
    const esp_console_cmd_t cmd = {
        .command = "timers",
        .help = "Print timers running on the sistem",
        .hint = "debug esp_timer subsystem",
        .func = &timers_command_callback,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}

void cli_register_timers_command(void)
{
    register_timers_command_function();
}
