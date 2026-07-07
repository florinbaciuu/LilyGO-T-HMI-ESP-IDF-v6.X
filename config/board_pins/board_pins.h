#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// code aici 
// comment here
#define BOARD_TFT_DATA0    (48)                // GPIO pin for TFT data line 0
#define BOARD_TFT_DATA1    (47)                // GPIO pin for TFT data line 1
#define BOARD_TFT_DATA2    (39)                // GPIO pin for TFT data line 2
#define BOARD_TFT_DATA3    (40)                // GPIO pin for TFT data line 3
#define BOARD_TFT_DATA4    (41)                // GPIO pin for TFT data line 4
#define BOARD_TFT_DATA5    (42)                // GPIO pin for TFT data line 5
#define BOARD_TFT_DATA6    (45)                // GPIO pin for TFT data line 6
#define BOARD_TFT_DATA7    (46)                // GPIO pin for TFT data line 7
#define BOARD_TFT_RST      (-1)                // GPIO pin for TFT reset, set to -1 if not used
#define BOARD_TFT_CS       (6)                 // GPIO pin for TFT chip select
#define BOARD_TFT_DC       (7)                 // GPIO pin for TFT data/command control
#define BOARD_TFT_WR       (8)                 // GPIO pin for TFT write control
#define LCD_WIDTH          (240)               // Width of the LCD in pixels
#define LCD_HEIGHT         (320)               // Height of the LCD in pixels
#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)  // LCD pixel clock frequency in Hz
#define BOARD_TFT_BL       (38)                // GPIO pin for backlight control
#define PWR_EN_PIN         (10)                // connected to the battery alone
//---------
#define PWR_ON_PIN \
    (14)                     // if you use an ext 5V power supply, you need to bring a magnet close to the
                             // ReedSwitch and set the PowerOn Pin (GPIO14) to HIGH
#define REED_SWICH_PIN (21)  // connected to the battery and the USB power supply, used to turn off the device
//---------
#define PIN_NUM_IRQ  (9)  // IRQ pin for touch controller
#define BAT_ADC_PIN  (5)  // ADC pin for battery voltage measurement
#define PIN_NUM_MISO (4)  // MISO pin for touch controller
#define PIN_NUM_MOSI (3)  // MOSI pin for touch controller
#define PIN_NUM_CLK  (1)  // CLK pin for touch controller
#define PIN_NUM_CS   (2)  // CS pin for touch controller


#ifdef __cplusplus
}
#endif