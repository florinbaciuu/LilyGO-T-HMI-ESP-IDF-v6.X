#include "tasks_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_err.h"
#include <time.h>
#include "argtable3/argtable3.h"

static const char* TAG = "ESP32_CLI";

#define TASK_MAX_COUNT 40
#define SECONDS_TO_MICROSECONDS(x) ((x) * 1000000)

const char* task_state[] = {"Running", "Ready", "Blocked", "Suspend", "Deleted", "Invalid"};

typedef struct
{
    uint32_t ulRunTimeCounter;
    uint32_t xTaskNumber;
} task_data_t;

static task_data_t previous_snapshot[TASK_MAX_COUNT];
static int         task_top_id   = 0;
static uint32_t    total_runtime = 0;

task_data_t* getPreviousTaskData(uint32_t xTaskNumber) {
    for (int i = 0; i < task_top_id; i++) {
        if (previous_snapshot[i].xTaskNumber == xTaskNumber) {
            return &previous_snapshot[i];
        }
    }  // Try to find the task in the list of tasks
    assert(task_top_id < TASK_MAX_COUNT);  // Allocate a new entry
    task_data_t* result = &previous_snapshot[task_top_id];
    result->xTaskNumber = xTaskNumber;
    task_top_id++;
    return result;
}

const char* getTimestamp() {
    static char timestamp[20];
    time_t      now = time(NULL);
    struct tm*  t   = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    return timestamp;
}

static int tasks_info() {
    ESP_LOGI(TAG, "-----------------Task Dump Start-----------------");
    printf("\n\r");
    uint32_t     totalRunTime              = 0;
    TaskStatus_t taskStats[TASK_MAX_COUNT] = {0};
    uint32_t     taskCount                 = uxTaskGetSystemState(taskStats, TASK_MAX_COUNT, &totalRunTime);
    assert(task_top_id < TASK_MAX_COUNT);
    uint32_t totalDelta = totalRunTime - total_runtime;
    float    f          = 100.0 / totalDelta;
    printf("%.4s\t%.6s\t%.8s\t%.8s\t%.4s\t%-20s\n",
        "Load",
        "Stack",
        "State",
        "CoreID",
        "PRIO",
        "Name");  // Format headers in a more visually appealing way
    for (uint32_t i = 0; i < taskCount; i++) {
        TaskStatus_t* stats            = &taskStats[i];
        task_data_t*  previousTaskData = getPreviousTaskData(stats->xTaskNumber);
        uint32_t      taskRunTime      = stats->ulRunTimeCounter;
        float         load             = f * (taskRunTime - previousTaskData->ulRunTimeCounter);

        char formattedTaskName[19];  // 16 caractere + 1 caracter pt terminatorul '\0' + 2 caractere
                                     // pt paranteze"[]"
        snprintf(formattedTaskName,
            sizeof(formattedTaskName),
            "[%-16s]",
            stats->pcTaskName);  // Format for the task's name with improved visibility
        char core_id_str[16];
        if (stats->xCoreID == -1 || stats->xCoreID == 2147483647) {
            snprintf(core_id_str, sizeof(core_id_str), "1/2");
        } else {
            snprintf(core_id_str, sizeof(core_id_str), "%d", stats->xCoreID);
        }  // Customize how core ID is displayed for better clarity
        printf("%.2f\t%" PRIu32 "\t%-4s\t%-4s\t%-4u\t%-19s\n",
            load,
            stats->usStackHighWaterMark,
            task_state[stats->eCurrentState],
            core_id_str,
            stats->uxBasePriority,
            formattedTaskName);  // Print formatted output
        previousTaskData->ulRunTimeCounter = taskRunTime;
    }
    total_runtime = totalRunTime;
    printf("\n\r");
    ESP_LOGI(TAG, "-----------------Task Dump End-------------------");
    return 0;
}

// ==========================================

// ==========================================

// -------------------------------------------------------------

/***
 * Structura necesara functiei principale
 * Structura care contine alte 2 structuri
 */
static struct
{
    struct arg_str* subcommand;
    struct arg_lit* list;  // <-- opțiunea nouă
    struct arg_lit* help;  // ⬅️ NOU
    struct arg_end* end;
} tasks_args;

typedef void (*info_func_t)(void);
typedef struct
{
    const char* name;
    void (*function)(void);
    const char* description;  // nou!
} tasks_command_entry_t;

