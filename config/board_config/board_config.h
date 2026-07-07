#pragma once
#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

void gpio_extra_set_init(uint32_t mode);
void power_latch_init(void);
void gfx_set_backlight(uint32_t mode);


#ifdef __cplusplus
}
#endif