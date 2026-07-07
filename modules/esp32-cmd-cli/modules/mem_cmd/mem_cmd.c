#include "mem_cmd.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ESP32_CLI";

#define MEM_TOP_DEFAULT_REFRESH_MS 1000
#define MEM_TOP_MIN_REFRESH_MS 100
#define MEM_TOP_MAX_REFRESH_MS 60000

typedef int (*mem_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    mem_func_t function;
    const char *description;
} mem_command_entry_t;

typedef struct {
    const char *name;
    uint32_t caps;
} mem_caps_entry_t;

static struct {
    struct arg_str *subcommand;
    struct arg_int *refresh_ms;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} mem_args;

static const mem_caps_entry_t s_mem_caps[] = {
    {"8BIT", MALLOC_CAP_8BIT},
    {"INTERNAL", MALLOC_CAP_INTERNAL},
    {"SPIRAM", MALLOC_CAP_SPIRAM},
    {"DMA", MALLOC_CAP_DMA},
    {"32BIT", MALLOC_CAP_32BIT},
};

static void print_bytes(const char *label, size_t bytes)
{
    printf("%-18s %10u B  %8.2f KiB  %7.2f MiB\n",
        label,
        (unsigned) bytes,
        (double) bytes / 1024.0,
        (double) bytes / (1024.0 * 1024.0));
}

static void print_heap_caps_row(const char *name, uint32_t caps)
{
    multi_heap_info_t info = {0};
    heap_caps_get_info(&info, caps);

    printf("%-10s %10u %10u %10u %10u %8u %8u\n",
        name,
        (unsigned) heap_caps_get_free_size(caps),
        (unsigned) heap_caps_get_minimum_free_size(caps),
        (unsigned) heap_caps_get_largest_free_block(caps),
        (unsigned) info.total_free_bytes,
        (unsigned) info.allocated_blocks,
        (unsigned) info.free_blocks);
}

static void print_caps_table(void)
{
    printf("%-10s %10s %10s %10s %10s %8s %8s\n",
        "Caps",
        "Free",
        "MinFree",
        "Largest",
        "InfoFree",
        "AllocBlk",
        "FreeBlk");
    printf("%-10s %10s %10s %10s %10s %8s %8s\n",
        "----------",
        "----------",
        "----------",
        "----------",
        "----------",
        "--------",
        "--------");

    for (size_t i = 0; i < sizeof(s_mem_caps) / sizeof(s_mem_caps[0]); i++) {
        print_heap_caps_row(s_mem_caps[i].name, s_mem_caps[i].caps);
    }
}

