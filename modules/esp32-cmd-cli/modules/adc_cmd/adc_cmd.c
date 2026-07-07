#include "adc_cmd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "board_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

static const char *TAG = "ESP32_CLI";

#define ADC_MAX_OPERANDS 2
#define ADC_DEFAULT_SAMPLES 16
#define ADC_MAX_SAMPLES 256
#define ADC_BATTERY_PIN BAT_ADC_PIN

typedef int (*adc_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    adc_func_t function;
    const char *description;
} adc_command_entry_t;

typedef enum {
    ADC_CALI_KIND_NONE = 0,
    ADC_CALI_KIND_CURVE,
    ADC_CALI_KIND_LINE,
} adc_cali_kind_t;

typedef struct {
    adc_oneshot_unit_handle_t unit_handle;
    adc_cali_handle_t cali_handle;
    adc_cali_kind_t cali_kind;
    adc_unit_t unit;
    adc_channel_t channel;
    adc_atten_t atten;
    int gpio;
} adc_read_context_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *operands;
    struct arg_int *samples;
    struct arg_int *atten;
    struct arg_int *scale;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} adc_args;

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

static bool parse_samples(uint32_t *samples)
{
    *samples = ADC_DEFAULT_SAMPLES;
    if (adc_args.samples->count == 0) {
        return true;
    }

    int value = adc_args.samples->ival[0];
    if (value < 1 || value > ADC_MAX_SAMPLES) {
        printf("samples must be 1..%u.\n", ADC_MAX_SAMPLES);
        return false;
    }

    *samples = (uint32_t) value;
    return true;
}

static bool parse_atten(adc_atten_t *atten)
{
    *atten = ADC_ATTEN_DB_12;
    if (adc_args.atten->count == 0) {
        return true;
    }

    int value = adc_args.atten->ival[0];
    switch (value) {
    case 0:
        *atten = ADC_ATTEN_DB_0;
        return true;
    case 2:
        *atten = ADC_ATTEN_DB_2_5;
        return true;
    case 6:
        *atten = ADC_ATTEN_DB_6;
        return true;
    case 11:
    case 12:
        *atten = ADC_ATTEN_DB_12;
        return true;
    default:
        printf("atten must be 0, 2, 6, 11 or 12 dB.\n");
        return false;
    }
}

static const char *atten_to_str(adc_atten_t atten)
{
    switch (atten) {
    case ADC_ATTEN_DB_0:
        return "0 dB";
    case ADC_ATTEN_DB_2_5:
        return "2.5 dB";
    case ADC_ATTEN_DB_6:
        return "6 dB";
    case ADC_ATTEN_DB_12:
        return "12 dB";
    default:
        return "unknown";
    }
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle, adc_cali_kind_t *out_kind)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    adc_cali_kind_t kind = ADC_CALI_KIND_NONE;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (kind == ADC_CALI_KIND_NONE) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            kind = ADC_CALI_KIND_CURVE;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (kind == ADC_CALI_KIND_NONE) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            kind = ADC_CALI_KIND_LINE;
        }
    }
#endif

    *out_handle = handle;
    *out_kind = kind;

    return ret == ESP_OK && kind != ADC_CALI_KIND_NONE;
}

static void adc_calibration_deinit(adc_cali_handle_t handle, adc_cali_kind_t kind)
{
    if (handle == NULL) {
        return;
    }

    switch (kind) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    case ADC_CALI_KIND_CURVE:
        adc_cali_delete_scheme_curve_fitting(handle);
        break;
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    case ADC_CALI_KIND_LINE:
        adc_cali_delete_scheme_line_fitting(handle);
        break;
#endif
    default:
        break;
    }
}

