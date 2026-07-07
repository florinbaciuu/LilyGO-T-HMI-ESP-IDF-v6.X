#include "wifi_cmd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "ESP32_CLI";

#define WIFI_MAX_OPERANDS 4
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_SCAN_MAX_APS 32

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef int (*wifi_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    wifi_func_t function;
    const char *description;
} wifi_command_entry_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *operands;
    struct arg_int *timeout;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} wifi_args;

static bool s_wifi_initialized;
static bool s_wifi_started;
static esp_netif_t *s_sta_netif;
static EventGroupHandle_t s_wifi_events;
static wifi_err_reason_t s_last_disconnect_reason;

static const char *auth_mode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "wep";
    case WIFI_AUTH_WPA_PSK:
        return "wpa";
    case WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "wpa/wpa2";
    case WIFI_AUTH_ENTERPRISE:
        return "enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK:
        return "wapi";
    default:
        return "unknown";
    }
}

static const char *wifi_reason_to_str(wifi_err_reason_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "auth expired";
    case WIFI_REASON_AUTH_FAIL:
        return "auth failed";
    case WIFI_REASON_NO_AP_FOUND:
        return "no AP found";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL:
        return "connection failed";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon timeout";
    case WIFI_REASON_ASSOC_LEAVE:
        return "association left";
    default:
        return "see numeric reason";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *) event_data;
        s_last_disconnect_reason = event ? event->reason : 0;
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
    }
}

static esp_err_t ensure_wifi_ready(void)
{
    if (s_wifi_initialized) {
        if (s_wifi_started) {
            return ESP_OK;
        }

        esp_err_t err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
            printf("esp_wifi_start failed: %s\n", esp_err_to_name(err));
            return err;
        }

        s_wifi_started = true;
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        printf("NVS init failed: %s\n", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("esp_netif_init failed: %s\n", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("esp_event_loop_create_default failed: %s\n", esp_err_to_name(err));
        return err;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            printf("Failed to create default Wi-Fi STA netif\n");
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        printf("esp_wifi_init failed: %s\n", esp_err_to_name(err));
        return err;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        printf("esp_wifi_start failed: %s\n", esp_err_to_name(err));
        return err;
    }

    s_wifi_initialized = true;
    s_wifi_started = true;
    return ESP_OK;
}

static int compare_scan_by_rssi(const void *lhs, const void *rhs)
{
    const wifi_ap_record_t *a = (const wifi_ap_record_t *) lhs;
    const wifi_ap_record_t *b = (const wifi_ap_record_t *) rhs;
    return b->rssi - a->rssi;
}

static void print_scan_header(void)
{
    printf("%-3s %-32s %-18s %-4s %-5s %-10s\n", "#", "SSID", "BSSID", "CH", "RSSI", "AUTH");
    printf("%-3s %-32s %-18s %-4s %-5s %-10s\n", "---", "--------------------------------", "------------------", "----", "-----", "----------");
}

static void print_ap_record(size_t index, const wifi_ap_record_t *ap)
{
    char ssid[33] = {0};
    memcpy(ssid, ap->ssid, sizeof(ap->ssid));
    if (ssid[0] == '\0') {
        strlcpy(ssid, "<hidden>", sizeof(ssid));
    }

    printf("%-3u %-32.32s " MACSTR " %-4u %-5d %-10s\n",
        (unsigned) index,
        ssid,
        MAC2STR(ap->bssid),
        (unsigned) ap->primary,
        ap->rssi,
        auth_mode_to_str(ap->authmode));
}

static int wifi_scan_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_err_t err = ensure_wifi_ready();
    if (err != ESP_OK) {
        return err;
    }

    const char *filter = wifi_args.operands->count > 0 ? wifi_args.operands->sval[0] : NULL;
    printf("Scanning Wi-Fi%s%s%s...\n", filter ? " for '" : "", filter ? filter : "", filter ? "'" : "");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        printf("Wi-Fi scan failed: %s\n", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count == 0) {
        printf("No APs found.\n");
        return ESP_OK;
    }

    uint16_t record_count = ap_count > WIFI_SCAN_MAX_APS ? WIFI_SCAN_MAX_APS : ap_count;
    wifi_ap_record_t *records = calloc(record_count, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&record_count, records);
    if (err != ESP_OK) {
        free(records);
        printf("Failed to read scan results: %s\n", esp_err_to_name(err));
        return err;
    }

    qsort(records, record_count, sizeof(records[0]), compare_scan_by_rssi);

    print_scan_header();
    size_t shown = 0;
    for (uint16_t i = 0; i < record_count; i++) {
        const char *ssid = (const char *) records[i].ssid;
        bool matches = filter == NULL || strstr(ssid, filter) != NULL;
        if (matches) {
            shown++;
            print_ap_record(shown, &records[i]);
        }
    }

    printf("APs shown: %u / %u found\n", (unsigned) shown, (unsigned) ap_count);
    free(records);
    return ESP_OK;
}