static int mem_info_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nMemory summary\n");
    printf("--------------\n");
    print_bytes("Heap free:", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    print_bytes("Heap min free:", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    print_bytes("Heap largest:", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    print_bytes("Internal free:", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    print_bytes("Internal largest:", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    print_bytes("DMA free:", heap_caps_get_free_size(MALLOC_CAP_DMA));

    if (esp_psram_is_initialized()) {
        print_bytes("PSRAM size:", esp_psram_get_size());
        print_bytes("PSRAM free:", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        print_bytes("PSRAM largest:", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    } else {
        printf("%-18s %s\n", "PSRAM:", "not initialized");
    }

    printf("\n");
    return ESP_OK;
}

static int mem_caps_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nMemory capabilities\n");
    printf("-------------------\n");
    print_caps_table();
    printf("\n");
    return ESP_OK;
}

static bool mem_read_stop_key(void)
{
    char ch;
    ssize_t read_count = read(fileno(stdin), &ch, sizeof(ch));

    if (read_count > 0) {
        while (read(fileno(stdin), &ch, sizeof(ch)) > 0) {
        }
        return true;
    }

    return read_count < 0 && errno != EAGAIN && errno != EWOULDBLOCK;
}

static bool mem_wait_for_stop_or_timeout(TickType_t timeout_ticks)
{
    const TickType_t step_ticks = pdMS_TO_TICKS(50);
    TickType_t elapsed_ticks = 0;

    while (elapsed_ticks < timeout_ticks) {
        if (mem_read_stop_key()) {
            return true;
        }

        TickType_t remaining_ticks = timeout_ticks - elapsed_ticks;
        TickType_t delay_ticks = remaining_ticks < step_ticks ? remaining_ticks : step_ticks;
        vTaskDelay(delay_ticks);
        elapsed_ticks += delay_ticks;
    }

    return mem_read_stop_key();
}

static uint32_t mem_get_top_refresh_ms(void)
{
    if (mem_args.refresh_ms->count == 0) {
        return MEM_TOP_DEFAULT_REFRESH_MS;
    }

    int refresh_ms = mem_args.refresh_ms->ival[0];
    if (refresh_ms < MEM_TOP_MIN_REFRESH_MS || refresh_ms > MEM_TOP_MAX_REFRESH_MS) {
        printf("Invalid refresh interval: %d ms. Allowed range: %u..%u ms.\n",
            refresh_ms,
            (unsigned) MEM_TOP_MIN_REFRESH_MS,
            (unsigned) MEM_TOP_MAX_REFRESH_MS);
        return 0;
    }

    return (uint32_t) refresh_ms;
}

static int mem_top_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    uint32_t refresh_ms = mem_get_top_refresh_ms();
    if (refresh_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int stdin_fd = fileno(stdin);
    const int old_flags = fcntl(stdin_fd, F_GETFL, 0);
    if (old_flags < 0) {
        printf("Failed to read console input flags\n");
        return ESP_FAIL;
    }

    if (fcntl(stdin_fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
        printf("Failed to set console input non-blocking mode\n");
        return ESP_FAIL;
    }

    printf("Starting mem top. Press any key to stop.\n");

    while (true) {
        if (mem_wait_for_stop_or_timeout(pdMS_TO_TICKS(refresh_ms))) {
            break;
        }

        printf("\033[2J\033[H");
        printf("mem top - refresh: %" PRIu32 " ms - press any key to stop\n\n", refresh_ms);
        mem_info_command(0, NULL);
        mem_caps_command(0, NULL);
    }

    fcntl(stdin_fd, F_SETFL, old_flags);
    printf("\nStopped mem top.\n");
    return ESP_OK;
}

static void print_mem_help(void);

static const mem_command_entry_t mem_cmds[] = {
    {"info", mem_info_command, "Show heap, internal RAM, DMA, and PSRAM summary"},
    {"caps", mem_caps_command, "Show heap usage grouped by malloc capability"},
    {"top", mem_top_command, "Refresh memory view until any key is pressed"},
};

#define MEM_CMD_COUNT (sizeof(mem_cmds) / sizeof(mem_cmds[0]))

static char mem_cmds_help[96] = {0};

static void generate_mem_cmds_help_text(void)
{
    strlcpy(mem_cmds_help, ": ", sizeof(mem_cmds_help));
    for (size_t i = 0; i < MEM_CMD_COUNT; i++) {
        strlcat(mem_cmds_help, mem_cmds[i].name, sizeof(mem_cmds_help));
        if (i < MEM_CMD_COUNT - 1) {
            strlcat(mem_cmds_help, "; ", sizeof(mem_cmds_help));
        }
    }
}

static void print_mem_commands(void)
{
    printf("Available mem commands:\n");
    for (size_t i = 0; i < MEM_CMD_COUNT; i++) {
        printf("  %-8s %s\n", mem_cmds[i].name, mem_cmds[i].description);
    }
}

static void print_mem_help(void)
{
    printf("\nUsage: mem <subcommand> [refresh_ms]\n\n");
    print_mem_commands();
    printf("\nOptions:\n");
    printf("  refresh_ms  Optional for `top`; range: %u..%u ms; default: %u ms\n",
        (unsigned) MEM_TOP_MIN_REFRESH_MS,
        (unsigned) MEM_TOP_MAX_REFRESH_MS,
        (unsigned) MEM_TOP_DEFAULT_REFRESH_MS);
    printf("\nExamples:\n");
    printf("  mem info\n");
    printf("  mem caps\n");
    printf("  mem top\n");
    printf("  mem top 500\n\n");
}

static int mem_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &mem_args);

    if (argc == 1 || mem_args.help->count > 0) {
        print_mem_help();
        return ESP_OK;
    }

    if (mem_args.list->count > 0) {
        print_mem_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, mem_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (mem_args.subcommand->count == 0 || mem_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `mem --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = mem_args.subcommand->sval[0];
    if (mem_args.refresh_ms->count > 0 && strcmp(subcommand, "top") != 0) {
        printf("Refresh interval is only supported by `mem top`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < MEM_CMD_COUNT; i++) {
        if (strcmp(subcommand, mem_cmds[i].name) == 0) {
            return mem_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown mem subcommand: %s\n", subcommand);
    print_mem_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_mem_command(void)
{
    generate_mem_cmds_help_text();

    mem_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", mem_cmds_help);
    mem_args.refresh_ms = arg_int0(NULL, NULL, "<refresh_ms>", "Optional refresh interval for `mem top`, in milliseconds");
    mem_args.list = arg_lit0("l", "list", "List available mem subcommands");
    mem_args.help = arg_lit0("h", "help", "Show mem command help");
    mem_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "mem",
        .help = "Memory diagnostics",
        .hint = NULL,
        .func = &mem_command,
        .argtable = &mem_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
