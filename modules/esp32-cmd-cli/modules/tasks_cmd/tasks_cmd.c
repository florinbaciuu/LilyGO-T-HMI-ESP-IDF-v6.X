#include "tasks_cmd.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"

static const char* TAG = "ESP32_CLI";

#define TASK_SNAPSHOT_MAX_COUNT 64
#define TASK_NAME_PRINT_LEN 20

#ifndef TASKS_TOP_REFRESH_MS
#define TASKS_TOP_REFRESH_MS 1000
#endif

#if TASKS_TOP_REFRESH_MS < 100
#error "TASKS_TOP_REFRESH_MS must be at least 100 ms"
#endif

#define TASKS_TOP_MIN_REFRESH_MS 100
#define TASKS_TOP_MAX_REFRESH_MS 60000

typedef struct {
    uint32_t runtime_counter;
    UBaseType_t task_number;
    bool valid;
} task_snapshot_t;

typedef struct {
    TaskStatus_t status;
    float cpu_percent;
} task_row_t;

static task_snapshot_t previous_snapshot[TASK_SNAPSHOT_MAX_COUNT];
static size_t snapshot_count;
static uint32_t previous_total_runtime;

/***
 * Structura necesara functiei principale
 * Structura care contine alte 2 structuri
 */
static struct
{
    struct arg_str* subcommand;
    struct arg_int* refresh_ms;
    struct arg_int* core_id;
    struct arg_int* limit;
    struct arg_lit* list;
    struct arg_lit* help;
    struct arg_end* end;
} tasks_args;

