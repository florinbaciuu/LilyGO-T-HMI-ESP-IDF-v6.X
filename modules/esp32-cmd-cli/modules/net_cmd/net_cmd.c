#include "net_cmd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

static const char *TAG = "ESP32_CLI";

#define NET_MAX_OPERANDS 3
#define NET_PING_DEFAULT_COUNT 4
#define NET_PING_DEFAULT_INTERVAL_MS 1000
#define NET_PING_DEFAULT_TIMEOUT_MS 1000
#define NET_PING_DEFAULT_SIZE 64
#define NET_PING_MAX_COUNT 100
#define NET_PING_MAX_SIZE 1400

typedef int (*net_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    net_func_t function;
    const char *description;
} net_command_entry_t;

typedef struct {
    SemaphoreHandle_t done;
} net_ping_context_t;

static struct {
    struct arg_str *subcommand;
    struct arg_str *operands;
    struct arg_int *count;
    struct arg_int *interval;
    struct arg_int *timeout;
    struct arg_int *size;
    struct arg_lit *list;
    struct arg_lit *help;
    struct arg_end *end;
} net_args;

static const char *safe_text(const char *text)
{
    return text != NULL && text[0] != '\0' ? text : "-";
}

static bool parse_u32_option(struct arg_int *arg, uint32_t fallback, uint32_t min, uint32_t max, const char *name, uint32_t *out_value)
{
    *out_value = fallback;
    if (arg->count == 0) {
        return true;
    }

    int value = arg->ival[0];
    if (value < (int) min || value > (int) max) {
        printf("%s must be %" PRIu32 "..%" PRIu32 ".\n", name, min, max);
        return false;
    }

    *out_value = (uint32_t) value;
    return true;
}

static void print_ip4(const char *label, const esp_ip4_addr_t *addr)
{
    printf("%-14s " IPSTR "\n", label, IP2STR(addr));
}

static int net_if_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    esp_netif_t *netif = NULL;
    size_t count = 0;

    printf("\nNetwork interfaces\n");
    printf("------------------\n");

    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        count++;

        char impl_name[8] = {0};
        const char *hostname = NULL;
        esp_netif_ip_info_t ip_info = {0};

        esp_netif_get_netif_impl_name(netif, impl_name);
        esp_netif_get_hostname(netif, &hostname);
        esp_netif_get_ip_info(netif, &ip_info);

        printf("[%u] %s\n", (unsigned) count, safe_text(esp_netif_get_desc(netif)));
        printf("%-14s %s\n", "Impl:", safe_text(impl_name));
        printf("%-14s %s\n", "State:", esp_netif_is_netif_up(netif) ? "up" : "down");
        printf("%-14s %s\n", "Hostname:", safe_text(hostname));
        print_ip4("IP:", &ip_info.ip);
        print_ip4("Gateway:", &ip_info.gw);
        print_ip4("Netmask:", &ip_info.netmask);
        printf("\n");
    }

    if (count == 0) {
        printf("No esp-netif interfaces found. Run `wifi status` or initialize networking first.\n\n");
    }

    return ESP_OK;
}

static void print_addrinfo_result(const char *host, const struct addrinfo *result)
{
    printf("\nDNS lookup: %s\n", host);
    printf("----------------\n");

    size_t count = 0;
    for (const struct addrinfo *it = result; it != NULL; it = it->ai_next) {
        char addr[INET6_ADDRSTRLEN] = {0};

        if (it->ai_family == AF_INET) {
            const struct sockaddr_in *sa = (const struct sockaddr_in *) it->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr));
            printf("%-6s %s\n", "IPv4:", addr);
            count++;
        } else if (it->ai_family == AF_INET6) {
            const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *) it->ai_addr;
            inet_ntop(AF_INET6, &sa6->sin6_addr, addr, sizeof(addr));
            printf("%-6s %s\n", "IPv6:", addr);
            count++;
        }
    }

    if (count == 0) {
        printf("No printable addresses returned.\n");
    }
    printf("\n");
}

