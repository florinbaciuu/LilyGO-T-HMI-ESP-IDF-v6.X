#include "part_cmd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ESP32_CLI";

typedef int (*part_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    part_func_t function;
    const char *description;
} part_command_entry_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *label;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} part_args;

static const char *partition_type_to_str(esp_partition_type_t type)
{
    switch (type) {
    case ESP_PARTITION_TYPE_APP:
        return "app";
    case ESP_PARTITION_TYPE_DATA:
        return "data";
    case ESP_PARTITION_TYPE_BOOTLOADER:
        return "boot";
    case ESP_PARTITION_TYPE_PARTITION_TABLE:
        return "ptable";
    default:
        return "custom";
    }
}

static const char *app_subtype_to_str(esp_partition_subtype_t subtype)
{
    switch (subtype) {
    case ESP_PARTITION_SUBTYPE_APP_FACTORY:
        return "factory";
    case ESP_PARTITION_SUBTYPE_APP_TEST:
        return "test";
    default:
        if (subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            static char ota_name[8];
            snprintf(ota_name, sizeof(ota_name), "ota_%u", (unsigned) (subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN));
            return ota_name;
        }
        return "app";
    }
}

static const char *data_subtype_to_str(esp_partition_subtype_t subtype)
{
    switch (subtype) {
    case ESP_PARTITION_SUBTYPE_DATA_OTA:
        return "ota";
    case ESP_PARTITION_SUBTYPE_DATA_PHY:
        return "phy";
    case ESP_PARTITION_SUBTYPE_DATA_NVS:
        return "nvs";
    case ESP_PARTITION_SUBTYPE_DATA_COREDUMP:
        return "coredump";
    case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS:
        return "nvs_keys";
    case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM:
        return "efuse_em";
    case ESP_PARTITION_SUBTYPE_DATA_UNDEFINED:
        return "undefined";
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
        return "fat";
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
        return "spiffs";
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
        return "littlefs";
    default:
        return "data";
    }
}

static const char *partition_subtype_to_str(esp_partition_type_t type, esp_partition_subtype_t subtype)
{
    if (type == ESP_PARTITION_TYPE_APP) {
        return app_subtype_to_str(subtype);
    }
    if (type == ESP_PARTITION_TYPE_DATA) {
        return data_subtype_to_str(subtype);
    }

    return "n/a";
}

static void format_size(size_t bytes, char *buffer, size_t buffer_len)
{
    if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_len, "%.2f MiB", (double) bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_len, "%.2f KiB", (double) bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_len, "%u B", (unsigned) bytes);
    }
}

static void print_partition_header(void)
{
    printf("%-1s %-16s %-7s %-10s %-8s %-10s %-10s %-5s %-3s\n",
        "",
        "Name",
        "Type",
        "Subtype",
        "SubHex",
        "Offset",
        "Size",
        "Enc",
        "RO");
    printf("%-1s %-16s %-7s %-10s %-8s %-10s %-10s %-5s %-3s\n",
        "-",
        "----------------",
        "-------",
        "----------",
        "--------",
        "----------",
        "----------",
        "-----",
        "---");
}

static void print_partition_row(const esp_partition_t *partition, const esp_partition_t *running)
{
    char size_text[16];
    format_size(partition->size, size_text, sizeof(size_text));

    printf("%-1s %-16s %-7s %-10s 0x%02x     0x%06" PRIx32 "   %-10s %-5s %-3s\n",
        partition == running ? "*" : "",
        partition->label,
        partition_type_to_str(partition->type),
        partition_subtype_to_str(partition->type, partition->subtype),
        (unsigned) partition->subtype,
        partition->address,
        size_text,
        partition->encrypted ? "yes" : "no",
        partition->readonly ? "yes" : "no");
}

