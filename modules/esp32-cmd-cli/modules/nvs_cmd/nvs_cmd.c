#include "nvs_cmd.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ESP32_CLI";

#define NVS_MAX_OPERANDS 4
#define NVS_MAX_NAMESPACES 64
#define NVS_BLOB_PREVIEW_BYTES 16

typedef int (*nvs_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    nvs_func_t function;
    const char *description;
} nvs_command_entry_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *operands;
    struct arg_str *partition;
    struct arg_lit *yes;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} nvs_args;

static const char *nvs_partition_name(void)
{
    if (nvs_args.partition->count > 0 && nvs_args.partition->sval[0] != NULL) {
        return nvs_args.partition->sval[0];
    }

    return NVS_DEFAULT_PART_NAME;
}

static esp_err_t ensure_nvs_ready(const char *part_name)
{
    nvs_stats_t stats = {0};
    esp_err_t err = nvs_get_stats(part_name, &stats);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    if (err != ESP_ERR_NVS_NOT_INITIALIZED) {
        return err;
    }

    if (strcmp(part_name, NVS_DEFAULT_PART_NAME) == 0) {
        return nvs_flash_init();
    }

    return nvs_flash_init_partition(part_name);
}

static const char *nvs_type_to_str(nvs_type_t type)
{
    switch (type) {
    case NVS_TYPE_U8:
        return "u8";
    case NVS_TYPE_I8:
        return "i8";
    case NVS_TYPE_U16:
        return "u16";
    case NVS_TYPE_I16:
        return "i16";
    case NVS_TYPE_U32:
        return "u32";
    case NVS_TYPE_I32:
        return "i32";
    case NVS_TYPE_U64:
        return "u64";
    case NVS_TYPE_I64:
        return "i64";
    case NVS_TYPE_STR:
        return "str";
    case NVS_TYPE_BLOB:
        return "blob";
    default:
        return "unknown";
    }
}

static bool parse_nvs_type(const char *text, nvs_type_t *type)
{
    if (text == NULL || type == NULL) {
        return false;
    }

    if (strcasecmp(text, "u8") == 0) {
        *type = NVS_TYPE_U8;
    } else if (strcasecmp(text, "i8") == 0) {
        *type = NVS_TYPE_I8;
    } else if (strcasecmp(text, "u16") == 0) {
        *type = NVS_TYPE_U16;
    } else if (strcasecmp(text, "i16") == 0) {
        *type = NVS_TYPE_I16;
    } else if (strcasecmp(text, "u32") == 0) {
        *type = NVS_TYPE_U32;
    } else if (strcasecmp(text, "i32") == 0) {
        *type = NVS_TYPE_I32;
    } else if (strcasecmp(text, "u64") == 0) {
        *type = NVS_TYPE_U64;
    } else if (strcasecmp(text, "i64") == 0) {
        *type = NVS_TYPE_I64;
    } else if (strcasecmp(text, "str") == 0 || strcasecmp(text, "string") == 0) {
        *type = NVS_TYPE_STR;
    } else if (strcasecmp(text, "blob") == 0 || strcasecmp(text, "hex") == 0) {
        *type = NVS_TYPE_BLOB;
    } else {
        return false;
    }

    return true;
}

static bool parse_i64(const char *text, int64_t *value)
{
    char *end = NULL;
    long long parsed = strtoll(text, &end, 0);
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }

    *value = (int64_t) parsed;
    return true;
}

static bool parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (text == NULL || end == text || *end != '\0') {
        return false;
    }

    *value = (uint64_t) parsed;
    return true;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static esp_err_t parse_hex_blob(const char *text, uint8_t **data, size_t *len)
{
    if (text == NULL || data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *hex = text;
    if (strncmp(hex, "0x", 2) == 0 || strncmp(hex, "0X", 2) == 0) {
        hex += 2;
    }

    size_t hex_len = strlen(hex);
    if (hex_len == 0 || (hex_len % 2) != 0) {
        printf("Blob value must be an even-length hex string, for example: 0A12ff\n");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *buffer = malloc(hex_len / 2);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < hex_len; i += 2) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            free(buffer);
            printf("Invalid hex character in blob value.\n");
            return ESP_ERR_INVALID_ARG;
        }
        buffer[i / 2] = (uint8_t) ((hi << 4) | lo);
    }

    *data = buffer;
    *len = hex_len / 2;
    return ESP_OK;
}