static int net_dns_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (net_args.operands->count < 1) {
        printf("Usage: net dns <host>\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *host = net_args.operands->sval[0];
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = NULL;

    int err = getaddrinfo(host, NULL, &hints, &result);
    if (err != 0) {
        printf("DNS lookup failed for '%s': %d\n", host, err);
        return ESP_FAIL;
    }

    print_addrinfo_result(host, result);
    freeaddrinfo(result);
    return ESP_OK;
}

static esp_err_t resolve_ping_target(const char *host, ip_addr_t *target_addr, char *addr_text, size_t addr_text_len)
{
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_RAW,
    };
    struct addrinfo *result = NULL;

    int err = getaddrinfo(host, NULL, &hints, &result);
    if (err != 0 || result == NULL) {
        printf("Failed to resolve '%s': %d\n", host, err);
        return ESP_FAIL;
    }

    const struct addrinfo *selected = result;
    for (const struct addrinfo *it = result; it != NULL; it = it->ai_next) {
        if (it->ai_family == AF_INET) {
            selected = it;
            break;
        }
    }

    memset(target_addr, 0, sizeof(*target_addr));
    if (selected->ai_family == AF_INET) {
        const struct sockaddr_in *sa = (const struct sockaddr_in *) selected->ai_addr;
        ip4_addr_set_u32(ip_2_ip4(target_addr), sa->sin_addr.s_addr);
        target_addr->type = IPADDR_TYPE_V4;
        inet_ntop(AF_INET, &sa->sin_addr, addr_text, addr_text_len);
    } else if (selected->ai_family == AF_INET6) {
        const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *) selected->ai_addr;
        memcpy(ip_2_ip6(target_addr)->addr, sa6->sin6_addr.un.u32_addr, sizeof(ip_2_ip6(target_addr)->addr));
        target_addr->type = IPADDR_TYPE_V6;
        inet_ntop(AF_INET6, &sa6->sin6_addr, addr_text, addr_text_len);
    } else {
        freeaddrinfo(result);
        return ESP_ERR_NOT_SUPPORTED;
    }

    freeaddrinfo(result);
    return ESP_OK;
}

static void net_ping_success(esp_ping_handle_t hdl, void *args)
{
    (void) args;

    uint8_t ttl = 0;
    uint16_t seqno = 0;
    uint32_t elapsed_time = 0;
    uint32_t recv_len = 0;
    ip_addr_t target_addr = {0};

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

    printf("%" PRIu32 " bytes from %s: icmp_seq=%" PRIu16 " ttl=%" PRIu8 " time=%" PRIu32 " ms\n",
        recv_len,
        ipaddr_ntoa(&target_addr),
        seqno,
        ttl,
        elapsed_time);
}

static void net_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    (void) args;

    uint16_t seqno = 0;
    ip_addr_t target_addr = {0};

    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));

    printf("From %s: icmp_seq=%" PRIu16 " timeout\n", ipaddr_ntoa(&target_addr), seqno);
}

static void net_ping_end(esp_ping_handle_t hdl, void *args)
{
    net_ping_context_t *ctx = (net_ping_context_t *) args;

    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t total_time_ms = 0;
    ip_addr_t target_addr = {0};

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));

    uint32_t loss = transmitted > 0 ? (uint32_t) ((1.0f - ((float) received / (float) transmitted)) * 100.0f) : 0;

    printf("\n--- %s ping statistics ---\n", ipaddr_ntoa(&target_addr));
    printf("%" PRIu32 " packets transmitted, %" PRIu32 " received, %" PRIu32 "%% packet loss, time %" PRIu32 " ms\n\n",
        transmitted,
        received,
        loss,
        total_time_ms);

    if (ctx != NULL && ctx->done != NULL) {
        xSemaphoreGive(ctx->done);
    }
}

