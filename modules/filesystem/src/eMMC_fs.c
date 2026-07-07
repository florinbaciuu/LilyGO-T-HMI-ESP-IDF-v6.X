

/**********************
 *   INCLUDES
 **********************/
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_system.h"

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

#include "esp_vfs_fat.h"

#include "defines.h"

#include "driver/spi_common.h"
#include "driver/spi_master.h"

/**********************
 *   FILESYSTEM
 **********************/
#ifdef SDMMC_SYSTEM
static const char* SDMMC_TAG = "SDMMCFS";

bool fs_test_sdmmc();  //. prototype

sdmmc_card_t* card;

// Init_SDMMC_FS
esp_err_t initialize_filesystem_sdmmc() {
    ESP_LOGI(SDMMC_TAG, "Initializing eMMC");
    esp_vfs_fat_mount_config_t mount_config = {.format_if_mount_failed = true,
        .max_files                                                     = 4,
        .allocation_unit_size                                          = 16 * 1024,
        .disk_status_check_enable                                      = false,
        .use_one_fat                                                   = false};

    const char mount_point[]                = SD_MOUNT_PATH;
    // sdmmc_host_t host                       = SDMMC_HOST_DEF();
    sdmmc_host_t host = SDMMC_HOST_DEF();

    host.max_freq_khz = SD_FREQ_HIGHSPEED;  // reducere la 20 MHz
    // host.max_freq_khz                       =SDMMC_FREQ_PROBING;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEF();
    ESP_LOGI(SDMMC_TAG, "Using SDMMC peripheral.");
    slot_config.clk = (gpio_num_t) SDMMC_CLK_PIN;
    slot_config.cmd = (gpio_num_t) SDMMC_CMD_PIN;
    slot_config.d0  = (gpio_num_t) SDMMC_D0_PIN;
    slot_config.d1  = (gpio_num_t) SDMMC_D1_PIN;
    // slot_config.d1    = (gpio_num_t) -1;
    slot_config.d2 = (gpio_num_t) SDMMC_D2_PIN;
    // slot_config.d2    = (gpio_num_t) -1;
    slot_config.d3 = (gpio_num_t) SDMMC_D3_PIN;
    // slot_config.d3 = (gpio_num_t) -1;
    slot_config.width = 1;
    slot_config.cd    = SDMMC_NO_CD;
    slot_config.wp    = SDMMC_NO_WP;
    ESP_LOGI(SDMMC_TAG, "Set GPIO.");
    gpio_set_pull_mode((gpio_num_t) SDMMC_CMD_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t) SDMMC_D0_PIN, GPIO_PULLUP_ONLY);
    if (SDMMC_D1_PIN != GPIO_NO) {
        gpio_set_pull_mode((gpio_num_t) SDMMC_D1_PIN, GPIO_PULLUP_ONLY);
    }
    if (SDMMC_D2_PIN != GPIO_NO) {
        gpio_set_pull_mode((gpio_num_t) SDMMC_D2_PIN, GPIO_PULLUP_ONLY);
    }
    if (SDMMC_D3_PIN != GPIO_NO) {
        gpio_set_pull_mode((gpio_num_t) SDMMC_D3_PIN, GPIO_PULLUP_ONLY);
    }
    ESP_LOGI(SDMMC_TAG, "Mounting SDMMC filesystem");
    esp_err_t InitMountFlagErr = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (InitMountFlagErr != ESP_OK) {
        ESP_LOGE(SDMMC_TAG,
            "Failed to mount SDMMC (%s)",
            "If you want the eMMC to be formatted, set the disk_status_check_enable = true.",
            "Make sure eMMC lines have pull-up resistors in place.",
            esp_err_to_name(InitMountFlagErr));
        sdmmc_host_deinit();  // de init the sdcard
        return InitMountFlagErr;
    }
    ESP_LOGI(SDMMC_TAG, "SD card mounted at %s", mount_point);
    sdmmc_card_print_info(stdout, card);
    fs_test_sdmmc();
    return ESP_OK;
}

#endif

#ifdef SPISD_SYSTEM

static const char* TAG       = "SD_SPI";
static const char* SDMMC_TAG = "SD_SPI";
sdmmc_card_t*      card;
 

    esp_err_t
    initialize_filesystem_sdmmc(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing SPI bus");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_NUM_MOSI,
        .miso_io_num     = PIN_NUM_MISO,
        .sclk_io_num     = PIN_NUM_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 16 * 1024};

    // gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return ret;
    }

    sdmmc_host_t host                       = SDSPI_HOST_DEFAULT();
    host.slot                               = SPI2_HOST;
    host.max_freq_khz                       = 20000;  // reducere la 20 MHz
    sdspi_device_config_t slot_config       = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs                     = PIN_NUM_CS;
    slot_config.host_id                     = SPI2_HOST;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024};

    ESP_LOGI(TAG, "Mounting SD card via SPI");

    ret = esp_vfs_fat_sdspi_mount("/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted");
    sdmmc_card_print_info(stdout, card);
    fs_test_sdmmc();

    return ESP_OK;
}

#endif

// --------------------------------------- //

esp_err_t deinitialize_filesystem_sdmmc() {
    // All done, unmount partition and disable SDMMC peripheral
    ESP_LOGI(SDMMC_TAG, "Unmounting SDMMC filesystem");
    esp_err_t DeInitMountFlagErr = esp_vfs_fat_sdcard_unmount(SD_MOUNT_PATH, card);
    ESP_LOGI(SDMMC_TAG, "Card unmounted");
    return DeInitMountFlagErr;
}

// --------------------------------------- //

bool fs_test_sdmmc() {
    //  First create a file.
    const char* file_hello = SD_MOUNT_PATH "/test_sd.txt";
    ESP_LOGI(SDMMC_TAG, "Opening file %s", file_hello);
    FILE* f = fopen(file_hello, "w");
    if (f == NULL) {
        ESP_LOGE(SDMMC_TAG, "Failed to open file for writing");
        return 0;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    ESP_LOGI(SDMMC_TAG, "File written");
    // Open renamed file for reading
    ESP_LOGI(SDMMC_TAG, "Reading file %s", file_hello);
    f = fopen(file_hello, "r");
    if (f == NULL) {
        ESP_LOGE(SDMMC_TAG, "Failed to open file for reading");
        return 0;
    }
    char line[128];                // Set A 128 buffer
    fgets(line, sizeof(line), f);  // Read a line from file
    fclose(f);
    char* pos = strchr(line, '\n');  // Strip newline
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(SDMMC_TAG, "Read from file: '%s'", line);
    return 1;
}

// --------------------------------------- //