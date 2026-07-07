#include "gpio_cmd.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "driver/gpio.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

static const char *TAG = "ESP32_CLI";

#define GPIO_MAX_OPERANDS 4
#define GPIO_MAX_PULSE_MS 10000
#define GPIO_MAX_BLINK_COUNT 1000
#define GPIO_MAX_BLINK_PERIOD_MS 10000

typedef int (*gpio_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    gpio_func_t function;
    const char *description;
} gpio_command_entry_t;

typedef struct {
    int pin;
    const char *reason;
} protected_gpio_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *operands;
    struct arg_lit *force;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} gpio_args;

static const protected_gpio_t s_protected_pins[] = {
    {0, "boot button / strapping"},
    {1, "touch SPI SCLK"},
    {2, "touch SPI CS"},
    {3, "touch SPI MOSI"},
    {4, "touch SPI MISO"},
    {5, "battery ADC"},
    {6, "LCD CS"},
    {7, "LCD DC"},
    {8, "LCD WR"},
    {9, "touch IRQ"},
    {10, "power enable"},
    {11, "filesystem SDMMC CMD / alternate SPI MOSI"},
    {12, "filesystem SDMMC CLK / alternate SPI CLK"},
    {13, "filesystem SDMMC D0 / alternate SPI MISO"},
    {14, "power-on hold"},
    {15, "alternate SPI CS"},
    {21, "reed switch / power input"},
    {38, "LCD backlight"},
    {39, "LCD data 2"},
    {40, "LCD data 3"},
    {41, "LCD data 4"},
    {42, "LCD data 5"},
    {45, "LCD data 6 / strapping"},
    {46, "LCD data 7 / strapping"},
    {47, "LCD data 1"},
    {48, "LCD data 0"},
};

static bool parse_int_arg(const char *text, int *out_value)
{
    char *end = NULL;
    long parsed = strtol(text, &end, 0);
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }

    *out_value = (int) parsed;
    return true;
}

static bool parse_pin_arg(size_t index, gpio_num_t *out_pin)
{
    if (gpio_args.operands->count <= index) {
        printf("Missing GPIO pin argument.\n");
        return false;
    }

    int pin = -1;
    if (!parse_int_arg(gpio_args.operands->sval[index], &pin)) {
        printf("Invalid GPIO pin: %s\n", gpio_args.operands->sval[index]);
        return false;
    }

    *out_pin = (gpio_num_t) pin;
    return true;
}

static const char *protected_pin_reason(gpio_num_t pin)
{
    for (size_t i = 0; i < sizeof(s_protected_pins) / sizeof(s_protected_pins[0]); i++) {
        if (s_protected_pins[i].pin == (int) pin) {
            return s_protected_pins[i].reason;
        }
    }

    return NULL;
}