static int wifi_status_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_err_t err = ensure_wifi_ready();
    if (err != ESP_OK) {
        return err;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);

    wifi_ap_record_t ap = {0};
    err = esp_wifi_sta_get_ap_info(&ap);

    printf("\nWi-Fi status\n");
    printf("------------\n");
    printf("%-16s %s\n", "Mode:", mode == WIFI_MODE_STA ? "STA" : "other");

    if (err == ESP_OK) {
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_get_ip_info(s_sta_netif, &ip_info);

        printf("%-16s connected\n", "State:");
        printf("%-16s %s\n", "SSID:", ap.ssid);
        printf("%-16s " MACSTR "\n", "BSSID:", MAC2STR(ap.bssid));
        printf("%-16s %u\n", "Channel:", (unsigned) ap.primary);
        printf("%-16s %d dBm\n", "RSSI:", ap.rssi);
        printf("%-16s " IPSTR "\n", "IP:", IP2STR(&ip_info.ip));
        printf("%-16s " IPSTR "\n", "Gateway:", IP2STR(&ip_info.gw));
        printf("%-16s " IPSTR "\n", "Netmask:", IP2STR(&ip_info.netmask));
    } else {
        printf("%-16s disconnected\n", "State:");
        printf("%-16s %u (%s)\n",
            "Last reason:",
            (unsigned) s_last_disconnect_reason,
            wifi_reason_to_str(s_last_disconnect_reason));
    }
    printf("\n");
    return ESP_OK;
}

static bool parse_timeout_ms(uint32_t *timeout_ms)
{
    *timeout_ms = WIFI_CONNECT_TIMEOUT_MS;
    if (wifi_args.timeout->count == 0) {
        return true;
    }

    int value = wifi_args.timeout->ival[0];
    if (value < 1000 || value > 60000) {
        printf("Timeout must be 1000..60000 ms.\n");
        return false;
    }

    *timeout_ms = (uint32_t) value;
    return true;
}

static int wifi_connect_common(bool open_network)
{
    if (wifi_args.operands->count < 1 || (!open_network && wifi_args.operands->count < 2)) {
        printf("Usage: %s\n", open_network ? "wifi connect-open <ssid> [--timeout ms]" : "wifi connect <ssid> <password> [--timeout ms]");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t timeout_ms = WIFI_CONNECT_TIMEOUT_MS;
    if (!parse_timeout_ms(&timeout_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_wifi_ready();
    if (err != ESP_OK) {
        return err;
    }

    const char *ssid = wifi_args.operands->sval[0];
    const char *password = open_network ? "" : wifi_args.operands->sval[1];

    if (strlen(ssid) > 32) {
        printf("SSID is too long. Max 32 bytes.\n");
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(password) > 64) {
        printf("Password is too long. Max 64 bytes.\n");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *) wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = open_network ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_last_disconnect_reason = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        printf("Failed to configure STA: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Connecting to '%s'...\n", ssid);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        printf("esp_wifi_connect failed: %s\n", esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        printf("Connected to '%s'.\n", ssid);
        return wifi_status_command(0, NULL);
    }

    if (bits & WIFI_FAIL_BIT) {
        printf("Failed to connect to '%s': reason %u (%s)\n",
            ssid,
            (unsigned) s_last_disconnect_reason,
            wifi_reason_to_str(s_last_disconnect_reason));
        return ESP_FAIL;
    }

    printf("Timed out connecting to '%s' after %" PRIu32 " ms.\n", ssid, timeout_ms);
    return ESP_ERR_TIMEOUT;
}

static int wifi_connect_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    return wifi_connect_common(false);
}

static int wifi_connect_open_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    return wifi_connect_common(true);
}

static int wifi_disconnect_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_err_t err = ensure_wifi_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
        printf("Wi-Fi disconnect failed: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Wi-Fi disconnected.\n");
    return ESP_OK;
}