static esp_err_t adc_read_context_open(int gpio, adc_atten_t atten, adc_read_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->gpio = gpio;
    ctx->atten = atten;

    esp_err_t err = adc_oneshot_io_to_channel(gpio, &ctx->unit, &ctx->channel);
    if (err != ESP_OK) {
        printf("GPIO %d is not ADC-capable on this target.\n", gpio);
        return err;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ctx->unit,
    };
    err = adc_oneshot_new_unit(&init_config, &ctx->unit_handle);
    if (err != ESP_OK) {
        printf("Failed to create ADC unit %d: %s\n", (int) ctx->unit + 1, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(ctx->unit_handle, ctx->channel, &channel_config);
    if (err != ESP_OK) {
        printf("Failed to configure ADC%d channel %d: %s\n",
            (int) ctx->unit + 1,
            (int) ctx->channel,
            esp_err_to_name(err));
        adc_oneshot_del_unit(ctx->unit_handle);
        ctx->unit_handle = NULL;
        return err;
    }

    adc_calibration_init(ctx->unit, ctx->channel, atten, &ctx->cali_handle, &ctx->cali_kind);
    return ESP_OK;
}

static void adc_read_context_close(adc_read_context_t *ctx)
{
    adc_calibration_deinit(ctx->cali_handle, ctx->cali_kind);
    if (ctx->unit_handle != NULL) {
        adc_oneshot_del_unit(ctx->unit_handle);
    }
}

static esp_err_t adc_take_average(adc_read_context_t *ctx, uint32_t samples, int *raw_avg, int *mv_avg, bool *has_mv)
{
    int64_t raw_sum = 0;
    int64_t mv_sum = 0;
    uint32_t mv_count = 0;

    for (uint32_t i = 0; i < samples; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(ctx->unit_handle, ctx->channel, &raw);
        if (err != ESP_OK) {
            printf("ADC read failed: %s\n", esp_err_to_name(err));
            return err;
        }

        raw_sum += raw;

        if (ctx->cali_handle != NULL) {
            int mv = 0;
            err = adc_cali_raw_to_voltage(ctx->cali_handle, raw, &mv);
            if (err == ESP_OK) {
                mv_sum += mv;
                mv_count++;
            }
        }
    }

    *raw_avg = (int) (raw_sum / samples);
    *has_mv = mv_count > 0;
    *mv_avg = *has_mv ? (int) (mv_sum / mv_count) : 0;
    return ESP_OK;
}

static void print_adc_result(const adc_read_context_t *ctx, uint32_t samples, int raw_avg, int mv_avg, bool has_mv, uint32_t scale_milli)
{
    printf("\nADC read\n");
    printf("--------\n");
    printf("%-16s GPIO %d\n", "Input:", ctx->gpio);
    printf("%-16s ADC%d channel %d\n", "Route:", (int) ctx->unit + 1, (int) ctx->channel);
    printf("%-16s %s\n", "Attenuation:", atten_to_str(ctx->atten));
    printf("%-16s %" PRIu32 "\n", "Samples:", samples);
    printf("%-16s %d\n", "Raw avg:", raw_avg);
    if (has_mv) {
        printf("%-16s %d mV\n", "Pin voltage:", mv_avg);
        if (scale_milli != 1000) {
            int64_t scaled_mv = ((int64_t) mv_avg * scale_milli) / 1000;
            printf("%-16s %" PRId64 " mV  (scale x%" PRIu32 ".%03" PRIu32 ")\n",
                "Scaled:",
                scaled_mv,
                scale_milli / 1000,
                scale_milli % 1000);
        }
    } else {
        printf("%-16s unavailable\n", "Pin voltage:");
    }
    printf("\n");
}

static int adc_read_gpio(int gpio, uint32_t scale_milli)
{
    uint32_t samples = ADC_DEFAULT_SAMPLES;
    adc_atten_t atten = ADC_ATTEN_DB_12;

    if (!parse_samples(&samples) || !parse_atten(&atten)) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_read_context_t ctx;
    esp_err_t err = adc_read_context_open(gpio, atten, &ctx);
    if (err != ESP_OK) {
        return err;
    }

    int raw_avg = 0;
    int mv_avg = 0;
    bool has_mv = false;
    err = adc_take_average(&ctx, samples, &raw_avg, &mv_avg, &has_mv);
    if (err == ESP_OK) {
        print_adc_result(&ctx, samples, raw_avg, mv_avg, has_mv, scale_milli);
    }

    adc_read_context_close(&ctx);
    return err;
}

static int adc_read_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (adc_args.operands->count < 1) {
        printf("Usage: adc read <gpio> [--samples n] [--atten db]\n");
        return ESP_ERR_INVALID_ARG;
    }

    int gpio = -1;
    if (!parse_int_arg(adc_args.operands->sval[0], &gpio)) {
        printf("Invalid GPIO: %s\n", adc_args.operands->sval[0]);
        return ESP_ERR_INVALID_ARG;
    }

    return adc_read_gpio(gpio, 1000);
}

static int adc_battery_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    uint32_t scale_milli = 1000;
    if (adc_args.scale->count > 0) {
        int value = adc_args.scale->ival[0];
        if (value < 1 || value > 20000) {
            printf("scale must be 1..20000, where 1000 means x1.000.\n");
            return ESP_ERR_INVALID_ARG;
        }
        scale_milli = (uint32_t) value;
    }

    printf("Reading battery ADC preset on GPIO %d.\n", ADC_BATTERY_PIN);
    return adc_read_gpio(ADC_BATTERY_PIN, scale_milli);
}

