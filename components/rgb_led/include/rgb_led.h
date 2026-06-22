#pragma once

#include "led_strip.h"


esp_err_t cycle_rgb_led_colors(void);
esp_err_t deinit_rgb_led(void);
esp_err_t clear_rgb_led(void);
esp_err_t set_rgb_led_color(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t init_rgb_led_pin(gpio_num_t gpio_num);
esp_err_t init_rgb_led(void);