static esp_err_t validate_pin(gpio_num_t pin, bool needs_output)
{
    if (!GPIO_IS_VALID_GPIO(pin)) {
        printf("Invalid GPIO: %d. Valid range for this target is 0..%d, with SoC-specific gaps.\n",
            (int) pin,
            SOC_GPIO_PIN_COUNT - 1);
        return ESP_ERR_INVALID_ARG;
    }

    if (needs_output && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        printf("GPIO %d is not output-capable on this target.\n", (int) pin);
        return ESP_ERR_INVALID_ARG;
    }

    const char *reason = protected_pin_reason(pin);
    if (reason != NULL && gpio_args.force->count == 0) {
        printf("GPIO %d is protected: %s. Add --force if you really want this.\n", (int) pin, reason);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static bool parse_level_arg(size_t index, int *out_level)
{
    if (gpio_args.operands->count <= index) {
        printf("Missing level argument. Use 0 or 1.\n");
        return false;
    }

    int level = -1;
    if (!parse_int_arg(gpio_args.operands->sval[index], &level) || (level != 0 && level != 1)) {
        printf("Invalid level: %s. Use 0 or 1.\n", gpio_args.operands->sval[index]);
        return false;
    }

    *out_level = level;
    return true;
}

static bool parse_mode(const char *text, gpio_mode_t *mode)
{
    if (strcasecmp(text, "disable") == 0 || strcasecmp(text, "off") == 0) {
        *mode = GPIO_MODE_DISABLE;
    } else if (strcasecmp(text, "input") == 0 || strcasecmp(text, "in") == 0) {
        *mode = GPIO_MODE_INPUT;
    } else if (strcasecmp(text, "output") == 0 || strcasecmp(text, "out") == 0) {
        *mode = GPIO_MODE_OUTPUT;
    } else if (strcasecmp(text, "output_od") == 0 || strcasecmp(text, "od") == 0 || strcasecmp(text, "open_drain") == 0) {
        *mode = GPIO_MODE_OUTPUT_OD;
    } else if (strcasecmp(text, "input_output") == 0 || strcasecmp(text, "inout") == 0) {
        *mode = GPIO_MODE_INPUT_OUTPUT;
    } else if (strcasecmp(text, "input_output_od") == 0 || strcasecmp(text, "inout_od") == 0) {
        *mode = GPIO_MODE_INPUT_OUTPUT_OD;
    } else {
        return false;
    }

    return true;
}

static const char *mode_to_str(gpio_io_config_t *config)
{
    if (config->ie && config->oe && config->od) {
        return "input_output_od";
    }
    if (config->ie && config->oe) {
        return "input_output";
    }
    if (config->oe && config->od) {
        return "output_od";
    }
    if (config->oe) {
        return "output";
    }
    if (config->ie) {
        return "input";
    }
    return "disable";
}

static bool parse_pull(const char *text, gpio_pull_mode_t *pull)
{
    if (strcasecmp(text, "none") == 0 || strcasecmp(text, "floating") == 0 || strcasecmp(text, "float") == 0) {
        *pull = GPIO_FLOATING;
    } else if (strcasecmp(text, "up") == 0 || strcasecmp(text, "pullup") == 0) {
        *pull = GPIO_PULLUP_ONLY;
    } else if (strcasecmp(text, "down") == 0 || strcasecmp(text, "pulldown") == 0) {
        *pull = GPIO_PULLDOWN_ONLY;
    } else if (strcasecmp(text, "updown") == 0 || strcasecmp(text, "both") == 0) {
        *pull = GPIO_PULLUP_PULLDOWN;
    } else {
        return false;
    }

    return true;
}

static bool parse_drive(const char *text, gpio_drive_cap_t *drive)
{
    int value = 0;
    if (!parse_int_arg(text, &value) || value < GPIO_DRIVE_CAP_0 || value >= GPIO_DRIVE_CAP_MAX) {
        return false;
    }

    *drive = (gpio_drive_cap_t) value;
    return true;
}

static int gpio_read_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, false);
    if (err != ESP_OK) {
        return err;
    }

    printf("GPIO %d level: %d\n", (int) pin, gpio_get_level(pin));
    return ESP_OK;
}