static int adc_map_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    printf("\nADC-capable GPIO map\n");
    printf("--------------------\n");
    printf("%-8s %-8s %-8s\n", "GPIO", "UNIT", "CHANNEL");

    size_t found = 0;
    for (int gpio = 0; gpio < SOC_GPIO_PIN_COUNT; gpio++) {
        adc_unit_t unit = ADC_UNIT_1;
        adc_channel_t channel = ADC_CHANNEL_0;
        if (adc_oneshot_io_to_channel(gpio, &unit, &channel) == ESP_OK) {
            printf("%-8d ADC%-5d %-8d\n", gpio, (int) unit + 1, (int) channel);
            found++;
        }
    }

    if (found == 0) {
        printf("No ADC-capable GPIOs reported by driver.\n");
    }
    printf("\n");
    return ESP_OK;
}

static void print_adc_help(void);

static const adc_command_entry_t adc_cmds[] = {
    {"read", adc_read_command, "Read an ADC-capable GPIO"},
    {"battery", adc_battery_command, "Read board battery ADC preset"},
    {"batt", adc_battery_command, "Alias for battery"},
    {"map", adc_map_command, "List GPIO to ADC unit/channel mapping"},
};

#define ADC_CMD_COUNT (sizeof(adc_cmds) / sizeof(adc_cmds[0]))

static char adc_cmds_help[128] = {0};

static void generate_adc_cmds_help_text(void)
{
    strlcpy(adc_cmds_help, ": ", sizeof(adc_cmds_help));
    for (size_t i = 0; i < ADC_CMD_COUNT; i++) {
        strlcat(adc_cmds_help, adc_cmds[i].name, sizeof(adc_cmds_help));
        if (i < ADC_CMD_COUNT - 1) {
            strlcat(adc_cmds_help, "; ", sizeof(adc_cmds_help));
        }
    }
}

static void print_adc_commands(void)
{
    printf("Available adc commands:\n");
    for (size_t i = 0; i < ADC_CMD_COUNT; i++) {
        printf("  %-8s %s\n", adc_cmds[i].name, adc_cmds[i].description);
    }
}

static void print_adc_help(void)
{
    printf("\nUsage:\n");
    printf("  adc read <gpio> [--samples n] [--atten db]\n");
    printf("  adc battery [--samples n] [--atten db] [--scale milli]\n");
    printf("  adc batt [--samples n] [--atten db] [--scale milli]\n");
    printf("  adc map\n\n");
    print_adc_commands();
    printf("\nOptions:\n");
    printf("  --atten db       0, 2, 6, 11 or 12. Default: 12 dB\n");
    printf("  --samples n      Average 1..%u samples. Default: %u\n", ADC_MAX_SAMPLES, ADC_DEFAULT_SAMPLES);
    printf("  --scale milli    For battery: 1000 = x1.000, 2000 = x2.000\n\n");
    printf("Examples:\n");
    printf("  adc map\n");
    printf("  adc read 5\n");
    printf("  adc read 5 --samples 64\n");
    printf("  adc batt --scale 2000\n\n");
}

static int adc_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &adc_args);

    if (argc == 1 || adc_args.help->count > 0) {
        print_adc_help();
        return ESP_OK;
    }

    if (adc_args.list->count > 0) {
        print_adc_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, adc_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (adc_args.subcommand->count == 0 || adc_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `adc --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = adc_args.subcommand->sval[0];
    for (size_t i = 0; i < ADC_CMD_COUNT; i++) {
        if (strcmp(subcommand, adc_cmds[i].name) == 0) {
            return adc_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown adc subcommand: %s\n", subcommand);
    print_adc_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_adc_command(void)
{
    generate_adc_cmds_help_text();

    adc_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", adc_cmds_help);
    adc_args.operands = arg_strn(NULL, NULL, "<arg>", 0, ADC_MAX_OPERANDS, "Command arguments");
    adc_args.samples = arg_int0("n", "samples", "<n>", "Number of samples to average");
    adc_args.atten = arg_int0("a", "atten", "<db>", "ADC attenuation: 0, 2, 6, 11 or 12");
    adc_args.scale = arg_int0(NULL, "scale", "<milli>", "Scale mV result, 1000 = x1.000");
    adc_args.list = arg_lit0("l", "list", "List available adc subcommands");
    adc_args.help = arg_lit0("h", "help", "Show adc command help");
    adc_args.end = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command = "adc",
        .help = "ADC read, battery preset, and GPIO mapping",
        .hint = NULL,
        .func = &adc_command,
        .argtable = &adc_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
