

#include "defines.h"

#include "FAT_fs.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_system.h"
#include <errno.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"

/**********************
 *   FILESYSTEM
 **********************/

// FAT INTERNAL

static const char* FFAT_TAG    = "FATFS";

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;  // Handle of the wear levelling library instance

esp_err_t initialize_internal_fat_filesystem() {
    const esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed   = true,
        .max_files                = 5,
        .allocation_unit_size     = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat              = false,
    };
    ESP_LOGI(FFAT_TAG, "Mounting internal filesystem");
    esp_err_t InitMountFlagErr = esp_vfs_fat_spiflash_mount_rw_wl(FAT_MOUNT_PATH, FAT_PARTITION_LABEL, &config, &s_wl_handle);
    if (InitMountFlagErr != ESP_OK) {
        ESP_LOGE(FFAT_TAG, "Failed to mount FATFS (%s)", esp_err_to_name(InitMountFlagErr));
        return InitMountFlagErr;
    }
    ESP_LOGI(FFAT_TAG, "Mounted FATFS at %s", FAT_MOUNT_PATH);

    // Use POSIX and C standard library functions to work with files.
    // First create a file.

    ESP_LOGI(FFAT_TAG, "Opening file");
    char path[64];
    snprintf(path, sizeof(path), "%s/hello.txt", FAT_MOUNT_PATH);
    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(FFAT_TAG, "fopen failed, errno=%d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    fprintf(f, "Hello World FAT!\n");
    fclose(f);
    ESP_LOGI(FFAT_TAG, "File written");
    ESP_LOGI(FFAT_TAG, "Reading file");
    f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(FFAT_TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[128] = {0};
    fgets(line, sizeof(line), f);
    fclose(f);
    char* pos = strpbrk(line, "\r\n"); // strip newline
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(FFAT_TAG, "Read from file: '%s'", line);
    return ESP_OK;
}