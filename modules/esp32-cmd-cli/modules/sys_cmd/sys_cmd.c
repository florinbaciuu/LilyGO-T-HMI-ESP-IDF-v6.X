#include "sys_cmd.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "spi_flash_mmap.h"

static const char *TAG = "ESP32_CLI";

extern esp_flash_t *esp_flash_default_chip;

typedef int (*sys_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    sys_func_t function;
    const char *description;
} sys_command_entry_t;

static struct {
    struct arg_str *subcommand;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} sys_args;

static const char *reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_EXT:
        return "external pin";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "unrecognized";
    }
}

static void print_bytes(const char *label, size_t bytes)
{
    printf("%-22s %10u B  %7.2f KiB  %6.2f MiB\n",
           label,
           (unsigned) bytes,
           (double) bytes / 1024.0,
           (double) bytes / (1024.0 * 1024.0));
}

static int sys_info_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    const char *model = "Unknown";
    switch (chip_info.model) {
    case CHIP_ESP32:
        model = "ESP32";
        break;
    case CHIP_ESP32S2:
        model = "ESP32-S2";
        break;
    case CHIP_ESP32S3:
        model = "ESP32-S3";
        break;
    case CHIP_ESP32C3:
        model = "ESP32-C3";
        break;
    case CHIP_ESP32C2:
        model = "ESP32-C2";
        break;
    case CHIP_ESP32C6:
        model = "ESP32-C6";
        break;
    case CHIP_ESP32H2:
        model = "ESP32-H2";
        break;
    case CHIP_ESP32P4:
        model = "ESP32-P4";
        break;
    default:
        break;
    }

    int64_t uptime_s = esp_timer_get_time() / 1000000;

    printf("\nSystem info\n");
    printf("-----------\n");
    printf("%-18s %s\n", "Target:", CONFIG_IDF_TARGET);
    printf("%-18s %s\n", "Chip:", model);
    printf("%-18s %d\n", "Cores:", chip_info.cores);
    printf("%-18s %d\n", "Revision:", chip_info.revision);
    printf("%-18s %s\n", "IDF:", esp_get_idf_version());
    printf("%-18s %" PRId64 " s\n", "Uptime:", uptime_s);
    printf("%-18s %s\n", "Reset reason:", reset_reason_to_str(esp_reset_reason()));
    printf("%-18s %s\n", "PSRAM:", esp_psram_is_initialized() ? "initialized" : "not initialized");

    printf("%-18s", "Features:");
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) {
        printf(" WiFi");
    }
    if (chip_info.features & CHIP_FEATURE_BT) {
        printf(" BT");
    }
    if (chip_info.features & CHIP_FEATURE_BLE) {
        printf(" BLE");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) {
        printf(" embedded-flash");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        printf(" embedded-psram");
    }
    printf("\n\n");

    return ESP_OK;
}

static int sys_heap_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nHeap summary\n");
    printf("------------\n");
    print_bytes("Internal free:", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    print_bytes("Internal min free:", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    print_bytes("Internal largest:", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    print_bytes("DMA free:", heap_caps_get_free_size(MALLOC_CAP_DMA));
    print_bytes("8-bit free:", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    if (esp_psram_is_initialized()) {
        print_bytes("PSRAM free:", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        print_bytes("PSRAM min free:", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
        print_bytes("PSRAM largest:", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
    printf("\n");
    return ESP_OK;
}

static int sys_psram_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nPSRAM\n");
    printf("-----\n");
    if (!esp_psram_is_initialized()) {
        printf("Status: not initialized\n\n");
        return ESP_OK;
    }

    printf("Status: initialized\n");
    print_bytes("Physical size:", esp_psram_get_size());
    print_bytes("Heap free:", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    print_bytes("Heap min free:", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    print_bytes("Largest block:", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    printf("\n");
    return ESP_OK;
}

static int sys_flash_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(esp_flash_default_chip, &flash_size);
    if (err != ESP_OK) {
        printf("Failed to read flash size: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("\nFlash\n");
    printf("-----\n");
    print_bytes("Detected size:", flash_size);
    printf("%-22s %s\n", "Configured size:", CONFIG_ESPTOOLPY_FLASHSIZE);
    printf("%-22s %s\n", "Mode:", CONFIG_ESPTOOLPY_FLASHMODE);
    printf("\n");
    return ESP_OK;
}

static int sys_reset_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_reset_reason_t reason = esp_reset_reason();
    printf("Reset reason: %d (%s)\n", reason, reset_reason_to_str(reason));
    return ESP_OK;
}

static void print_sys_command_list(void);
static void print_sys_help(void);

static const sys_command_entry_t sys_cmds[] = {
    {"info", sys_info_command, "Show chip, IDF, uptime, features, and reset summary"},
    {"heap", sys_heap_command, "Show internal heap, DMA heap, and PSRAM heap usage"},
    {"psram", sys_psram_command, "Show PSRAM status and memory usage"},
    {"flash", sys_flash_command, "Show detected and configured flash information"},
    {"reset", sys_reset_command, "Show the last reset reason"},
};

#define SYS_CMD_COUNT (sizeof(sys_cmds) / sizeof(sys_cmds[0]))

static char sys_cmds_help[160] = {0};

static void generate_sys_cmds_help_text(void)
{
    strlcpy(sys_cmds_help, ": ", sizeof(sys_cmds_help));
    for (size_t i = 0; i < SYS_CMD_COUNT; i++) {
        strlcat(sys_cmds_help, sys_cmds[i].name, sizeof(sys_cmds_help));
        if (i < SYS_CMD_COUNT - 1) {
            strlcat(sys_cmds_help, "; ", sizeof(sys_cmds_help));
        }
    }
}

static void print_sys_command_list(void)
{
    printf("Available sys commands:\n");
    for (size_t i = 0; i < SYS_CMD_COUNT; i++) {
        printf("  %-8s %s\n", sys_cmds[i].name, sys_cmds[i].description);
    }
}

static void print_sys_help(void)
{
    printf("\nUsage: sys <subcommand> [options]\n\n");
    print_sys_command_list();
    printf("\nExamples:\n");
    printf("  sys info\n");
    printf("  sys heap\n");
    printf("  sys flash\n\n");
}

static int sys_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &sys_args);

    if (argc == 1 || sys_args.help->count > 0) {
        print_sys_help();
        return ESP_OK;
    }

    if (sys_args.list->count > 0) {
        print_sys_command_list();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, sys_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (!sys_args.subcommand || sys_args.subcommand->count == 0 || !sys_args.subcommand->sval[0]) {
        printf("No subcommand provided. Use `sys --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = sys_args.subcommand->sval[0];
    for (size_t i = 0; i < SYS_CMD_COUNT; i++) {
        if (strcmp(subcommand, sys_cmds[i].name) == 0) {
            return sys_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown sys subcommand: %s\n", subcommand);
    printf("Use `sys --list` to see available subcommands.\n");
    return ESP_ERR_NOT_FOUND;
}

void cli_register_sys_command(void)
{
    generate_sys_cmds_help_text();

    sys_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", sys_cmds_help);
    sys_args.list = arg_lit0("l", "list", "List available sys subcommands");
    sys_args.help = arg_lit0("h", "help", "Show sys command help");
    sys_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "sys",
        .help = "System diagnostics",
        .hint = NULL,
        .func = &sys_command,
        .argtable = &sys_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