static int gpio_write_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    int level;
    if (!parse_pin_arg(0, &pin) || !parse_level_arg(1, &level)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(pin, (uint32_t) level);
    if (err != ESP_OK) {
        printf("Failed to write GPIO %d: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d set to %d\n", (int) pin, level);
    return ESP_OK;
}

static int gpio_toggle_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    int next_level = gpio_get_level(pin) ? 0 : 1;
    err = gpio_set_level(pin, (uint32_t) next_level);
    if (err != ESP_OK) {
        printf("Failed to toggle GPIO %d: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d toggled to %d\n", (int) pin, next_level);
    return ESP_OK;
}

static int gpio_mode_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin) || gpio_args.operands->count < 2) {
        printf("Usage: gpio mode <pin> <input|output|input_output|output_od|input_output_od|disable> [--force]\n");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_mode_t mode;
    if (!parse_mode(gpio_args.operands->sval[1], &mode)) {
        printf("Unknown GPIO mode: %s\n", gpio_args.operands->sval[1]);
        return ESP_ERR_INVALID_ARG;
    }

    bool needs_output = (mode & GPIO_MODE_DEF_OUTPUT) != 0;
    esp_err_t err = validate_pin(pin, needs_output);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_direction(pin, mode);
    if (err != ESP_OK) {
        printf("Failed to set GPIO %d mode: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d mode set to %s\n", (int) pin, gpio_args.operands->sval[1]);
    return ESP_OK;
}

static int gpio_pull_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin) || gpio_args.operands->count < 2) {
        printf("Usage: gpio pull <pin> <none|up|down|both> [--force]\n");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_pull_mode_t pull;
    if (!parse_pull(gpio_args.operands->sval[1], &pull)) {
        printf("Unknown pull mode: %s\n", gpio_args.operands->sval[1]);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = validate_pin(pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_pull_mode(pin, pull);
    if (err != ESP_OK) {
        printf("Failed to set GPIO %d pull mode: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d pull mode set to %s\n", (int) pin, gpio_args.operands->sval[1]);
    return ESP_OK;
}

static int gpio_pulse_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    int ms = 0;
    if (!parse_pin_arg(0, &pin) || gpio_args.operands->count < 2 || !parse_int_arg(gpio_args.operands->sval[1], &ms)) {
        printf("Usage: gpio pulse <pin> <ms> [--force]\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (ms <= 0 || ms > GPIO_MAX_PULSE_MS) {
        printf("Pulse duration must be 1..%u ms.\n", GPIO_MAX_PULSE_MS);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    int original_level = gpio_get_level(pin);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, (uint32_t) original_level);
    printf("GPIO %d pulsed high for %d ms, restored to %d\n", (int) pin, ms, original_level);
    return ESP_OK;
}

static int gpio_blink_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    int count = 0;
    int period_ms = 0;
    if (!parse_pin_arg(0, &pin) ||
        gpio_args.operands->count < 3 ||
        !parse_int_arg(gpio_args.operands->sval[1], &count) ||
        !parse_int_arg(gpio_args.operands->sval[2], &period_ms)) {
        printf("Usage: gpio blink <pin> <count> <period_ms> [--force]\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (count <= 0 || count > GPIO_MAX_BLINK_COUNT) {
        printf("Blink count must be 1..%u.\n", GPIO_MAX_BLINK_COUNT);
        return ESP_ERR_INVALID_ARG;
    }
    if (period_ms < 2 || period_ms > GPIO_MAX_BLINK_PERIOD_MS) {
        printf("Blink period must be 2..%u ms.\n", GPIO_MAX_BLINK_PERIOD_MS);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    int original_level = gpio_get_level(pin);
    TickType_t half_period = pdMS_TO_TICKS((uint32_t) period_ms / 2);
    if (half_period == 0) {
        half_period = 1;
    }

    for (int i = 0; i < count; i++) {
        gpio_set_level(pin, 1);
        vTaskDelay(half_period);
        gpio_set_level(pin, 0);
        vTaskDelay(half_period);
    }

    gpio_set_level(pin, (uint32_t) original_level);
    printf("GPIO %d blinked %d times at %d ms period, restored to %d\n", (int) pin, count, period_ms, original_level);
    return ESP_OK;
}

static int gpio_drive_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin) || gpio_args.operands->count < 2) {
        printf("Usage: gpio drive <pin> <0|1|2|3> [--force]\n");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_drive_cap_t drive;
    if (!parse_drive(gpio_args.operands->sval[1], &drive)) {
        printf("Drive capability must be 0..3.\n");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_drive_capability(pin, drive);
    if (err != ESP_OK) {
        printf("Failed to set GPIO %d drive: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d drive capability set to %d\n", (int) pin, (int) drive);
    return ESP_OK;
}

static int gpio_hold_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_hold_en(pin);
    if (err != ESP_OK) {
        printf("Failed to hold GPIO %d: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d hold enabled\n", (int) pin);
    return ESP_OK;
}

static int gpio_unhold_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, true);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_hold_dis(pin);
    if (err != ESP_OK) {
        printf("Failed to unhold GPIO %d: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d hold disabled\n", (int) pin);
    return ESP_OK;
}

static int gpio_reset_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, false);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_reset_pin(pin);
    if (err != ESP_OK) {
        printf("Failed to reset GPIO %d: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    printf("GPIO %d reset to default GPIO state\n", (int) pin);
    return ESP_OK;
}

static int gpio_info_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    gpio_num_t pin;
    if (!parse_pin_arg(0, &pin)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_pin(pin, false);
    if (err != ESP_OK && gpio_args.force->count == 0) {
        return err;
    }

    gpio_io_config_t config = {0};
    err = gpio_get_io_config(pin, &config);
    if (err != ESP_OK) {
        printf("Failed to read GPIO %d config: %s\n", (int) pin, esp_err_to_name(err));
        return err;
    }

    gpio_drive_cap_t drive = GPIO_DRIVE_CAP_DEFAULT;
    gpio_get_drive_capability(pin, &drive);

    printf("\nGPIO %d\n", (int) pin);
    printf("-------\n");
    printf("%-14s %d\n", "Level:", gpio_get_level(pin));
    printf("%-14s %s\n", "Mode:", mode_to_str(&config));
    printf("%-14s %s\n", "Pull-up:", config.pu ? "enabled" : "disabled");
    printf("%-14s %s\n", "Pull-down:", config.pd ? "enabled" : "disabled");
    printf("%-14s %s\n", "Open-drain:", config.od ? "enabled" : "disabled");
    printf("%-14s %d\n", "Drive:", (int) drive);
    printf("%-14s %u\n", "Func sel:", (unsigned) config.fun_sel);
    printf("%-14s %u\n", "Sig out:", (unsigned) config.sig_out);
    printf("%-14s %s\n", "Protected:", protected_pin_reason(pin) ? protected_pin_reason(pin) : "no");
    printf("\n");
    return ESP_OK;
}

static int gpio_dump_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (gpio_args.operands->count > 0) {
        gpio_num_t pin;
        if (!parse_pin_arg(0, &pin)) {
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t err = validate_pin(pin, false);
        if (err != ESP_OK && gpio_args.force->count == 0) {
            return err;
        }
        return gpio_dump_io_configuration(stdout, 1ULL << pin);
    }

    return gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
}

static int gpio_protected_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("Protected GPIO pins:\n");
    for (size_t i = 0; i < sizeof(s_protected_pins) / sizeof(s_protected_pins[0]); i++) {
        printf("  GPIO %-2d %s\n", s_protected_pins[i].pin, s_protected_pins[i].reason);
    }
    printf("Use --force to bypass protection for a command.\n");
    return ESP_OK;
}

static void print_gpio_help(void);

static const gpio_command_entry_t gpio_cmds[] = {
    {"read", gpio_read_command, "Read GPIO level"},
    {"write", gpio_write_command, "Write GPIO level"},
    {"toggle", gpio_toggle_command, "Toggle GPIO level"},
    {"mode", gpio_mode_command, "Set GPIO direction/mode"},
    {"pull", gpio_pull_command, "Set pull mode"},
    {"pulse", gpio_pulse_command, "Pulse GPIO high for N ms"},
    {"blink", gpio_blink_command, "Blink GPIO count times"},
    {"drive", gpio_drive_command, "Set drive capability 0..3"},
    {"hold", gpio_hold_command, "Enable GPIO hold"},
    {"unhold", gpio_unhold_command, "Disable GPIO hold"},
    {"reset", gpio_reset_command, "Reset GPIO to default state"},
    {"info", gpio_info_command, "Show GPIO configuration"},
    {"dump", gpio_dump_command, "Dump GPIO configuration"},
    {"protected", gpio_protected_command, "List protected board pins"},
};

#define GPIO_CMD_COUNT (sizeof(gpio_cmds) / sizeof(gpio_cmds[0]))

static char gpio_cmds_help[192] = {0};

static void generate_gpio_cmds_help_text(void)
{
    strlcpy(gpio_cmds_help, ": ", sizeof(gpio_cmds_help));
    for (size_t i = 0; i < GPIO_CMD_COUNT; i++) {
        strlcat(gpio_cmds_help, gpio_cmds[i].name, sizeof(gpio_cmds_help));
        if (i < GPIO_CMD_COUNT - 1) {
            strlcat(gpio_cmds_help, "; ", sizeof(gpio_cmds_help));
        }
    }
}

static void print_gpio_commands(void)
{
    printf("Available gpio commands:\n");
    for (size_t i = 0; i < GPIO_CMD_COUNT; i++) {
        printf("  %-10s %s\n", gpio_cmds[i].name, gpio_cmds[i].description);
    }
}

static void print_gpio_help(void)
{
    printf("\nUsage:\n");
    printf("  gpio read <pin>\n");
    printf("  gpio write <pin> <0|1> [--force]\n");
    printf("  gpio toggle <pin> [--force]\n");
    printf("  gpio mode <pin> <input|output|input_output|output_od|input_output_od|disable> [--force]\n");
    printf("  gpio pull <pin> <none|up|down|both> [--force]\n");
    printf("  gpio pulse <pin> <ms> [--force]\n");
    printf("  gpio blink <pin> <count> <period_ms> [--force]\n");
    printf("  gpio drive <pin> <0|1|2|3> [--force]\n");
    printf("  gpio hold <pin> [--force]\n");
    printf("  gpio unhold <pin> [--force]\n");
    printf("  gpio reset <pin> [--force]\n");
    printf("  gpio info <pin> [--force]\n");
    printf("  gpio dump [pin] [--force]\n");
    printf("  gpio protected\n\n");
    print_gpio_commands();
    printf("\nExamples:\n");
    printf("  gpio mode 16 output\n");
    printf("  gpio write 16 1\n");
    printf("  gpio blink 16 5 200\n");
    printf("  gpio info 16\n");
    printf("  gpio protected\n\n");
}

static int gpio_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &gpio_args);

    if (argc == 1 || gpio_args.help->count > 0) {
        print_gpio_help();
        return ESP_OK;
    }

    if (gpio_args.list->count > 0) {
        print_gpio_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, gpio_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (gpio_args.subcommand->count == 0 || gpio_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `gpio --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = gpio_args.subcommand->sval[0];
    for (size_t i = 0; i < GPIO_CMD_COUNT; i++) {
        if (strcmp(subcommand, gpio_cmds[i].name) == 0) {
            return gpio_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown gpio subcommand: %s\n", subcommand);
    print_gpio_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_gpio_command(void)
{
    generate_gpio_cmds_help_text();

    gpio_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", gpio_cmds_help);
    gpio_args.operands = arg_strn(NULL, NULL, "<arg>", 0, GPIO_MAX_OPERANDS, "Command arguments");
    gpio_args.force = arg_lit0(NULL, "force", "Allow protected board pins");
    gpio_args.list = arg_lit0("l", "list", "List available gpio subcommands");
    gpio_args.help = arg_lit0("h", "help", "Show gpio command help");
    gpio_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "gpio",
        .help = "GPIO inspect and control",
        .hint = NULL,
        .func = &gpio_command,
        .argtable = &gpio_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