static int print_partitions(esp_partition_type_t type, const char *label)
{
    esp_partition_iterator_t it = esp_partition_find(type, ESP_PARTITION_SUBTYPE_ANY, label);
    if (it == NULL) {
        printf("No partitions found");
        if (label != NULL) {
            printf(" for label '%s'", label);
        }
        printf(".\n");
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t count = 0;

    print_partition_header();
    while (it != NULL) {
        const esp_partition_t *partition = esp_partition_get(it);
        if (partition != NULL) {
            print_partition_row(partition, running);
            count++;
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    printf("\nPartitions shown: %u\n", (unsigned) count);
    if (running != NULL) {
        printf("Running app: %s at 0x%06" PRIx32 "\n", running->label, running->address);
    }
    printf("\n");
    return ESP_OK;
}

static int part_list_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nPartition table\n");
    printf("---------------\n");
    return print_partitions(ESP_PARTITION_TYPE_ANY, NULL);
}

static int part_app_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nApp partitions\n");
    printf("--------------\n");
    return print_partitions(ESP_PARTITION_TYPE_APP, NULL);
}

static int part_data_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nData partitions\n");
    printf("---------------\n");
    return print_partitions(ESP_PARTITION_TYPE_DATA, NULL);
}

static int part_find_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (part_args.label->count == 0 || part_args.label->sval[0] == NULL) {
        printf("Usage: part find <name>\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *label = part_args.label->sval[0];
    printf("\nPartition: %s\n", label);
    printf("----------------\n");
    return print_partitions(ESP_PARTITION_TYPE_ANY, label);
}

static void print_part_help(void);

static const part_command_entry_t part_cmds[] = {
    {"list", part_list_command, "Show all partitions"},
    {"app", part_app_command, "Show app partitions"},
    {"data", part_data_command, "Show data partitions"},
    {"find", part_find_command, "Find partition by label"},
};

#define PART_CMD_COUNT (sizeof(part_cmds) / sizeof(part_cmds[0]))

static char part_cmds_help[128] = {0};

static void generate_part_cmds_help_text(void)
{
    strlcpy(part_cmds_help, ": ", sizeof(part_cmds_help));
    for (size_t i = 0; i < PART_CMD_COUNT; i++) {
        strlcat(part_cmds_help, part_cmds[i].name, sizeof(part_cmds_help));
        if (i < PART_CMD_COUNT - 1) {
            strlcat(part_cmds_help, "; ", sizeof(part_cmds_help));
        }
    }
}

static void print_part_commands(void)
{
    printf("Available part commands:\n");
    for (size_t i = 0; i < PART_CMD_COUNT; i++) {
        printf("  %-8s %s\n", part_cmds[i].name, part_cmds[i].description);
    }
}

static void print_part_help(void)
{
    printf("\nUsage:\n");
    printf("  part list\n");
    printf("  part app\n");
    printf("  part data\n");
    printf("  part find <name>\n\n");
    print_part_commands();
    printf("\nExamples:\n");
    printf("  part list\n");
    printf("  part find nvs\n");
    printf("  part find storage\n\n");
}

static int part_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &part_args);

    if (argc == 1 || part_args.help->count > 0) {
        print_part_help();
        return ESP_OK;
    }

    if (part_args.list->count > 0) {
        print_part_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, part_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (part_args.subcommand->count == 0 || part_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `part --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = part_args.subcommand->sval[0];
    if (part_args.label->count > 0 && strcmp(subcommand, "find") != 0) {
        printf("Partition label is only supported by `part find`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < PART_CMD_COUNT; i++) {
        if (strcmp(subcommand, part_cmds[i].name) == 0) {
            return part_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown part subcommand: %s\n", subcommand);
    print_part_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_part_command(void)
{
    generate_part_cmds_help_text();

    part_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", part_cmds_help);
    part_args.label = arg_str0(NULL, NULL, "<name>", "Partition label for `part find`");
    part_args.list = arg_lit0("l", "list", "List available part subcommands");
    part_args.help = arg_lit0("h", "help", "Show part command help");
    part_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "part",
        .help = "Partition table diagnostics",
        .hint = NULL,
        .func = &part_command,
        .argtable = &part_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
