#include "log_cmd.h"

#include <stdio.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_level.h"

static const char *TAG = "ESP32_CLI";

static struct {
    struct arg_str *subcommand;
    struct arg_str *tag;
    struct arg_str *level;
    struct arg_lit *levels;
    struct arg_lit *help;
    struct arg_end *end;
} log_args;

typedef int (*log_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    log_func_t function;
    const char *description;
} log_command_entry_t;

typedef struct {
    const char *name;
    esp_log_level_t level;
} log_level_entry_t;

static const log_level_entry_t s_log_levels[] = {
    {"none", ESP_LOG_NONE},
    {"error", ESP_LOG_ERROR},
    {"warn", ESP_LOG_WARN},
    {"warning", ESP_LOG_WARN},
    {"info", ESP_LOG_INFO},
    {"debug", ESP_LOG_DEBUG},
    {"verbose", ESP_LOG_VERBOSE},
};

static const char *log_level_to_str(esp_log_level_t level)
{
    switch (level) {
    case ESP_LOG_NONE:
        return "none";
    case ESP_LOG_ERROR:
        return "error";
    case ESP_LOG_WARN:
        return "warn";
    case ESP_LOG_INFO:
        return "info";
    case ESP_LOG_DEBUG:
        return "debug";
    case ESP_LOG_VERBOSE:
        return "verbose";
    default:
        return "unknown";
    }
}

static bool parse_log_level(const char *text, esp_log_level_t *out_level)
{
    if (text == NULL || out_level == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(s_log_levels) / sizeof(s_log_levels[0]); i++) {
        if (strcasecmp(text, s_log_levels[i].name) == 0) {
            *out_level = s_log_levels[i].level;
            return true;
        }
    }

    return false;
}

static void print_levels(void)
{
    printf("Available log levels:\n");
    printf("  none\n");
    printf("  error\n");
    printf("  warn\n");
    printf("  info\n");
    printf("  debug\n");
    printf("  verbose\n");
}

static int log_get_command(int argc, char **argv)
{
    (void) argc;

    if (log_args.tag->count == 0) {
        printf("Usage: log get <tag>\n");
        printf("Use '*' for the default tag level.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *tag = log_args.tag->sval[0];
    esp_log_level_t level = esp_log_level_get(tag);
    printf("Log level for '%s': %s\n", tag, log_level_to_str(level));
    return ESP_OK;
}

static int log_set_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (log_args.tag->count == 0 || log_args.level->count == 0) {
        printf("Usage: log set <tag> <level>\n");
        printf("Example: log set * warn\n");
        return ESP_ERR_INVALID_ARG;
    }

    esp_log_level_t level;
    const char *tag = log_args.tag->sval[0];
    const char *level_name = log_args.level->sval[0];

    if (!parse_log_level(level_name, &level)) {
        printf("Unknown log level: %s\n", level_name);
        print_levels();
        return ESP_ERR_INVALID_ARG;
    }

    esp_log_level_set(tag, level);
    printf("Log level for '%s' set to %s\n", tag, log_level_to_str(level));
    return ESP_OK;
}

static void print_log_help(void);

static const log_command_entry_t log_cmds[] = {
    {"get", log_get_command, "Show log level for a tag"},
    {"set", log_set_command, "Set log level for a tag or '*'"},
};

#define LOG_CMD_COUNT (sizeof(log_cmds) / sizeof(log_cmds[0]))

static char log_cmds_help[96] = {0};

static void generate_log_cmds_help_text(void)
{
    strlcpy(log_cmds_help, ": ", sizeof(log_cmds_help));
    for (size_t i = 0; i < LOG_CMD_COUNT; i++) {
        strlcat(log_cmds_help, log_cmds[i].name, sizeof(log_cmds_help));
        if (i < LOG_CMD_COUNT - 1) {
            strlcat(log_cmds_help, "; ", sizeof(log_cmds_help));
        }
    }
}

static void print_log_commands(void)
{
    printf("Available log commands:\n");
    for (size_t i = 0; i < LOG_CMD_COUNT; i++) {
        printf("  %-8s %s\n", log_cmds[i].name, log_cmds[i].description);
    }
}

static void print_log_help(void)
{
    printf("\nUsage:\n");
    printf("  log get <tag>\n");
    printf("  log set <tag> <level>\n");
    printf("  log --levels\n\n");
    print_log_commands();
    printf("\nExamples:\n");
    printf("  log set * warn\n");
    printf("  log set CLI debug\n");
    printf("  log get ESP32_CLI\n");
    printf("\n");
}

static int log_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &log_args);

    if (argc == 1 || log_args.help->count > 0) {
        print_log_help();
        return ESP_OK;
    }

    if (log_args.levels->count > 0) {
        print_levels();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, log_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (log_args.subcommand->count == 0 || log_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `log --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = log_args.subcommand->sval[0];
    for (size_t i = 0; i < LOG_CMD_COUNT; i++) {
        if (strcmp(subcommand, log_cmds[i].name) == 0) {
            return log_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown log subcommand: %s\n", subcommand);
    print_log_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_log_command(void)
{
    generate_log_cmds_help_text();

    log_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", log_cmds_help);
    log_args.tag = arg_str0(NULL, NULL, "<tag>", "Log tag, or '*' for all tags");
    log_args.level = arg_str0(NULL, NULL, "<level>", "none|error|warn|info|debug|verbose");
    log_args.levels = arg_lit0(NULL, "levels", "List log levels");
    log_args.help = arg_lit0("h", "help", "Show log command help");
    log_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "log",
        .help = "Runtime log level control",
        .hint = NULL,
        .func = &log_command,
        .argtable = &log_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