static int wifi_stop_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (!s_wifi_initialized) {
        printf("Wi-Fi is not initialized.\n");
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        printf("Wi-Fi stop failed: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("Wi-Fi stopped. Run any wifi command to start it again.\n");
    s_wifi_started = false;
    return ESP_OK;
}

static void print_wifi_help(void);

static const wifi_command_entry_t wifi_cmds[] = {
    {"scan", wifi_scan_command, "Scan access points; optional SSID text filter"},
    {"search", wifi_scan_command, "Alias for scan with filter"},
    {"connect", wifi_connect_command, "Connect to WPA/WPA2/WPA3 network"},
    {"connect-open", wifi_connect_open_command, "Connect to an open network"},
    {"status", wifi_status_command, "Show current station status"},
    {"disconnect", wifi_disconnect_command, "Disconnect station"},
    {"stop", wifi_stop_command, "Stop Wi-Fi driver"},
};

#define WIFI_CMD_COUNT (sizeof(wifi_cmds) / sizeof(wifi_cmds[0]))

static char wifi_cmds_help[128] = {0};

static void generate_wifi_cmds_help_text(void)
{
    strlcpy(wifi_cmds_help, ": ", sizeof(wifi_cmds_help));
    for (size_t i = 0; i < WIFI_CMD_COUNT; i++) {
        strlcat(wifi_cmds_help, wifi_cmds[i].name, sizeof(wifi_cmds_help));
        if (i < WIFI_CMD_COUNT - 1) {
            strlcat(wifi_cmds_help, "; ", sizeof(wifi_cmds_help));
        }
    }
}

static void print_wifi_commands(void)
{
    printf("Available wifi commands:\n");
    for (size_t i = 0; i < WIFI_CMD_COUNT; i++) {
        printf("  %-12s %s\n", wifi_cmds[i].name, wifi_cmds[i].description);
    }
}

static void print_wifi_help(void)
{
    printf("\nUsage:\n");
    printf("  wifi scan [text]\n");
    printf("  wifi search <text>\n");
    printf("  wifi connect <ssid> <password> [--timeout ms]\n");
    printf("  wifi connect-open <ssid> [--timeout ms]\n");
    printf("  wifi status\n");
    printf("  wifi disconnect\n");
    printf("  wifi stop\n\n");
    print_wifi_commands();
    printf("\nExamples:\n");
    printf("  wifi scan\n");
    printf("  wifi scan Home\n");
    printf("  wifi connect HomeNetwork secretpass --timeout 20000\n");
    printf("  wifi connect-open Guest\n");
    printf("  wifi status\n\n");
}

static int wifi_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &wifi_args);

    if (argc == 1 || wifi_args.help->count > 0) {
        print_wifi_help();
        return ESP_OK;
    }

    if (wifi_args.list->count > 0) {
        print_wifi_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (wifi_args.subcommand->count == 0 || wifi_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `wifi --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = wifi_args.subcommand->sval[0];
    for (size_t i = 0; i < WIFI_CMD_COUNT; i++) {
        if (strcmp(subcommand, wifi_cmds[i].name) == 0) {
            return wifi_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown wifi subcommand: %s\n", subcommand);
    print_wifi_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_wifi_command(void)
{
    generate_wifi_cmds_help_text();

    wifi_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", wifi_cmds_help);
    wifi_args.operands = arg_strn(NULL, NULL, "<arg>", 0, WIFI_MAX_OPERANDS, "Command arguments");
    wifi_args.timeout = arg_int0(NULL, "timeout", "<ms>", "Connect timeout in milliseconds");
    wifi_args.list = arg_lit0("l", "list", "List available wifi subcommands");
    wifi_args.help = arg_lit0("h", "help", "Show wifi command help");
    wifi_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "Wi-Fi scan and station control",
        .hint = NULL,
        .func = &wifi_command,
        .argtable = &wifi_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