static const char* task_state_to_str(eTaskState state)
{
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

static const char* task_core_to_str(BaseType_t core_id)
{
    static char core_str[16];

#if defined(tskNO_AFFINITY)
    if (core_id == tskNO_AFFINITY) {
        return "any";
    }
#endif

    if (core_id < 0) {
        return "any";
    }

    snprintf(core_str, sizeof(core_str), "%d", (int) core_id);
    return core_str;
}

static task_snapshot_t* get_previous_task_data(UBaseType_t task_number)
{
    for (size_t i = 0; i < snapshot_count; i++) {
        if (previous_snapshot[i].valid && previous_snapshot[i].task_number == task_number) {
            return &previous_snapshot[i];
        }
    }

    if (snapshot_count >= TASK_SNAPSHOT_MAX_COUNT) {
        return NULL;
    }

    task_snapshot_t* result = &previous_snapshot[snapshot_count++];
    result->task_number = task_number;
    result->runtime_counter = 0;
    result->valid = true;
    return result;
}

static int compare_task_rows_by_cpu(const void* lhs, const void* rhs)
{
    const task_row_t* a = (const task_row_t*) lhs;
    const task_row_t* b = (const task_row_t*) rhs;

    if (a->cpu_percent < b->cpu_percent) {
        return 1;
    }
    if (a->cpu_percent > b->cpu_percent) {
        return -1;
    }
    return strcmp(a->status.pcTaskName, b->status.pcTaskName);
}

static bool task_core_is_any(BaseType_t core_id)
{
#if defined(tskNO_AFFINITY)
    if (core_id == tskNO_AFFINITY) {
        return true;
    }
#endif

    return core_id < 0;
}

static bool task_matches_core_filter(BaseType_t task_core_id, int core_filter)
{
    if (core_filter < 0) {
        return true;
    }

    return task_core_is_any(task_core_id) || task_core_id == core_filter;
}

static int tasks_get_core_filter(void)
{
    if (tasks_args.core_id->count == 0) {
        return -1;
    }

    int core_id = tasks_args.core_id->ival[0];
    if (core_id < 0 || core_id >= configNUMBER_OF_CORES) {
        printf("Invalid core: %d. Allowed range: 0..%d.\n", core_id, configNUMBER_OF_CORES - 1);
        return -2;
    }

    return core_id;
}

static esp_err_t tasks_get_limit(size_t* limit_out)
{
    *limit_out = 0;

    if (tasks_args.limit->count == 0) {
        return ESP_OK;
    }

    int limit = tasks_args.limit->ival[0];
    if (limit <= 0 || limit > TASK_SNAPSHOT_MAX_COUNT) {
        printf("Invalid limit: %d. Allowed range: 1..%u.\n", limit, (unsigned) TASK_SNAPSHOT_MAX_COUNT);
        return ESP_ERR_INVALID_ARG;
    }

    *limit_out = (size_t) limit;
    return ESP_OK;
}

static int tasks_sample(bool print_output, const char* title, int core_filter, size_t limit)
{
    UBaseType_t task_capacity = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t* task_stats = calloc(task_capacity, sizeof(TaskStatus_t));
    task_row_t* rows = calloc(task_capacity, sizeof(task_row_t));
    if (task_stats == NULL || rows == NULL) {
        free(task_stats);
        free(rows);
        printf("Failed to allocate task snapshot buffers\n");
        return ESP_ERR_NO_MEM;
    }

    uint32_t total_runtime = 0;
    UBaseType_t task_count = uxTaskGetSystemState(task_stats, task_capacity, &total_runtime);
    if (task_count > task_capacity) {
        task_count = task_capacity;
    }

    uint32_t total_delta = total_runtime - previous_total_runtime;
    bool has_baseline = previous_total_runtime != 0;
    size_t row_count = 0;

    for (UBaseType_t i = 0; i < task_count; i++) {
        TaskStatus_t* stats = &task_stats[i];
        task_snapshot_t* previous = get_previous_task_data(stats->xTaskNumber);
        uint32_t previous_runtime = previous ? previous->runtime_counter : stats->ulRunTimeCounter;
        uint32_t task_delta = stats->ulRunTimeCounter - previous_runtime;

        if (task_matches_core_filter(stats->xCoreID, core_filter)) {
            rows[row_count].status = *stats;
            rows[row_count].cpu_percent = total_delta > 0 ? (100.0f * (float) task_delta) / (float) total_delta : 0.0f;
            row_count++;
        }

        if (previous != NULL) {
            previous->runtime_counter = stats->ulRunTimeCounter;
        }
    }

    previous_total_runtime = total_runtime;

    qsort(rows, row_count, sizeof(rows[0]), compare_task_rows_by_cpu);

    if (print_output) {
        size_t shown_count = limit > 0 && limit < row_count ? limit : row_count;

        printf("\n%s", title ? title : "Tasks snapshot");
        if (has_baseline) {
            printf(" (delta since previous sample)\n");
        } else {
            printf(" (first sample since boot)\n");
        }
        printf("Filter: core=%s  limit=",
            core_filter >= 0 ? task_core_to_str(core_filter) : "all");
        if (limit > 0) {
            printf("%u\n", (unsigned) limit);
        } else {
            printf("none\n");
        }
        printf("---------------------------------------------------------------\n");
        printf("%6s  %8s  %-9s  %-4s  %-4s  %-*s\n",
            "CPU%",
            "StackHW",
            "State",
            "Core",
            "Prio",
            TASK_NAME_PRINT_LEN,
            "Name");
        printf("%6s  %8s  %-9s  %-4s  %-4s  %-*s\n",
            "------",
            "--------",
            "---------",
            "----",
            "----",
            TASK_NAME_PRINT_LEN,
            "--------------------");

        for (size_t i = 0; i < shown_count; i++) {
            const TaskStatus_t* stats = &rows[i].status;
            printf("%6.2f  %8u  %-9s  %-4s  %-4u  %-*.*s\n",
                (double) rows[i].cpu_percent,
                (unsigned) stats->usStackHighWaterMark,
                task_state_to_str(stats->eCurrentState),
                task_core_to_str(stats->xCoreID),
                (unsigned) stats->uxBasePriority,
                TASK_NAME_PRINT_LEN,
                TASK_NAME_PRINT_LEN,
                stats->pcTaskName);
        }

        printf("---------------------------------------------------------------\n");
        printf("Tasks: %u shown / %u matched, runtime delta: %" PRIu32 "\n\n",
            (unsigned) shown_count,
            (unsigned) row_count,
            total_delta);
    }

    free(rows);
    free(task_stats);
    return ESP_OK;
}

static int tasks_info(void)
{
    int core_filter = tasks_get_core_filter();
    if (core_filter == -2) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t limit = 0;
    esp_err_t err = tasks_get_limit(&limit);
    if (err != ESP_OK) {
        return err;
    }

    return tasks_sample(true, "Tasks snapshot", core_filter, limit);
}

static int tasks_reset(void)
{
    memset(previous_snapshot, 0, sizeof(previous_snapshot));
    snapshot_count = 0;
    previous_total_runtime = 0;
    printf("Task CPU baseline reset. Run `tasks info` to take a new sample.\n");
    return ESP_OK;
}

static bool tasks_read_stop_key(void)
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

static bool tasks_wait_for_stop_or_timeout(TickType_t timeout_ticks)
{
    const TickType_t step_ticks = pdMS_TO_TICKS(50);
    TickType_t elapsed_ticks = 0;

    while (elapsed_ticks < timeout_ticks) {
        if (tasks_read_stop_key()) {
            return true;
        }

        TickType_t remaining_ticks = timeout_ticks - elapsed_ticks;
        TickType_t delay_ticks = remaining_ticks < step_ticks ? remaining_ticks : step_ticks;
        vTaskDelay(delay_ticks);
        elapsed_ticks += delay_ticks;
    }

    return tasks_read_stop_key();
}

static uint32_t tasks_get_top_refresh_ms(void)
{
    if (tasks_args.refresh_ms->count == 0) {
        return TASKS_TOP_REFRESH_MS;
    }

    int refresh_ms = tasks_args.refresh_ms->ival[0];
    if (refresh_ms < TASKS_TOP_MIN_REFRESH_MS || refresh_ms > TASKS_TOP_MAX_REFRESH_MS) {
        printf("Invalid refresh interval: %d ms. Allowed range: %u..%u ms.\n",
            refresh_ms,
            (unsigned) TASKS_TOP_MIN_REFRESH_MS,
            (unsigned) TASKS_TOP_MAX_REFRESH_MS);
        return 0;
    }

    return (uint32_t) refresh_ms;
}

static int tasks_top(void)
{
    uint32_t refresh_ms = tasks_get_top_refresh_ms();
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

    printf("Starting tasks top. Press any key to stop.\n");

    int core_filter = tasks_get_core_filter();
    if (core_filter == -2) {
        fcntl(stdin_fd, F_SETFL, old_flags);
        return ESP_ERR_INVALID_ARG;
    }

    size_t limit = 0;
    esp_err_t limit_err = tasks_get_limit(&limit);
    if (limit_err != ESP_OK) {
        fcntl(stdin_fd, F_SETFL, old_flags);
        return limit_err;
    }

    tasks_sample(false, NULL, core_filter, limit);

    while (true) {
        if (tasks_wait_for_stop_or_timeout(pdMS_TO_TICKS(refresh_ms))) {
            break;
        }

        printf("\033[2J\033[H");
        printf("tasks top - refresh: %" PRIu32 " ms - press any key to stop\n", refresh_ms);
        esp_err_t err = tasks_sample(true, "Tasks live", core_filter, limit);
        if (err != ESP_OK) {
            fcntl(stdin_fd, F_SETFL, old_flags);
            return err;
        }
    }

    fcntl(stdin_fd, F_SETFL, old_flags);
    printf("\nStopped tasks top.\n");
    return ESP_OK;
}

// -------------------------------------------------------------

typedef struct
{
    const char* name;
    int (*function)(void);
    const char* description;
} tasks_command_entry_t;

// -------------------------------------

static const tasks_command_entry_t tasks_cmds[] = {
    {"info", tasks_info, "Show task CPU, stack high-water mark, state, core, priority, and name"},
    {"top", tasks_top, "Refresh task CPU view until any key is pressed"},
    {"reset", tasks_reset, "Reset the CPU usage baseline used by `tasks info`"},
};

// -------------------------------------

static void print_tasks_command_list(void)
{
    printf("Available tasks commands:\n");
    for (size_t i = 0; i < sizeof(tasks_cmds) / sizeof(tasks_cmds[0]); i++) {
        printf("  %-8s %s\n",
            tasks_cmds[i].name,
            tasks_cmds[i].description ? tasks_cmds[i].description : "No description");
    }
}

// -------------------------------------

#define TASKS_CMD_COUNT (sizeof(tasks_cmds) / sizeof(tasks_cmds[0]))

static char tasks_cmds_help[128] = {0};

static void generate_tasks_cmds_help_text(void)
{
    strlcpy(tasks_cmds_help, ": ", sizeof(tasks_cmds_help));
    for (size_t i = 0; i < TASKS_CMD_COUNT; i++) {
        strlcat(tasks_cmds_help, tasks_cmds[i].name, sizeof(tasks_cmds_help));
        if (i < TASKS_CMD_COUNT - 1) {
            strlcat(tasks_cmds_help, "; ", sizeof(tasks_cmds_help));
        }
    }
}

static void print_tasks_help(void)
{
    printf("\nUsage: tasks <subcommand> [refresh_ms] [--core N] [--limit N]\n\n");
    print_tasks_command_list();
    printf("\nOptions:\n");
    printf("  refresh_ms  Optional for `top`; range: %u..%u ms; default: %u ms\n",
        (unsigned) TASKS_TOP_MIN_REFRESH_MS,
        (unsigned) TASKS_TOP_MAX_REFRESH_MS,
        (unsigned) TASKS_TOP_REFRESH_MS);
    printf("  --core N    Optional for `info` and `top`; valid cores: 0..%d; includes tasks with any-core affinity\n",
        configNUMBER_OF_CORES - 1);
    printf("  --limit N   Optional for `info` and `top`; show first N rows after CPU sorting\n");
    printf("\nExamples:\n");
    printf("  tasks info\n");
    printf("  tasks info --core 0 --limit 8\n");
    printf("  tasks top\n");
    printf("  tasks top 500\n");
    printf("  tasks top 500 --core 1 --limit 10\n");
    printf("  tasks reset\n\n");
}

// -------------------------------------

static int tasks_command(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &tasks_args);

    if (argc == 1 || tasks_args.help->count > 0) {
        print_tasks_help();
        return ESP_OK;
    }

    if (tasks_args.list->count > 0) {
        print_tasks_command_list();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, tasks_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (!tasks_args.subcommand || tasks_args.subcommand->count == 0 || !tasks_args.subcommand->sval[0]) {
        printf("No subcommand provided. Use `tasks --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char* subcommand = tasks_args.subcommand->sval[0];
    size_t      num_cmds   = sizeof(tasks_cmds) / sizeof(tasks_cmds[0]);
    bool        is_view_cmd = strcmp(subcommand, "info") == 0 || strcmp(subcommand, "top") == 0;

    if (tasks_args.refresh_ms->count > 0 && strcmp(subcommand, "top") != 0) {
        printf("Refresh interval is only supported by `tasks top`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    if ((tasks_args.core_id->count > 0 || tasks_args.limit->count > 0) && !is_view_cmd) {
        printf("Core and limit filters are only supported by `tasks info` and `tasks top`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < num_cmds; ++i) {
        if (strcmp(subcommand, tasks_cmds[i].name) == 0) {
            return tasks_cmds[i].function();
        }
    }

    printf("Unknown subcommand: %s\n", subcommand);
    printf("Type `tasks --help` to see help.\n");
    printf("Type `tasks --list` to see available subcommands.\n");
    return ESP_ERR_NOT_FOUND;
}

// -------------------------------------
// -------------------------------------

void cli_register_tasks_command(void)
{
    generate_tasks_cmds_help_text();
    tasks_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", tasks_cmds_help);
    tasks_args.refresh_ms = arg_int0(NULL, NULL, "<refresh_ms>", "Optional refresh interval for `tasks top`, in milliseconds");
    tasks_args.core_id    = arg_int0(NULL, "core", "<core>", "Filter by core for `tasks info` and `tasks top`");
    tasks_args.limit      = arg_int0(NULL, "limit", "<rows>", "Limit rows for `tasks info` and `tasks top`");
    tasks_args.list       = arg_lit0("l", "list", "List all available subcommands");
    tasks_args.help       = arg_lit0("h", "help", "Show tasks command help");
    tasks_args.end        = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command  = "tasks",
        .help     = "Tasks",
        .hint     = NULL,
        .func     = &tasks_command,
        .argtable = &tasks_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
    return;
}