static int net_ping_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (net_args.operands->count < 1) {
        printf("Usage: net ping <host> [--count n] [--interval ms] [--timeout ms] [--size bytes]\n");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t count = NET_PING_DEFAULT_COUNT;
    uint32_t interval_ms = NET_PING_DEFAULT_INTERVAL_MS;
    uint32_t timeout_ms = NET_PING_DEFAULT_TIMEOUT_MS;
    uint32_t data_size = NET_PING_DEFAULT_SIZE;

    if (!parse_u32_option(net_args.count, count, 1, NET_PING_MAX_COUNT, "count", &count) ||
        !parse_u32_option(net_args.interval, interval_ms, 100, 60000, "interval", &interval_ms) ||
        !parse_u32_option(net_args.timeout, timeout_ms, 100, 60000, "timeout", &timeout_ms) ||
        !parse_u32_option(net_args.size, data_size, 0, NET_PING_MAX_SIZE, "size", &data_size)) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *host = net_args.operands->sval[0];
    char addr_text[INET6_ADDRSTRLEN] = {0};
    ip_addr_t target_addr = {0};

    esp_err_t err = resolve_ping_target(host, &target_addr, addr_text, sizeof(addr_text));
    if (err != ESP_OK) {
        return err;
    }

    net_ping_context_t ctx = {
        .done = xSemaphoreCreateBinary(),
    };
    if (ctx.done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target_addr;
    config.count = count;
    config.interval_ms = interval_ms;
    config.timeout_ms = timeout_ms;
    config.data_size = data_size;

    esp_ping_callbacks_t callbacks = {
        .cb_args = &ctx,
        .on_ping_success = net_ping_success,
        .on_ping_timeout = net_ping_timeout,
        .on_ping_end = net_ping_end,
    };

    esp_ping_handle_t ping = NULL;
    err = esp_ping_new_session(&config, &callbacks, &ping);
    if (err != ESP_OK) {
        printf("Failed to create ping session: %s\n", esp_err_to_name(err));
        vSemaphoreDelete(ctx.done);
        return err;
    }

    printf("PING %s (%s): %" PRIu32 " data bytes, count=%" PRIu32 "\n", host, addr_text, data_size, count);
    err = esp_ping_start(ping);
    if (err != ESP_OK) {
        printf("Failed to start ping: %s\n", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        vSemaphoreDelete(ctx.done);
        return err;
    }

    uint32_t wait_ms = (timeout_ms + interval_ms + 250) * count + 1000;
    if (xSemaphoreTake(ctx.done, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        printf("Ping command timed out waiting for session end.\n");
        esp_ping_stop(ping);
    }

    esp_ping_delete_session(ping);
    vSemaphoreDelete(ctx.done);
    return ESP_OK;
}

static void print_net_help(void);

static const net_command_entry_t net_cmds[] = {
    {"if", net_if_command, "List esp-netif interfaces and IPv4 configuration"},
    {"dns", net_dns_command, "Resolve a hostname"},
    {"ping", net_ping_command, "Ping a host using ICMP"},
};

#define NET_CMD_COUNT (sizeof(net_cmds) / sizeof(net_cmds[0]))

static char net_cmds_help[96] = {0};

static void generate_net_cmds_help_text(void)
{
    strlcpy(net_cmds_help, ": ", sizeof(net_cmds_help));
    for (size_t i = 0; i < NET_CMD_COUNT; i++) {
        strlcat(net_cmds_help, net_cmds[i].name, sizeof(net_cmds_help));
        if (i < NET_CMD_COUNT - 1) {
            strlcat(net_cmds_help, "; ", sizeof(net_cmds_help));
        }
    }
}

static void print_net_commands(void)
{
    printf("Available net commands:\n");
    for (size_t i = 0; i < NET_CMD_COUNT; i++) {
        printf("  %-8s %s\n", net_cmds[i].name, net_cmds[i].description);
    }
}

static void print_net_help(void)
{
    printf("\nUsage:\n");
    printf("  net if\n");
    printf("  net dns <host>\n");
    printf("  net ping <host> [--count n] [--interval ms] [--timeout ms] [--size bytes]\n\n");
    print_net_commands();
    printf("\nExamples:\n");
    printf("  net if\n");
    printf("  net dns example.com\n");
    printf("  net ping 8.8.8.8\n");
    printf("  net ping example.com --count 10 --timeout 2000\n\n");
}

static int net_command(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &net_args);

    if (argc == 1 || net_args.help->count > 0) {
        print_net_help();
        return ESP_OK;
    }

    if (net_args.list->count > 0) {
        print_net_commands();
        return ESP_OK;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, net_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (net_args.subcommand->count == 0 || net_args.subcommand->sval[0] == NULL) {
        printf("No subcommand provided. Use `net --help`.\n");
        return ESP_ERR_INVALID_ARG;
    }

    const char *subcommand = net_args.subcommand->sval[0];
    for (size_t i = 0; i < NET_CMD_COUNT; i++) {
        if (strcmp(subcommand, net_cmds[i].name) == 0) {
            return net_cmds[i].function(argc, argv);
        }
    }

    printf("Unknown net subcommand: %s\n", subcommand);
    print_net_commands();
    return ESP_ERR_NOT_FOUND;
}

void cli_register_net_command(void)
{
    generate_net_cmds_help_text();

    net_args.subcommand = arg_str0(NULL, NULL, "<subcommand>", net_cmds_help);
    net_args.operands = arg_strn(NULL, NULL, "<arg>", 0, NET_MAX_OPERANDS, "Command arguments");
    net_args.count = arg_int0("c", "count", "<n>", "Ping packet count");
    net_args.interval = arg_int0("i", "interval", "<ms>", "Ping interval in milliseconds");
    net_args.timeout = arg_int0("W", "timeout", "<ms>", "Ping per-packet timeout in milliseconds");
    net_args.size = arg_int0("s", "size", "<bytes>", "Ping payload size");
    net_args.list = arg_lit0("l", "list", "List available net subcommands");
    net_args.help = arg_lit0("h", "help", "Show net command help");
    net_args.end = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command = "net",
        .help = "Network diagnostics: interfaces, DNS, ping",
        .hint = NULL,
        .func = &net_command,
        .argtable = &net_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_LOGI(TAG, "'%s' command registered.", cmd.command);
}
