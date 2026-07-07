
#include "spi_bsp_interface.h"
#include "board_pins.h"
#include "esp_log.h"
#include "driver/spi_master.h"

static const char* TAG = "SPI Interface";


void spi_bus_config() {
    //  Configurare SPI Touch IO
    spi_bus_config_t buscfg = {.mosi_io_num = (int) PIN_NUM_MOSI,
        .miso_io_num                        = (int) PIN_NUM_MISO,
        .sclk_io_num                        = (int) PIN_NUM_CLK,
        .quadwp_io_num                      = (int) (-1),
        .quadhd_io_num                      = (int) (-1),
        .data4_io_num                       = (int) (-1),
        .data5_io_num                       = (int) (-1),
        .data6_io_num                       = (int) (-1),
        .data7_io_num                       = (int) (-1),
        .data_io_default_level              = false,
        .max_transfer_sz                    = (int) 4096,
        .flags                              = (int) 0,
        .isr_cpu_id                         = (esp_intr_cpu_affinity_t) 0,
        .intr_flags                         = (int) 0};
    ESP_LOGI(TAG, "Initializing SPI bus with MOSI: %d, MISO: %d, CLK: %d", PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_CLK);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");
}