// -------------------------------------

void printTasksCommandList();

void printTasksInfo() {
    tasks_info();
}
// -------------------------------------

static const tasks_command_entry_t tasks_cmds[] = {
    {"info", printTasksInfo, "Display chip model, cores, and revision"},
    //{"--list", printTasksCommandList, "List all available subcommands"},
};

// -------------------------------------

void printTasksCommandList() {
    printf("╔═══════════════════════ AVAILABLE TASKS COMMANDS ══════════════════════════╗\n");
    printf("║ %-10s │ %-60s ║\n", "Command", "Description");
    printf("╟─────────┼─────────────────────────────────────────────────────────────────╢\n");
    for (size_t i = 0; i < sizeof(tasks_cmds) / sizeof(tasks_cmds[0]); ++i) {
        printf("║ %-10s │ %-60s ║\n",
            tasks_cmds[i].name,
            tasks_cmds[i].description ? tasks_cmds[i].description : "No description");
    }
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
}

// -------------------------------------

#define TASKS_CMD_COUNT (sizeof(tasks_cmds) / sizeof(tasks_cmds[0]))

static char tasks_cmds_help[128] = {0};

static void generate_tasks_cmds_help_text(void) {
    strcpy(tasks_cmds_help, ":   ");
    for (size_t i = 0; i < TASKS_CMD_COUNT; i++) {
        strcat(tasks_cmds_help, tasks_cmds[i].name);
        if (i < TASKS_CMD_COUNT - 1)
            strcat(tasks_cmds_help, "; ");
    }
}

void PrintHelpFunc() {
    printf("╔═══════════════════════════ TASKS COMMAND HELP ═════════════════════════════╗\n");
    printf("║ Usage: tasks <subcommand> [--<argument>] [-<argument]                      ║\n");
    printf("║                                                                            ║\n");
    printf("║ Available subcommands:                                                     ║\n");
    for (size_t i = 0; i < TASKS_CMD_COUNT; i++) {
        printf("║   %-10s - %-60s║\n", tasks_cmds[i].name, tasks_cmds[i].description);
    }
    printf("║                                                                            ║\n");
    printf("║ Use 'tasks <subcommand> --help' for more information.                      ║\n");
    printf("║ Use 'tasks <subcommand> --list' to print avaible subcommands.              ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
}

// -------------------------------------

static int tasks_command(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**) &tasks_args);

    // Dacă nu are niciun argument sau a cerut help global
    if (argc == 1 || tasks_args.help->count > 0) {
        PrintHelpFunc();
        return 0;
    }

    // Afișare listă dacă s-a cerut explicit
    if (tasks_args.list->count > 0) {
        printTasksCommandList();
        return 0;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, tasks_args.end, argv[0]);
        return 1;
    }

    // Verificare subcommand valid
    if (!tasks_args.subcommand || tasks_args.subcommand->count == 0 || !tasks_args.subcommand->sval[0]) {
        printf("No subcommand provided. Use `tasks --help`.\n");
        return 1;
    }

    const char* subcommand = tasks_args.subcommand->sval[0];
    size_t      num_cmds   = sizeof(tasks_cmds) / sizeof(tasks_cmds[0]);

    for (size_t i = 0; i < num_cmds; ++i) {
        if (strcmp(subcommand, tasks_cmds[i].name) == 0) {
            if (argc > 2 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) {
                printf("Help for '%s': %s\n", tasks_cmds[i].name, tasks_cmds[i].description);
                return 0;
            }
            tasks_cmds[i].function();
            return 0;
        }
    }

    printf("Unknown subcommand: %s\n", subcommand);
    printf("Type `tasks --help` to see help.\n");
    printf("Type `tasks --list` to see available subcommands.\n");
    return 1;
}

// -------------------------------------
// -------------------------------------

void cli_register_tasks_command(void) {
    generate_tasks_cmds_help_text();
    tasks_args.subcommand       = arg_str1(NULL,  // nu are flag scurt, gen `-s
        NULL,                                     // nu are flag lung, gen `--subcmd`
        "<subcommand>",                           // numele argumentului (pentru help/usage)
        tasks_cmds_help);                         // descrierea lui
    tasks_args.list             = arg_lit0("l", "list", "List all available subcommands");
    tasks_args.help             = arg_lit0("h", "help", "Show help for 'info' command");
    tasks_args.end              = arg_end(1);

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