static esp_err_t find_entry_info(const char *part_name, const char *namespace_name, const char *key, nvs_entry_info_t *out_info)
{
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(part_name, namespace_name, NVS_TYPE_ANY, &it);
    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info = {0};
        esp_err_t info_err = nvs_entry_info(it, &info);
        if (info_err == ESP_OK && strcmp(info.key, key) == 0) {
            *out_info = info;
            nvs_release_iterator(it);
            return ESP_OK;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return ESP_ERR_NVS_NOT_FOUND;
}

static void print_blob_preview(const uint8_t *data, size_t len)
{
    size_t preview_len = len < NVS_BLOB_PREVIEW_BYTES ? len : NVS_BLOB_PREVIEW_BYTES;
    printf("0x");
    for (size_t i = 0; i < preview_len; i++) {
        printf("%02x", data[i]);
    }
    if (preview_len < len) {
        printf("...");
    }
}

static esp_err_t print_entry_value(const char *part_name, const nvs_entry_info_t *info, bool verbose)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(part_name, info->namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        printf("<open failed: %s>", esp_err_to_name(err));
        return err;
    }

    switch (info->type) {
    case NVS_TYPE_U8: {
        uint8_t value = 0;
        err = nvs_get_u8(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%u", (unsigned) value);
        }
        break;
    }
    case NVS_TYPE_I8: {
        int8_t value = 0;
        err = nvs_get_i8(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%d", (int) value);
        }
        break;
    }
    case NVS_TYPE_U16: {
        uint16_t value = 0;
        err = nvs_get_u16(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%u", (unsigned) value);
        }
        break;
    }
    case NVS_TYPE_I16: {
        int16_t value = 0;
        err = nvs_get_i16(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%d", (int) value);
        }
        break;
    }
    case NVS_TYPE_U32: {
        uint32_t value = 0;
        err = nvs_get_u32(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%" PRIu32, value);
        }
        break;
    }
    case NVS_TYPE_I32: {
        int32_t value = 0;
        err = nvs_get_i32(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%" PRId32, value);
        }
        break;
    }
    case NVS_TYPE_U64: {
        uint64_t value = 0;
        err = nvs_get_u64(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%" PRIu64, value);
        }
        break;
    }
    case NVS_TYPE_I64: {
        int64_t value = 0;
        err = nvs_get_i64(handle, info->key, &value);
        if (err == ESP_OK) {
            printf("%" PRId64, value);
        }
        break;
    }
    case NVS_TYPE_STR: {
        size_t len = 0;
        err = nvs_get_str(handle, info->key, NULL, &len);
        if (err == ESP_OK) {
            char *value = malloc(len);
            if (value == NULL) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            err = nvs_get_str(handle, info->key, value, &len);
            if (err == ESP_OK) {
                printf("\"%s\"", value);
            }
            free(value);
        }
        break;
    }
    case NVS_TYPE_BLOB: {
        size_t len = 0;
        err = nvs_get_blob(handle, info->key, NULL, &len);
        if (err == ESP_OK) {
            uint8_t *value = malloc(len);
            if (value == NULL) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            err = nvs_get_blob(handle, info->key, value, &len);
            if (err == ESP_OK) {
                if (verbose) {
                    print_blob_preview(value, len);
                } else {
                    printf("<blob:%u B> ", (unsigned) len);
                    print_blob_preview(value, len);
                }
            }
            free(value);
        }
        break;
    }
    default:
        err = ESP_ERR_NVS_TYPE_MISMATCH;
        break;
    }

    if (err != ESP_OK) {
        printf("<read failed: %s>", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

static int nvs_stats_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    const char *part_name = nvs_partition_name();
    esp_err_t err = ensure_nvs_ready(part_name);
    if (err != ESP_OK) {
        printf("NVS init failed for partition '%s': %s\n", part_name, esp_err_to_name(err));
        return err;
    }

    nvs_stats_t stats = {0};
    err = nvs_get_stats(part_name, &stats);
    if (err != ESP_OK) {
        printf("Failed to read NVS stats: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("\nNVS stats (%s)\n", part_name);
    printf("---------------\n");
    printf("%-18s %u\n", "Used entries:", (unsigned) stats.used_entries);
    printf("%-18s %u\n", "Free entries:", (unsigned) stats.free_entries);
    printf("%-18s %u\n", "All entries:", (unsigned) stats.total_entries);
    printf("%-18s %u\n", "Namespaces:", (unsigned) stats.namespace_count);
    printf("\n");
    return ESP_OK;
}

static bool namespace_seen(char namespaces[][NVS_NS_NAME_MAX_SIZE], size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(namespaces[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static int nvs_namespaces_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    const char *part_name = nvs_partition_name();
    esp_err_t ready_err = ensure_nvs_ready(part_name);
    if (ready_err != ESP_OK) {
        printf("NVS init failed for partition '%s': %s\n", part_name, esp_err_to_name(ready_err));
        return ready_err;
    }

    char namespaces[NVS_MAX_NAMESPACES][NVS_NS_NAME_MAX_SIZE] = {0};
    size_t namespace_count = 0;
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(part_name, NULL, NVS_TYPE_ANY, &it);

    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info = {0};
        if (nvs_entry_info(it, &info) == ESP_OK &&
            !namespace_seen(namespaces, namespace_count, info.namespace_name) &&
            namespace_count < NVS_MAX_NAMESPACES) {
            strlcpy(namespaces[namespace_count], info.namespace_name, NVS_NS_NAME_MAX_SIZE);
            namespace_count++;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
        printf("Failed to list NVS namespaces: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("\nNVS namespaces (%s)\n", part_name);
    printf("--------------------\n");
    for (size_t i = 0; i < namespace_count; i++) {
        nvs_handle_t handle = 0;
        size_t used_entries = 0;
        esp_err_t open_err = nvs_open_from_partition(part_name, namespaces[i], NVS_READONLY, &handle);
        if (open_err == ESP_OK) {
            nvs_get_used_entry_count(handle, &used_entries);
            nvs_close(handle);
        }
        printf("  %-16s %u entries\n", namespaces[i], (unsigned) used_entries);
    }
    printf("Namespaces shown: %u\n\n", (unsigned) namespace_count);
    return ESP_OK;
}

static int list_entries(const char *namespace_filter, const char *text_filter)
{
    const char *part_name = nvs_partition_name();
    esp_err_t ready_err = ensure_nvs_ready(part_name);
    if (ready_err != ESP_OK) {
        printf("NVS init failed for partition '%s': %s\n", part_name, esp_err_to_name(ready_err));
        return ready_err;
    }

    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find(part_name, namespace_filter, NVS_TYPE_ANY, &it);
    size_t count = 0;

    printf("\nNVS entries (%s)\n", part_name);
    printf("-----------------\n");
    printf("%-16s %-16s %-6s %s\n", "Namespace", "Key", "Type", "Value");
    printf("%-16s %-16s %-6s %s\n", "----------------", "----------------", "------", "----------------");

    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info = {0};
        if (nvs_entry_info(it, &info) == ESP_OK) {
            bool matches_text = text_filter == NULL ||
                strstr(info.namespace_name, text_filter) != NULL ||
                strstr(info.key, text_filter) != NULL;
            if (matches_text) {
                printf("%-16s %-16s %-6s ",
                    info.namespace_name,
                    info.key,
                    nvs_type_to_str(info.type));
                print_entry_value(part_name, &info, false);
                printf("\n");
                count++;
            }
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
        printf("Failed to list NVS entries: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Entries shown: %u\n\n", (unsigned) count);
    return ESP_OK;
}

static int nvs_list_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    const char *namespace_filter = nvs_args.operands->count > 0 ? nvs_args.operands->sval[0] : NULL;
    return list_entries(namespace_filter, NULL);
}

static int nvs_find_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (nvs_args.operands->count < 1) {
        printf("Usage: nvs find <text> [--part nvs]\n");
        return ESP_ERR_INVALID_ARG;
    }

    return list_entries(NULL, nvs_args.operands->sval[0]);
}

static int nvs_get_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (nvs_args.operands->count < 2) {
        printf("Usage: nvs get <namespace> <key> [--part nvs]\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *part_name = nvs_partition_name();
    const char *namespace_name = nvs_args.operands->sval[0];
    const char *key = nvs_args.operands->sval[1];
    nvs_entry_info_t info = {0};
    esp_err_t err = find_entry_info(part_name, namespace_name, key, &info);
    if (err != ESP_OK) {
        printf("NVS key not found: %s/%s\n", namespace_name, key);
        return err;
    }

    printf("%s/%s (%s) = ", namespace_name, key, nvs_type_to_str(info.type));
    print_entry_value(part_name, &info, true);
    printf("\n");
    return ESP_OK;
}

static esp_err_t set_nvs_value(nvs_handle_t handle, const char *key, nvs_type_t type, const char *value_text)
{
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0;

    switch (type) {
    case NVS_TYPE_U8:
        if (!parse_u64(value_text, &unsigned_value) || unsigned_value > UINT8_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_u8(handle, key, (uint8_t) unsigned_value);
    case NVS_TYPE_I8:
        if (!parse_i64(value_text, &signed_value) || signed_value < INT8_MIN || signed_value > INT8_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_i8(handle, key, (int8_t) signed_value);
    case NVS_TYPE_U16:
        if (!parse_u64(value_text, &unsigned_value) || unsigned_value > UINT16_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_u16(handle, key, (uint16_t) unsigned_value);
    case NVS_TYPE_I16:
        if (!parse_i64(value_text, &signed_value) || signed_value < INT16_MIN || signed_value > INT16_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_i16(handle, key, (int16_t) signed_value);
    case NVS_TYPE_U32:
        if (!parse_u64(value_text, &unsigned_value) || unsigned_value > UINT32_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_u32(handle, key, (uint32_t) unsigned_value);
    case NVS_TYPE_I32:
        if (!parse_i64(value_text, &signed_value) || signed_value < INT32_MIN || signed_value > INT32_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_i32(handle, key, (int32_t) signed_value);
    case NVS_TYPE_U64:
        if (!parse_u64(value_text, &unsigned_value)) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_u64(handle, key, unsigned_value);
    case NVS_TYPE_I64:
        if (!parse_i64(value_text, &signed_value)) {
            return ESP_ERR_INVALID_ARG;
        }
        return nvs_set_i64(handle, key, signed_value);
    case NVS_TYPE_STR:
        return nvs_set_str(handle, key, value_text);
    case NVS_TYPE_BLOB: {
        uint8_t *blob = NULL;
        size_t blob_len = 0;
        esp_err_t err = parse_hex_blob(value_text, &blob, &blob_len);
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_set_blob(handle, key, blob, blob_len);
        free(blob);
        return err;
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static int nvs_set_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (nvs_args.operands->count < 4) {
        printf("Usage: nvs set <namespace> <key> <type> <value> [--part nvs]\n");
        printf("Types: u8 i8 u16 i16 u32 i32 u64 i64 str blob\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *part_name = nvs_partition_name();
    const char *namespace_name = nvs_args.operands->sval[0];
    const char *key = nvs_args.operands->sval[1];
    const char *type_text = nvs_args.operands->sval[2];
    const char *value_text = nvs_args.operands->sval[3];

    nvs_type_t type;
    if (!parse_nvs_type(type_text, &type)) {
        printf("Unknown NVS type: %s\n", type_text);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition(part_name, namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("Failed to open NVS namespace '%s': %s\n", namespace_name, esp_err_to_name(err));
        return err;
    }

    err = set_nvs_value(handle, key, type, value_text);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        printf("Failed to set %s/%s: %s\n", namespace_name, key, esp_err_to_name(err));
        return err;
    }

    printf("NVS value set: %s/%s (%s)\n", namespace_name, key, nvs_type_to_str(type));
    return ESP_OK;
}

static bool require_yes(const char *what)
{
    if (nvs_args.yes->count > 0) {
        return true;
    }

    printf("Refusing to erase %s without --yes.\n", what);
    return false;
}

static int nvs_erase_key_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (nvs_args.operands->count < 2) {
        printf("Usage: nvs erase-key <namespace> <key> --yes [--part nvs]\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (!require_yes("one NVS key")) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *part_name = nvs_partition_name();
    const char *namespace_name = nvs_args.operands->sval[0];
    const char *key = nvs_args.operands->sval[1];
    nvs_handle_t handle = 0;

    esp_err_t err = nvs_open_from_partition(part_name, namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("Failed to open NVS namespace '%s': %s\n", namespace_name, esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        printf("Failed to erase %s/%s: %s\n", namespace_name, key, esp_err_to_name(err));
        return err;
    }

    printf("Erased NVS key: %s/%s\n", namespace_name, key);
    return ESP_OK;
}

static int nvs_erase_namespace_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (nvs_args.operands->count < 1) {
        printf("Usage: nvs erase-namespace <namespace> --yes [--part nvs]\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (!require_yes("one NVS namespace")) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *part_name = nvs_partition_name();
    const char *namespace_name = nvs_args.operands->sval[0];
    nvs_handle_t handle = 0;

    esp_err_t err = nvs_open_from_partition(part_name, namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("Failed to open NVS namespace '%s': %s\n", namespace_name, esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        printf("Failed to erase namespace %s: %s\n", namespace_name, esp_err_to_name(err));
        return err;
    }

    printf("Erased NVS namespace entries: %s\n", namespace_name);
    return ESP_OK;
}

static int nvs_erase_partition_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    const char *part_name = nvs_partition_name();
    if (!require_yes("the full NVS partition")) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = strcmp(part_name, NVS_DEFAULT_PART_NAME) == 0 ? nvs_flash_deinit() : nvs_flash_deinit_partition(part_name);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
        printf("Failed to deinit NVS partition '%s': %s\n", part_name, esp_err_to_name(err));
        return err;
    }

    err = strcmp(part_name, NVS_DEFAULT_PART_NAME) == 0 ? nvs_flash_erase() : nvs_flash_erase_partition(part_name);
    if (err != ESP_OK) {
        printf("Failed to erase NVS partition '%s': %s\n", part_name, esp_err_to_name(err));
        return err;
    }

    err = strcmp(part_name, NVS_DEFAULT_PART_NAME) == 0 ? nvs_flash_init() : nvs_flash_init_partition(part_name);
    if (err != ESP_OK) {
        printf("NVS partition erased, but reinit failed: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Erased and reinitialized NVS partition: %s\n", part_name);
    return ESP_OK;
}

static void print_nvs_help(void);

static const nvs_command_entry_t nvs_cmds[] = {
    {"stats", nvs_stats_command, "Show NVS entry statistics"},
    {"namespaces", nvs_namespaces_command, "List namespaces and entry counts"},
    {"list", nvs_list_command, "List entries, optionally from one namespace"},
    {"find", nvs_find_command, "Search namespace/key names"},
    {"get", nvs_get_command, "Read one value"},
    {"set", nvs_set_command, "Write one value"},
    {"erase-key", nvs_erase_key_command, "Erase one key; requires --yes"},
    {"erase-namespace", nvs_erase_namespace_command, "Erase all entries in a namespace; requires --yes"},
    {"erase-partition", nvs_erase_partition_command, "Erase the full NVS partition; requires --yes"},
};

#define NVS_CMD_COUNT (sizeof(nvs_cmds) / sizeof(nvs_cmds[0]))

static char nvs_cmds_help[192] = {0};

static void generate_nvs_cmds_help_text(void)
{
    strlcpy(nvs_cmds_help, ": ", sizeof(nvs_cmds_help));
    for (size_t i = 0; i < NVS_CMD_COUNT; i++) {
        strlcat(nvs_cmds_help, nvs_cmds[i].name, sizeof(nvs_cmds_help));
        if (i < NVS_CMD_COUNT - 1) {
            strlcat(nvs_cmds_help, "; ", sizeof(nvs_cmds_help));
        }
    }
}

static void print_nvs_commands(void)
{
    printf("Available nvs commands:\n");
    for (size_t i = 0; i < NVS_CMD_COUNT; i++) {
        printf("  %-16s %s\n", nvs_cmds[i].name, nvs_cmds[i].description);
    }
}

static void print_nvs_help(void)
{
    printf("\nUsage:\n");
    printf("  nvs stats [--part nvs]\n");
    printf("  nvs namespaces [--part nvs]\n");
    printf("  nvs list [namespace] [--part nvs]\n");
    printf("  nvs find <text> [--part nvs]\n");
    printf("  nvs get <namespace> <key> [--part nvs]\n");
    printf("  nvs set <namespace> <key> <type> <value> [--part nvs]\n");
    printf("  nvs erase-key <namespace> <key> --yes [--part nvs]\n");
    printf("  nvs erase-namespace <namespace> --yes [--part nvs]\n");
    printf("  nvs erase-partition --yes [--part nvs]\n\n");
    print_nvs_commands();
    printf("\nTypes:\n");
    printf("  u8 i8 u16 i16 u32 i32 u64 i64 str blob\n");
    printf("\nExamples:\n");
    printf("  nvs stats\n");
    printf("  nvs list wifi\n");
    printf("  nvs find ssid\n");
    printf("  nvs get wifi ssid\n");
    printf("  nvs set app boot_count u32 12\n");
    printf("  nvs set app token str hello\n");
    printf("  nvs set app raw blob 0A12ff\n");
    printf("  nvs erase-key app token --yes\n\n");
}

static int nvs_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &nvs_args);

    if (argc == 1 || nvs_args.help->count > 0) {
        print_nvs_help();
        return ESP_OK;
    }

    if (nvs_args.list->count > 0) {
        print_nvs_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, nvs_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (nvs_args.subcommand->count == 0 || nvs_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `nvs --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = nvs_args.subcommand->sval[0];
    for (size_t i = 0; i < NVS_CMD_COUNT; i++) {
        if (strcmp(subcommand, nvs_cmds[i].name) == 0) {
            return nvs_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown nvs subcommand: %s\n", subcommand);
    print_nvs_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_nvs_command(void)
{
    generate_nvs_cmds_help_text();

    nvs_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", nvs_cmds_help);
    nvs_args.operands = arg_strn(NULL, NULL, "<arg>", 0, NVS_MAX_OPERANDS, "Command arguments");
    nvs_args.partition = arg_str0(NULL, "part", "<partition>", "NVS partition label; default: nvs");
    nvs_args.yes = arg_lit0(NULL, "yes", "Confirm destructive erase commands");
    nvs_args.list = arg_lit0("l", "list", "List available nvs subcommands");
    nvs_args.help = arg_lit0("h", "help", "Show nvs command help");
    nvs_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "nvs",
        .help = "NVS inspect, read, write, and erase",
        .hint = NULL,
        .func = &nvs_command,
        .argtable = &nvs_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
